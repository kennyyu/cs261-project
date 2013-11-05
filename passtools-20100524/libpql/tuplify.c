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
 * Convert parse tree to tuple calculus.
 */

#include <string.h>

#include "utils.h"
#include "pql.h"
#include "columns.h"
#include "pqlvalue.h"
#include "pttree.h"
#include "tcalc.h"
#include "passes.h"

struct tuplify {
   struct pqlcontext *pql;

   struct {
      struct ptglobalvararray pt;
      struct tcglobalarray tc;
   } globalvars;
   struct {
      struct ptcolumnvararray pt;
      struct colnamearray tc;
   } columnvars;

   /* The names for the columns of the all-objects table. */
   struct colname *leftobjcolumn;
   struct colname *edgecolumn;
   struct colname *rightobjcolumn;
};

////////////////////////////////////////////////////////////

static void tuplify_init(struct tuplify *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;

   ptglobalvararray_init(&ctx->globalvars.pt);
   tcglobalarray_init(&ctx->globalvars.tc);
   ptcolumnvararray_init(&ctx->columnvars.pt);
   colnamearray_init(&ctx->columnvars.tc);

   ctx->leftobjcolumn = mkcolname_fresh(ctx->pql);
   ctx->edgecolumn = mkcolname_fresh(ctx->pql);
   ctx->rightobjcolumn = mkcolname_fresh(ctx->pql);
}

static void tuplify_cleanup(struct tuplify *ctx) {

   ptglobalvararray_setsize(ctx->pql, &ctx->globalvars.pt, 0);
   ptglobalvararray_cleanup(ctx->pql, &ctx->globalvars.pt);
   tcglobalarray_setsize(ctx->pql, &ctx->globalvars.tc, 0);
   tcglobalarray_cleanup(ctx->pql, &ctx->globalvars.tc);

   ptcolumnvararray_setsize(ctx->pql, &ctx->columnvars.pt, 0);
   ptcolumnvararray_cleanup(ctx->pql, &ctx->columnvars.pt);
   colnamearray_setsize(ctx->pql, &ctx->columnvars.tc, 0);
   colnamearray_cleanup(ctx->pql, &ctx->columnvars.tc);

   colname_decref(ctx->pql, ctx->leftobjcolumn);
   colname_decref(ctx->pql, ctx->edgecolumn);
   colname_decref(ctx->pql, ctx->rightobjcolumn);

   ctx->leftobjcolumn = NULL;
   ctx->edgecolumn = NULL;
   ctx->rightobjcolumn = NULL;
}

////////////////////////////////////////////////////////////

static struct tcexpr *ptexpr_tuplify(struct tuplify *ctx, struct ptexpr *pe,
				     struct tcvar *curtuplevar);

static struct tcglobal *ptglobalvar_tuplify(struct tuplify *ctx,
					    struct ptglobalvar *gv) {
   unsigned i, num;
   struct tcglobal *tcg;

   /*
    * This should be smarter.
    *
    * XXX: is there any reason to bother keeping global var objects
    * explicitly, instead of just a name?
    */

   num = ptglobalvararray_num(&ctx->globalvars.pt);
   for (i=0; i<num; i++) {
      if (ptglobalvararray_get(&ctx->globalvars.pt, i) == gv) {
	 tcg = tcglobalarray_get(&ctx->globalvars.tc, i);
	 tcglobal_incref(tcg);
	 return tcg;
      }
   }
   
   tcg = mktcglobal(ctx->pql, gv->name);
   ptglobalvararray_add(ctx->pql, &ctx->globalvars.pt, gv, NULL);
   tcglobalarray_add(ctx->pql, &ctx->globalvars.tc, tcg, NULL);
   return tcg;
}

static struct colname *ptcolumnvar_tuplify(struct tuplify *ctx,
					    struct ptcolumnvar *col) {
   unsigned i, num;
   struct colname *tccol;

   /*
    * This should be smarter.
    */

   num = ptcolumnvararray_num(&ctx->columnvars.pt);
   for (i=0; i<num; i++) {
      if (ptcolumnvararray_get(&ctx->columnvars.pt, i) == col) {
	 tccol = colnamearray_get(&ctx->columnvars.tc, i);
	 colname_incref(tccol);
	 return tccol;
      }
   }
   
   tccol = mkcolname(ctx->pql, col->name);
   ptcolumnvararray_add(ctx->pql, &ctx->columnvars.pt, col, NULL);
   colnamearray_add(ctx->pql, &ctx->columnvars.tc, tccol, NULL);
   return tccol;
}

static struct colset *ptcolumnvararray_tuplify(struct tuplify *ctx,
					       struct ptcolumnvararray *arr) {
   unsigned i, num;
   struct ptcolumnvar *cv;
   struct colname *tccol;
   struct colset *ret;

   ret = colset_empty(ctx->pql);

   num = ptcolumnvararray_num(arr);
   for (i=0; i<num; i++) {
      cv = ptcolumnvararray_get(arr, i);
      tccol = ptcolumnvar_tuplify(ctx, cv);
      colset_add(ctx->pql, ret, tccol);
   }

   return ret;
}

/*
 * Make a tuple expression for a path.
 *
 * STARTEXPR is a tcexpr for the point to start from. The caller
 * should consume it, exactly once. If it needs to be referenced more
 * than once it should be let-bound.
 *
 * Each path node takes an input expression and a start column to use
 * in that input expression, and produces an output expression and a
 * an output column, which is the start column for the next path step.
 *
 * Refcounting discipline for columns here:
 *    1. startcolumn is not an owned reference.
 *       Any time it's put in an expression it should be incref'd first.
 *
 *    2. *ret_column *is* an owned reference.
 *       It should be decref'd or used by the caller.
 *
 * The caller is expected to strip startcolumn from the result unless
 * it's supposed to stay there. The caller is also expected to strip
 * the returned *ret_column from the result once done with it. The
 * latter should never be a column associated with bound variables in
 * the input.
 */
static void ptpath_tuplify(struct tuplify *ctx,
			   struct tcexpr *startexpr,
			   struct colname *startcolumn,
			   struct ptpath *pt,
			   struct tcexpr **ret_expr,
			   struct colname **ret_column) {
   unsigned i, num;

   PQLASSERT(pt->bindobjbefore == NULL);

   switch (pt->type) {
    case PTP_SEQUENCE:
     PQLASSERT(pt->bindobjafter == NULL);
     PQLASSERT(pt->bindpath == NULL);
     {
	struct ptpath *ptsub;
	struct tcexpr *te;
	struct colname *col, *prevcol;

	te = startexpr;
	col = startcolumn;
	prevcol = NULL;

	num = ptpatharray_num(&pt->sequence.items);
	for (i=0; i<num; i++) {
	   ptsub = ptpatharray_get(&pt->sequence.items, i);
	   ptpath_tuplify(ctx, te, col, ptsub, &te, &col);
	   if (prevcol != NULL) {
	      /* consume the *previous* returned reference */
	      te = mktcexpr_strip_one(ctx->pql, te, prevcol);
	   }
	   prevcol = col;
	}

	*ret_expr = te;
	*ret_column = col;
     }
     break;

    case PTP_ALTERNATES:
     PQLASSERT(pt->bindobjafter == NULL);
     PQLASSERT(pt->bindpath == NULL);
     {
	struct tcvar *startvar;
	struct tcexpr *sub, *ret;
	struct colname *outcolumn;

	startvar = mktcvar_fresh(ctx->pql);
	ret = NULL;
	num = ptpatharray_num(&pt->sequence.items);
	PQLASSERT(num > 0);
	for (i=0; i<num; i++) {
	   tcvar_incref(startvar);
	   ptpath_tuplify(ctx, mktcexpr_readvar(ctx->pql, startvar),
			  startcolumn,
			  ptpatharray_get(&pt->alternates.items, i),
			  &sub, &outcolumn);
	   sub = mktcexpr_strip_one(ctx->pql, sub, outcolumn);
	   if (ret == NULL) {
	      ret = sub;
	   }
	   else {
	      ret = mktcexpr_bop(ctx->pql, ret, F_UNIONALL, sub);
	   }
	}

	PQLASSERT(ret != NULL);

	ret = mktcexpr_let(ctx->pql, startvar, startexpr, ret);
	*ret_expr = ret;

	// XXX I think this is wrong.
	//
	// 1. We shouldn't be returning tailvar but an adjoined copy
	// of it, because when the caller strips *ret_column it will
	// abolish the tailvar column and bad things will happen.
	//
	// 2. Worse, since tailvar is arranged as a let-binding
	// upstream, it doesn't actually exist yet here, and if the
	// caller tries to use it for a subsequent path step or
	// whatnot things will blow up. I think that let-binding is
	// going to need to be moved into the ptpath instead of being
	// dropped in afterwards. Blah...
	//*ret_column = ptcolumnvar_tuplify(ctx, pt->alternates.tailvar);

	// yes, it's wrong. for now, make up a dummy column; this at
	// least doesn't blow up if it isn't used. XXX
	{
	   struct colname *foo;

	   foo = mkcolname_fresh(ctx->pql);
	   colname_incref(foo);
	   *ret_expr = mktcexpr_distinguish(ctx->pql, *ret_expr, foo);
	   *ret_column = foo;
	}
     }
     break;

    case PTP_OPTIONAL:
     /*PQLASSERT(pt->bindobjafter == NULL); -- no such luck */
     PQLASSERT(pt->bindpath == NULL);
     {
	struct ptcolumnvar *ptcol;
	struct tcexpr *tcsub, *ret, *clonefunc, *nil;
	struct colname *sub_outcolumn, *resultcolumn, *nilcolumn;
	struct tcvar *startvar, *lambdavar;

	resultcolumn = mkcolname_fresh(ctx->pql);
	
	/* Make a variable to bind the input expression. */
	startvar = mktcvar_fresh(ctx->pql);

	/*
	 * First case: the optional stuff is matched.
	 */

	/* Build the expression for the optional stuff. */
	tcvar_incref(startvar);
	ptpath_tuplify(ctx, mktcexpr_readvar(ctx->pql, startvar),
		       startcolumn,
		       pt->optional.sub,
		       &ret, &sub_outcolumn);

	/*
	 * Since we're expected to strip sub_outcolumn, we don't need to
	 * adjoin a copy of it but can just rename it in place.
	 */
#if 0
	/* Adjoin a copy of sub_outcolumn as resultcolumn */
	lambdavar = mktcvar_fresh(ctx->pql);
	tcvar_incref(lambdavar);
	colname_incref(sub_outcolumn);
	clonefunc = mktcexpr_project_one(ctx->pql,
					 mktcexpr_readvar(ctx->pql, lambdavar),
					 sub_outcolumn);
	clonefunc = mktcexpr_lambda(ctx->pql, lambdavar, clonefunc);
	colname_incref(resultcolumn);
	ret = mktcexpr_adjoin(ctx->pql, ret, clonefunc, resultcolumn);
	/*colname_incref(sub_outcolumn) -- consume returned reference */
	ret = mktcexpr_strip_one(ctx->pql, ret, sub_outcolumn);
#else
	colname_incref(resultcolumn);
	ret = mktcexpr_rename(ctx->pql, ret, sub_outcolumn, resultcolumn);
#endif

	/*
	 * Second case: the optional stuff is skipped.
	 */

	/* Get a copy of the input */
	tcvar_incref(startvar);
	tcsub = mktcexpr_readvar(ctx->pql, startvar);

	/* for each nil-bind column, adjoin nil */
	num = ptcolumnvararray_num(&pt->optional.nilcolumns);
	for (i=0; i<num; i++) {
	   ptcol = ptcolumnvararray_get(&pt->optional.nilcolumns, i);
	   nilcolumn = ptcolumnvar_tuplify(ctx, ptcol);
	   nil = mktcexpr_value(ctx->pql, pqlvalue_nil(ctx->pql));
	   nil = mktcexpr_lambda(ctx->pql, mktcvar_fresh(ctx->pql), nil);
	   tcsub = mktcexpr_adjoin(ctx->pql, tcsub, nil, nilcolumn);
	}

	/* adjoin a copy of the start column as the result column */
	lambdavar = mktcvar_fresh(ctx->pql);
	tcvar_incref(lambdavar);
	colname_incref(startcolumn);
	clonefunc = mktcexpr_project_one(ctx->pql,
					 mktcexpr_readvar(ctx->pql, lambdavar),
					 startcolumn);
	clonefunc = mktcexpr_lambda(ctx->pql, lambdavar, clonefunc);
	colname_incref(resultcolumn);
	tcsub = mktcexpr_adjoin(ctx->pql, tcsub, clonefunc, resultcolumn);

	/*
	 * Take the union of both cases
	 */
	ret = mktcexpr_bop(ctx->pql, ret, F_UNIONALL, tcsub);

	/* let-bind the input context */
	/*tcvar_incref(startvar) -- use up the creation reference */
	ret = mktcexpr_let(ctx->pql, startvar, startexpr, ret);

	/* If we have something to bind, adjoin a copy of the result */
	if (pt->bindobjafter != NULL) {
	   struct tcexpr *objexpr;
	   struct tcvar *lvar;
	   struct colname *bindcolumn;

	   lvar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(lvar);

	   colname_incref(resultcolumn);
	   objexpr = mktcexpr_project_one(ctx->pql,
					  mktcexpr_readvar(ctx->pql, lvar),
					  resultcolumn);
	   objexpr = mktcexpr_lambda(ctx->pql, lvar, objexpr);

	   bindcolumn = ptcolumnvar_tuplify(ctx, pt->bindobjafter);
	   ret = mktcexpr_adjoin(ctx->pql, ret, objexpr, bindcolumn);
	}

	*ret_expr = ret;
	*ret_column = resultcolumn;
     }
     break;

    case PTP_REPEATED:
     /*PQLASSERT(pt->bindobjafter == NULL); -- no such luck */
     PQLASSERT(pt->bindpath == NULL);
     {
	struct tcexpr *tcsub, *ret;
	struct colname *bodypathcolumn, *reppathcolumn;
	struct colname *bodystartcolumn, *bodyendcolumn, *rependcolumn;
	struct tcvar *loopvar;

	loopvar = mktcvar_fresh(ctx->pql);
	bodystartcolumn = mkcolname_fresh(ctx->pql);
	rependcolumn = mkcolname_fresh(ctx->pql);

	tcvar_incref(loopvar);
	colname_incref(bodystartcolumn);
	ptpath_tuplify(ctx,
		       mktcexpr_readvar(ctx->pql, loopvar),
		       bodystartcolumn,
		       pt->repeated.sub,
		       &tcsub, &bodyendcolumn);

	if (pt->repeated.pathfrominside != NULL) {
	   bodypathcolumn = ptcolumnvar_tuplify(ctx,
						pt->repeated.pathfrominside);
	   reppathcolumn = ptcolumnvar_tuplify(ctx,
					       pt->repeated.pathonoutside);
	}
	else {
	   bodypathcolumn = NULL;
	   reppathcolumn = NULL;
	}

	colname_incref(startcolumn);
	/*tcvar_incref(loopvar) -- consume creation reference */
	colname_incref(bodystartcolumn);
	colname_incref(rependcolumn);
	ret = mktcexpr_repeat(ctx->pql,
			      startexpr,
			      startcolumn,
			      loopvar,
			      bodystartcolumn,
			      tcsub,
			      bodypathcolumn,
			      bodyendcolumn,
			      reppathcolumn,
			      rependcolumn);

	if (pt->bindobjafter != NULL) {
	   struct tcexpr *objexpr;
	   struct tcvar *lvar;
	   struct colname *bindcolumn;

	   lvar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(lvar);

	   colname_incref(rependcolumn);
	   objexpr = mktcexpr_project_one(ctx->pql,
					  mktcexpr_readvar(ctx->pql, lvar),
					  rependcolumn);
	   objexpr = mktcexpr_lambda(ctx->pql, lvar, objexpr);

	   bindcolumn = ptcolumnvar_tuplify(ctx, pt->bindobjafter);
	   ret = mktcexpr_adjoin(ctx->pql, ret, objexpr, bindcolumn);
	}

	/*colname_incref(bodystartcolumn) -- consume creation reference */
	// don't need to do this now
	//ret = mktcexpr_strip_one(ctx->pql, ret, bodystartcolumn);
	colname_decref(ctx->pql, bodystartcolumn);

	*ret_expr = ret;
	*ret_column = rependcolumn;
     }
     break;

    case PTP_NILBIND:
     PQLASSERT(pt->bindobjafter == NULL);
     PQLASSERT(pt->bindpath == NULL);
     {
	unsigned i, num;
	struct ptcolumnvar *ptcol;
	struct colname *tccol;
	struct tcvar *tcvar;
	struct tcexpr *tcsub;
	
	*ret_expr = startexpr;

	num = ptcolumnvararray_num(&pt->nilbind.columnsbefore);
	for (i=0; i<num; i++) {
	   ptcol = ptcolumnvararray_get(&pt->nilbind.columnsbefore, i);
	   tccol = ptcolumnvar_tuplify(ctx, ptcol);
	   tcsub = mktcexpr_value(ctx->pql, pqlvalue_nil(ctx->pql));
	   /* maybe we should have an adjoin-constant node type... */
	   tcvar = mktcvar_fresh(ctx->pql);
	   tcsub = mktcexpr_lambda(ctx->pql, tcvar, tcsub);
	   *ret_expr = mktcexpr_adjoin(ctx->pql, *ret_expr, tcsub, tccol);
	   /* don't change ret_column */
	}

	ptpath_tuplify(ctx, *ret_expr, startcolumn, pt->nilbind.sub,
		       ret_expr, ret_column);

	num = ptcolumnvararray_num(&pt->nilbind.columnsafter);
	for (i=0; i<num; i++) {
	   ptcol = ptcolumnvararray_get(&pt->nilbind.columnsafter, i);
	   tccol = ptcolumnvar_tuplify(ctx, ptcol);
	   tcsub = mktcexpr_value(ctx->pql, pqlvalue_nil(ctx->pql));
	   /* maybe we should have an adjoin-constant node type... */
	   tcvar = mktcvar_fresh(ctx->pql);
	   tcsub = mktcexpr_lambda(ctx->pql, tcvar, tcsub);
	   *ret_expr = mktcexpr_adjoin(ctx->pql, *ret_expr, tcsub, tccol);
	   /* don't change ret_column; use the subexpression's */
	}
     }
     break;

    case PTP_EDGE:
     {
	struct colname *fromcolumn, *tocolumn, *computededgecolumn;
	struct colname *aftercolumn;
	struct tcvar *lambdavar;
	struct tcexpr *left, *right, *scan, *predicate;
	struct tcexpr *ret;

	if (pt->edge.iscomputed) {
	   struct tcvar *startvar;
	   struct tcexpr *edge;
	
	   computededgecolumn = mkcolname_fresh(ctx->pql);
	   colname_incref(computededgecolumn); /* consumed below */

	   startvar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(startvar);
	   edge = ptexpr_tuplify(ctx, pt->edge.computedname, startvar);
	   edge = mktcexpr_lambda(ctx->pql, startvar, edge);
	   /* add to startexpr so this is used below */
	   startexpr = mktcexpr_adjoin(ctx->pql, startexpr,
				       edge, computededgecolumn);
	}
	else {
	   computededgecolumn = NULL;
	}

	if (pt->edge.reversed) {
	   fromcolumn = ctx->rightobjcolumn;
	   tocolumn = ctx->leftobjcolumn;
	}
	else {
	   fromcolumn = ctx->leftobjcolumn;
	   tocolumn = ctx->rightobjcolumn;
	}

	/* assemble the join condition */
	lambdavar = mktcvar_fresh(ctx->pql);

	tcvar_incref(lambdavar);
	colname_incref(startcolumn);
	left = mktcexpr_project_one(ctx->pql,
				    mktcexpr_readvar(ctx->pql, lambdavar),
				    startcolumn);
	tcvar_incref(lambdavar);
	colname_incref(fromcolumn);
	right = mktcexpr_project_one(ctx->pql,
				     mktcexpr_readvar(ctx->pql, lambdavar),
				     fromcolumn);
	predicate = mktcexpr_bop(ctx->pql, left, F_EQ, right);

	tcvar_incref(lambdavar);
	colname_incref(ctx->edgecolumn);
	left = mktcexpr_project_one(ctx->pql,
				    mktcexpr_readvar(ctx->pql, lambdavar),
				    ctx->edgecolumn);
	if (computededgecolumn != NULL) {
	   tcvar_incref(lambdavar);
	   right = mktcexpr_project_one(ctx->pql,
					mktcexpr_readvar(ctx->pql, lambdavar),
					computededgecolumn);
	   right = mktcexpr_bop(ctx->pql, left, F_EQ, right);
	}
	else {
	   right = mktcexpr_value(ctx->pql,
				  pqlvalue_string(ctx->pql,
						  pt->edge.staticname));

	   if (strchr(pt->edge.staticname, '%') != NULL ||
	       strchr(pt->edge.staticname, '_') != NULL) {
	      right = mktcexpr_bop(ctx->pql, left, F_LIKE, right);
	   }
	   else {
	      right = mktcexpr_bop(ctx->pql, left, F_EQ, right);
	   }
	}
	predicate = mktcexpr_bop(ctx->pql, predicate, F_AND, right);

	predicate = mktcexpr_lambda(ctx->pql, lambdavar, predicate);

	/* Fetch the table of objects */
	colname_incref(ctx->leftobjcolumn);
	colname_incref(ctx->edgecolumn);
	colname_incref(ctx->rightobjcolumn);
	scan = mktcexpr_scan(ctx->pql,
			     ctx->leftobjcolumn,
			     ctx->edgecolumn,
			     ctx->rightobjcolumn,
			     NULL);

	/* Do the join */
	ret = mktcexpr_join(ctx->pql, startexpr, scan, predicate);

	/* Prepare the output */
	if (pt->bindpath != NULL) {
	   struct tcvar *lvar;
	   struct tcexpr *pathexpr;

	   lvar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(lvar);

	   colname_incref(ctx->leftobjcolumn);
	   colname_incref(ctx->edgecolumn);
	   colname_incref(ctx->rightobjcolumn);
	   pathexpr = mktcexpr_project_three(ctx->pql,
					     mktcexpr_readvar(ctx->pql, lvar),
					     ctx->leftobjcolumn,
					     ctx->edgecolumn,
					     ctx->rightobjcolumn);
	   pathexpr = mktcexpr_createpathelement(ctx->pql, pathexpr);
	   pathexpr = mktcexpr_lambda(ctx->pql, lvar, pathexpr);

	   ret = mktcexpr_adjoin(ctx->pql, ret, pathexpr,
				 ptcolumnvar_tuplify(ctx, pt->bindpath));
	}
	if (pt->bindobjafter != NULL) {
	   struct tcexpr *objexpr;
	   struct tcvar *lvar;
	   struct colname *bindcol;

	   lvar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(lvar);

	   colname_incref(tocolumn);
	   objexpr = mktcexpr_project_one(ctx->pql,
					  mktcexpr_readvar(ctx->pql, lvar),
					  tocolumn);
	   objexpr = mktcexpr_lambda(ctx->pql, lvar, objexpr);

	   bindcol = ptcolumnvar_tuplify(ctx, pt->bindobjafter);
	   ret = mktcexpr_adjoin(ctx->pql, ret, objexpr, bindcol);
	}

	colname_incref(ctx->edgecolumn);
	ret = mktcexpr_strip_one(ctx->pql, ret, ctx->edgecolumn);

	colname_incref(fromcolumn);
	ret = mktcexpr_strip_one(ctx->pql, ret, fromcolumn);

	aftercolumn = mkcolname_fresh(ctx->pql);

	colname_incref(aftercolumn);
	colname_incref(tocolumn);
	ret = mktcexpr_rename(ctx->pql, ret, tocolumn, aftercolumn);
     
	*ret_expr = ret;
	*ret_column = aftercolumn;
     }
     break;
   }
}

/*
 * One item in the from-clause.
 */
static struct tcexpr *ptexpr_onefrom_tuplify(struct tuplify *ctx,
					 struct ptexpr *pe,
					 struct tcvar *tuplevar) {
   struct ptexpr *sublet;
   struct tcvar *subvar;
   struct tcexpr *te, *subte;
   unsigned i, num;

   /*
    * Should be either a path or an assignment (with no body)
    */
   if (pe->type != PTE_PATH) {
      PQLASSERT(pe->type == PTE_ASSIGN);
      PQLASSERT(pe->assign.body == NULL);
      return ptexpr_tuplify(ctx, pe, tuplevar);
   }

   /* Path might have a global variable or a column as its root */
   if (pe->path.root->type == PTE_READGLOBALVAR) {
      struct tcglobal *root;
      struct colname *startcolumn, *outcolumn;

      /* Read the root. */
      root = ptglobalvar_tuplify(ctx, pe->path.root->readglobalvar);
      startcolumn = mkcolname_fresh(ctx->pql);
      te = mktcexpr_readglobal(ctx->pql, root);

      colname_incref(startcolumn);
      te = mktcexpr_rename(ctx->pql, te, NULL /*XXX?*/, startcolumn);

      /* Build the path. */
      ptpath_tuplify(ctx, te, startcolumn, pe->path.body, &te, &outcolumn);
      te = mktcexpr_strip_one(ctx->pql, te, outcolumn);

      /* Strip the root out. */
      te = mktcexpr_strip_one(ctx->pql, te, startcolumn);

      /*
       * If we have a context, result is context x path. (The first
       * thing in the outermost from-clause has no context.)
       */
      if (tuplevar != NULL) {
	 tcvar_incref(tuplevar);
	 te = mktcexpr_join(ctx->pql,
			    mktcexpr_readvar(ctx->pql, tuplevar), te, NULL);
      }
   }
   else {
      struct colname *root, *outcolumn;

      PQLASSERT(pe->path.root->type == PTE_READCOLUMNVAR);

      tcvar_incref(tuplevar);
      te = mktcexpr_readvar(ctx->pql, tuplevar);

      root = ptcolumnvar_tuplify(ctx, pe->path.root->readcolumnvar);
      ptpath_tuplify(ctx, te, root, pe->path.body, &te, &outcolumn);
      te = mktcexpr_strip_one(ctx->pql, te, outcolumn);
      colname_decref(ctx->pql, root);
   }

   num = ptexprarray_num(&pe->path.morebindings);
   for (i=0; i<num; i++) {
      sublet = ptexprarray_get(&pe->path.morebindings, i);
      PQLASSERT(sublet->type == PTE_ASSIGN);
      PQLASSERT(sublet->assign.body == NULL);

      subvar = mktcvar_fresh(ctx->pql);
      subte = ptexpr_tuplify(ctx, sublet, subvar);
      te = mktcexpr_let(ctx->pql, subvar, te, subte);
   }

   return te;
}

/*
 * full from-clause
 */
static struct tcexpr *ptexpr_from_tuplify(struct tuplify *ctx,
					 struct ptexpr *pe,
					 struct tcvar *tuplevar) {
   unsigned i, num;
   struct tcvararray vars;
   struct tcexprarray tcexprs;
   struct tcvar *var;
   struct tcexpr *te;

   PQLASSERT(pe->type == PTE_FROM);

   tcvararray_init(&vars);
   tcexprarray_init(&tcexprs);

   num = ptexprarray_num(pe->from);
   tcvararray_setsize(ctx->pql, &vars, num);
   tcexprarray_setsize(ctx->pql, &tcexprs, num);

   PQLASSERT(num > 0);

   /*
    * For each from-clause item, tuplify it using a fresh variable for
    * the context. Each item's context will be bound to the (complete)
    * result from the previous item below.
    *
    * For the first item, use the input context, if there is one.
    * Otherwise, leave the var null.
    */
   for (i=0; i<num; i++) {
      if (i == 0) {
	 /* This would make the array uniform wrt. refcounts */
	 //if (tuplevar != NULL) {
	 //   tcvar_incref(tuplevar);
	 //}
	 var = tuplevar;
      }
      else {
	 var = mktcvar_fresh(ctx->pql);
      }
      te = ptexpr_onefrom_tuplify(ctx, ptexprarray_get(pe->from, i), var);
      tcvararray_set(&vars, i, var);
      tcexprarray_set(&tcexprs, i, te);
   }

   /*
    * Now sew it all together with a series of let-bindings. This has
    * to run back-to-front to set them up properly.
    *
    * Given tuplevar and:
    *
    *      v0        e0		(v0 is the previous context for e0)
    *      v1        e1		(v1 is the previous context for e1)
    *      v2        e2		(v2 is the previous context for e2)
    *
    * we generate
    *
    *      let v1 = e0
    *          v2 = e1
    *      in e2
    *
    * and since v0 == tuplevar (the input var) we assume it's already
    * bound.
    *
    * Things could be numbered so the indexes in this loop lines up,
    * but then the numbers in the previous loop don't. It would be
    * clearer as a recursive function, I suppose.
    */
   te = tcexprarray_get(&tcexprs, num-1);
   for (i=num-1; i-- > 0;) {
      var = tcvararray_get(&vars, i+1);
      te = mktcexpr_let(ctx->pql, var, tcexprarray_get(&tcexprs, i), te);
   }

   PQLASSERT(tcvararray_get(&vars, 0) == tuplevar);

   /* This matches the commented-out incref above */
   //if (tuplevar != NULL) {
   //   tcvar_decref(tuplevar);
   //}

   tcvararray_setsize(ctx->pql, &vars, 0);
   tcexprarray_setsize(ctx->pql, &tcexprs, 0);
   tcvararray_cleanup(ctx->pql, &vars);
   tcexprarray_cleanup(ctx->pql, &tcexprs);

   return te;
}

static struct tcexpr *ptexpr_tuplify(struct tuplify *ctx, struct ptexpr *pe,
				     struct tcvar *curtuplevar) {
   struct ptexpr *ptsub;
   struct tcexpr *ret, *tcsub;
   struct colname *col;
   unsigned i, num;

   ret = NULL; // gcc 4.1

   switch (pe->type) {
    case PTE_SELECT:
     tcsub = ptexpr_tuplify(ctx, pe->select.sub, curtuplevar);

     /* Get a new tuplevar for the select-clause */
     curtuplevar = mktcvar_fresh(ctx->pql);

     ret = ptexpr_tuplify(ctx, pe->select.result, curtuplevar);
     ret = mktcexpr_map(ctx->pql, curtuplevar, tcsub, ret);

     if (pe->select.distinct) {
	ret = mktcexpr_order(ctx->pql, ret);
	ret = mktcexpr_uniq(ctx->pql, ret);
     }
     break;

    case PTE_FROM:
     if (ptexprarray_num(pe->from) > 0) {
	ret = ptexpr_from_tuplify(ctx, pe, curtuplevar);
     }
     else {
	struct pqlvalue *set;

	set = pqlvalue_emptyset(ctx->pql);
	pqlvalue_set_add(set, pqlvalue_nil(ctx->pql));
	ret = mktcexpr_value(ctx->pql, set);
     }
     break;

    case PTE_WHERE:
     {
	struct tcvar *lambdavar;

	tcsub = ptexpr_tuplify(ctx, pe->where.sub, curtuplevar);

	lambdavar = mktcvar_fresh(ctx->pql);

	ret = ptexpr_tuplify(ctx, pe->where.where, lambdavar);
	ret = mktcexpr_lambda(ctx->pql, lambdavar, ret);

	ret = mktcexpr_filter(ctx->pql, tcsub, ret);
     }
     break;

    case PTE_GROUP:
     {
	struct colset *cols;
	struct colname *newcol;

	cols = ptcolumnvararray_tuplify(ctx, pe->group.vars);
	newcol = ptcolumnvar_tuplify(ctx, pe->group.newvar);

	colset_mark_tocomplement(cols);

	tcsub = ptexpr_tuplify(ctx, pe->group.sub, curtuplevar);
	ret = mktcexpr_nest_set(ctx->pql, tcsub, cols, newcol);
     }
     break;

    case PTE_UNGROUP:
     {
	struct colname *col;

	col = ptcolumnvar_tuplify(ctx, pe->ungroup.var);
	tcsub = ptexpr_tuplify(ctx, pe->ungroup.sub, curtuplevar);
	ret = mktcexpr_unnest(ctx->pql, tcsub, col);
     }
     break;

    case PTE_RENAME:
     PQLASSERT(!"PTE_RENAME is not supposed to happen");
     ret = ptexpr_tuplify(ctx, pe->rename.sub, curtuplevar);
     if (pe->rename.iscomputed) {
	// XXX this is no good
	PQLASSERT(0);

	tcsub = ptexpr_tuplify(ctx, pe->rename.computedname, curtuplevar);
	col = mkcolname_fresh(ctx->pql);
	colname_incref(col);
	ret = mktcexpr_adjoin(ctx->pql, ret, tcsub, col);
     }
     else {
	col = mkcolname(ctx->pql, pe->rename.staticname);
     }
     colname_decref(ctx->pql, col);
     // XXX this is not right
     ret = mktcexpr_rename(ctx->pql, ret, NULL, col);
     break;

    case PTE_PATH:
     // handled in ptexpr_sfw_tuplify()
     PQLASSERT(0);
     ret = NULL;
     break;

    case PTE_TUPLE:
     num = ptexprarray_num(pe->tuple);
     PQLASSERT(num > 0);
     ret = mktcexpr_tuple(ctx->pql, num);
     for (i=0; i<num; i++) {
	ptsub = ptexprarray_get(pe->tuple, i);
	if (ptsub->type == PTE_RENAME) {
	   tcsub = ptexpr_tuplify(ctx, ptsub->rename.sub, curtuplevar);
	   if (ptsub->rename.iscomputed) {
	      ptsub = ptsub->rename.computedname;
	      tcsub = mktcexpr_splatter(ctx->pql, tcsub,
					ptexpr_tuplify(ctx, ptsub,
						       curtuplevar));
	      col = mkcolname_fresh(ctx->pql);
	   }
	   else {
	      col = mkcolname(ctx->pql, ptsub->rename.staticname);
	   }
	}
	else {
	   tcsub = ptexpr_tuplify(ctx, ptsub, curtuplevar);
	   col = mkcolname_fresh(ctx->pql);
	}
	tcexprarray_set(&ret->tuple.exprs, i, tcsub);
	colset_set(ret->tuple.columns, i, col);
     }
     break;

    case PTE_FORALL:
    case PTE_EXISTS:
     // removed upstream
     PQLASSERT(0);
     break;

    case PTE_MAP:
     {
	struct colname *varcol, *distcol, *resultcol;
	struct tcvar *setlambdavar, *bodylambdavar;
	struct tcexpr *set, *result;
	struct tcexpr *herecontext, *subcontext;

	/*
	 * Columns we'll need: one for the bound variable, one for a
	 * distinguisher, and one for the result.
	 */
	varcol = ptcolumnvar_tuplify(ctx, pe->map.var);
	colname_incref(varcol);
	distcol = mkcolname_fresh(ctx->pql);
	colname_incref(distcol);
	resultcol = mkcolname_fresh(ctx->pql);
	colname_incref(resultcol);
	colname_incref(resultcol);
	colname_incref(resultcol);
	colname_incref(resultcol);

	/*
	 * First, convert the set expression and adjoin it to the
	 * current tuple set. This provides the context tuple for the
	 * body of the map expression.
	 */
	if (curtuplevar != NULL) {
	   setlambdavar = mktcvar_fresh(ctx->pql);
	   set = ptexpr_tuplify(ctx, pe->map.set, setlambdavar);
	   set = mktcexpr_lambda(ctx->pql, setlambdavar, set);
	   tcvar_incref(curtuplevar);
	   herecontext = mktcexpr_readvar(ctx->pql, curtuplevar);
	   subcontext = mktcexpr_adjoin(ctx->pql, herecontext, set, varcol);
	}
	else {
	   /*
	    * If we have no current tuple, we're outside all select
	    * clauses and the current tuple we're working with is
	    * effectively unit. We could create that unit and adjoin
	    * the set to it. But instead of giving us ( (), set ),
	    * which would work (if we stripped off the spurious unit
	    * later), that gives us ( set ), which is just the set,
	    * and then adjoining the distinguisher gives us the
	    * wrong thing. So instead we'll wrap the whole thing in
	    * another set constructor.
	    */
	   set = ptexpr_tuplify(ctx, pe->map.set, curtuplevar);
	   subcontext = mktcexpr_rename(ctx->pql, set, NULL, varcol);
	   subcontext = mktcexpr_uop(ctx->pql, F_MAKESET, subcontext);
	}

	/*
 	 * Add a distinguisher and unnest.
	 */
	subcontext = mktcexpr_distinguish(ctx->pql, subcontext, distcol);
	subcontext = mktcexpr_unnest(ctx->pql, subcontext, varcol);

	/*
	 * Convert the body using a lambda-bound variable and adjoin it
	 * to the subcontext.
	 */
	bodylambdavar = mktcvar_fresh(ctx->pql);
	result = ptexpr_tuplify(ctx, pe->map.result, bodylambdavar);
	result = mktcexpr_lambda(ctx->pql, bodylambdavar, result);
	result = mktcexpr_adjoin(ctx->pql, subcontext, result, resultcol);

	/*
	 * Project out just the distinguisher and the result, so the
	 * nest DTRT.
	 */
	result = mktcexpr_project_two(ctx->pql, result, resultcol, distcol);

	/*
	 * Now re-nest. The distinguisher forces the proper grouping.
	 */
	result = mktcexpr_nest_one(ctx->pql, result, resultcol, resultcol);

	/*
	 * Project out the result and we're done: we have a one-column
	 * set with a set of results for each of the sets found in the
	 * input context.
	 */
	result = mktcexpr_project_one(ctx->pql, result, resultcol);

	/*
	 * Except for one last thing: if we wrapped the set value as
	 * a singleton set, unwrap it.
	 */
	if (curtuplevar == NULL) {
	   result = mktcexpr_uop(ctx->pql, F_GETELEMENT, result);
	}

	ret = result;
     }
     break;

    case PTE_ASSIGN:
     {
	struct colname *varcol;
	struct tcvar *lambdavar, *newtuplevar;
	struct tcexpr *value, *curtuple, *newtuple, *body;

	varcol = ptcolumnvar_tuplify(ctx, pe->assign.var);

	if (curtuplevar != NULL) {
	   lambdavar = mktcvar_fresh(ctx->pql);
	   value = ptexpr_tuplify(ctx, pe->assign.value, lambdavar);
	   value = mktcexpr_lambda(ctx->pql, lambdavar, value);

	   /* Fetch curtuplevar, adjoin value */
	   tcvar_incref(curtuplevar);
	   curtuple = mktcexpr_readvar(ctx->pql, curtuplevar);
	   newtuple = mktcexpr_adjoin(ctx->pql, curtuple, value, varcol);
	}
	else {
	   newtuple = ptexpr_tuplify(ctx, pe->assign.value, curtuplevar);
	   newtuple = mktcexpr_rename(ctx->pql, newtuple, NULL, varcol);
	}

	if (pe->assign.body != NULL) {
	   /* use newtuple as context for body */
	   newtuplevar = mktcvar_fresh(ctx->pql);
	   tcvar_incref(newtuplevar);
	   body = ptexpr_tuplify(ctx, pe->assign.body, newtuplevar);
	   ret = mktcexpr_let(ctx->pql, newtuplevar, newtuple, body);
	}
	else {
	   /* return the whole tuple with the new column adjoined */
	   ret = newtuple;
	}
     }
     break;

    case PTE_BOP:
     ret = mktcexpr_bop(ctx->pql,
			ptexpr_tuplify(ctx, pe->bop.l, curtuplevar),
			pe->bop.op,
			ptexpr_tuplify(ctx, pe->bop.r, curtuplevar));
     break;

    case PTE_UOP:
     ret = mktcexpr_uop(ctx->pql,
			pe->uop.op,
			ptexpr_tuplify(ctx, pe->uop.sub, curtuplevar));
     break;

    case PTE_FUNC:
     num = pe->func.args ? ptexprarray_num(pe->func.args) : 0;
     ret = mktcexpr_func(ctx->pql, pe->func.op);
     for (i=0; i<num; i++) {
	ptsub = ptexprarray_get(pe->func.args, i);
	tcsub = ptexpr_tuplify(ctx, ptsub, curtuplevar);
	tcexprarray_add(ctx->pql, &ret->func.args, tcsub, NULL);
     }
     break;

    case PTE_READANYVAR:
     // not allowed here
     PQLASSERT(0);
     break;

    case PTE_READCOLUMNVAR:
     col = ptcolumnvar_tuplify(ctx, pe->readcolumnvar);
     tcvar_incref(curtuplevar);
     ret = mktcexpr_project_one(ctx->pql,
				mktcexpr_readvar(ctx->pql, curtuplevar), col);
     break;

    case PTE_READGLOBALVAR:
     ret = mktcexpr_readglobal(ctx->pql,
			       ptglobalvar_tuplify(ctx, pe->readglobalvar));
     break;

    case PTE_VALUE:
     ret = mktcexpr_value(ctx->pql, pqlvalue_clone(ctx->pql, pe->value));
     break;
   }

   return ret;
}

////////////////////////////////////////////////////////////

struct tcexpr *tuplify(struct pqlcontext *pql, struct ptexpr *pe) {
   struct tuplify ctx;
   struct tcexpr *ret;

   tuplify_init(&ctx, pql);
   ret = ptexpr_tuplify(&ctx, pe, NULL);
   tuplify_cleanup(&ctx);

   return ret;
}
