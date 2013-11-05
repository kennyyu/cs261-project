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
 * Simple optimizations.
 *
 * This module should contain only simple transforms that require
 * little or no analysis, context tracking, or non-local knowledge,
 * and do only one pass over the tree.
 *
 * It should also not contain any transforms that are subtle in either
 * semantics or implementation, because when we're looking at dumps we
 * really want to be able to read the post-baseopt dump instead of the
 * raw output from tuplify, and we want to be able to do so without
 * worrying excessively over whether it's correct.
 */

#include <stdlib.h> /* for qsort */
#include <string.h> /* for strcmp */

#include "datatype.h"
#include "columns.h"
#include "pqlvalue.h"
#include "tcalc.h"
#include "passes.h"

struct baseopt {
   struct pqlcontext *pql;
};

static void tcexpr_replacevar(struct baseopt *ctx,
			      struct tcexpr *te,
			      struct tcvar *oldvar,
			      struct tcvar *newvar);

////////////////////////////////////////////////////////////
// context management

static void baseopt_init(struct baseopt *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
}

static void baseopt_cleanup(struct baseopt *ctx) {
   (void)ctx;
}

////////////////////////////////////////////////////////////
// small tools/utils

static int unsigned_sortfunc(const void *av, const void *bv) {
   unsigned a = *(const unsigned *)av;
   unsigned b = *(const unsigned *)bv;

   return (a < b) ? -1 : (a == b) ? 0 : 1;
}

/*
 * Return "P1 and P2", pretending each is "true" if it's NULL.
 *
 * XXX this should be shared with the similar code in stepjoins.c.
 */
static struct tcexpr *combine_predicates(struct baseopt *ctx,
					 struct tcexpr *p1,
					 struct tcexpr *p2) {
   if (p1 == NULL) {
      return p2;
   }
   if (p2 == NULL) {
      return p1;
   }

   PQLASSERT(p1->type == TCE_LAMBDA);
   PQLASSERT(p2->type == TCE_LAMBDA);

   tcexpr_replacevar(ctx, p2->lambda.body, p2->lambda.var, p1->lambda.var);

   p1->lambda.body = mktcexpr_bop(ctx->pql,
				  p1->lambda.body, F_AND, p2->lambda.body);
   p2->lambda.body = NULL;
   tcexpr_destroy(ctx->pql, p2);
   return p1;
}

/*
 * Check if TE is a constant true/false value.
 */

static bool tcexpr_istrue(struct tcexpr *te) {
   return (te->type == TCE_VALUE && pqlvalue_isbool(te->value)
	   && pqlvalue_bool_get(te->value));
}

static bool tcexpr_isfalse(struct tcexpr *te) {
   return (te->type == TCE_VALUE && pqlvalue_isbool(te->value)
	   && !pqlvalue_bool_get(te->value));
}

////////////////////////////////////////////////////////////
// larger tests

/*
 * Check if TE uses COL.
 */
static bool tcexpr_uses_column(struct baseopt *ctx, struct tcexpr *te,
			       struct colname *col) {
   unsigned i, num;
   bool ret;

   ret = false; // gcc 4.1

   if (te == NULL) {
      return false;
   }

   switch (te->type) {
    case TCE_FILTER:
     ret = tcexpr_uses_column(ctx, te->filter.sub, col) ||
	tcexpr_uses_column(ctx, te->filter.predicate, col);
     break;
    case TCE_PROJECT:
     ret = tcexpr_uses_column(ctx, te->project.sub, col) ||
	colset_contains(te->project.cols, col);
     break;
    case TCE_STRIP:
     ret = tcexpr_uses_column(ctx, te->strip.sub, col) ||
	colset_contains(te->strip.cols, col);
     break;
    case TCE_RENAME:
     ret = te->rename.oldcol == col || te->rename.newcol == col ||
	tcexpr_uses_column(ctx, te->rename.sub, col);
     break;
    case TCE_JOIN:
     ret = tcexpr_uses_column(ctx, te->join.left, col) ||
	tcexpr_uses_column(ctx, te->join.right, col) ||
	tcexpr_uses_column(ctx, te->join.predicate, col);
     break;
    case TCE_ORDER:
     ret = tcexpr_uses_column(ctx, te->order.sub, col) ||
	colset_contains(te->order.cols, col);
     break;
    case TCE_UNIQ:
     ret = tcexpr_uses_column(ctx, te->uniq.sub, col) ||
	colset_contains(te->uniq.cols, col);
     break;
    case TCE_NEST:
     ret = te->nest.newcol == col ||
	tcexpr_uses_column(ctx, te->nest.sub, col) ||
	colset_contains(te->nest.cols, col);
     break;
    case TCE_UNNEST:
     ret = te->unnest.col == col ||
	tcexpr_uses_column(ctx, te->unnest.sub, col);
     break;
    case TCE_DISTINGUISH:
     ret = te->distinguish.newcol == col ||
	tcexpr_uses_column(ctx, te->distinguish.sub, col);
     break;
    case TCE_ADJOIN:
     ret = te->adjoin.newcol == col ||
	tcexpr_uses_column(ctx, te->adjoin.left, col) ||
	tcexpr_uses_column(ctx, te->adjoin.func, col);
     break;
    case TCE_STEP:
     ret = te->step.subcolumn == col ||
	te->step.leftobjcolumn == col ||
	te->step.edgecolumn == col ||
	te->step.rightobjcolumn == col ||
	tcexpr_uses_column(ctx, te->step.sub, col) ||
	tcexpr_uses_column(ctx, te->step.predicate, col);
     break;
    case TCE_REPEAT:
     ret = te->repeat.subendcolumn == col ||
	te->repeat.bodystartcolumn == col ||
	te->repeat.bodypathcolumn == col ||
	te->repeat.bodyendcolumn == col ||
	te->repeat.repeatpathcolumn == col ||
	te->repeat.repeatendcolumn == col ||
	tcexpr_uses_column(ctx, te->repeat.sub, col) ||
	tcexpr_uses_column(ctx, te->repeat.body, col);
     break;
    case TCE_SCAN:
     ret = te->scan.leftobjcolumn == col ||
	te->scan.edgecolumn == col ||
	te->scan.rightobjcolumn == col ||
	tcexpr_uses_column(ctx, te->scan.predicate, col);
     break;
    case TCE_BOP:
     ret = tcexpr_uses_column(ctx, te->bop.left, col) ||
	tcexpr_uses_column(ctx, te->bop.right, col);
     break;
    case TCE_UOP:
     ret = tcexpr_uses_column(ctx, te->uop.sub, col);
     break;
    case TCE_FUNC:
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	if (tcexpr_uses_column(ctx, tcexprarray_get(&te->func.args,i), col)) {
	   return true;
	}
     }
     ret = false;
     break;
    case TCE_MAP:
     ret = tcexpr_uses_column(ctx, te->map.set, col) ||
	tcexpr_uses_column(ctx, te->map.result, col);
     break;
    case TCE_LET:
     ret = tcexpr_uses_column(ctx, te->let.value, col) ||
	tcexpr_uses_column(ctx, te->let.body, col);
     break;
    case TCE_LAMBDA:
     ret = tcexpr_uses_column(ctx, te->lambda.body, col);
     break;
    case TCE_APPLY:
     ret = tcexpr_uses_column(ctx, te->apply.lambda, col) ||
	tcexpr_uses_column(ctx, te->apply.arg, col);
     break;
    case TCE_READVAR:
    case TCE_READGLOBAL:
     ret = false;
     break;
    case TCE_CREATEPATHELEMENT:
     ret = tcexpr_uses_column(ctx, te->createpathelement, col);
     break;
    case TCE_SPLATTER:
     ret = tcexpr_uses_column(ctx, te->splatter.value, col) ||
	tcexpr_uses_column(ctx, te->splatter.name, col);
     break;
    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     for (i=0; i<num; i++) {
	if (tcexpr_uses_column(ctx, tcexprarray_get(&te->tuple.exprs,i), col)){
	   return true;
	}
     }
     ret = false;
     break;
    case TCE_VALUE:
     ret = false;
     break;
   }
   return ret;
}

/*
 * Check quickly if TCA and TCB are the same. This is not meant to be
 * real CSE, just a simple way to scoop up obvious multiple references
 * to the same variable.
 */
static bool tcexpr_simple_same(struct tcexpr *tca, struct tcexpr *tcb) {
   if (tca->type != tcb->type) {
      return false;
   }
   switch (tca->type) {
    case TCE_PROJECT:
     if (!tcexpr_simple_same(tca->project.sub, tcb->project.sub)) {
	return false;
     }
     return colset_eq(tca->project.cols, tcb->project.cols);
    case TCE_STRIP:
     if (!tcexpr_simple_same(tca->strip.sub, tcb->strip.sub)) {
	return false;
     }
     return colset_eq(tca->strip.cols, tcb->strip.cols);
    case TCE_BOP:
     if (tca->bop.op != tcb->bop.op) {
	return false;
     }
     if (tcexpr_simple_same(tca->bop.left, tcb->bop.left) &&
	 tcexpr_simple_same(tca->bop.right, tcb->bop.right)) {
	return true;
     }
     if (function_commutes(tca->bop.op) &&
	 tcexpr_simple_same(tca->bop.left, tcb->bop.right) &&
	 tcexpr_simple_same(tca->bop.right, tcb->bop.left)) {
	return true;
     }
     return false;
    case TCE_UOP:
     if (tca->uop.op != tcb->uop.op) {
	return false;
     }
     return tcexpr_simple_same(tca->uop.sub, tcb->uop.sub);

    case TCE_READVAR:
     return tca->readvar == tcb->readvar;

    case TCE_READGLOBAL:
     return tca->readglobal == tcb->readglobal;

    case TCE_VALUE:
     return pqlvalue_identical(tca->value, tcb->value);

    default:
     break;
   }
   return false;
}

////////////////////////////////////////////////////////////
// larger operations

/*
 * change a column reference to a variable reference
 */
static struct tcexpr *tcexpr_column2var(struct baseopt *ctx,
					struct tcexpr *te,
					struct colname *oldcol,
					struct tcvar *newvar) {
   unsigned i, num;

   if (te == NULL) {
      return NULL;
   }
   switch (te->type) {
    case TCE_FILTER:
     te->filter.sub = tcexpr_column2var(ctx, te->filter.sub, oldcol, newvar);
     te->filter.predicate = 
	tcexpr_column2var(ctx, te->filter.predicate, oldcol, newvar);
     break;

    case TCE_PROJECT:
     if (colset_contains(te->project.cols, oldcol)) {
	if (colset_num(te->project.cols) == 1) {
	   /* kill off the whole works, including te->project.sub */
	   tcexpr_destroy(ctx->pql, te);
	   tcvar_incref(newvar);
	   te = mktcexpr_readvar(ctx->pql, newvar);
	   te->datatype = newvar->datatype;
	   te->colnames = coltree_clone(ctx->pql, newvar->colnames);
	}
	else {
	   /* yuk, let's hope we don't need to implement this */
	   PQLASSERT(0); // XXX
	}
     }
     else {
	te->project.sub = tcexpr_column2var(ctx, te->project.sub,
					    oldcol, newvar);
     }
     break;

    case TCE_STRIP:
     /* should not happen */
     PQLASSERT(0);
     break;

    case TCE_RENAME:
     if (te->rename.newcol == oldcol) {
	struct tcexpr *tmp;

	tmp = tcexpr_column2var(ctx, te->rename.sub,
				te->rename.oldcol, newvar);
	te->rename.sub = NULL;
	tcexpr_destroy(ctx->pql, te);
	te = tmp;
     }
     else {
	te->rename.sub = tcexpr_column2var(ctx, te->rename.sub,
					   oldcol, newvar);
     }
     break;

    case TCE_JOIN:
     te->join.left = tcexpr_column2var(ctx, te->join.left, oldcol, newvar);
     te->join.right = tcexpr_column2var(ctx, te->join.right, oldcol, newvar);
     te->join.predicate = tcexpr_column2var(ctx, te->join.predicate,
					   oldcol, newvar);
     break;

    case TCE_ORDER:
    case TCE_UNIQ:
     /* should not happen */
     PQLASSERT(0);
     break;

    case TCE_NEST:
    case TCE_UNNEST:
     /* cannot handle, but should not happen, I hope */
     PQLASSERT(0); // XXX?
     break;

    case TCE_DISTINGUISH:
     /* the column came from outside, so we can't have created it */
     PQLASSERT(oldcol != te->distinguish.newcol);
     te->distinguish.sub = tcexpr_column2var(ctx, te->distinguish.sub,
					     oldcol, newvar);
     break;

    case TCE_ADJOIN:
     /* same deal */
     PQLASSERT(oldcol != te->adjoin.newcol);
     te->adjoin.left = tcexpr_column2var(ctx, te->adjoin.left, oldcol, newvar);
     te->adjoin.func = tcexpr_column2var(ctx, te->adjoin.func, oldcol, newvar);
     break;

    case TCE_STEP:
     /* this one may not work */
     PQLASSERT(oldcol != te->step.subcolumn);
     /* same deal again */
     PQLASSERT(oldcol != te->step.leftobjcolumn);
     PQLASSERT(oldcol != te->step.edgecolumn);
     PQLASSERT(oldcol != te->step.rightobjcolumn);
     te->step.sub = tcexpr_column2var(ctx, te->step.sub,
				      oldcol, newvar);
     te->step.predicate = tcexpr_column2var(ctx, te->step.predicate,
					    oldcol, newvar);
     break;
     
    case TCE_REPEAT:
     /* this one may not work */
     PQLASSERT(oldcol != te->repeat.subendcolumn);
     /* same deal again */
     PQLASSERT(oldcol != te->repeat.bodystartcolumn);
     PQLASSERT(oldcol != te->repeat.bodypathcolumn);
     PQLASSERT(oldcol != te->repeat.bodyendcolumn);
     PQLASSERT(oldcol != te->repeat.repeatpathcolumn);
     PQLASSERT(oldcol != te->repeat.repeatendcolumn);
     te->repeat.sub = tcexpr_column2var(ctx, te->repeat.sub,
					oldcol, newvar);
     te->repeat.body = tcexpr_column2var(ctx, te->repeat.body,
					 oldcol, newvar);
     break;

    case TCE_SCAN:
     /* same deal again */
     PQLASSERT(oldcol != te->scan.leftobjcolumn);
     PQLASSERT(oldcol != te->scan.edgecolumn);
     PQLASSERT(oldcol != te->scan.rightobjcolumn);
     te->scan.predicate = tcexpr_column2var(ctx, te->scan.predicate,
					    oldcol, newvar);
     break;

    case TCE_BOP:
     te->bop.left = tcexpr_column2var(ctx, te->bop.left, oldcol, newvar);
     te->bop.right = tcexpr_column2var(ctx, te->bop.right, oldcol, newvar);
     break;

    case TCE_UOP:
     te->uop.sub = tcexpr_column2var(ctx, te->uop.sub, oldcol, newvar);
     break;

    case TCE_FUNC:
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	struct tcexpr *sub;

	sub = tcexprarray_get(&te->func.args, i);
	sub = tcexpr_column2var(ctx, sub, oldcol, newvar);
	tcexprarray_set(&te->func.args, i, sub);
     }
     break;

    case TCE_MAP:
     te->map.set = tcexpr_column2var(ctx, te->map.set, oldcol, newvar);
     te->map.result = tcexpr_column2var(ctx, te->map.result, oldcol, newvar);
     break;

    case TCE_LET:
     te->let.value = tcexpr_column2var(ctx, te->let.value, oldcol, newvar);
     te->let.body = tcexpr_column2var(ctx, te->let.body, oldcol, newvar);
     break;

    case TCE_LAMBDA:
     te->lambda.body = tcexpr_column2var(ctx, te->lambda.body, oldcol, newvar);
     break;

    case TCE_APPLY:
     te->apply.lambda = tcexpr_column2var(ctx, te->apply.lambda,
					  oldcol, newvar);
     te->apply.arg = tcexpr_column2var(ctx, te->apply.arg,
				       oldcol, newvar);
     break;

    case TCE_READVAR:
    case TCE_READGLOBAL:
     break;
     
    case TCE_CREATEPATHELEMENT:
     te->createpathelement = tcexpr_column2var(ctx, te->createpathelement,
					       oldcol, newvar);
     break;

    case TCE_SPLATTER:
     te->splatter.value = tcexpr_column2var(ctx, te->splatter.value,
					  oldcol, newvar);
     te->splatter.name = tcexpr_column2var(ctx, te->splatter.name,
				       oldcol, newvar);
     break;

    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     for (i=0; i<num; i++) {
	struct tcexpr *sub;

	sub = tcexprarray_get(&te->tuple.exprs, i);
	sub = tcexpr_column2var(ctx, sub, oldcol, newvar);
	tcexprarray_set(&te->tuple.exprs, i, sub);
     }
     break;

    case TCE_VALUE:
     break;
   }
   return te;
}

static void tcexpr_replacevar(struct baseopt *ctx,
			      struct tcexpr *te,
			      struct tcvar *oldvar,
			      struct tcvar *newvar) {
   unsigned i, num;

   if (te == NULL) {
      return;
   }

   switch (te->type) {
    case TCE_FILTER:
     tcexpr_replacevar(ctx, te->filter.sub, oldvar, newvar);
     tcexpr_replacevar(ctx, te->filter.predicate, oldvar, newvar);
     break;

    case TCE_PROJECT:
     tcexpr_replacevar(ctx, te->project.sub, oldvar, newvar);
     break;

    case TCE_STRIP:
     tcexpr_replacevar(ctx, te->strip.sub, oldvar, newvar);
     break;

    case TCE_RENAME:
     tcexpr_replacevar(ctx, te->rename.sub, oldvar, newvar);
     break;

    case TCE_JOIN:
     tcexpr_replacevar(ctx, te->join.left, oldvar, newvar);
     tcexpr_replacevar(ctx, te->join.right, oldvar, newvar);
     tcexpr_replacevar(ctx, te->join.predicate, oldvar, newvar);
     break;

    case TCE_ORDER:
     tcexpr_replacevar(ctx, te->order.sub, oldvar, newvar);
     break;

    case TCE_UNIQ:
     tcexpr_replacevar(ctx, te->uniq.sub, oldvar, newvar);
     break;

    case TCE_NEST:
     tcexpr_replacevar(ctx, te->nest.sub, oldvar, newvar);
     break;

    case TCE_UNNEST:
     tcexpr_replacevar(ctx, te->unnest.sub, oldvar, newvar);
     break;

    case TCE_DISTINGUISH:
     tcexpr_replacevar(ctx, te->distinguish.sub, oldvar, newvar);
     break;

    case TCE_ADJOIN:
     tcexpr_replacevar(ctx, te->adjoin.left, oldvar, newvar);
     tcexpr_replacevar(ctx, te->adjoin.func, oldvar, newvar);
     break;

    case TCE_STEP:
     tcexpr_replacevar(ctx, te->step.sub, oldvar, newvar);
     tcexpr_replacevar(ctx, te->step.predicate, oldvar, newvar);
     break;

    case TCE_REPEAT:
     tcexpr_replacevar(ctx, te->repeat.sub, oldvar, newvar);
     tcexpr_replacevar(ctx, te->repeat.body, oldvar, newvar);
     break;

    case TCE_SCAN:
     tcexpr_replacevar(ctx, te->scan.predicate, oldvar, newvar);
     break;

    case TCE_BOP:
     tcexpr_replacevar(ctx, te->bop.left, oldvar, newvar);
     tcexpr_replacevar(ctx, te->bop.right, oldvar, newvar);
     break;

    case TCE_UOP:
     tcexpr_replacevar(ctx, te->uop.sub, oldvar, newvar);
     break;

    case TCE_FUNC:
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	tcexpr_replacevar(ctx, tcexprarray_get(&te->func.args, i),
			  oldvar, newvar);
     }
     break;

    case TCE_MAP:
     tcexpr_replacevar(ctx, te->map.set, oldvar, newvar);
     tcexpr_replacevar(ctx, te->map.result, oldvar, newvar);
     break;

    case TCE_LET:
     tcexpr_replacevar(ctx, te->let.value, oldvar, newvar);
     tcexpr_replacevar(ctx, te->let.body, oldvar, newvar);
     break;

    case TCE_LAMBDA:
     tcexpr_replacevar(ctx, te->lambda.body, oldvar, newvar);
     break;

    case TCE_APPLY:
     tcexpr_replacevar(ctx, te->apply.lambda, oldvar, newvar);
     tcexpr_replacevar(ctx, te->apply.arg, oldvar, newvar);
     break;

    case TCE_READVAR:
     if (te->readvar == oldvar) {
	tcvar_decref(ctx->pql, te->readvar);
	tcvar_incref(newvar);
	te->readvar = newvar;
	/* types? XXX */
     }
     break;

    case TCE_READGLOBAL:
     break;

    case TCE_CREATEPATHELEMENT:
     tcexpr_replacevar(ctx, te->createpathelement, oldvar, newvar);
     break;

    case TCE_SPLATTER:
     tcexpr_replacevar(ctx, te->splatter.value, oldvar, newvar);
     tcexpr_replacevar(ctx, te->splatter.name, oldvar, newvar);
     break;

    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     for (i=0; i<num; i++) {
	tcexpr_replacevar(ctx, tcexprarray_get(&te->tuple.exprs, i),
			  oldvar, newvar);
     }
     break;

    case TCE_VALUE:
     break;
   }
}

////////////////////////////////////////////////////////////
// optimizations

/*
 * This module works as follows:
 *
 * 1. Process leaves first. This means that when working at a
 * particular point, all the subnodes are already optimized as far as
 * we can take them, so except in a few special cases there's no need
 * to reprocess whole subtrees again after working.
 *
 * 1a. After an optimization changes the node type in a particular
 * place in the tree, rerun any_baseopt() on it but not its unchanged
 * descendents.
 *
 * 2. Try combining first. If we can't combine, then try moving past.
 *
 * 3. Separate the transformation logic (that munges the tree) from
 * the decision logic that matches the tree and chooses what munging
 * to do. This makes it possible to review the logic independently of
 * mechanism, and is (I think) worthwhile even though most of the
 * transformations will be used exactly once.
 *
 * 4. The decision logic should repeat as long as the node it's
 * looking at remains the same type. If the node changes type it
 * should make a tail-call to any_baseopt().
 */

static struct tcexpr *any_baseopt(struct baseopt *ctx, struct tcexpr *te);

////////////////////////////////////////////////////////////
// destruction transforms

/*
 * Destroy TE and return its principal subexpression to replace it.
 *
 * For nodes that have multiple subexpressions, such as let bindings,
 * the node that's returned is the one we still need after the
 * optimization that calls here. Nodes where this isn't clear or isn't
 * uniquely determined will cause assertion failure.
 */
static struct tcexpr *tcexpr_destruct(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *ret;
   struct tcexpr **slot;

   (void)ctx;

   switch (te->type) {
    case TCE_FILTER:      slot = &te->filter.sub; break;
    case TCE_PROJECT:     slot = &te->project.sub; break;
    case TCE_STRIP:       slot = &te->strip.sub; break;
    case TCE_RENAME:      slot = &te->rename.sub; break;
    case TCE_ORDER:       slot = &te->order.sub; break;
    case TCE_UNIQ:        slot = &te->uniq.sub; break;
    case TCE_NEST:        slot = &te->nest.sub; break;
    case TCE_UNNEST:      slot = &te->unnest.sub; break;
    case TCE_DISTINGUISH: slot = &te->distinguish.sub; break;
    case TCE_ADJOIN:      slot = &te->adjoin.left; break;
    case TCE_STEP:        slot = &te->step.sub; break;
    case TCE_UOP:         slot = &te->uop.sub; break;
    case TCE_MAP:         slot = &te->map.set; break;
    case TCE_LET:         slot = &te->let.body; break;
    case TCE_LAMBDA:      slot = &te->lambda.body; break;
    case TCE_APPLY:       slot = &te->apply.lambda; break;
    case TCE_SPLATTER:    slot = &te->splatter.value; break;

    case TCE_JOIN:
    case TCE_REPEAT:
    case TCE_BOP:
    case TCE_FUNC:
    case TCE_CREATEPATHELEMENT:
    case TCE_TUPLE:
     /* two or more subexpressions of near-equal weight */
     PQLASSERT(0);
     return te;

    case TCE_SCAN:
    case TCE_READVAR:
    case TCE_READGLOBAL:
    case TCE_VALUE:
     /* no subexpression */
     PQLASSERT(0);
     return te;
   }

   ret = *slot;
   *slot = NULL;
   tcexpr_destroy(ctx->pql, te);
   return ret;
}

static struct tcexpr *bop_destruct_keepleft(struct baseopt *ctx,
					    struct tcexpr *te) {
   struct tcexpr *ret;

   PQLASSERT(te->type == TCE_BOP);
   ret = te->bop.left;
   te->bop.left = NULL;
   tcexpr_destroy(ctx->pql, te);
   return ret;
}

static struct tcexpr *bop_destruct_keepright(struct baseopt *ctx,
					     struct tcexpr *te) {
   struct tcexpr *ret;

   PQLASSERT(te->type == TCE_BOP);
   ret = te->bop.right;
   te->bop.right = NULL;
   tcexpr_destroy(ctx->pql, te);
   return ret;
}

////////////////////////////////////////////////////////////
// combining transforms

//////////////////////////////
// generic

/*
 * foo => constant
 * (consumes VAL)
 */
static struct tcexpr *comb_toconstant(struct baseopt *ctx,
				      struct tcexpr *te,
				      struct pqlvalue *val) {
   struct coltree *cols;
   struct datatype *t;

   /* Keep the type and columns, destroy the rest */
   cols = te->colnames;
   te->colnames = NULL;
   t = te->datatype;
   te->datatype = NULL;;
   tcexpr_destroy(ctx->pql, te);

   /*
    * FUTURE: should be an op in tcalc.c to clean just the union part
    * of a tcexpr, so we don't have to allocate a new one here.
    */

   te = mktcexpr_value(ctx->pql, val);
   te->colnames = cols;
   te->datatype = t;
   return te;
}

/*
 * uop(X x Y) => uop(X) newbop uop(Y)
 */
static struct tcexpr *comb_distribute_uop_over_join(struct baseopt *ctx,
						    struct tcexpr *te,
						    enum functions newbop) {
   struct tcexpr *sub;
   enum functions uop;

   PQLASSERT(te->type == TCE_UOP);

   uop = te->uop.op;
   sub = te->uop.sub;
   te->uop.sub = NULL;

   PQLASSERT(sub->type == TCE_JOIN);
   PQLASSERT(sub->join.predicate == NULL);

   te->type = TCE_BOP;
   te->bop.op = newbop;
   te->bop.left = mktcexpr_uop(ctx->pql, uop, sub->join.left);
   te->bop.right = mktcexpr_uop(ctx->pql, uop, sub->join.right);

   te->bop.left->datatype = te->datatype;
   te->bop.left->colnames = coltree_create_scalar_fresh(ctx->pql);
   te->bop.right->datatype = te->datatype;
   te->bop.right->colnames = coltree_create_scalar_fresh(ctx->pql);
   /* keep te->datatype/te->colnames unchanged */

   sub->join.left = sub->join.right = NULL;
   tcexpr_destroy(ctx->pql, sub);

   return te;
}

/*
 * uop(X bop Y) => uop(X) newbop uop(Y)
 */
static struct tcexpr *comb_distribute_uop_over_bop(struct baseopt *ctx,
						   struct tcexpr *te,
						   enum functions newbop) {
   struct tcexpr *sub;
   enum functions uop;

   PQLASSERT(te->type == TCE_UOP);

   uop = te->uop.op;
   sub = te->uop.sub;
   te->uop.sub = NULL;

   PQLASSERT(sub->type == TCE_BOP);

   te->type = TCE_BOP;
   te->bop.op = newbop;
   te->bop.left = mktcexpr_uop(ctx->pql, uop, sub->bop.left);
   te->bop.right = mktcexpr_uop(ctx->pql, uop, sub->bop.right);

   te->bop.left->datatype = te->datatype;
   te->bop.left->colnames = coltree_create_scalar_fresh(ctx->pql);
   te->bop.right->datatype = te->datatype;
   te->bop.right->colnames = coltree_create_scalar_fresh(ctx->pql);
   /* keep te->datatype/te->colnames unchanged */

   sub->bop.left = sub->bop.right = NULL;
   tcexpr_destroy(ctx->pql, sub);

   return te;
}

//////////////////////////////
// set

/*
 * X in (A union B) => X in A or X in B
 * X in (A intersect B) => X in A and X in B
 * X in (A except B) => X in A and not X in B
 */
static struct tcexpr *comb_element_setop(struct baseopt *ctx,
					 struct tcexpr *te,
					 enum functions newop,
					 bool addnot) {
   struct tcexpr *a, *b, *x;
   struct tcexpr *let, *newte;
   struct tcvar *letvar;

   PQLASSERT(te->type == TCE_BOP && te->bop.op == F_IN);
   PQLASSERT(te->bop.right->type == TCE_BOP);

   /* we will let-bind the element, to avoid having to do CSE on it later */

   letvar = mktcvar_fresh(ctx->pql);
   letvar->datatype = te->bop.left->datatype;
   letvar->colnames = coltree_clone(ctx->pql, te->bop.left->colnames);

   a = te->bop.right->bop.left;
   b = te->bop.right->bop.right;
   te->bop.right->bop.left = NULL;
   te->bop.right->bop.right = NULL;

   x = mktcexpr_readvar(ctx->pql, letvar);
   x->datatype = letvar->datatype;
   x->colnames = coltree_clone(ctx->pql, letvar->colnames);

   a = mktcexpr_bop(ctx->pql, x, F_IN, a);
   a->datatype = datatype_bool(ctx->pql);
   a->colnames = coltree_create_scalar_fresh(ctx->pql);

   x = mktcexpr_readvar(ctx->pql, letvar);
   x->datatype = letvar->datatype;
   x->colnames = coltree_clone(ctx->pql, letvar->colnames);

   b = mktcexpr_bop(ctx->pql, x, F_IN, b);
   b->datatype = datatype_bool(ctx->pql);
   b->colnames = coltree_create_scalar_fresh(ctx->pql);

   if (addnot) {
      b = mktcexpr_uop(ctx->pql, F_NOT, b);
      b->datatype = b->uop.sub->datatype;
      b->colnames = coltree_clone(ctx->pql, b->uop.sub->colnames);
   }

   newte = mktcexpr_bop(ctx->pql, a, newop, b);
   newte->datatype = te->datatype;
   newte->colnames = te->colnames;
   te->datatype = NULL;
   te->colnames = NULL;

   let = mktcexpr_let(ctx->pql, letvar, te->bop.left, newte);
   te->bop.left = NULL;

   tcexpr_destroy(ctx->pql, te);
   return let;
}
 
//////////////////////////////
// aggregate

/*
 * count(foo X) => count(X)
 */
static void comb_count_destroysub(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_UOP && te->uop.op == F_COUNT);
   te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
}

/*
 * count(unnest_a X) => sum(map X0 in (project_a X): count(X0))
 * sum(unnest_a X) => sum(map X0 in (project_a X): sum(X0))
 * max(unnest_a X) => max(map X0 in (project_a X): max(X0))
 * min(unnest_a X) => min(map X0 in (project_a X): min(X0))
 * anytrue(unnest_a X) => anytrue(map X0 in (project_a X): anytrue(X0))
 * alltrue(unnest_a X) => alltrue(map X0 in (project_a X): alltrue(X0))
 *
 * note: won't work for avg() directly
 */
static struct tcexpr *comb_agg_unnest(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *inner, *proj, *count, *map;
   struct tcvar *itervar;
   struct colname *col;
   unsigned index;
   int result;
   enum functions startop, combineop;

   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.op == F_COUNT ||
	     te->uop.op == F_SUM ||
	     te->uop.op == F_MAX ||
	     te->uop.op == F_MIN ||
	     te->uop.op == F_ANYTRUE ||
	     te->uop.op == F_ALLTRUE);
   PQLASSERT(te->uop.sub->type == TCE_UNNEST);

   startop = te->uop.op;
   if (startop == F_COUNT) {
      combineop = F_SUM;
   }
   else {
      combineop = startop;
   }

   /* pull out the subexpression and the unnest column */
   inner = te->uop.sub->unnest.sub;
   col = te->uop.sub->unnest.col;

   /* reuse the unnest expression node for the projection */

   proj = te->uop.sub;
   te->uop.sub = NULL;

   proj->type = TCE_PROJECT;
   proj->project.sub = inner;
   proj->project.cols = colset_singleton(ctx->pql, col);
   result = coltree_find(inner->colnames, col, &index);
   PQLASSERT(result == 0);
   proj->datatype = datatype_getnth(inner->datatype, index);
   proj->colnames = coltree_project(ctx->pql, inner->colnames,
				    proj->project.cols);

   /* cons up the rest */

   itervar = mktcvar_fresh(ctx->pql);
   tcvar_incref(itervar);
   itervar->datatype = datatype_set_member(proj->datatype);
   itervar->colnames = coltree_clone(ctx->pql, proj->colnames);

   count = mktcexpr_uop(ctx->pql, startop,
			mktcexpr_readvar(ctx->pql,itervar));
   count->datatype = datatype_int(ctx->pql);
   count->colnames = coltree_clone(ctx->pql, te->colnames);

   map = mktcexpr_map(ctx->pql, itervar, proj, count);
   map->datatype = datatype_set(ctx->pql, count->datatype);
   map->colnames = coltree_clone(ctx->pql, count->colnames);

   te->uop.op = combineop;
   te->uop.sub = map;

   return te;
}

/*
 * count(A ++ B) => count(A) + count(B)
 */
static struct tcexpr *comb_count_concat(struct baseopt *ctx,struct tcexpr *te){
   struct tcexpr *sub;

   PQLASSERT(te->type == TCE_UOP && te->uop.op == F_COUNT);
   PQLASSERT(te->uop.sub->type == TCE_BOP);

   sub = te->uop.sub;

   te->type = TCE_BOP;
   te->bop.op = F_ADD;
   te->bop.left = mktcexpr_uop(ctx->pql, F_COUNT, sub->bop.left);
   te->bop.right = mktcexpr_uop(ctx->pql, F_COUNT, sub->bop.right);

   te->bop.left->datatype = te->datatype;
   te->bop.left->colnames = coltree_clone(ctx->pql, te->colnames);
   te->bop.right->datatype = te->datatype;
   te->bop.right->colnames = coltree_clone(ctx->pql, te->colnames);

   sub->bop.left = NULL;
   sub->bop.right = NULL;
   tcexpr_destroy(ctx->pql, sub);

   return te;
}

////////////////////////////////////////////////////////////
// code motion transforms

/*
 * X op map A in B: C  =>  aggop(map A in B: X newop C)
 */
static struct tcexpr *move_bop_into_map_on_right(struct baseopt *ctx,
						 struct tcexpr *te,
						 enum functions aggop,
						 struct datatype *intermed,
						 enum functions newop) {
   struct tcexpr *map, *body, *agg;

   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.right->type == TCE_MAP);

   map = te->bop.right;
   body = map->map.result;

   te->bop.right = body;
   te->bop.op = newop;
   map->map.result = te;

   agg = mktcexpr_uop(ctx->pql, aggop, map);

   agg->datatype = te->datatype;
   agg->colnames = te->colnames;

   map->datatype = datatype_set(ctx->pql, intermed);
   map->colnames = coltree_create_scalar_fresh(ctx->pql);

   te->datatype = intermed;
   te->colnames = coltree_create_scalar_fresh(ctx->pql);

   return agg;
}

/*
 * X op let A = B: C  =>  let A = B: X op C
 */
static struct tcexpr *move_bop_into_let_on_right(struct baseopt *ctx,
						 struct tcexpr *te) {
   struct tcexpr *let, *body;

   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.right->type == TCE_LET);

   let = te->bop.right;
   body = let->let.body;

   te->bop.right = body;
   let->let.body = te;

   let->datatype = te->datatype;
   coltree_destroy(ctx->pql, let->colnames);
   let->colnames = coltree_clone(ctx->pql, te->colnames);

   return let;
}

/*
 * op let A = B: C  =>  let A = B: op C
 */
static struct tcexpr *move_uop_into_let(struct baseopt *ctx,
					struct tcexpr *te) {
   struct tcexpr *let, *body;

   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.sub->type == TCE_LET);

   let = te->uop.sub;
   body = let->let.body;

   te->uop.sub = body;
   let->let.body = te;

   let->datatype = te->datatype;
   coltree_destroy(ctx->pql, let->colnames);
   let->colnames = coltree_clone(ctx->pql, te->colnames);

   return let;
}

////////////////////////////////////////////////////////////
// optimization matching for functions

//////////////////////////////
// set

static struct tcexpr *union_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_UNION);

   (void)ctx;

   /*
    * A union (B union C) <=> (A union B) union C
    *
    * This requires data about estimated sizes, which we don't have
    * here, so this optimization will be done in a later pass,
    * probably as a side effect of join planning.
    *
    * FUTURE: we should consider transforming trees of unions into a
    * single multiunion(A, B, C) operation, because that will make it
    * easier to choose a processing order downstream. Alternatively it
    * allows processing them all at once, as large unions are best
    * done as basically a merge sort.
    */

   /*
    * A union B => (uniq A) union (uniq B)
    *
    * This has to wait until we figure out how to represent
    * sortedness. XXX
    */

   /*
    * (A intersect B) union (A intersect C) => A intersect (B union C)
    * (A except B) union (A except C) => A except (B intersect C)
    * (A except B) union (A intersect C) => A except (B except C)
    * etc.
    *
    * this is a pain to test, since there are four ways two of the
    * subexpressions could be the same. future/XXX
    */


   /* A union A => A */
   if (tcexpr_simple_same(te->bop.left, te->bop.right)) {
      return bop_destruct_keepleft(ctx, te);
   }

   if (te->bop.right->type == TCE_VALUE) {
      /* A union {} => A */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} union B => B */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepright(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE &&
       te->bop.right->type == TCE_VALUE &&
       pqlvalue_isset(te->bop.left->value) &&
       pqlvalue_isset(te->bop.right->value) &&
       pqlvalue_set_getnum(te->bop.left->value) == 1 &&
       pqlvalue_set_getnum(te->bop.right->value) == 1) {
      /* catch the (set(1) union set(2)) case */
      const struct pqlvalue *val;

      // XXX this is wrong if the two sets contain the same element */

      val = pqlvalue_set_get(te->bop.right->value, 0);
      pqlvalue_set_add(te->bop.left->value, pqlvalue_clone(ctx->pql, val)); 
      return bop_destruct_keepleft(ctx, te);
   }


   return te;
}

static struct tcexpr *intersect_baseopt(struct baseopt *ctx,struct tcexpr *te){
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_INTERSECT);

   (void)ctx;

   /*
    * A intersect (B intersect C) <=> (A intersect B) intersect C
    * (same as unions; see above)
    */

   /*
    * A intersect B => (uniq A) intersect (uniq B)
    * A intersect B => (uniq A) intersectall (uniq B)
    *
    * This has to wait until we figure out how to represent
    * sortedness. XXX
    */

   /*
    * (A union B) intersect (A union C) => A union (B intersect C)
    *
    * this is a pain to test, since there are four ways two of the
    * subexpressions could be the same. future/XXX
    */


   /* A intersect A => A */
   if (tcexpr_simple_same(te->bop.left, te->bop.right)) {
      return bop_destruct_keepleft(ctx, te);
   }

   if (te->bop.right->type == TCE_VALUE) {
      /* A intersect {} => {} */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepright(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} intersect B => {} */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   return te;
}

static struct tcexpr *except_baseopt(struct baseopt *ctx,struct tcexpr *te){
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_EXCEPT);

   /*
    * A except B => (uniq A) except (uniq B)
    * A except B => (uniq A) exceptall (uniq B)
    *
    * This has to wait until we figure out how to represent
    * sortedness. XXX
    */

   /*
    * (A union B) except (C except A) => A union (B except C)
    * this is a pain to test, as above. XXX
    */


   /* A except A => {} */
   if (tcexpr_simple_same(te->bop.left, te->bop.right)) {
      return comb_toconstant(ctx, te, pqlvalue_emptyset(ctx->pql));
   }

   if (te->bop.right->type == TCE_VALUE) {
      /* A except {} => A */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} except B => {} */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   return te;
}

static struct tcexpr *unionall_baseopt(struct baseopt *ctx, struct tcexpr *te){
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_UNIONALL);

   (void)ctx;

   /*
    * A unionall (B unionall C) <=> (A unionall B) unionall C
    *
    * Even this requires data about estimated sizes (it is or can be
    * marginally more efficient to start with the largest set and add
    * the others to it) so it won't be done here.
    */

   if (te->bop.right->type == TCE_VALUE) {
      /* A unionall {} => A */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} unionall B => B */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepright(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE &&
       te->bop.right->type == TCE_VALUE &&
       pqlvalue_isset(te->bop.left->value) &&
       pqlvalue_isset(te->bop.right->value) &&
       pqlvalue_set_getnum(te->bop.left->value) == 1 &&
       pqlvalue_set_getnum(te->bop.right->value) == 1) {
      /* catch the (set(1) unionall set(2)) case */
      const struct pqlvalue *val;

      val = pqlvalue_set_get(te->bop.right->value, 0);
      pqlvalue_set_add(te->bop.left->value, pqlvalue_clone(ctx->pql, val)); 
      return bop_destruct_keepleft(ctx, te);
   }

   return te;
}

static struct tcexpr *intersectall_baseopt(struct baseopt *ctx,
					   struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_INTERSECTALL);

   (void)ctx;

   /*
    * A intersectall (B intersectall C) <=> (A intersectall B) intersectall C
    * (same as unions; see above)
    */

   /* A intersectall A => A */
   if (tcexpr_simple_same(te->bop.left, te->bop.right)) {
      return bop_destruct_keepleft(ctx, te);
   }

   if (te->bop.right->type == TCE_VALUE) {
      /* A intersectall {} => {} */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepright(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} intersectall B => {} */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   return te;
}

static struct tcexpr *exceptall_baseopt(struct baseopt *ctx,struct tcexpr *te){
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_EXCEPTALL);

   /* A exceptall A => {} */
   if (tcexpr_simple_same(te->bop.left, te->bop.right)) {
      return comb_toconstant(ctx, te, pqlvalue_emptyset(ctx->pql));
   }

   if (te->bop.right->type == TCE_VALUE) {
      /* A exceptall {} => A */
      if (pqlvalue_isset(te->bop.right->value) &&
	  pqlvalue_set_getnum(te->bop.right->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   if (te->bop.left->type == TCE_VALUE) {
      /* {} exceptall B => {} */
      if (pqlvalue_isset(te->bop.left->value) &&
	  pqlvalue_set_getnum(te->bop.left->value) == 0) {
	 return bop_destruct_keepleft(ctx, te);
      }
   }

   return te;
}

static struct tcexpr *subset_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_IN);

   /* set(X) in set(Y)  =>  X = Y */
   if (te->bop.left->type == TCE_UOP && te->bop.left->uop.op == F_MAKESET &&
       te->bop.right->type == TCE_UOP && te->bop.right->uop.op == F_MAKESET) {
      te->bop.left = tcexpr_destruct(ctx, te->bop.left);
      te->bop.right = tcexpr_destruct(ctx, te->bop.right);
      te->bop.op = F_EQ;
      return any_baseopt(ctx, te);
   }

   return te;
}

static struct tcexpr *element_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_IN);

   /* X in set(Y)  =>  X = Y */
   if (te->bop.right->type == TCE_UOP && te->bop.right->uop.op == F_MAKESET) {
      te->bop.right = tcexpr_destruct(ctx, te->bop.right);
      te->bop.op = F_EQ;
      return any_baseopt(ctx, te);
   }

   /*
    * X in (A union B) => let T = X: T in A or T in B
    * X in (A intersect B) => let T = X: T in A and T in B
    * X in (A except B) => let T = X: T in A and not T in B
    * X in (A ++ B) => let T = X: T in A or T in B
    */
   if (te->bop.right->type == TCE_BOP && 
       (te->bop.right->bop.op == F_UNION ||
	te->bop.right->bop.op == F_INTERSECT ||
	te->bop.right->bop.op == F_EXCEPT ||
	te->bop.right->bop.op == F_CONCAT)) {
      enum functions newop;
      bool addnot;

      switch (te->bop.right->bop.op) {
       case F_UNION:     newop = F_OR;  addnot = false; break;
       case F_INTERSECT: newop = F_AND; addnot = false; break;
       case F_EXCEPT:    newop = F_AND; addnot = true;  break;
       case F_CONCAT:    newop = F_OR;  addnot = false; break;
       default: PQLASSERT(0); break;
      }
      te = comb_element_setop(ctx, te, newop, addnot);
      PQLASSERT(te->type == TCE_LET);
      PQLASSERT(te->let.body->type == TCE_BOP);
      if (addnot) {
	 PQLASSERT(te->let.body->bop.right->type == TCE_UOP);
	 te->let.body->bop.right->uop.sub =
	    any_baseopt(ctx, te->let.body->bop.right->uop.sub);
      }
      te->let.body->bop.right = any_baseopt(ctx, te->let.body->bop.right);
      te->let.body->bop.left = any_baseopt(ctx, te->let.body->bop.left);
      te->let.body = any_baseopt(ctx, te->let.body);
      return any_baseopt(ctx, te);
   }

   /* X in (A where \K -> F(K))  =>  let T = X: T in A and F(T) */
   /* XXX notyet */

   /* X in A x B  =>  X1 in A and X2 in B, where X = X1 x X2 */
   /* XXX notyet */

   /*
    * X in (A order by ...) => X in A
    * X in (uniq A) => X in A
    */
   if (te->bop.right->type == TCE_ORDER ||
       te->bop.right->type == TCE_UNIQ) {
      te->bop.right = tcexpr_destruct(ctx, te->bop.right);
      /* do some more, by tail call */
      return element_baseopt(ctx, te);
   }

   /*
    * X in A |+| \K -> F(K) as C  =>  strip_C(X) in A and project_C(X) = F(X)
    */
   /* XXX notyet */

   /*
    * X in map A in B: C  =>  anytrue(map A in B: X = C)
    */
   if (te->bop.right->type == TCE_MAP) {
      te = move_bop_into_map_on_right(ctx, te, F_ANYTRUE,
				      datatype_bool(ctx->pql), F_EQ);
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.sub->type == TCE_MAP);
      te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
      te->uop.sub = any_baseopt(ctx, te->uop.sub);
      return any_baseopt(ctx, te);
   }

   /*
    * X in let A = B: C  =>  let A = B: X in C
    */
   if (te->bop.right->type == TCE_LET) {
      te = move_bop_into_let_on_right(ctx, te);
      PQLASSERT(te->type == TCE_LET);
      te->let.body = any_baseopt(ctx, te->let.body);
      return any_baseopt(ctx, te);
   }

   /*
    * X in {} -> false
    */
   if (te->bop.right->type == TCE_VALUE &&
       ((pqlvalue_isset(te->bop.right->value) &&
	 pqlvalue_set_getnum(te->bop.right->value) == 0) ||
	(pqlvalue_issequence(te->bop.right->value) &&
	 pqlvalue_sequence_getnum(te->bop.right->value) == 0))) {
      return comb_toconstant(ctx, te, pqlvalue_bool(ctx->pql, false));
   }

   return te;
}

static struct tcexpr *in_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_IN);

   /* XXX should distinguish SUBSET from ELEMENT upstream */
   if (datatype_eq(te->bop.left->datatype, te->bop.right->datatype)) {
      return subset_baseopt(ctx, te);
   }
   else {
      return element_baseopt(ctx, te);
   }
}

static struct tcexpr *nonempty_baseopt(struct baseopt *ctx, struct tcexpr *te){
   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.op == F_NONEMPTY);

   /*
    * nonempty(foo X) => X
    */
   if (te->uop.sub->type == TCE_PROJECT ||
       te->uop.sub->type == TCE_STRIP ||
       te->uop.sub->type == TCE_RENAME ||
       te->uop.sub->type == TCE_ORDER ||
       te->uop.sub->type == TCE_UNIQ ||
       te->uop.sub->type == TCE_NEST ||
       te->uop.sub->type == TCE_UNNEST ||
       te->uop.sub->type == TCE_DISTINGUISH ||
       te->uop.sub->type == TCE_ADJOIN ||
       te->uop.sub->type == TCE_LET ||
       te->uop.sub->type == TCE_MAP) {
      te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
      return nonempty_baseopt(ctx, te);
   }

   /*
    * nonempty(A x B) => nonempty(A) and nonempty(B)
    */
   if (te->uop.sub->type == TCE_JOIN && te->uop.sub->join.predicate == NULL) {
      te = comb_distribute_uop_over_join(ctx, te, F_AND);
      PQLASSERT(te->type == TCE_BOP);
      te->bop.left = any_baseopt(ctx, te->bop.left);
      te->bop.right = any_baseopt(ctx, te->bop.right);
      return any_baseopt(ctx, te);
   }

   if (te->uop.sub->type == TCE_BOP &&
       (te->bop.right->bop.op == F_UNION ||
	te->bop.right->bop.op == F_INTERSECT ||
	te->bop.right->bop.op == F_CONCAT)) {
      enum functions newop;

      switch (te->uop.sub->bop.op) {
       case F_UNION:     newop = F_AND; break;
       case F_INTERSECT: newop = F_OR;  break;
       case F_CONCAT:    newop = F_AND; break;
       default: PQLASSERT(0); break;
      }
      te = comb_distribute_uop_over_bop(ctx, te, newop);
      PQLASSERT(te->type == TCE_BOP);
      te->bop.right = any_baseopt(ctx, te->bop.right);
      te->bop.left = any_baseopt(ctx, te->bop.left);
      return any_baseopt(ctx, te);
   }

   if (te->uop.sub->type == TCE_UOP && te->uop.sub->uop.op == F_MAKESET) {
      return comb_toconstant(ctx, te, pqlvalue_bool(ctx->pql, true));
   }

   if (te->uop.sub->type == TCE_LET) {
      te = move_uop_into_let(ctx, te);
      PQLASSERT(te->type == TCE_LET);
      te->let.body = any_baseopt(ctx, te->let.body);
      return any_baseopt(ctx, te);
   }

   if (te->uop.sub->type == TCE_VALUE && pqlvalue_isset(te->uop.sub->value)) {
      struct pqlvalue *val;

      val = pqlvalue_bool(ctx->pql,
			  pqlvalue_set_getnum(te->uop.sub->value) == 0);
      return comb_toconstant(ctx, te, val);
   }

   return te;
}

static struct tcexpr *makeset_baseopt(struct baseopt *ctx,
				      struct tcexpr *te) {
   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.op == F_MAKESET);

   if (te->uop.sub->type == TCE_VALUE) {
      struct pqlvalue *set;

      set = pqlvalue_emptyset(ctx->pql);
      pqlvalue_set_add(set, te->uop.sub->value);
      te->uop.sub->value = NULL;
      return comb_toconstant(ctx, te, set);
   }

   return te;
}

static struct tcexpr *getelement_baseopt(struct baseopt *ctx,
					 struct tcexpr *te) {
   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.op == F_GETELEMENT);

   if (te->uop.sub->type == TCE_UOP && te->uop.sub->uop.op == F_MAKESET) {
      te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
      return tcexpr_destruct(ctx, te);
   }

   /* constant folding */
   if (te->uop.sub->type == TCE_VALUE) {
      /*
       * Do this only for sets of one; for larger sets we need an
       * error value (not nil) and we don't have that yet. (XXX)
       */
      if (pqlvalue_isset(te->uop.sub->value) &&
	  pqlvalue_set_getnum(te->uop.sub->value) == 1) {
	 const struct pqlvalue *val;

	 val = pqlvalue_set_get(te->uop.sub->value, 0);
	 return comb_toconstant(ctx, te, pqlvalue_clone(ctx->pql, val));
      }
      if (pqlvalue_issequence(te->uop.sub->value) &&
	  pqlvalue_sequence_getnum(te->uop.sub->value) == 1) {
	 const struct pqlvalue *val;

	 val = pqlvalue_sequence_get(te->uop.sub->value, 0);
	 return comb_toconstant(ctx, te, pqlvalue_clone(ctx->pql, val));
      }
   }

   return te;
}

//////////////////////////////
// aggregator

static struct tcexpr *count_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   while (1) {
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.op == F_COUNT);
      sub = te->uop.sub;

      /*
       * count(project_* X) => count(X)
       * count(strip_* X) => count(X)
       * count(rename_* X) => count(X)
       * count(A order-by ...) => count(A)
       * count(distinguish(X)) => count(X)
       * count(adjoin X ...) => count(X)
       * count(map X0 in X: f(X)) => count(X)
       */
      if (sub->type == TCE_PROJECT ||
	  sub->type == TCE_STRIP ||
	  sub->type == TCE_RENAME ||
	  sub->type == TCE_ORDER ||
	  sub->type == TCE_DISTINGUISH ||
	  sub->type == TCE_ADJOIN ||
	  sub->type == TCE_MAP) {
	 comb_count_destroysub(ctx, te);
	 continue;
      }

      /*
       * count(X x Y) => count(X) + count(Y)
       */
      if (sub->type == TCE_JOIN && sub->join.predicate == NULL) {
	 te = comb_distribute_uop_over_join(ctx, te, F_ADD);
	 PQLASSERT(te->type == TCE_BOP);
	 te->bop.left = any_baseopt(ctx, te->bop.left);
	 te->bop.right = any_baseopt(ctx, te->bop.right);
	 return any_baseopt(ctx, te);
      }

      /*
       * count(unnest_a X) => sum(map X0 in (project_a X): count(X0))
       */
      if (sub->type == TCE_UNNEST) {
	 te = comb_agg_unnest(ctx, te);
	 PQLASSERT(te->type == TCE_UOP);
	 PQLASSERT(te->uop.sub->type == TCE_MAP);
	 te->uop.sub->map.set = any_baseopt(ctx, te->uop.sub->map.set);
	 te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
	 te->uop.sub = any_baseopt(ctx, te->uop.sub);
	 return any_baseopt(ctx, te);
      }

      /* count(A union-all B) => count(A) + count(B) */
      if (sub->type == TCE_BOP) {
	 /* notyet */
      }

      /* count(A ++ B) => count(A) + count(B), if A/B are sequences */
      if (sub->type == TCE_BOP && sub->bop.op == F_CONCAT &&
	  datatype_issequence(sub->datatype)) {
	 te = comb_count_concat(ctx, te);
	 //te = comb_distribute_uop_over_bop(ctx, te, F_ADD); // XXX notyet
	 PQLASSERT(te->type == TCE_BOP);
	 te->bop.left = any_baseopt(ctx, te->bop.left);
	 te->bop.right = any_baseopt(ctx, te->bop.right);
	 return any_baseopt(ctx, te);
      }

      /* count(set(X)) => 1 */
      if (sub->type == TCE_UOP && sub->uop.op == F_MAKESET) {
	 return comb_toconstant(ctx, te, pqlvalue_int(ctx->pql, 1));
      }

      /* constant folding */
      if (sub->type == TCE_VALUE) {
	 unsigned val;

	 if (pqlvalue_isset(sub->value)) {
	    val = pqlvalue_set_getnum(sub->value);
	 }
	 else {
	    PQLASSERT(pqlvalue_issequence(sub->value));
	    val = pqlvalue_sequence_getnum(sub->value);
	 }
	 return comb_toconstant(ctx, te, pqlvalue_int(ctx->pql, val));
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

static struct tcexpr *sum_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   while (1) {
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.op == F_SUM);
      sub = te->uop.sub;

      /*
       * sum(rename_* X) => count(X)
       * sum(A order-by ...) => count(A)
       */
      if (sub->type == TCE_RENAME ||
	  sub->type == TCE_ORDER) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 continue;
      }

      /*
       * sum(A union B) => sum(A) + sum(B) - sum(A intersect B)
       *  ... not worthwhile
       */

      /*
       * sum(map X0 in X: X0 * K) => K * sum(X)
       */
      /* XXX notyet */

      /*
       * sum(unnest_a X) => sum(map X0 in (project_a X): sum(X0))
       */
      if (sub->type == TCE_UNNEST) {
	 te = comb_agg_unnest(ctx, te);
	 PQLASSERT(te->type == TCE_UOP);
	 PQLASSERT(te->uop.sub->type == TCE_MAP);
	 te->uop.sub->map.set = any_baseopt(ctx, te->uop.sub->map.set);
	 te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
	 te->uop.sub = any_baseopt(ctx, te->uop.sub);
	 return any_baseopt(ctx, te);
      }

      /* sum(A unionall B) => sum(A) + sum(B) */
      if (sub->type == TCE_BOP) {
	 /* notyet XXX */
      }

      /* sum(A ++ B) => sum(A) + sum(B), if A/B are sequences */
      if (sub->type == TCE_BOP && sub->bop.op == F_CONCAT &&
	  datatype_issequence(sub->datatype)) {
	 te = comb_distribute_uop_over_bop(ctx, te, F_ADD);
	 PQLASSERT(te->type == TCE_BOP);
	 te->bop.left = any_baseopt(ctx, te->bop.left);
	 te->bop.right = any_baseopt(ctx, te->bop.right);
	 return any_baseopt(ctx, te);
      }

      /* sum(set(X)) => X */
      if (sub->type == TCE_UOP && sub->uop.op == F_MAKESET) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 return tcexpr_destruct(ctx, te);
      }

      /* constant folding */
      if (sub->type == TCE_VALUE) {
	 unsigned num, i, val;
	 const struct pqlvalue *subval;

	 val = 0;
	 if (pqlvalue_isset(sub->value)) {
	    num = pqlvalue_set_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_set_get(sub->value, i);
	       PQLASSERT(pqlvalue_isint(subval));
	       val += pqlvalue_int_get(subval);
	    }
	 }
	 else {
	    PQLASSERT(pqlvalue_issequence(sub->value));
	    num = pqlvalue_sequence_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_sequence_get(sub->value, i);
	       PQLASSERT(pqlvalue_isint(subval));
	       val += pqlvalue_int_get(subval);
	    }
	 }
	 return comb_toconstant(ctx, te, pqlvalue_int(ctx->pql, val));
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

static struct tcexpr *minmax_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;
   bool ismin;

   while (1) {
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.op == F_MIN || te->uop.op == F_MAX);
      ismin = te->uop.op == F_MIN;
      sub = te->uop.sub;

      /*
       * min(rename_* X) => min(X)
       * min(A order-by ...) => min(A)
       */
      if (sub->type == TCE_RENAME ||
	  sub->type == TCE_ORDER) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 continue;
      }

      /*
       * min(A union B) => min(min(A), min(B))
       * min(A unionall B) => min(min(A), min(B))
       * min(A ++ B) => min(min(A), min(B))
       */
      /* XXX notyet */

      /*
       * min(unnest_a X) => min(map X0 in (project_a X): min(X0))
       */
      if (sub->type == TCE_UNNEST) {
	 te = comb_agg_unnest(ctx, te);
	 PQLASSERT(te->type == TCE_UOP);
	 PQLASSERT(te->uop.sub->type == TCE_MAP);
	 te->uop.sub->map.set = any_baseopt(ctx, te->uop.sub->map.set);
	 te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
	 te->uop.sub = any_baseopt(ctx, te->uop.sub);
	 return any_baseopt(ctx, te);
      }

      /* min(set(X)) => X */
      if (sub->type == TCE_UOP && sub->uop.op == F_MAKESET) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 return tcexpr_destruct(ctx, te);
      }

      /* constant folding */
      if (sub->type == TCE_VALUE) {
	 unsigned num, i, val, x;
	 bool hasval;
	 const struct pqlvalue *subval;
	 struct pqlvalue *newval;

	 hasval = false;
	 if (pqlvalue_isset(sub->value)) {
	    num = pqlvalue_set_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_set_get(sub->value, i);
	       PQLASSERT(pqlvalue_isint(subval));
	       x = pqlvalue_int_get(subval);
	       if (!hasval || (ismin ? x < val : x > val)) {
		  val = x;
		  hasval = true;
	       }
	    }
	 }
	 else {
	    PQLASSERT(pqlvalue_issequence(sub->value));
	    num = pqlvalue_sequence_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_sequence_get(sub->value, i);
	       PQLASSERT(pqlvalue_isint(subval));
	       x = pqlvalue_int_get(subval);
	       if (!hasval || (ismin ? x < val : x > val)) {
		  val = x;
		  hasval = true;
	       }
	    }
	 }
	 if (hasval) {
	    newval = pqlvalue_int(ctx->pql, val);
	 }
	 else {
	    newval = pqlvalue_nil(ctx->pql);
	 }
	 return comb_toconstant(ctx, te, newval);
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

static struct tcexpr *anytrue_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   while (1) {
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.op == F_ANYTRUE);
      sub = te->uop.sub;

      /*
       * anytrue(rename_* X) => anytrue(X)
       * anytrue(A order-by ...) => anytrue(A)
       */
      if (sub->type == TCE_RENAME ||
	  sub->type == TCE_ORDER) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 continue;
      }

      /*
       * anytrue(A union B) => anytrue(A) or anytrue(B)
       * anytrue(A unionall B) => anytrue(A) or anytrue(B)
       * anytrue(A intersect B) => anytrue(A) and anytrue(B)
       * anytrue(A intersectall B) => anytrue(A) and anytrue(B)
       * anytrue(A except B) => anytrue(A) and not anytrue(B)
       * anytrue(A exceptall B) => [does not work]
       * anytrue(A ++ B) => anytrue(A) or anytrue(B)
       */
      if (te->uop.sub->type == TCE_BOP &&
	  (te->uop.sub->bop.op == F_UNION ||
	   te->uop.sub->bop.op == F_INTERSECT ||
	   /*te->uop.sub->bop.op == F_EXCEPT ||  (XXX can't do addnot) */
	   te->uop.sub->bop.op == F_CONCAT)) {
	 enum functions newop;
	 bool addnot;

	 switch (te->bop.right->bop.op) {
	  case F_UNION:     newop = F_OR;  addnot = false; break;
	  case F_INTERSECT: newop = F_AND; addnot = false; break;
	  case F_EXCEPT:    newop = F_AND; addnot = true;  break;
	  case F_CONCAT:    newop = F_OR;  addnot = false; break;
	  default: PQLASSERT(0); break;
	 }
	 /* XXX distribute doesn't have an addnot feature */
	 te = comb_distribute_uop_over_bop(ctx, te, newop /*, addnot*/);
	 PQLASSERT(te->type == TCE_BOP);
	 if (addnot) {
	    PQLASSERT(te->bop.right->type == TCE_UOP);
	    te->bop.right->uop.sub = any_baseopt(ctx, te->bop.right->uop.sub);
	 }
	 te->bop.right = any_baseopt(ctx, te->bop.right);
	 te->bop.left = any_baseopt(ctx, te->bop.left);
	 return any_baseopt(ctx, te);
      }

      /*
       * anytrue(unnest_a X) => anytrue(map X0 in (project_a X): anytrue(X0))
       */
      if (sub->type == TCE_UNNEST) {
	 te = comb_agg_unnest(ctx, te);
	 PQLASSERT(te->type == TCE_UOP);
	 PQLASSERT(te->uop.sub->type == TCE_MAP);
	 te->uop.sub->map.set = any_baseopt(ctx, te->uop.sub->map.set);
	 te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
	 te->uop.sub = any_baseopt(ctx, te->uop.sub);
	 return any_baseopt(ctx, te);
      }

      /* anytrue(set(X)) => X   XXX: what about nil? */
      if (sub->type == TCE_UOP && sub->uop.op == F_MAKESET) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 return tcexpr_destruct(ctx, te);
      }

      /* constant folding */
      if (sub->type == TCE_VALUE) {
	 unsigned num, i;
	 const struct pqlvalue *subval;
	 bool val;

	 val = false;
	 if (pqlvalue_isset(sub->value)) {
	    num = pqlvalue_set_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_set_get(sub->value, i);
	       PQLASSERT(pqlvalue_isbool(subval));
	       if (pqlvalue_bool_get(subval)) {
		  val = true;
	       }
	    }
	 }
	 else {
	    PQLASSERT(pqlvalue_issequence(sub->value));
	    num = pqlvalue_sequence_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_sequence_get(sub->value, i);
	       PQLASSERT(pqlvalue_isbool(subval));
	       if (pqlvalue_bool_get(subval)) {
		  val = true;
	       }
	    }
	 }
	 return comb_toconstant(ctx, te, pqlvalue_bool(ctx->pql, val));
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

static struct tcexpr *alltrue_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   while (1) {
      PQLASSERT(te->type == TCE_UOP);
      PQLASSERT(te->uop.op == F_ALLTRUE);
      sub = te->uop.sub;

      /*
       * alltrue(rename_* X) => alltrue(X)
       * alltrue(A order-by ...) => alltrue(A)
       */
      if (sub->type == TCE_RENAME ||
	  sub->type == TCE_ORDER) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 continue;
      }

      /*
       * alltrue(A union B) => alltrue(A) and alltrue(B)
       * alltrue(A unionall B) => alltrue(A) and alltrue(B)
       * alltrue(A intersect B) => alltrue(A) or alltrue(B)
       * alltrue(A intersectall B) => alltrue(A) or alltrue(B)
       * alltrue(A ++ B) => alltrue(A) and alltrue(B)
       */
      if (te->uop.sub->type == TCE_BOP &&
	  (te->uop.sub->bop.op == F_UNION ||
	   te->uop.sub->bop.op == F_INTERSECT ||
	   te->uop.sub->bop.op == F_CONCAT)) {
	 enum functions newop;

	 switch (te->bop.right->bop.op) {
	  case F_UNION:     newop = F_OR;  break;
	  case F_INTERSECT: newop = F_AND; break;
	  case F_CONCAT:    newop = F_OR;  break;
	  default: PQLASSERT(0); break;
	 }
	 te = comb_distribute_uop_over_bop(ctx, te, newop);
	 PQLASSERT(te->type == TCE_BOP);
	 te->bop.right = any_baseopt(ctx, te->bop.right);
	 te->bop.left = any_baseopt(ctx, te->bop.left);
	 return any_baseopt(ctx, te);
      }

      /*
       * alltrue(A except B) => alltrue(A) and not anytrue(B)
       * alltrue(A exceptall B) => [does not work]
       */
      /* XXX available transform doesn't support this */


      /*
       * alltrue(unnest_a X) => alltrue(map X0 in (project_a X): alltrue(X0))
       */
      if (sub->type == TCE_UNNEST) {
	 te = comb_agg_unnest(ctx, te);
	 PQLASSERT(te->type == TCE_UOP);
	 PQLASSERT(te->uop.sub->type == TCE_MAP);
	 te->uop.sub->map.set = any_baseopt(ctx, te->uop.sub->map.set);
	 te->uop.sub->map.result = any_baseopt(ctx, te->uop.sub->map.result);
	 te->uop.sub = any_baseopt(ctx, te->uop.sub);
	 return any_baseopt(ctx, te);
      }

      /* alltrue(set(X)) => X   XXX: what about nil? */
      if (sub->type == TCE_UOP && sub->uop.op == F_MAKESET) {
	 te->uop.sub = tcexpr_destruct(ctx, te->uop.sub);
	 return tcexpr_destruct(ctx, te);
      }

      /* constant folding */
      if (sub->type == TCE_VALUE) {
	 unsigned num, i;
	 const struct pqlvalue *subval;
	 bool val;

	 val = true;
	 if (pqlvalue_isset(sub->value)) {
	    num = pqlvalue_set_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_set_get(sub->value, i);
	       PQLASSERT(pqlvalue_isbool(subval));
	       if (!pqlvalue_bool_get(subval)) {
		  val = false;
	       }
	    }
	 }
	 else {
	    PQLASSERT(pqlvalue_issequence(sub->value));
	    num = pqlvalue_sequence_getnum(sub->value);
	    for (i=0; i<num; i++) {
	       subval = pqlvalue_sequence_get(sub->value, i);
	       PQLASSERT(pqlvalue_isbool(subval));
	       if (!pqlvalue_bool_get(subval)) {
		  val = false;
	       }
	    }
	 }
	 return comb_toconstant(ctx, te, pqlvalue_bool(ctx->pql, val));
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

//////////////////////////////
// boolean

static struct tcexpr *and_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *ret;

   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_AND);
   (void)ctx;

   /*
    * XXX. what about nil?
    */

   /* true and X => X */
   if (tcexpr_istrue(te->bop.left)) {
      ret = te->bop.right;
      te->bop.right = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }
   if (tcexpr_istrue(te->bop.right)) {
      ret = te->bop.left;
      te->bop.left = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* false and X => false */
   if (tcexpr_isfalse(te->bop.left)) {
      ret = te->bop.left;
      te->bop.left = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }
   if (tcexpr_isfalse(te->bop.right)) {
      ret = te->bop.right;
      te->bop.right = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* X and not X => false */
   if ((te->bop.right->type == TCE_UOP && te->bop.right->uop.op == F_NOT &&
       tcexpr_simple_same(te->bop.left, te->bop.right->uop.sub)) ||
       (te->bop.left->type == TCE_UOP && te->bop.left->uop.op == F_NOT &&
	tcexpr_simple_same(te->bop.right, te->bop.left->uop.sub))) {
      tcexpr_destroy(ctx->pql, te->bop.left);
      tcexpr_destroy(ctx->pql, te->bop.right);
      te->type = TCE_VALUE;
      te->value = pqlvalue_bool(ctx->pql, false);
      return te;
   }

   return te;
}

static struct tcexpr *or_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *ret;

   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_OR);
   (void)ctx;

   /*
    * XXX. what about nil?
    */

   /* false or X => X */
   if (tcexpr_isfalse(te->bop.left)) {
      ret = te->bop.right;
      te->bop.right = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }
   if (tcexpr_isfalse(te->bop.right)) {
      ret = te->bop.left;
      te->bop.left = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* true or X => true */
   if (tcexpr_istrue(te->bop.left)) {
      ret = te->bop.left;
      te->bop.left = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }
   if (tcexpr_istrue(te->bop.right)) {
      ret = te->bop.right;
      te->bop.right = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* X or not X => true */
   if ((te->bop.right->type == TCE_UOP && te->bop.right->uop.op == F_NOT &&
       tcexpr_simple_same(te->bop.left, te->bop.right->uop.sub)) ||
       (te->bop.left->type == TCE_UOP && te->bop.left->uop.op == F_NOT &&
	tcexpr_simple_same(te->bop.right, te->bop.left->uop.sub))) {
      tcexpr_destroy(ctx->pql, te->bop.left);
      tcexpr_destroy(ctx->pql, te->bop.right);
      te->type = TCE_VALUE;
      te->value = pqlvalue_bool(ctx->pql, true);
      return te;
   }

   return te;
}

static struct tcexpr *not_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *ret;

   PQLASSERT(te->type == TCE_UOP);
   PQLASSERT(te->uop.op == F_NOT);
   (void)ctx;

   /*
    * XXX. what about nil?
    */

   /* not not X => X */
   if (te->uop.sub->type == TCE_UOP && te->uop.sub->uop.op == F_NOT) {
      ret = te->uop.sub->uop.sub;
      te->uop.sub->uop.sub = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* not A == B => A != B */
   if (te->uop.sub->type == TCE_BOP && te->uop.sub->bop.op == F_EQ) {
      te->uop.sub->bop.op = F_NOTEQ;
      ret = te->uop.sub;
      te->uop.sub = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   /* not A != B => A == B */
   if (te->uop.sub->type == TCE_BOP && te->uop.sub->bop.op == F_NOTEQ) {
      te->uop.sub->bop.op = F_EQ;
      ret = te->uop.sub;
      te->uop.sub = NULL;
      tcexpr_destroy(ctx->pql, te);
      return ret;
   }

   return te;
}

//////////////////////////////
// comparison

static struct tcexpr *like_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_LIKE);

   /* foo like % => true */
   if (te->bop.right->type == TCE_VALUE &&
       pqlvalue_isstring(te->bop.right->value) &&
       !strcmp(pqlvalue_string_get(te->bop.right->value), "%")) {
      tcexpr_destroy(ctx->pql, te->bop.left);
      tcexpr_destroy(ctx->pql, te->bop.right);
      te->type = TCE_VALUE;
      te->value = pqlvalue_bool(ctx->pql, true);
      return te;
   }

   return te;
}

static struct tcexpr *glob_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_GLOB);

   /* foo glob * => true */
   if (te->bop.right->type == TCE_VALUE &&
       pqlvalue_isstring(te->bop.right->value) &&
       !strcmp(pqlvalue_string_get(te->bop.right->value), "*")) {
      tcexpr_destroy(ctx->pql, te->bop.left);
      tcexpr_destroy(ctx->pql, te->bop.right);
      te->type = TCE_VALUE;
      te->value = pqlvalue_bool(ctx->pql, true);
      return te;
   }

   return te;
}

static struct tcexpr *grep_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_BOP);
   PQLASSERT(te->bop.op == F_GREP);

   /* foo grep .* => true */
   if (te->bop.right->type == TCE_VALUE &&
       pqlvalue_isstring(te->bop.right->value) &&
       !strcmp(pqlvalue_string_get(te->bop.right->value), ".*")) {
      tcexpr_destroy(ctx->pql, te->bop.left);
      tcexpr_destroy(ctx->pql, te->bop.right);
      te->type = TCE_VALUE;
      te->value = pqlvalue_bool(ctx->pql, true);
      return te;
   }

   return te;
}

//////////////////////////////
// string/sequence

//////////////////////////////
// nil

//////////////////////////////
// numeric

////////////////////////////////////////////////////////////
// optimization matching for expressions

//////////////////////////////
// filter

static struct tcexpr *filter_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   PQLASSERT(te->type == TCE_FILTER);

   sub = te->filter.sub;

   /*
    * Combine filter conditions.
    */

   if (sub->type == TCE_FILTER) {
      /* filter twice -> combine them */
      sub->filter.predicate = combine_predicates(ctx, 
						sub->filter.predicate,
						te->filter.predicate);
      sub->filter.predicate = any_baseopt(ctx, sub->filter.predicate);
      goto destroy;
   }

   if (sub->type == TCE_JOIN) {
      /* combine the filter with the join's internal filter */
      sub->join.predicate = combine_predicates(ctx,
					      sub->join.predicate,
					      te->filter.predicate);
      sub->join.predicate = any_baseopt(ctx, sub->join.predicate);
      goto destroy;
   }

   if (sub->type == TCE_SCAN) {
      /* combine the filter with the scan's internal filter */
      sub->scan.predicate = combine_predicates(ctx,
					       sub->scan.predicate,
					       te->filter.predicate);
      sub->scan.predicate = any_baseopt(ctx, sub->scan.predicate);
      goto destroy;
   }


   /* Nothing matched; give up. */
   return te;

 destroy:
   te->filter.sub = NULL;
   te->filter.predicate = NULL;
   tcexpr_destroy(ctx->pql, te);
   return any_baseopt(ctx, sub);
}

//////////////////////////////
// strip

/*
 * Return true if SUB, which is an adjoin, adjoins an exact copy of 
 * some other column, and if so return that column in FROMCOL_RET.
 *
 * Does not return a new reference in FROMCOL_RET - caller should
 * incref if the column is affixed anywhere persistent.
 */
static bool adjoin_copies_column(struct baseopt *ctx, struct tcexpr *sub,
				 struct colname **fromcol_ret) {
   struct tcvar *lvar;
   struct tcexpr *lexp;

   (void)ctx;

   PQLASSERT(sub->type == TCE_ADJOIN);
   PQLASSERT(sub->adjoin.func->type == TCE_LAMBDA);
   lvar = sub->adjoin.func->lambda.var;
   lexp = sub->adjoin.func->lambda.body;

   if (lexp->type == TCE_PROJECT &&
       lexp->project.sub->type == TCE_READVAR &&
       lexp->project.sub->readvar == lvar &&
       colset_num(lexp->project.cols) == 1) {
      *fromcol_ret = colset_get(lexp->project.cols, 0);
      return true;
   }
   return false;
}

static struct tcexpr *strip_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;
   struct colname *fromcol;

   PQLASSERT(te->type == TCE_STRIP);

   while (1) {
      sub = te->strip.sub;

      /*
       * strip_a(project_a(x)) => strip_*(x)
       * strip_a(project_ab(x)) => project_b(x)
       */
      if (sub->type == TCE_PROJECT) {
	 unsigned *indexes;
	 unsigned i, num;
	 int result;

	 num = colset_num(te->strip.cols);

	 if (num == colset_num(sub->project.cols)) {
	    /* Result is unit. Drop the projection entirely. */
	    te->strip.sub = sub->project.sub;
	    sub->project.sub = NULL;
	    tcexpr_destroy(ctx->pql, sub);

	    /* Rebuild the strip to strip everything. */
	    colset_destroy(ctx->pql, te->strip.cols);
	    te->strip.cols = colset_fromcoltree(ctx->pql,
						te->strip.sub->colnames);

	    /* te's datatype/colnames should be unchanged */
	    continue;
	 }

	 indexes = domalloc(ctx->pql, num * sizeof(*indexes));
	 for (i=0; i<num; i++) {
	    result = colset_find(sub->project.cols,
				 colset_get(te->strip.cols, i),
				 &indexes[i]);
	    PQLASSERT(result == 0);
	 }
	 qsort(indexes, num, sizeof(indexes[0]), unsigned_sortfunc);
	 for (i=num; i-- > 0; ) {
	    colname_decref(ctx->pql,
			   colset_get(sub->project.cols, indexes[i]));
	    colset_removebyindex(sub->project.cols, indexes[i]);
	    sub->datatype = datatype_tupleset_strip(ctx->pql, sub->datatype,
						    indexes[i]);
	 }

	 if (colset_num(sub->project.cols) == 1) {
	    struct coltree *nct;
	    unsigned index;

	    result = coltree_find(sub->colnames,
				  colset_get(sub->project.cols, 0), &index);
	    PQLASSERT(result == 0);
	    nct = coltree_clone(ctx->pql,
				coltree_getsubtree(sub->colnames, index));
	    coltree_destroy(ctx->pql, sub->colnames);
	    sub->colnames = nct;
	 }
	 else {
	    for (i=num; i-- > 0; ) {
	       coltree_removebyindex(ctx->pql, sub->colnames, i);
	    }
	 }

	 dofree(ctx->pql, indexes, num * sizeof(*indexes));

	 te->strip.sub = NULL;
	 tcexpr_destroy(ctx->pql, te);

	 return any_baseopt(ctx, sub);
      }

      /*
       * strip_a(strip_b(x)) => strip_ab(x)
       */
      if (sub->type == TCE_STRIP) {
	 colset_moveappend(ctx->pql, te->strip.cols, sub->strip.cols);
	 te->strip.sub = sub->strip.sub;
	 sub->strip.sub = NULL;
	 tcexpr_destroy(ctx->pql, sub);
	 continue;
      }

      /*
       * strip_a(x with b as a) => strip_b(x)
       */
      if (sub->type == TCE_RENAME) {
	 unsigned index;
	 struct colname *uppername;
	 struct coltree *newtree;

	 if (colset_find(te->strip.cols, sub->rename.newcol, &index) == 0) {
	    /* adjust strip column list */
	    uppername = colset_get(te->strip.cols, index);
	    PQLASSERT(uppername == sub->rename.newcol);
	    colname_decref(ctx->pql, uppername);
	    colname_incref(sub->rename.oldcol);
	    colset_set(te->strip.cols, index, sub->rename.oldcol);

	    /* types don't change */

	    /* update colnames */
	    newtree = coltree_rename(ctx->pql, te->colnames,
				     sub->rename.newcol, sub->rename.oldcol);
	    coltree_destroy(ctx->pql, te->colnames);
	    te->colnames = newtree;

	    te->strip.sub = sub->rename.sub;
	    sub->rename.sub = NULL;
	    tcexpr_destroy(ctx->pql, sub);
	    continue;
	 }
      }

      /*
       * strip_ab(nest_c(x) as a) => strip_b(uniq(sort(strip_c(x))))
       */
      if (sub->type == TCE_NEST) {
	 /* notyet */
      }

      /*
       * Ideally, if unnest produces a column X that we're going to
       * strip, we want to go find the corresponding nest or repeat
       * that creates X and shoot X before it's generated at all.
       * However, this is nontrivial.
       *
       * Nothing else useful for unnest here.
       */

      /*
       * strip_a(distinguish x as a) => x
       */
      if (sub->type == TCE_DISTINGUISH) {
	 if (colset_contains(te->strip.cols, sub->distinguish.newcol)) {
	    colset_remove(te->strip.cols, sub->distinguish.newcol);
	    colname_decref(ctx->pql, sub->distinguish.newcol);
	    te->strip.sub = sub->distinguish.sub;
	    sub->distinguish.sub = NULL;
	    tcexpr_destroy(ctx->pql, sub);
	    continue;
	 }
      }

      /*
       * strip_a(x |+| y as a) => x
       * strip_a(x |+| \x1 -> a as b) => reorder(x with a as b)
       */
      if (sub->type == TCE_ADJOIN) {
	 if (colset_contains(te->strip.cols, sub->adjoin.newcol)) {
	    colset_remove(te->strip.cols, sub->adjoin.newcol);
	    colname_decref(ctx->pql, sub->adjoin.newcol);
	    te->strip.sub = sub->adjoin.left;
	    sub->adjoin.left = NULL;
	    tcexpr_destroy(ctx->pql, sub);
	    continue;
	 }
	 else if (adjoin_copies_column(ctx, sub, &fromcol) &&
		  colset_contains(te->strip.cols, fromcol)) {
	    /*
	     * The adjoin adds a clone of a column we're supposed to
	     * wipe out. Nuke the adjoin and replace with a rename and
	     * a project (so the order of columns is preserved) and
	     * remove the column in question from the strip.
	     */
	    struct tcexpr *rn, *proj;
	    unsigned i, num, index;
	    struct colname *col;
	    struct datatype *coltype, *membertype, *newtype;
	    int result;
	    bool isset = false, isseq = false;

	    colname_incref(fromcol);
	    rn = mktcexpr_rename(ctx->pql, sub->adjoin.left,
				 fromcol, sub->adjoin.newcol);
	    sub->adjoin.left = NULL;
	    sub->adjoin.newcol = NULL;
	    te->strip.sub = NULL;
	    tcexpr_destroy(ctx->pql, sub);

	    rn->datatype = rn->rename.sub->datatype;
	    rn->colnames = coltree_rename(ctx->pql, rn->rename.sub->colnames,
					  rn->rename.oldcol,
					  rn->rename.newcol);

	    membertype = rn->datatype;
	    if (datatype_isset(membertype)) {
	       membertype = datatype_set_member(membertype);
	       isset = true;
	    }
	    else if (datatype_issequence(membertype)) {
	       membertype = datatype_sequence_member(membertype);
	       isseq = true;
	    }

	    proj = mktcexpr_project_none(ctx->pql, rn);
	    newtype = datatype_unit(ctx->pql);

	    result = coltree_find(rn->colnames, rn->rename.newcol, &index);
	    PQLASSERT(result == 0);
	    num = coltree_num(rn->colnames);
	    for (i=0; i<num; i++) {
	       if (i != index) {
		  col = coltree_get(rn->colnames, i);
		  colname_incref(col);
		  colset_add(ctx->pql, proj->project.cols, col);
		  coltype = datatype_getnth(membertype, i);
		  newtype = datatype_tuple_append(ctx->pql, newtype, coltype);
	       }
	    }
	    col = coltree_get(rn->colnames, index);
	    colname_incref(col);
	    colset_add(ctx->pql, proj->project.cols, col);
	    coltype = datatype_getnth(membertype, index);
	    newtype = datatype_tuple_append(ctx->pql, newtype, coltype);
	    if (isset) {
	       newtype = datatype_set(ctx->pql, newtype);
	    }
	    else if (isseq) {
	       newtype = datatype_sequence(ctx->pql, newtype);
	    }

	    proj->datatype = newtype;
	    proj->colnames = coltree_project(ctx->pql,
					     rn->colnames, proj->project.cols);

	    colset_remove(te->strip.cols, fromcol);
	    colname_decref(ctx->pql, fromcol);

	    /* Call across to norenames to clear out the rename we added */
	    proj->project.sub = norenames(ctx->pql, proj->project.sub);

	    /*
	     * Now need to rerun baseopt on the changed bits
	     */

	    /* The rename should have gone away, but baseopt what's there... */
	    proj->project.sub = any_baseopt(ctx, proj->project.sub);

	    /* Do the projection... */
	    if (colset_num(te->strip.cols) == 0) {
	       /* nothing left to strip, drop it */
	       tcexpr_destroy(ctx->pql, te);
	       return any_baseopt(ctx, proj);
	    }
	    te->strip.sub = any_baseopt(ctx, proj);

	    /* and the strip itself, if it still exists */
	    continue;
	 }
      }

      /*
       * Stripping out the repeat path output should delete it from the
       * repeat.
       */
      if (sub->type == TCE_REPEAT) {
	 if (sub->repeat.repeatpathcolumn != NULL &&
	     colset_contains(te->strip.cols, sub->repeat.repeatpathcolumn)) {
	    /* notyet */
	 }
      }

      /*
       * strip_a(scan as a, b, c) => scan as _, b, c
       * strip_b(scan as a, b, c) => scan as a, _, c
       * strip_c(scan as a, b, c) => scan as a, b, _
       */
      if (sub->type == TCE_SCAN) {
	 if (sub->scan.leftobjcolumn != NULL &&
	     colset_contains(te->strip.cols, sub->scan.leftobjcolumn)) {
	    colset_remove(te->strip.cols, sub->scan.leftobjcolumn);
	    colname_decref(ctx->pql, sub->scan.leftobjcolumn);
	    colname_decref(ctx->pql, sub->scan.leftobjcolumn);
	    sub->scan.leftobjcolumn = NULL;
	 }
	 if (sub->scan.edgecolumn != NULL &&
	     colset_contains(te->strip.cols, sub->scan.edgecolumn)) {
	    colset_remove(te->strip.cols, sub->scan.edgecolumn);
	    colname_decref(ctx->pql, sub->scan.edgecolumn);
	    colname_decref(ctx->pql, sub->scan.edgecolumn);
	    sub->scan.edgecolumn = NULL;
	 }
	 if (sub->scan.rightobjcolumn != NULL &&
	     colset_contains(te->strip.cols, sub->scan.rightobjcolumn)) {
	    colset_remove(te->strip.cols, sub->scan.rightobjcolumn);
	    colname_decref(ctx->pql, sub->scan.rightobjcolumn);
	    colname_decref(ctx->pql, sub->scan.rightobjcolumn);
	    sub->scan.rightobjcolumn = NULL;
	 }
	 /* there's nothing left to strip, so we must be done! */
	 PQLASSERT(colset_num(te->strip.cols) == 0);
	 te->strip.sub = NULL;
	 tcexpr_destroy(ctx->pql, te);
	 return te;
      }

      /*
       * strip_b(x as a, y as b, z as c, ...) => (x as a, z as c, ...)
       */
      if (sub->type == TCE_TUPLE) {
	 struct tcexpr *subexpr;
	 unsigned *indexes;
	 unsigned i, num;
	 int result;

	 num = colset_num(te->strip.cols);
	 indexes = domalloc(ctx->pql, num * sizeof(*indexes));
	 for (i=0; i<num; i++) {
	    result = colset_find(sub->tuple.columns,
				 colset_get(te->strip.cols, i),
				 &indexes[i]);
	    PQLASSERT(result == 0);
	 }
	 qsort(indexes, num, sizeof(indexes[0]), unsigned_sortfunc);
	 for (i=num; i-- > 0; ) {
	    colname_decref(ctx->pql,
			   colset_get(sub->tuple.columns, indexes[i]));
	    colset_removebyindex(sub->tuple.columns, indexes[i]);
	    subexpr = tcexprarray_get(&sub->tuple.exprs, i);
	    tcexpr_destroy(ctx->pql, subexpr);
	    tcexprarray_remove(&sub->tuple.exprs, i);
	 }
	 dofree(ctx->pql, indexes, num * sizeof(*indexes));

	 sub->datatype = te->datatype;
	 coltree_destroy(ctx->pql, sub->colnames);
	 sub->colnames = te->colnames;
	 te->colnames = NULL;
	 tcexpr_destroy(ctx->pql, te);
	 return any_baseopt(ctx, sub);
      }

      /*
       * strip_a(nil) = nil
       */
      if (sub->type == TCE_VALUE) {
	 if (pqlvalue_isnil(sub->value)) {
	    te->strip.sub = NULL;
	    tcexpr_destroy(ctx->pql, te);
	    return sub;
	 }
      }

      /* Nothing matched; give up. */
      break;
   }

   return te;
}

//////////////////////////////
// join

static struct tcexpr *join_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_JOIN);

   // XXX
   (void)ctx;

   return te;
}

static struct tcexpr *order_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   PQLASSERT(te->type == TCE_ORDER);
   (void)ctx;

   sub = te->order.sub;

   /* Sets of cardinality 1 are already ordered. */
   if ((sub->type == TCE_UOP && sub->uop.op == F_MAKESET) ||
       (sub->type == TCE_FUNC && sub->func.op == F_MAKESET &&
	tcexprarray_num(&sub->func.args) == 1)) {
      te->order.sub = NULL;
      tcexpr_destroy(ctx->pql, te);
      return sub;
   }

   return te;
}

static struct tcexpr *uniq_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   PQLASSERT(te->type == TCE_UNIQ);
   (void)ctx;

   sub = te->uniq.sub;

   /* Sets of cardinality 1 are already unique. */
   if ((sub->type == TCE_UOP && sub->uop.op == F_MAKESET) ||
       (sub->type == TCE_FUNC && sub->func.op == F_MAKESET &&
	tcexprarray_num(&sub->func.args) == 1)) {
      te->uniq.sub = NULL;
      tcexpr_destroy(ctx->pql, te);
      return sub;
   }

   return te;
}

static struct tcexpr *bop_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *left, *right;

   PQLASSERT(te->type == TCE_BOP);
   (void)ctx;

   left = te->bop.left;
   right = te->bop.right;

   if (tcexpr_simple_same(left, right)) {
      struct tcexpr *ret;

      switch (te->bop.op) {
       case F_UNION:
       case F_INTERSECT:
	/* A union A => A; A intersect A => A */
	ret = te->bop.left;
	te->bop.left = NULL;
	tcexpr_destroy(ctx->pql, te);
	return ret;

       case F_EXCEPT:
	/* A except A => {} */
	ret = mktcexpr_value(ctx->pql, pqlvalue_emptyset(ctx->pql));
	ret->datatype = te->datatype;
	ret->colnames = te->colnames;
	te->colnames = NULL;
	tcexpr_destroy(ctx->pql, te);
	return ret;

       case F_IN:
       case F_AND:
       case F_OR:
       case F_EQ:
       case F_LTEQ:
       case F_GTEQ:
       case F_LIKE:
       case F_SOUNDEX:
	/* A in A => true; A and A => true; A or A => true; etc. */
	ret = mktcexpr_value(ctx->pql, pqlvalue_bool(ctx->pql, true));
	ret->datatype = te->datatype;
	ret->colnames = te->colnames;
	te->colnames = NULL;
	tcexpr_destroy(ctx->pql, te);
	return ret;

       case F_NOTEQ:
       case F_LT:
       case F_GT:
	/* A != A => false; etc. */
	ret = mktcexpr_value(ctx->pql, pqlvalue_bool(ctx->pql, false));
	ret->datatype = te->datatype;
	ret->colnames = te->colnames;
	te->colnames = NULL;
	tcexpr_destroy(ctx->pql, te);
	return ret;

       case F_ADD:
	/* A + A => A * 2 */
	tcexpr_destroy(ctx->pql, te->bop.right);
	te->bop.right = mktcexpr_value(ctx->pql, pqlvalue_int(ctx->pql, 2));
	te->bop.right->datatype = datatype_int(ctx->pql);
	te->bop.right->colnames = coltree_create_scalar_fresh(ctx->pql);
	te->bop.op = F_MUL;
	break;

       case F_SUB:
	/* A - A => 0 */
	ret = mktcexpr_value(ctx->pql, pqlvalue_int(ctx->pql, 0));
	ret->datatype = te->datatype;
	ret->colnames = te->colnames;
	te->colnames = NULL;
	tcexpr_destroy(ctx->pql, te);
	return ret;

       case F_DIV:
       case F_MOD:
	/* note: A/A => 1 and A%A => 0, but only if A != 0 */
	break;

       default:
	break;
      }
   }

   switch (te->bop.op) {
    case F_NONEMPTY:
    case F_MAKESET:
    case F_GETELEMENT:
    case F_COUNT:
    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
    case F_ALLTRUE:
    case F_ANYTRUE:
    case F_NOT:
    case F_NEW:
    case F_CTIME:
    case F_TOSTRING:
    case F_NEG:
    case F_ABS:
     /* not binary */
     PQLASSERT(0);
     break;

    case F_UNION:
     te = union_baseopt(ctx, te);
     break;
    case F_INTERSECT:
     te = intersect_baseopt(ctx, te);
     break;
    case F_EXCEPT:
     te = except_baseopt(ctx, te);
     break;
    case F_UNIONALL:
     te = unionall_baseopt(ctx, te);
     break;
    case F_INTERSECTALL:
     te = intersectall_baseopt(ctx, te);
     break;
    case F_EXCEPTALL:
     te = exceptall_baseopt(ctx, te);
     break;
    case F_IN:
     te = in_baseopt(ctx, te);
     break;
    case F_AND:
     te = and_baseopt(ctx, te);
     break;
    case F_OR:
     te = or_baseopt(ctx, te);
     break;
    case F_EQ:
    case F_NOTEQ:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
     break;
    case F_LIKE:
     te = like_baseopt(ctx, te);
     break;
    case F_GLOB:
     te = glob_baseopt(ctx, te);
     break;
    case F_GREP:
     te = grep_baseopt(ctx, te);
     break;
    case F_SOUNDEX:
    case F_CONCAT:
    case F_CHOOSE:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     break;
   }

   return te;
}

static struct tcexpr *uop_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   struct tcexpr *sub;

   PQLASSERT(te->type == TCE_UOP);

   sub = te->uop.sub;

   switch (te->uop.op) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_IN:
    case F_AND:
    case F_OR:
    case F_CTIME:
    case F_EQ:
    case F_NOTEQ:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
    case F_CONCAT:
    case F_CHOOSE:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     /* not unary */
     PQLASSERT(0);
     break;

    case F_NONEMPTY:
     break;
    case F_MAKESET:
     return makeset_baseopt(ctx, te);
    case F_GETELEMENT:
     return getelement_baseopt(ctx, te);
    case F_COUNT:
     return count_baseopt(ctx, te);
    case F_SUM:
     return sum_baseopt(ctx, te);
    case F_AVG:
     break;
    case F_MIN:
    case F_MAX:
     return minmax_baseopt(ctx, te);
    case F_ALLTRUE:
     return alltrue_baseopt(ctx, te);
    case F_ANYTRUE:
     return anytrue_baseopt(ctx, te);
    case F_NOT:
     return not_baseopt(ctx, te);
     break;
    case F_NEW:
    case F_TOSTRING:
    case F_NEG:
    case F_ABS:
     break;
   }

   return te;
}

static struct tcexpr *func_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_FUNC);

   /* Switch to uop/bop if possible as it reduces consing during eval */
   if (tcexprarray_num(&te->func.args) == 1) {
      enum functions op;
      struct tcexpr *sub;

      op = te->func.op;
      sub = tcexprarray_get(&te->func.args, 0);
      tcexprarray_setsize(ctx->pql, &te->func.args, 0);
      tcexprarray_cleanup(ctx->pql, &te->func.args);
      te->type = TCE_UOP;
      te->uop.op = op;
      te->uop.sub = sub;
      return uop_baseopt(ctx, te);
   }
   else if (tcexprarray_num(&te->func.args) == 2) {
      enum functions op;
      struct tcexpr *left, *right;

      op = te->func.op;
      left = tcexprarray_get(&te->func.args, 0);
      right = tcexprarray_get(&te->func.args, 1);
      tcexprarray_setsize(ctx->pql, &te->func.args, 0);
      tcexprarray_cleanup(ctx->pql, &te->func.args);
      te->type = TCE_BOP;
      te->bop.left = left;
      te->bop.op = op;
      te->bop.right = right;
      return bop_baseopt(ctx, te);
   }

   return te;
}

static struct tcexpr *map_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_MAP);

   // XXX
   (void)ctx;

   return te;
}

static struct tcexpr *let_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_LET);

   // XXX
   (void)ctx;

   return te;
}

static struct tcexpr *apply_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   PQLASSERT(te->type == TCE_APPLY);

   // XXX
   (void)ctx;

   return te;
}

////////////////////////////////////////////////////////////
// dispatch for specific optimizations

static struct tcexpr *any_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   switch (te->type) {
    case TCE_FILTER:
     te = filter_baseopt(ctx, te);
     break;
    case TCE_PROJECT:
     //te = project_baseopt(ctx, te); // notyet
     break;
    case TCE_STRIP:
     te = strip_baseopt(ctx, te);
     break;
    case TCE_RENAME:
     break;
    case TCE_JOIN:
     te = join_baseopt(ctx, te);
     break;
    case TCE_ORDER:
     te = order_baseopt(ctx, te);
     break;
    case TCE_UNIQ:
     te = uniq_baseopt(ctx, te);
     break;
    case TCE_NEST:
    case TCE_UNNEST:
    case TCE_DISTINGUISH:
    case TCE_ADJOIN:
    case TCE_STEP:
    case TCE_REPEAT:
    case TCE_SCAN:
     break;
    case TCE_BOP:
     te = bop_baseopt(ctx, te);
     break;
    case TCE_UOP:
     te = uop_baseopt(ctx, te);
     break;
    case TCE_FUNC:
     te = func_baseopt(ctx, te);
     break;
    case TCE_MAP:
     te = map_baseopt(ctx, te);
     break;
    case TCE_LET:
     te = let_baseopt(ctx, te);
     break;
    case TCE_LAMBDA:
     break;
    case TCE_APPLY:
     te = apply_baseopt(ctx, te);
     break;
    case TCE_READVAR:
    case TCE_READGLOBAL:
    case TCE_CREATEPATHELEMENT:
    case TCE_SPLATTER:
    case TCE_TUPLE:
    case TCE_VALUE:
     break;
   }
   return te;
}

////////////////////////////////////////////////////////////
// recursive traversal

static struct tcexpr *tcexpr_baseopt(struct baseopt *ctx, struct tcexpr *te) {
   unsigned i, num;
   struct tcexpr *subexpr;

   switch (te->type) {
    case TCE_FILTER:
     te->filter.sub = tcexpr_baseopt(ctx, te->filter.sub);
     te->filter.predicate = tcexpr_baseopt(ctx, te->filter.predicate);
     break;
    case TCE_PROJECT:
     te->project.sub = tcexpr_baseopt(ctx, te->project.sub);
     break;
    case TCE_STRIP:
     te->strip.sub = tcexpr_baseopt(ctx, te->strip.sub);
     break;
    case TCE_RENAME:
     te->rename.sub = tcexpr_baseopt(ctx, te->rename.sub);
     break;
    case TCE_JOIN:
     te->join.left = tcexpr_baseopt(ctx, te->join.left);
     te->join.right = tcexpr_baseopt(ctx, te->join.right);
     if (te->join.predicate != NULL) {
	te->join.predicate = tcexpr_baseopt(ctx, te->join.predicate);
     }
     break;
    case TCE_ORDER:
     te->order.sub = tcexpr_baseopt(ctx, te->order.sub);
     break;
    case TCE_UNIQ:
     te->uniq.sub = tcexpr_baseopt(ctx, te->uniq.sub);
     break;
    case TCE_NEST:
     te->nest.sub = tcexpr_baseopt(ctx, te->nest.sub);
     break;
    case TCE_UNNEST:
     te->unnest.sub = tcexpr_baseopt(ctx, te->unnest.sub);
     break;
    case TCE_DISTINGUISH:
     te->distinguish.sub = tcexpr_baseopt(ctx, te->distinguish.sub);
     break;
    case TCE_ADJOIN:
     te->adjoin.left = tcexpr_baseopt(ctx, te->adjoin.left);
     te->adjoin.func = tcexpr_baseopt(ctx, te->adjoin.func);
     break;
    case TCE_STEP:
     te->join.left = tcexpr_baseopt(ctx, te->step.sub);
     if (te->step.predicate != NULL) {
	te->step.predicate = tcexpr_baseopt(ctx, te->step.predicate);
     }
     break;
    case TCE_REPEAT:
     te->repeat.sub = tcexpr_baseopt(ctx, te->repeat.sub);
     te->repeat.body = tcexpr_baseopt(ctx, te->repeat.body);
     break;
    case TCE_SCAN:
     if (te->scan.predicate) {
	te->scan.predicate = tcexpr_baseopt(ctx, te->scan.predicate);
     }
     break;
    case TCE_BOP:
     te->bop.left = tcexpr_baseopt(ctx, te->bop.left);
     te->bop.right = tcexpr_baseopt(ctx, te->bop.right);
     break;
    case TCE_UOP:
     te->uop.sub = tcexpr_baseopt(ctx, te->uop.sub);
     break;
    case TCE_FUNC:
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	subexpr = tcexprarray_get(&te->func.args, i);
	subexpr = tcexpr_baseopt(ctx, subexpr);
	tcexprarray_set(&te->func.args, i, subexpr);
     }
     break;
    case TCE_MAP:
     te->map.set = tcexpr_baseopt(ctx, te->map.set);
     te->map.result = tcexpr_baseopt(ctx, te->map.result);
     break;
    case TCE_LET:
     te->let.value = tcexpr_baseopt(ctx, te->let.value);
     te->let.body = tcexpr_baseopt(ctx, te->let.body);
     break;
    case TCE_LAMBDA:
     te->lambda.body = tcexpr_baseopt(ctx, te->lambda.body);
     break;
    case TCE_APPLY:
     te->apply.lambda = tcexpr_baseopt(ctx, te->apply.lambda);
     te->apply.arg = tcexpr_baseopt(ctx, te->apply.arg);
     break;
    case TCE_READVAR:
     break;
    case TCE_READGLOBAL:
     break;
    case TCE_CREATEPATHELEMENT:
     te->createpathelement = tcexpr_baseopt(ctx, te->createpathelement);
     break;
    case TCE_SPLATTER:
     te->splatter.value = tcexpr_baseopt(ctx, te->splatter.value);
     te->splatter.name = tcexpr_baseopt(ctx, te->splatter.name);
     break;
    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     for (i=0; i<num; i++) {
	subexpr = tcexprarray_get(&te->tuple.exprs, i);
	subexpr = tcexpr_baseopt(ctx, subexpr);
	tcexprarray_set(&te->tuple.exprs, i, subexpr);
     }
     break;
    case TCE_VALUE:
     break;
   }
   te = any_baseopt(ctx, te);
   return te;
}

////////////////////////////////////////////////////////////
// entry point

struct tcexpr *baseopt(struct pqlcontext *pql, struct tcexpr *te) {
   struct baseopt ctx;

   baseopt_init(&ctx, pql);
   te = tcexpr_baseopt(&ctx, te);
   baseopt_cleanup(&ctx);

   return te;
}
