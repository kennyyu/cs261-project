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
 * Apply and remove all rename nodes.
 *
 * Because all our column names are distinguished (even if the user
 * recklessly gives some of them the same string names), there's no
 * danger of any nonsense about variable capture or conflating things
 * that are supposed to be different.
 *
 * Also, because all the names arise structurally (as opposed to being
 * read from the database on the fly) we *can* do this and have no
 * renames left afterwards.
 */

#include "columns.h"
#include "tcalc.h"
#include "passes.h"

struct norenames {
   struct pqlcontext *pql;
   struct tcexprarray renamelist;
};

////////////////////////////////////////////////////////////
// context management

static void norenames_init(struct norenames *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   tcexprarray_init(&ctx->renamelist);
}

static void norenames_cleanup(struct norenames *ctx) {
   PQLASSERT(tcexprarray_num(&ctx->renamelist) == 0);
   tcexprarray_cleanup(ctx->pql, &ctx->renamelist);
}

////////////////////////////////////////////////////////////
// the works

static struct tcexpr *tcexpr_norenames(struct norenames *ctx,
				       struct tcexpr *te);

static void colset_norenames(struct norenames *ctx, struct colset *set) {
   unsigned i, num;
   struct tcexpr *rn;

   if (set == NULL) {
      return;
   }

   num = tcexprarray_num(&ctx->renamelist);
   for (i=num; i-- > 0; ) {
      rn = tcexprarray_get(&ctx->renamelist, i);
      PQLASSERT(rn->type == TCE_RENAME);
      colset_replace(ctx->pql, set, rn->rename.oldcol, rn->rename.newcol);
   }
}

static struct colname *colname_norenames(struct norenames *ctx,
					 struct colname *col) {
   unsigned i, num;
   struct tcexpr *rn;

   if (col == NULL) {
      return NULL;
   }

   num = tcexprarray_num(&ctx->renamelist);
   for (i=num; i-- > 0; ) {
      rn = tcexprarray_get(&ctx->renamelist, i);
      PQLASSERT(rn->type == TCE_RENAME);
      if (col == rn->rename.oldcol) {
	 colname_decref(ctx->pql, rn->rename.oldcol);
	 colname_incref(rn->rename.newcol);
	 return rn->rename.newcol;
      }
   }
   return col;
}

static struct coltree *coltree_norenames(struct norenames *ctx,
					 struct coltree *ct) {
   unsigned i, num;
   struct tcexpr *rn;

   num = tcexprarray_num(&ctx->renamelist);
   for (i=num; i-- > 0; ) {
      rn = tcexprarray_get(&ctx->renamelist, i);
      PQLASSERT(rn->type == TCE_RENAME);
      if (ct == NULL) {
	 if (rn->rename.oldcol == NULL) {
	    colname_incref(rn->rename.newcol);
	    ct = coltree_create_scalar(ctx->pql, rn->rename.newcol);
	 }
      }
      else {
	 coltree_replace(ctx->pql, ct, rn->rename.oldcol, rn->rename.newcol);
      }
   }
   return ct;
}

static void tcvar_norenames(struct norenames *ctx, struct tcvar *var) {
   var->colnames = coltree_norenames(ctx, var->colnames);
}

static void tcexprarray_norenames(struct norenames *ctx,
				  struct tcexprarray *arr) {
   struct tcexpr *sub;
   unsigned i, num;
   
   num = tcexprarray_num(arr);
   for (i=0; i<num; i++) {
      sub = tcexprarray_get(arr, i);
      sub = tcexpr_norenames(ctx, sub);
      tcexprarray_set(arr, i, sub);
   }
}

static struct tcexpr *tcexpr_norenames(struct norenames *ctx,
				       struct tcexpr *te) {
   if (te == NULL) {
      return NULL;
   }
   switch (te->type) {
    case TCE_FILTER:
     te->filter.sub = tcexpr_norenames(ctx, te->filter.sub);
     te->filter.predicate = tcexpr_norenames(ctx, te->filter.predicate);
     break;
    case TCE_PROJECT:
     te->project.sub = tcexpr_norenames(ctx, te->project.sub);
     colset_norenames(ctx, te->project.cols);
     break;
    case TCE_STRIP:
     te->strip.sub = tcexpr_norenames(ctx, te->strip.sub);
     colset_norenames(ctx, te->strip.cols);
     break;
    case TCE_RENAME:
     {
	struct tcexpr *sub;
	unsigned pos;

	tcexprarray_add(ctx->pql, &ctx->renamelist, te, &pos);
	sub = te->rename.sub;
	te->rename.sub = NULL;
	sub = tcexpr_norenames(ctx, sub);
	PQLASSERT(tcexprarray_num(&ctx->renamelist) == pos+1);
	tcexprarray_setsize(ctx->pql, &ctx->renamelist, pos);

	tcexpr_destroy(ctx->pql, te);
	return sub;
     }
     break;
    case TCE_JOIN:
     te->join.left = tcexpr_norenames(ctx, te->join.left);
     te->join.right = tcexpr_norenames(ctx, te->join.right);
     te->join.predicate = tcexpr_norenames(ctx, te->join.predicate);
     break;
    case TCE_ORDER:
     te->order.sub = tcexpr_norenames(ctx, te->order.sub);
     colset_norenames(ctx, te->order.cols);
     break;
    case TCE_UNIQ:
     te->uniq.sub = tcexpr_norenames(ctx, te->uniq.sub);
     colset_norenames(ctx, te->uniq.cols);
     break;
    case TCE_NEST:
     te->nest.sub = tcexpr_norenames(ctx, te->nest.sub);
     colset_norenames(ctx, te->nest.cols);
     te->nest.newcol = colname_norenames(ctx, te->nest.newcol);
     break;
    case TCE_UNNEST:
     te->unnest.sub = tcexpr_norenames(ctx, te->unnest.sub);
     te->unnest.col = colname_norenames(ctx, te->unnest.col);
     break;
    case TCE_DISTINGUISH:
     te->distinguish.sub = tcexpr_norenames(ctx, te->distinguish.sub);
     te->distinguish.newcol = colname_norenames(ctx, te->distinguish.newcol);
     break;
    case TCE_ADJOIN:
     te->adjoin.left = tcexpr_norenames(ctx, te->adjoin.left);
     te->adjoin.func = tcexpr_norenames(ctx, te->adjoin.func);
     te->adjoin.newcol = colname_norenames(ctx, te->adjoin.newcol);
     break;
    case TCE_STEP:
     /* optimization result, should not appear here */
     PQLASSERT(0);
     break;
    case TCE_REPEAT:
     te->repeat.sub = tcexpr_norenames(ctx, te->repeat.sub);
     te->repeat.subendcolumn = colname_norenames(ctx, te->repeat.subendcolumn);
     tcvar_norenames(ctx, te->repeat.loopvar);
     te->repeat.bodystartcolumn = colname_norenames(ctx,
						  te->repeat.bodystartcolumn);
     te->repeat.body = tcexpr_norenames(ctx, te->repeat.body);
     te->repeat.bodypathcolumn = colname_norenames(ctx,
						  te->repeat.bodypathcolumn);
     te->repeat.bodyendcolumn = colname_norenames(ctx,
						  te->repeat.bodyendcolumn);
     te->repeat.repeatpathcolumn = colname_norenames(ctx,
						  te->repeat.repeatpathcolumn);
     te->repeat.repeatendcolumn = colname_norenames(ctx,
						  te->repeat.repeatendcolumn);
     break;
    case TCE_SCAN:
     te->scan.leftobjcolumn = colname_norenames(ctx, te->scan.leftobjcolumn);
     te->scan.edgecolumn = colname_norenames(ctx, te->scan.edgecolumn);
     te->scan.rightobjcolumn = colname_norenames(ctx, te->scan.rightobjcolumn);
     te->scan.predicate = tcexpr_norenames(ctx, te->scan.predicate);
     break;
    case TCE_BOP:
     te->bop.left = tcexpr_norenames(ctx, te->bop.left);
     te->bop.right = tcexpr_norenames(ctx, te->bop.right);
     break;
    case TCE_UOP:
     te->uop.sub = tcexpr_norenames(ctx, te->uop.sub);
     break;
    case TCE_FUNC:
     tcexprarray_norenames(ctx, &te->func.args);
     break;
    case TCE_MAP:
     tcvar_norenames(ctx, te->map.var);
     te->map.set = tcexpr_norenames(ctx, te->map.set);
     te->map.result = tcexpr_norenames(ctx, te->map.result);
     break;
    case TCE_LET:
     tcvar_norenames(ctx, te->let.var);
     te->let.value = tcexpr_norenames(ctx, te->let.value);
     te->let.body = tcexpr_norenames(ctx, te->let.body);
     break;
    case TCE_LAMBDA:
     tcvar_norenames(ctx, te->lambda.var);
     te->lambda.body = tcexpr_norenames(ctx, te->lambda.body);
     break;
    case TCE_APPLY:
     te->apply.lambda = tcexpr_norenames(ctx, te->apply.lambda);
     te->apply.arg = tcexpr_norenames(ctx, te->apply.arg);
     break;
    case TCE_READVAR:
     tcvar_norenames(ctx, te->readvar);
     break;
    case TCE_READGLOBAL:
     break;
    case TCE_CREATEPATHELEMENT:
     te->createpathelement = tcexpr_norenames(ctx, te->createpathelement);
     break;
    case TCE_SPLATTER:
     te->splatter.value = tcexpr_norenames(ctx, te->splatter.value);
     te->splatter.name = tcexpr_norenames(ctx, te->splatter.name);
     break;
    case TCE_TUPLE:
     tcexprarray_norenames(ctx, &te->tuple.exprs);
     colset_norenames(ctx, te->tuple.columns);
     break;
    case TCE_VALUE:
     break;
   }
   te->colnames = coltree_norenames(ctx, te->colnames);
   return te;
}

////////////////////////////////////////////////////////////
// entry point

struct tcexpr *norenames(struct pqlcontext *pql, struct tcexpr *te) {
   struct norenames ctx;

   norenames_init(&ctx, pql);
   te = tcexpr_norenames(&ctx, te);
   norenames_cleanup(&ctx);
   return te;
}
