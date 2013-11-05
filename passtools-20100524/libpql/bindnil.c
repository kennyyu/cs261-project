/*
 * Copyright 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Arrange to have column-variable bindings that are skipped over
 * (e.g. because of being inside optional path sections) bound to nil.
 */

#include "pttree.h"
#include "passes.h"

struct bindnil {
   struct pqlcontext *pql;
};

////////////////////////////////////////////////////////////
// context management

static void bindnil_init(struct bindnil *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
}

static void bindnil_cleanup(struct bindnil *ctx) {
   (void)ctx;
}

////////////////////////////////////////////////////////////
// inspection tool

static void ptpath_getvars(struct bindnil *ctx, struct ptpath *pp,
			   struct ptcolumnvararray *fill) {
   unsigned i, num;

   switch (pp->type) {
    case PTP_SEQUENCE:
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	ptpath_getvars(ctx, ptpatharray_get(&pp->sequence.items, i), fill);
     }
     break;

    case PTP_ALTERNATES:
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	ptpath_getvars(ctx, ptpatharray_get(&pp->alternates.items, i), fill);
     }
     break;

    case PTP_OPTIONAL:
     /*
      * pp->optional.nilcolumns should have already been filled; could
      * use it instead of recursing again.
      */
     ptpath_getvars(ctx, pp->optional.sub, fill);
     break;

    case PTP_REPEATED:
     ptpath_getvars(ctx, pp->repeated.sub, fill);
     /*
      * Because each column var is distinct we don't need to check only ones
      * form the subpath.
      */
     num = ptcolumnvararray_num(fill);
     for (i=0; i<num; i++) {
	if (ptcolumnvararray_get(fill, i) == pp->repeated.pathfrominside) {
	   ptcolumnvararray_set(fill, i, pp->repeated.pathonoutside);
	   /*ptcolumnvar_decref(pp->repeated.pathfrominside) - region alloc */
	   ptcolumnvar_incref(pp->repeated.pathonoutside);
	}
     }
     break;

    case PTP_NILBIND:
     num = ptcolumnvararray_num(&pp->nilbind.columnsbefore);
     for (i=0; i<num; i++) {
	ptcolumnvararray_add(ctx->pql, fill,
			     ptcolumnvararray_get(&pp->nilbind.columnsbefore, i),
			     NULL);
     }
     ptpath_getvars(ctx, pp->nilbind.sub, fill);
     num = ptcolumnvararray_num(&pp->nilbind.columnsafter);
     for (i=0; i<num; i++) {
	ptcolumnvararray_add(ctx->pql, fill,
			     ptcolumnvararray_get(&pp->nilbind.columnsafter, i),
			     NULL);
     }
     break;

    case PTP_EDGE:
     /*
      * In the abstract there might be more vars bound inside the
      * column name of a computed column name. For the time being
      * let's pretend that's impossible. (XXX?)
      */
     break;
   }

   PQLASSERT(pp->bindobjbefore == NULL);
   if (pp->bindpath != NULL) {
      ptcolumnvararray_add(ctx->pql, fill, pp->bindpath, NULL);
   }
   if (pp->bindobjafter != NULL) {
      ptcolumnvararray_add(ctx->pql, fill, pp->bindobjafter, NULL);
   }
}

////////////////////////////////////////////////////////////
// recursive traversal

static void ptexpr_bindnil(struct bindnil *ctx, struct ptexpr *pe);

/*
 * Paths.
 */
static struct ptpath *ptpath_bindnil(struct bindnil *ctx, struct ptpath *pp) {
   unsigned i, num;
   struct ptpath *subpath;

   switch (pp->type) {
    case PTP_SEQUENCE:
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	subpath = ptpatharray_get(&pp->sequence.items, i);
	subpath = ptpath_bindnil(ctx, subpath);
	ptpatharray_set(&pp->sequence.items, i, subpath);
     }
     break;

    case PTP_ALTERNATES:
     /* First, traverse recursively. */
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	subpath = ptpatharray_get(&pp->alternates.items, i);
	subpath = ptpath_bindnil(ctx, subpath);
	ptpatharray_set(&pp->alternates.items, i, subpath);
     }

     /*
      * Collect the variables each alternative binds. Remember which
      * alternative each variable came from. Then for each alternative
      * nil-bind the variables it *doesn't* bind. There can't be
      * duplicates in multiple alternatives because that's ruled out
      * upstream. (If we want to support that, this is the place to
      * make it work.)
      */

     {
	struct ptcolumnvararray vars;
	struct ptcolumnvar *thiscol;
	struct ptpath *ppnil;
	unsigned *numafter, j, vnum, skipstart, skipend;

	ptcolumnvararray_init(&vars);
	numafter = domalloc(ctx->pql, num * sizeof(*numafter));

	for (i=0; i<num; i++) {
	   ptpath_getvars(ctx, ptpatharray_get(&pp->alternates.items, i),
			  &vars);
	   numafter[i] = ptcolumnvararray_num(&vars);
	}
	vnum = ptcolumnvararray_num(&vars);
	for (i=0; i<num; i++) {
	   skipstart = (i==0) ? 0 : numafter[i-1];
	   skipend = numafter[i];

	   ppnil = mkptpath_nilbind(ctx->pql, NULL);

	   for (j=0; j<vnum; j++) {
	      if (j < skipstart) {
		 thiscol = ptcolumnvararray_get(&vars, j);
		 ptcolumnvar_incref(thiscol);
		 ptcolumnvararray_add(ctx->pql,
				      &ppnil->nilbind.columnsbefore, thiscol,
				      NULL);
	      }
	      else if (j>=skipstart && j<skipend) {
		 continue;
	      }
	      else {
		 thiscol = ptcolumnvararray_get(&vars, j);
		 ptcolumnvar_incref(thiscol);
		 ptcolumnvararray_add(ctx->pql,
				      &ppnil->nilbind.columnsafter, thiscol,
				      NULL);
	      }
	   }

	   subpath = ptpatharray_get(&pp->alternates.items, i);
	   ppnil->nilbind.sub = subpath;
	   ptpatharray_set(&pp->alternates.items, i, ppnil);
	}

	dofree(ctx->pql, numafter, num * sizeof(*numafter));
	ptcolumnvararray_setsize(ctx->pql, &vars, 0);
	ptcolumnvararray_cleanup(ctx->pql, &vars);
     }
     break;

    case PTP_OPTIONAL:
     pp->optional.sub = ptpath_bindnil(ctx, pp->optional.sub);
     ptpath_getvars(ctx, pp->optional.sub, &pp->optional.nilcolumns);
     break;

    case PTP_REPEATED:
     pp->repeated.sub = ptpath_bindnil(ctx, pp->repeated.sub);
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	ptexpr_bindnil(ctx, pp->edge.computedname);
     }
     break;
   }
   return pp;
}

/*
 * Expressions. Traverse only.
 */
static void ptexpr_bindnil(struct bindnil *ctx, struct ptexpr *pe) {
   unsigned i, num;

   switch (pe->type) {
    case PTE_SELECT:
     ptexpr_bindnil(ctx, pe->select.sub);
     ptexpr_bindnil(ctx, pe->select.result);
     break;

    case PTE_FROM:
     num = ptexprarray_num(pe->from);
     for (i=0; i<num; i++) {
	ptexpr_bindnil(ctx, ptexprarray_get(pe->from, i));
     }
     break;

    case PTE_WHERE:
     ptexpr_bindnil(ctx, pe->where.sub);
     ptexpr_bindnil(ctx, pe->where.where);
     break;

    case PTE_GROUP:
     ptexpr_bindnil(ctx, pe->group.sub);
     break;

    case PTE_UNGROUP:
     ptexpr_bindnil(ctx, pe->ungroup.sub);
     break;

    case PTE_RENAME:
     ptexpr_bindnil(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	ptexpr_bindnil(ctx, pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     ptexpr_bindnil(ctx, pe->path.root);
     pe->path.body = ptpath_bindnil(ctx, pe->path.body);
     num = ptexprarray_num(&pe->path.morebindings);
     for (i=0; i<num; i++) {
	ptexpr_bindnil(ctx, ptexprarray_get(&pe->path.morebindings, i));
     }
     break;

    case PTE_TUPLE:
     num = ptexprarray_num(pe->tuple);
     for (i=0; i<num; i++) {
	ptexpr_bindnil(ctx, ptexprarray_get(pe->tuple, i));
     }
     break;

    case PTE_FORALL:
     ptexpr_bindnil(ctx, pe->forall.set);
     ptexpr_bindnil(ctx, pe->forall.predicate);
     break;

    case PTE_EXISTS:
     ptexpr_bindnil(ctx, pe->exists.set);
     ptexpr_bindnil(ctx, pe->exists.predicate);
     break;

    case PTE_MAP:
     ptexpr_bindnil(ctx, pe->map.set);
     ptexpr_bindnil(ctx, pe->map.result);
     break;

    case PTE_ASSIGN:
     ptexpr_bindnil(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	ptexpr_bindnil(ctx, pe->assign.body);
     }
     break;

    case PTE_BOP:
     ptexpr_bindnil(ctx, pe->bop.l);
     ptexpr_bindnil(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     ptexpr_bindnil(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	num = ptexprarray_num(pe->func.args);
	for (i=0; i<num; i++) {
	   ptexpr_bindnil(ctx, ptexprarray_get(pe->func.args, i));
	}
     }
     break;

    case PTE_READANYVAR:
     /* not allowed here */
     PQLASSERT(0);
     break;

    case PTE_READCOLUMNVAR:
    case PTE_READGLOBALVAR:
    case PTE_VALUE:
     break;
   }
}

////////////////////////////////////////////////////////////
// entry point

struct ptexpr *bindnil(struct pqlcontext *pql, struct ptexpr *pe) {
   struct bindnil ctx;

   bindnil_init(&ctx, pql);
   ptexpr_bindnil(&ctx, pe);
   bindnil_cleanup(&ctx);

   return pe;
}
