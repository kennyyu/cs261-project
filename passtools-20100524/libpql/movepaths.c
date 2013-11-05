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

#include "pttree.h"
#include "passes.h"
#include "pqlcontext.h" // for complain()

/*
 * Move paths to the from clause.
 *
 * This code does not attempt to deal with sets vs. non-sets or
 * anything; it just shovels. It's (probably) not suitable for a
 * set-canonicalization world.
 *
 * There are two complications we do need to worry about: (1) paths
 * rooted in variables bound logically after the from-clause, and
 * similarly (2) expressions in computed path edge names that depend
 * on such variables.
 *
 * For this reason we keep track of which column variables came from
 * the from-clause (including nested from-clauses that are in scope)
 * and test path roots and computed edge names for not being dependent
 * on any other columnvars.
 *
 * While in principle we could move the dependent bits into the
 * from-clause ourselves, that's delicate and totally not worthwhile;
 * so if such stuff appears we complain and fail.
 */

struct movepaths {
   struct pqlcontext *pql;			/* Global context. */
   struct ptcolumnvararray fromvars;		/* Vars from from-clauses.*/
   bool infrom;					/* Currently in from-clause? */
   struct ptexprarray *curfrom;			/* The target from-clause. */
   bool failed;					/* If we've bombed. */
};

////////////////////////////////////////////////////////////
// context management

static void movepaths_init(struct movepaths *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   ptcolumnvararray_init(&ctx->fromvars);
   ctx->infrom = false;
   ctx->curfrom = NULL;
   ctx->failed = false;
}

static void movepaths_cleanup(struct movepaths *ctx) {
   ptcolumnvararray_setsize(ctx->pql, &ctx->fromvars, 0);
   ptcolumnvararray_cleanup(ctx->pql, &ctx->fromvars);
   PQLASSERT(ctx->infrom == false);
   PQLASSERT(ctx->curfrom == NULL);
}

static unsigned movepaths_savevars(struct movepaths *ctx) {
   return ptcolumnvararray_num(&ctx->fromvars);
}

static void movepaths_restorevars(struct movepaths *ctx, unsigned mark) {
   PQLASSERT(mark <= ptcolumnvararray_num(&ctx->fromvars));
   ptcolumnvararray_setsize(ctx->pql, &ctx->fromvars, mark);
}

static void movepaths_notevar(struct movepaths *ctx, struct ptcolumnvar *var) {
   if (ctx->infrom) {
      ptcolumnvararray_add(ctx->pql, &ctx->fromvars, var, NULL);
   }
}

/*
 * Return true if the variable VAR was bound in the from clause, and
 * thus a reference to it can thus be safely moved *to* (the end of)
 * the from clause.
 */
static bool movepaths_ok_var(struct movepaths *ctx, struct ptcolumnvar *var) {
   unsigned i, num;

   num = ptcolumnvararray_num(&ctx->fromvars);
   for (i=0; i<num; i++) {
      if (ptcolumnvararray_get(&ctx->fromvars, i) == var) {
	 return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////
// recursive traversal for checking for bad stuff

static bool ptexpr_is_moveable(struct movepaths *ctx, struct ptexpr *pe);

static bool ptpath_is_moveable(struct movepaths *ctx, struct ptpath *pp) {
   unsigned i, num;
   struct ptpath *sub;
   bool ret = true;

   switch (pp->type) {
    case PTP_SEQUENCE:
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->sequence.items, i);
	if (!ptpath_is_moveable(ctx, sub)) {
	   ret = false;
	}
     }
     break;

    case PTP_ALTERNATES:
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->alternates.items, i);
	if (!ptpath_is_moveable(ctx, sub)) {
	   ret = false;
	}
     }
     break;

    case PTP_OPTIONAL:
     if (!ptpath_is_moveable(ctx, pp->optional.sub)) {
	ret = false;
     }
     break;

    case PTP_REPEATED:
     if (!ptpath_is_moveable(ctx, pp->repeated.sub)) {
	ret = false;
     }
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	if (!ptexpr_is_moveable(ctx, pp->edge.computedname)) {
	   ret = false;
	}
     }
     break;
   }

   return ret;
}

static bool ptexpr_is_moveable(struct movepaths *ctx, struct ptexpr *pe) {
   unsigned i, num;
   struct ptexpr *sub;
   bool ret = true;

   switch (pe->type) {
    case PTE_SELECT:
     if (!ptexpr_is_moveable(ctx, pe->select.sub)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->select.result)) {
	ret = false;
     }
     break;

    case PTE_FROM:
     if (pe->from != NULL) {
	num = ptexprarray_num(pe->from);
	for (i=0; i<num; i++) {
	   sub = ptexprarray_get(pe->from, i);
	   if (!ptexpr_is_moveable(ctx, sub)) {
	      ret = false;
	   }
	}
     }
     break;

    case PTE_WHERE:
     if (!ptexpr_is_moveable(ctx, pe->where.sub)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->where.where)) {
	ret = false;
     }
     break;

    case PTE_GROUP:
     if (!ptexpr_is_moveable(ctx, pe->group.sub)) {
	ret = false;
     }
     break;

    case PTE_UNGROUP:
     if (!ptexpr_is_moveable(ctx, pe->ungroup.sub)) {
	ret = false;
     }
     break;

    case PTE_RENAME:
     if (!ptexpr_is_moveable(ctx, pe->rename.sub)) {
	ret = false;
     }
     if (pe->rename.iscomputed) {
	if (!ptexpr_is_moveable(ctx, pe->rename.computedname)) {
	   ret = false;
	}
     }
     break;

    case PTE_PATH:
     if (!ptexpr_is_moveable(ctx, pe->path.root)) {
	ret = false;
     }
     if (!ptpath_is_moveable(ctx, pe->path.body)) {
	ret = false;
     }
     break;

    case PTE_TUPLE:
     num = ptexprarray_num(pe->tuple);
     for (i=0; i<num; i++) {
	sub = ptexprarray_get(pe->tuple, i);
	if (!ptexpr_is_moveable(ctx, sub)) {
	   ret = false;
	}
     }
     break;

    case PTE_FORALL:
     if (!ptexpr_is_moveable(ctx, pe->forall.set)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->forall.predicate)) {
	ret = false;
     }
     break;

    case PTE_EXISTS:
     if (!ptexpr_is_moveable(ctx, pe->exists.set)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->exists.predicate)) {
	ret = false;
     }
     break;

    case PTE_MAP:
     if (!ptexpr_is_moveable(ctx, pe->map.set)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->map.result)) {
	ret = false;
     }
     break;

    case PTE_ASSIGN:
     if (!ptexpr_is_moveable(ctx, pe->assign.value)) {
	ret = false;
     }
     if (pe->assign.body != NULL) {
	if (!ptexpr_is_moveable(ctx, pe->assign.body)) {
	   ret = false;
	}
     }
     break;

    case PTE_BOP:
     if (!ptexpr_is_moveable(ctx, pe->bop.l)) {
	ret = false;
     }
     if (!ptexpr_is_moveable(ctx, pe->bop.r)) {
	ret = false;
     }
     break;

    case PTE_UOP:
     if (!ptexpr_is_moveable(ctx, pe->uop.sub)) {
	ret = false;
     }
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	num = ptexprarray_num(pe->func.args);
	for (i=0; i<num; i++) {
	   sub = ptexprarray_get(pe->func.args, i);
	   if (!ptexpr_is_moveable(ctx, sub)) {
	      ret = false;
	   }
	}
     }
     break;

    case PTE_READANYVAR:
     /* not allowed here */
     PQLASSERT(0);
     break;

    case PTE_READCOLUMNVAR:
     if (!movepaths_ok_var(ctx, pe->readcolumnvar)) {
	complain(ctx->pql, pe->readcolumnvar->line, pe->readcolumnvar->column,
		 "Locally-bound variable cannot %s be used in a path",
		 pe->readcolumnvar->name);
	complain(ctx->pql, pe->readcolumnvar->line, pe->readcolumnvar->column,
		 "(move path and variable binding to the from-clause)");
	ctx->failed = true;
	ret = false;
     }
     break;

    case PTE_READGLOBALVAR:
     /* not relevant */
     break;

    case PTE_VALUE:
     /* nothing */
     break;
   }

   return ret;
}

////////////////////////////////////////////////////////////
// recursive traversal for moving

static struct ptexpr *ptexpr_movepaths(struct movepaths *ctx,
				       struct ptexpr *pe);
/*
 * Path elements.
 *
 * This records the variables bound, and recurses to get at embedded
 * expressions.
 */
static struct ptpath *ptpath_movepaths(struct movepaths *ctx,
				       struct ptpath *pp) {
   unsigned i, num;
   struct ptpath *sub;

   switch (pp->type) {
    case PTP_SEQUENCE:
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->sequence.items, i);
	sub = ptpath_movepaths(ctx, sub);
	ptpatharray_set(&pp->sequence.items, i, sub);
     }
     break;

    case PTP_ALTERNATES:
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->alternates.items, i);
	sub = ptpath_movepaths(ctx, sub);
	ptpatharray_set(&pp->alternates.items, i, sub);
     }
     break;

    case PTP_OPTIONAL:
     pp->optional.sub = ptpath_movepaths(ctx, pp->optional.sub);
     break;

    case PTP_REPEATED:
     pp->repeated.sub = ptpath_movepaths(ctx, pp->repeated.sub);
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	pp->edge.computedname = ptexpr_movepaths(ctx, pp->edge.computedname);
     }
     break;
   }

   PQLASSERT(pp->bindobjbefore == NULL);

   if (pp->bindpath != NULL) {
      movepaths_notevar(ctx, pp->bindpath);
   }
   if (pp->bindobjafter != NULL) {
      movepaths_notevar(ctx, pp->bindobjafter);
   }
   
   return pp;
}

/*
 * Expressions.
 */
static struct ptexpr *ptexpr_movepaths(struct movepaths *ctx,
				       struct ptexpr *pe) {
   unsigned i, num, mark;
   struct ptexpr *sub, *newpe;
   struct ptcolumnvar *cv;
   bool saveinfrom;
   struct ptexprarray *savecurfrom;

   switch (pe->type) {
    case PTE_SELECT:
     mark = movepaths_savevars(ctx);
     saveinfrom = ctx->infrom;
     savecurfrom = ctx->curfrom;
     ctx->infrom = false;
     ctx->curfrom = NULL;

     pe->select.sub = ptexpr_movepaths(ctx, pe->select.sub);
     pe->select.result = ptexpr_movepaths(ctx, pe->select.result);

     ctx->infrom = saveinfrom;
     ctx->curfrom = savecurfrom;
     movepaths_restorevars(ctx, mark);
     break;

    case PTE_FROM:
     PQLASSERT(ctx->curfrom == NULL);
     PQLASSERT(ctx->infrom == false);
     ctx->curfrom = pe->from;
     ctx->infrom = true;
     num = ptexprarray_num(pe->from);
     for (i=0; i<num; i++) {
	sub = ptexprarray_get(pe->from, i);
	sub = ptexpr_movepaths(ctx, sub);
	ptexprarray_set(pe->from, i, sub);
     }
     ctx->infrom = false;
     break;

    case PTE_WHERE:
     pe->where.sub = ptexpr_movepaths(ctx, pe->where.sub);
     pe->where.where = ptexpr_movepaths(ctx, pe->where.where);
     break;

    case PTE_GROUP:
     pe->group.sub = ptexpr_movepaths(ctx, pe->group.sub);
     break;

    case PTE_UNGROUP:
     pe->ungroup.sub = ptexpr_movepaths(ctx, pe->ungroup.sub);
     break;
	
    case PTE_RENAME:
     pe->rename.sub = ptexpr_movepaths(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	pe->rename.computedname = ptexpr_movepaths(ctx,
						   pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     /*
      * First recurse on the contents to pick up anything nested, like
      * computed edge names.
      */
     pe->path.root = ptexpr_movepaths(ctx, pe->path.root);
     pe->path.body = ptpath_movepaths(ctx, pe->path.body);
     num = ptexprarray_num(&pe->path.morebindings);
     for (i=0; i<num; i++) {
	sub = ptexprarray_get(&pe->path.morebindings, i);
	sub = ptexpr_movepaths(ctx, sub);
	ptexprarray_set(&pe->path.morebindings, i, sub);
     }

     /* Now search for things that would prohibit moving it. */
     if (!ctx->infrom && ptexpr_is_moveable(ctx, pe)) {

	if (ctx->curfrom == NULL) {
	   /* Whoops, have to create a from-clause. */
	   ctx->curfrom = ptexprarray_create(ctx->pql);
	   /* bleh, ideally this would be encapsulated elsewhere */
	   ptmanager_add_exprarray(ctx->pql->ptm, ctx->curfrom);

	}

	/* Move it. */
	ptexprarray_add(ctx->pql, ctx->curfrom, pe, NULL);

	/* Replace with an expression that reads the var it binds. */
	cv = ptpath_get_tailvar(ctx->pql, pe->path.body);
	newpe = mkptexpr_readcolumnvar(ctx->pql, cv);

	/* Now that it's been moved, redo the scan for vars, incl. CV. */
	saveinfrom = ctx->infrom;
	ctx->infrom = true;
	pe->path.body = ptpath_movepaths(ctx, pe->path.body);
	ctx->infrom = saveinfrom;

	/* and return the replacement expr. */
	pe = newpe;
     }
     break;

    case PTE_TUPLE:
     num = ptexprarray_num(pe->tuple);
     for (i=0; i<num; i++) {
	sub = ptexprarray_get(pe->tuple, i);
	sub = ptexpr_movepaths(ctx, sub);
	ptexprarray_set(pe->tuple, i, sub);
     }
     break;

    case PTE_FORALL:
     /* ignore var - scope is limited to the predicate */
     pe->forall.set = ptexpr_movepaths(ctx, pe->forall.set);
     pe->forall.predicate = ptexpr_movepaths(ctx, pe->forall.predicate);
     break;

    case PTE_EXISTS:
     /* ignore var - scope is limited to the predicate */
     pe->exists.set = ptexpr_movepaths(ctx, pe->exists.set);
     pe->exists.predicate = ptexpr_movepaths(ctx, pe->exists.predicate);
     break;

    case PTE_MAP:
     /* ignore var - scope is limited to the result expr */
     pe->map.set = ptexpr_movepaths(ctx, pe->map.set);
     pe->map.result = ptexpr_movepaths(ctx, pe->map.result);
     break;

    case PTE_ASSIGN:
     pe->assign.value = ptexpr_movepaths(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	/* ignore var - scope is limited to the body */
	pe->assign.body = ptexpr_movepaths(ctx, pe->assign.body);
     }
     else {
	/* Scope of var extends past the assignment, so take note of it. */
	movepaths_notevar(ctx, pe->assign.var);
     }
     break;

    case PTE_BOP:
     pe->bop.l = ptexpr_movepaths(ctx, pe->bop.l);
     pe->bop.r = ptexpr_movepaths(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     pe->uop.sub = ptexpr_movepaths(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	num = ptexprarray_num(pe->func.args);
	for (i=0; i<num; i++) {
	   sub = ptexprarray_get(pe->func.args, i);
	   sub = ptexpr_movepaths(ctx, sub);
	   ptexprarray_set(pe->func.args, i, sub);
	}
     }
     break;

    case PTE_READANYVAR:
     /* removed far, far upstream */
     PQLASSERT(0);
     break;

    case PTE_READCOLUMNVAR:
    case PTE_READGLOBALVAR:
    case PTE_VALUE:
     /* nothing */
     break;
   }

   return pe;
}

////////////////////////////////////////////////////////////

struct ptexpr *movepaths(struct pqlcontext *pql, struct ptexpr *pe) {
   struct movepaths ctx;
   bool failed;

   movepaths_init(&ctx, pql);
   pe = ptexpr_movepaths(&ctx, pe);

   if (ctx.curfrom != NULL) {
      /* Had to create a from-clause, and nowhere to put it! Make a select. */

      pe = mkptexpr_select(pql, mkptexpr_from(pql, ctx.curfrom),
			   pe, false);
      ctx.curfrom = NULL;
   }

   failed = ctx.failed;
   movepaths_cleanup(&ctx);

   if (failed) {
      //ptexpr_destroy(pe); -- handled by region allocator
      return NULL;
   }
   
   return pe;
}
