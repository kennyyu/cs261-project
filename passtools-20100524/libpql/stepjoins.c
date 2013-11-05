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
 * Pick joins that can be done as graph steps.
 */

#include "datatype.h"
#include "columns.h"
#include "pqlvalue.h"
#include "tcalc.h"
#include "passes.h"

struct stepjoins {
   struct pqlcontext *pql;
};

////////////////////////////////////////////////////////////
// context management

static void stepjoins_init(struct stepjoins *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
}

static void stepjoins_cleanup(struct stepjoins *ctx) {
   (void)ctx;
}

////////////////////////////////////////////////////////////
// tools

/*
 * Return "P1 and P2", pretending each is "true" if it's NULL.
 *
 * XXX this should be shared with the copy in baseopt.c.
 */
static struct tcexpr *combine_predicates(struct stepjoins *ctx,
					 struct tcexpr *p1,
					 struct tcexpr *p2) {
   struct tcexpr *ret;

   if (p1 == NULL) {
      return p2;
   }
   if (p2 == NULL) {
      return p1;
   }
   ret = mktcexpr_bop(ctx->pql, p1, F_AND, p2);
   ret->datatype = datatype_bool(ctx->pql);
   return ret;
}

////////////////////////////////////////////////////////////
// the real work

/*
 * Inspect the predicate on a join to see if (part of) it corresponds
 * to a step operation from one of the top-level columns in NAME. If
 * so, update the predicate to remove that condition, set *BACK_RET
 * based on whether the step is forward or backward, set *COL_RET to
 * the column in question, and return true. If not, return false.
 */
static bool check_join_predicate(struct stepjoins *ctx,
				 struct tcexpr **predicate_inout,
				 const struct coltree *name,
				 struct tcvar *lvar,
				 struct colname *leftcol,
				 struct colname *rightcol,
				 bool *back_ret,
				 struct colname **col_ret) {
   struct tcexpr *predicate;
   struct colname *c1, *c2;

   predicate = *predicate_inout;

   /*
    * We expect the predicate to be of the form T1 && T2 && ..., since
    * that's what's generated upstream. If one of the subexpressions
    * compares a column in COLS to either LEFTCOL or RIGHTCOL, we win.
    * Prune the subexpression in question.
    */

   if (predicate->type == TCE_BOP && predicate->bop.op == F_AND) {
      if (check_join_predicate(ctx, &predicate->bop.left, name, 
			       lvar, leftcol, rightcol, back_ret, col_ret)) {
	 return true;
      }
      if (check_join_predicate(ctx, &predicate->bop.right, name,
			       lvar, leftcol, rightcol, back_ret, col_ret)) {
	 return true;
      }
   }

   if (predicate->type == TCE_BOP && predicate->bop.op == F_EQ) {
      if (predicate->bop.left->type == TCE_PROJECT &&
	  predicate->bop.left->project.sub->type == TCE_READVAR &&
	  predicate->bop.left->project.sub->readvar == lvar &&
	  colset_num(predicate->bop.left->project.cols) == 1 &&
	  predicate->bop.right->type == TCE_PROJECT &&
	  predicate->bop.right->project.sub->type == TCE_READVAR &&
	  predicate->bop.right->project.sub->readvar == lvar &&
	  colset_num(predicate->bop.right->project.cols) == 1) {
	 c1 = colset_get(predicate->bop.left->project.cols, 0);
	 c2 = colset_get(predicate->bop.right->project.cols, 0);

	 if (coltree_contains_toplevel(name, c1) && c2 == leftcol) {
	    *back_ret = false;
	    *col_ret = c1;
	    goto matched;
	 }
	 else if (coltree_contains_toplevel(name, c1) && c2 == rightcol) {
	    *back_ret = true;
	    *col_ret = c1;
	    goto matched;
	 }
	 else if (coltree_contains_toplevel(name, c2) && c1 == leftcol) {
	    *back_ret = false;
	    *col_ret = c2;
	    goto matched;
	 }
	 else if (coltree_contains_toplevel(name, c2) && c1 == rightcol) {
	    *back_ret = true;
	    *col_ret = c2;
	    goto matched;
	 }
      }
   }

   return false;

 matched:
   tcexpr_destroy(ctx->pql, predicate);
   *predicate_inout = mktcexpr_value(ctx->pql, pqlvalue_bool(ctx->pql, true));
   (*predicate_inout)->datatype = datatype_bool(ctx->pql);
   return true;
}

/*
 * Inspect a predicate on a join to see if (part of) it compares
 * EDGECOL to a constant. If so, update the predicate to remove that
 * condition, place the constant in *VALUE_RET, and return true. If
 * not, return false.
 */
static bool check_edge_predicate(struct stepjoins *ctx,
				 struct tcexpr **predicate_inout,
				 struct tcvar *lvar,
				 struct colname *edgecol,
				 struct pqlvalue **value_ret) {
   struct tcexpr *predicate;

   predicate = *predicate_inout;

   /*
    * We expect the predicate to be of the form T1 && T2 && ..., since
    * that's what's generated upstream.
    */

   if (predicate->type == TCE_BOP && predicate->bop.op == F_AND) {
      if (check_edge_predicate(ctx, &predicate->bop.left,
			       lvar, edgecol, value_ret)) {
	 return true;
      }
      if (check_edge_predicate(ctx, &predicate->bop.right,
			       lvar, edgecol, value_ret)) {
	 return true;
      }
   }

   if (predicate->type == TCE_BOP && predicate->bop.op == F_EQ) {

      if (predicate->bop.left->type == TCE_PROJECT &&
	  predicate->bop.left->project.sub->type == TCE_READVAR &&
	  predicate->bop.left->project.sub->readvar == lvar &&
	  colset_num(predicate->bop.left->project.cols) == 1 &&
	  colset_get(predicate->bop.left->project.cols, 0) == edgecol &&
	  predicate->bop.right->type == TCE_VALUE) {

	 *value_ret = predicate->bop.right->value;
	 predicate->bop.right->value = NULL;
	 goto matched;
      }

      if (predicate->bop.right->type == TCE_PROJECT &&
	  predicate->bop.right->project.sub->type == TCE_READVAR &&
	  predicate->bop.right->project.sub->readvar == lvar &&
	  colset_num(predicate->bop.right->project.cols) == 1 &&
	  colset_get(predicate->bop.right->project.cols, 0) == edgecol &&
	  predicate->bop.left->type == TCE_VALUE) {

	 *value_ret = predicate->bop.left->value;
	 predicate->bop.left->value = NULL;
	 goto matched;
      }

   }

   return false;

 matched:
   tcexpr_destroy(ctx->pql, predicate);
   *predicate_inout = mktcexpr_value(ctx->pql, pqlvalue_bool(ctx->pql, true));
   (*predicate_inout)->datatype = datatype_bool(ctx->pql);
   return true;
}

/*
 * Inspect and perhaps convert a join expression.
 */
static struct tcexpr *try_convert_stepjoin(struct stepjoins *ctx,
					   struct tcexpr *join) {
   struct tcexpr *scan, *other, *step;
   struct colname *othercol;
   struct pqlvalue *edgeval;
   bool onleft, reversed, ok;

   PQLASSERT(join->type == TCE_JOIN);

   /*
    * We have the form of a step join if:
    *
    *   - one side is a scan;
    *   - the predicate compares a column of the other side to one
    *     object of the scan;
    *   - optionally, the predicate compares the edge of the scan to
    *     a constant.
    */

   if (join->join.left->type == TCE_SCAN) {
      onleft = true;
      scan = join->join.left;
      other = join->join.right;
   }
   else if (join->join.right->type == TCE_SCAN) {
      onleft = false;
      scan = join->join.right;
      other = join->join.left;
   }
   else {
      /* no luck */
      return join;
   }

   /*
    * Check the predicate on the join.
    */

   if (join->join.predicate == NULL) {
      /* nope. */
      return join;
   }

   PQLASSERT(join->join.predicate->type == TCE_LAMBDA);
   ok = check_join_predicate(ctx, &join->join.predicate->lambda.body,
			     other->colnames,
			     join->join.predicate->lambda.var,
			     scan->scan.leftobjcolumn,
			     scan->scan.rightobjcolumn,
			     &reversed, &othercol);


   if (!ok) {
      /* no good */
      return join;
   }

   /*
    * Look for a condition on the edge name.
    */

   //PQLASSERT(join->join.predicate->type == TCE_LAMBDA); -- duplicate
   ok = check_edge_predicate(ctx, &join->join.predicate->lambda.body,
			     join->join.predicate->lambda.var,
			     scan->scan.edgecolumn, &edgeval);
   if (!ok && scan->scan.predicate != NULL) {
      PQLASSERT(scan->scan.predicate->type == TCE_LAMBDA);
      check_edge_predicate(ctx, &scan->scan.predicate->lambda.body,
			   scan->scan.predicate->lambda.var,
			   scan->scan.edgecolumn, &edgeval);
   }
   if (!ok) {
      edgeval = NULL;
   }

   join->join.predicate = combine_predicates(ctx,
					    scan->scan.predicate,
					    join->join.predicate);
   scan->scan.predicate = NULL;

   colname_incref(othercol);
   step = mktcexpr_step(ctx->pql,
			other,
			othercol,
			edgeval,
			reversed,
			scan->scan.leftobjcolumn,
			scan->scan.edgecolumn,
			scan->scan.rightobjcolumn,
			join->join.predicate);

   scan->scan.leftobjcolumn = NULL;
   scan->scan.edgecolumn = NULL;
   scan->scan.rightobjcolumn = NULL;
   join->join.predicate = NULL;
   if (onleft) {
      PQLASSERT(join->join.right == other);
      join->join.right = NULL;
   }
   else {
      PQLASSERT(join->join.left == other);
      join->join.left = NULL;
   }

   step->datatype = join->datatype;
   step->colnames = join->colnames;
   join->colnames = NULL;

   /* destroy JOIN and SCAN */
   tcexpr_destroy(ctx->pql, join);

   return step;
}

////////////////////////////////////////////////////////////
// recursive traversal

static struct tcexpr *tcexpr_stepjoins(struct stepjoins *ctx,
				       struct tcexpr *te) {
   unsigned i, num;
   struct tcexpr *subexpr;

   switch (te->type) {
    case TCE_FILTER:
     te->filter.sub = tcexpr_stepjoins(ctx, te->filter.sub);
     te->filter.predicate = tcexpr_stepjoins(ctx, te->filter.predicate);
     break;
    case TCE_PROJECT:
     te->project.sub = tcexpr_stepjoins(ctx, te->project.sub);
     break;
    case TCE_STRIP:
     te->strip.sub = tcexpr_stepjoins(ctx, te->strip.sub);
     break;
    case TCE_RENAME:
     te->rename.sub = tcexpr_stepjoins(ctx, te->rename.sub);
     break;
    case TCE_JOIN:
     te->join.left = tcexpr_stepjoins(ctx, te->join.left);
     te->join.right = tcexpr_stepjoins(ctx, te->join.right);
     if (te->join.predicate != NULL) {
	te->join.predicate = tcexpr_stepjoins(ctx, te->join.predicate);
     }
     te = try_convert_stepjoin(ctx, te);
     break;
    case TCE_ORDER:
     te->order.sub = tcexpr_stepjoins(ctx, te->order.sub);
     break;
    case TCE_UNIQ:
     te->uniq.sub = tcexpr_stepjoins(ctx, te->uniq.sub);
     break;
    case TCE_NEST:
     te->nest.sub = tcexpr_stepjoins(ctx, te->nest.sub);
     break;
    case TCE_UNNEST:
     te->unnest.sub = tcexpr_stepjoins(ctx, te->unnest.sub);
     break;
    case TCE_DISTINGUISH:
     te->distinguish.sub = tcexpr_stepjoins(ctx, te->distinguish.sub);
     break;
    case TCE_ADJOIN:
     te->adjoin.left = tcexpr_stepjoins(ctx, te->adjoin.left);
     te->adjoin.func = tcexpr_stepjoins(ctx, te->adjoin.func);
     break;
    case TCE_STEP:
     /* shouldn't see one of these since we create them */
     te->step.sub = tcexpr_stepjoins(ctx, te->step.sub);
     if (te->step.predicate != NULL) {
	te->step.predicate = tcexpr_stepjoins(ctx, te->step.predicate);
     }
     break;
    case TCE_REPEAT:
     te->repeat.sub = tcexpr_stepjoins(ctx, te->repeat.sub);
     te->repeat.body = tcexpr_stepjoins(ctx, te->repeat.body);
     break;
    case TCE_SCAN:
     if (te->scan.predicate) {
	te->scan.predicate = tcexpr_stepjoins(ctx, te->scan.predicate);
     }
     break;
    case TCE_BOP:
     te->bop.left = tcexpr_stepjoins(ctx, te->bop.left);
     te->bop.right = tcexpr_stepjoins(ctx, te->bop.right);
     break;
    case TCE_UOP:
     te->uop.sub = tcexpr_stepjoins(ctx, te->uop.sub);
     break;
    case TCE_FUNC:
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	subexpr = tcexprarray_get(&te->func.args, i);
	subexpr = tcexpr_stepjoins(ctx, subexpr);
	tcexprarray_set(&te->func.args, i, subexpr);
     }
     break;
    case TCE_MAP:
     te->map.set = tcexpr_stepjoins(ctx, te->map.set);
     te->map.result = tcexpr_stepjoins(ctx, te->map.result);
     break;
    case TCE_LET:
     te->let.value = tcexpr_stepjoins(ctx, te->let.value);
     te->let.body = tcexpr_stepjoins(ctx, te->let.body);
     break;
    case TCE_LAMBDA:
     te->lambda.body = tcexpr_stepjoins(ctx, te->lambda.body);
     break;
    case TCE_APPLY:
     te->apply.lambda = tcexpr_stepjoins(ctx, te->apply.lambda);
     te->apply.arg = tcexpr_stepjoins(ctx, te->apply.arg);
     break;
    case TCE_READVAR:
     break;
    case TCE_READGLOBAL:
     break;
    case TCE_CREATEPATHELEMENT:
     te->createpathelement = tcexpr_stepjoins(ctx, te->createpathelement);
     break;
    case TCE_SPLATTER:
     te->splatter.value = tcexpr_stepjoins(ctx, te->splatter.value);
     te->splatter.name = tcexpr_stepjoins(ctx, te->splatter.name);
     break;
    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     for (i=0; i<num; i++) {
	subexpr = tcexprarray_get(&te->tuple.exprs, i);
	subexpr = tcexpr_stepjoins(ctx, subexpr);
	tcexprarray_set(&te->tuple.exprs, i, subexpr);
     }
     break;
    case TCE_VALUE:
     break;
   }
   return te;
}

////////////////////////////////////////////////////////////
// entry point

struct tcexpr *stepjoins(struct pqlcontext *pql, struct tcexpr *te) {
   struct stepjoins ctx;

   stepjoins_init(&ctx, pql);
   te = tcexpr_stepjoins(&ctx, te);
   stepjoins_cleanup(&ctx);

   return te;
}

