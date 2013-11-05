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
 * Convert quantifier expressions to map expressions.
 */

#include "pttree.h"
#include "passes.h"

/*
 * Context. We don't need anything but the pqlcontext, but for now at least
 * let's declare the type anyhow in case we want to add to it later.
 */
struct dequantify {
   struct pqlcontext *pql;
};

////////////////////////////////////////////////////////////
// context management

static void dequantify_init(struct dequantify *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
}

static void dequantify_cleanup(struct dequantify *ctx) {
   (void)ctx;
}

////////////////////////////////////////////////////////////
// recursive traversal

static struct ptexpr *ptexpr_dequantify(struct dequantify *ctx,
					struct ptexpr *pe);

static void ptpath_dequantify(struct dequantify *ctx, struct ptpath *pp) {
   unsigned num, i;

   switch (pp->type) {
    case PTP_SEQUENCE:
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	ptpath_dequantify(ctx, ptpatharray_get(&pp->sequence.items, i));
     }
     break;

    case PTP_ALTERNATES:
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	ptpath_dequantify(ctx, ptpatharray_get(&pp->alternates.items, i));
     }
     break;

    case PTP_OPTIONAL:
     ptpath_dequantify(ctx, pp->optional.sub);
     break;

    case PTP_REPEATED:
     ptpath_dequantify(ctx, pp->repeated.sub);
     break;

    case PTP_NILBIND:
     ptpath_dequantify(ctx, pp->nilbind.sub);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	pp->edge.computedname = ptexpr_dequantify(ctx, pp->edge.computedname);
     }
     break;
   }
}

static void ptexprarray_dequantify(struct dequantify *ctx,
				   struct ptexprarray *arr) {
   unsigned num, i;
   struct ptexpr *pe;

   num = ptexprarray_num(arr);
   for (i=0; i<num; i++) {
      pe = ptexprarray_get(arr, i);
      ptexpr_dequantify(ctx, pe);
      ptexprarray_set(arr, i, pe);
   }
}

static struct ptexpr *ptexpr_dequantify(struct dequantify *ctx,
					struct ptexpr *pe) {
   struct ptexpr *newpe;

   switch (pe->type) {
    case PTE_SELECT:
     pe->select.sub = ptexpr_dequantify(ctx, pe->select.sub);
     pe->select.result = ptexpr_dequantify(ctx, pe->select.result);
     break;

    case PTE_FROM:
     ptexprarray_dequantify(ctx, pe->from);
     break;

    case PTE_WHERE:
     pe->where.sub = ptexpr_dequantify(ctx, pe->where.sub);
     pe->where.where = ptexpr_dequantify(ctx, pe->where.where);
     break;

    case PTE_GROUP:
     pe->group.sub = ptexpr_dequantify(ctx, pe->group.sub);
     break;

    case PTE_UNGROUP:
     pe->ungroup.sub = ptexpr_dequantify(ctx, pe->ungroup.sub);
     break;

    case PTE_RENAME:
     pe->rename.sub = ptexpr_dequantify(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	pe->rename.computedname = ptexpr_dequantify(ctx,
						    pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     pe->path.root = ptexpr_dequantify(ctx, pe->path.root);
     ptpath_dequantify(ctx, pe->path.body);
     ptexprarray_dequantify(ctx, &pe->path.morebindings);
     break;

    case PTE_TUPLE:
     ptexprarray_dequantify(ctx, pe->tuple);
     break;

    case PTE_FORALL:
     /* forall K in S: P(K) => alltrue(map K in S: P(K)) */
     newpe = mkptexpr_map(ctx->pql, pe->forall.var,
			  ptexpr_dequantify(ctx, pe->forall.set),
			  ptexpr_dequantify(ctx, pe->forall.predicate));
     pe->forall.var = NULL;
     pe->forall.set = NULL;
     pe->forall.predicate = NULL;
     //ptexpr_destroy(pe); -- handled by region allocator
     pe = mkptexpr_uop(ctx->pql, F_ALLTRUE, newpe);
     break;
			  
    case PTE_EXISTS:
     /* exists K in S: P(K) => anytrue(map K in S: P(K)) */
     newpe = mkptexpr_map(ctx->pql, pe->exists.var,
			  ptexpr_dequantify(ctx, pe->exists.set),
			  ptexpr_dequantify(ctx, pe->exists.predicate));
     pe->exists.var = NULL;
     pe->exists.set = NULL;
     pe->exists.predicate = NULL;
     //ptexpr_destroy(pe); -- handled by region allocator
     pe = mkptexpr_uop(ctx->pql, F_ANYTRUE, newpe);
     break;

    case PTE_MAP:
     pe->map.set = ptexpr_dequantify(ctx, pe->map.set);
     pe->map.result = ptexpr_dequantify(ctx, pe->map.result);
     break;

    case PTE_ASSIGN:
     pe->assign.value = ptexpr_dequantify(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	pe->assign.body = ptexpr_dequantify(ctx, pe->assign.body);
     }
     break;

    case PTE_BOP:
     pe->bop.l = ptexpr_dequantify(ctx, pe->bop.l);
     pe->bop.r = ptexpr_dequantify(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     pe->uop.sub = ptexpr_dequantify(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	ptexprarray_dequantify(ctx, pe->func.args);
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
   return pe;
}

////////////////////////////////////////////////////////////
// top level

struct ptexpr *dequantify(struct pqlcontext *pql, struct ptexpr *pe) {
   struct dequantify ctx;

   dequantify_init(&ctx, pql);
   pe = ptexpr_dequantify(&ctx, pe);
   dequantify_cleanup(&ctx);

   return pe;
}
