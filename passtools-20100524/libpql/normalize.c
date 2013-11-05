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
 * Normalize paths and expressions.
 */

#include "utils.h"
#include "pttree.h"
#include "passes.h"
#include "pql.h"

struct norm {
   struct pqlcontext *pql;
   bool infrom;
   struct ptexprarray exprs;
};

////////////////////////////////////////////////////////////

static void norm_init(struct norm *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   ptexprarray_init(&ctx->exprs);
}

static void norm_cleanup(struct norm *ctx) {
   PQLASSERT(ptexprarray_num(&ctx->exprs) == 0);
   ptexprarray_cleanup(ctx->pql, &ctx->exprs);
}

static void norm_getexprs_forward(struct ptexprarray *fill, struct norm *ctx) {
   unsigned i, num;

   num = ptexprarray_num(&ctx->exprs);
   for (i=0; i<num; i++) {
      ptexprarray_add(ctx->pql, fill, ptexprarray_get(&ctx->exprs, i), NULL);
   }
   ptexprarray_setsize(ctx->pql, &ctx->exprs, 0);
}

static void norm_getexprs_back(struct ptexprarray *fill, struct norm *ctx) {
   unsigned i, num;

   num = ptexprarray_num(&ctx->exprs);
   for (i=num; i-- > 0; ) {
      ptexprarray_add(ctx->pql, fill, ptexprarray_get(&ctx->exprs, i), NULL);
   }
   ptexprarray_setsize(ctx->pql, &ctx->exprs, 0);
}

static void norm_mkletbind(struct norm *ctx, struct ptcolumnvar *var,
			   struct ptexpr *pe) {
   struct ptexpr *let;

   let = mkptexpr_assign(ctx->pql, var, pe, NULL);
   ptexprarray_add(ctx->pql, &ctx->exprs, let, NULL);
}

static void norm_mkletbindvar(struct norm *ctx, struct ptcolumnvar *var,
			      struct ptcolumnvar *othervar) {
   norm_mkletbind(ctx, var, mkptexpr_readcolumnvar(ctx->pql, othervar));
}

////////////////////////////////////////////////////////////

/*
 * Collect nested sequences.
 */
static void ptpath_sequence_collect(struct norm *ctx,
				    struct ptpath *dest, struct ptpath *src) {
   unsigned num, i;
   struct ptpath *sub;

   PQLASSERT(dest->type == PTP_SEQUENCE);
   PQLASSERT(src->type == PTP_SEQUENCE);

   num = ptpatharray_num(&src->sequence.items);
   for (i=0; i<num; i++) {
      sub = ptpatharray_get(&src->sequence.items, i);
      if (sub->type == PTP_SEQUENCE) {
	 ptpath_sequence_collect(ctx, dest, sub);
	 //ptpath_destroy(sub); -- done by region allocator
      }
      else {
	 ptpatharray_add(ctx->pql, &dest->sequence.items, sub, NULL);
      }
   }
   ptpatharray_setsize(ctx->pql, &src->sequence.items, 0);
   PQLASSERT(src->bindobjbefore == NULL);
   PQLASSERT(src->bindobjafter == NULL);
   PQLASSERT(src->bindpath == NULL);
}

/*
 * Collect nested alternates.
 *
 * (.a|.b)|.c = (.a|.b|.c)
 *
 * Note that before we get here all bound vars on the alternates
 * themselves should have been removed.
 */
static void ptpath_alternates_collect(struct norm *ctx,
				      struct ptpath *dest, struct ptpath *src){
   unsigned num, i;
   struct ptpath *sub;

   PQLASSERT(dest->type == PTP_ALTERNATES);
   PQLASSERT(src->type == PTP_ALTERNATES);

   num = ptpatharray_num(&src->alternates.items);
   for (i=0; i<num; i++) {
      sub = ptpatharray_get(&src->alternates.items, i);
      if (sub->type == PTP_ALTERNATES) {
	 ptpath_alternates_collect(ctx, dest, sub);
	 //ptpath_destroy(sub); -- done by region allocator
      }
      else {
	 ptpatharray_add(ctx->pql, &dest->alternates.items, sub, NULL);
      }
   }
   ptpatharray_setsize(ctx->pql, &src->alternates.items, 0);
   PQLASSERT(src->bindobjbefore == NULL);
   PQLASSERT(src->bindobjafter == NULL);
   PQLASSERT(src->bindpath == NULL);
}

/*
 * Combine nested repetition.
 *
 * Because repeat is greedy, all repeats inside repeats collapse, as
 * do optionals inside optionals. Optional commutes with repeat.
 *
 * The following regexp-level rules apply:
 *
 *    (P?)?  =>  P?
 *    (P*)?  =>  P*
 *    (P+)?  =>  P*
 *
 *    (P?)*  =>  P*
 *    (P*)*  =>  P*
 *    (P+)*  =>  P*
 *
 *    (P?)+  =>  P*
 *    (P*)+  =>  P*
 *    (P+)+  =>  P+
 *
 * These translate directly into code-level rules as follows:
 *
 *    optional->optional                      =>  optional
 *    optional->repeated->optional            =>  optional->repeated
 *    repeated->optional                      =>  optional->repeated
 *
 *    optional->optional->repeated            =>  optional->repeated
 *    optional->repeated->optional->repeated  =>  optional->repeated
 *    repeated->optional->repeated            =>  optional->repeated
 *
 *    optional->repeated                      =>  optional->repeated
 *    optional->repeated->repeated            =>  optional->repeated
 *    repeated->repeated                      =>  repeated
 *
 * which can be simplified to
 *
 *    repeated->optional  =>  optional->repeated
 *    optional->optional  =>  optional
 *    repeated->repeated  =>  repeated
 *
 * The canonical output form is optional->repeated, not repeated->optional.
 */
static struct ptpath *ptpath_repetition_combine(struct ptpath *pp) {
   PQLASSERT(pp->type == PTP_OPTIONAL || pp->type == PTP_REPEATED);

   while (1) {
      if (pp->type == PTP_REPEATED && pp->repeated.sub->type == PTP_OPTIONAL) {
	 struct ptpath *qq, *sub;

	 /* Exchange the two. Exchange the contents; keep the bindings. */
	 qq = pp->repeated.sub;
	 sub = qq->optional.sub;

	 PQLASSERT(ptcolumnvararray_num(&qq->optional.nilcolumns) == 0);

	 qq->type = PTP_REPEATED;
	 qq->repeated.sub = sub;
	 qq->repeated.pathfrominside = pp->repeated.pathfrominside;
	 qq->repeated.pathonoutside = pp->repeated.pathonoutside;

	 pp->type = PTP_OPTIONAL;
	 pp->optional.sub = qq;

	 /* see if the stuff below simplifies more */
	 pp->optional.sub = ptpath_repetition_combine(pp->optional.sub);

	 continue;
      }

      if (pp->type == PTP_OPTIONAL && pp->optional.sub->type == PTP_OPTIONAL) {
	 PQLASSERT(pp->bindobjafter == NULL);
	 PQLASSERT(pp->bindpath == NULL);

	 PQLASSERT(ptcolumnvararray_num(&pp->optional.nilcolumns) == 0);

	 pp->optional.sub->parens = pp->parens || pp->optional.sub->parens;
	 pp->optional.sub->dontmerge = pp->dontmerge ||
	    pp->optional.sub->dontmerge;

	 //del = pp;
	 pp = pp->optional.sub;
	 //ptpath_destroy(del); -- handled by region allocator

	 continue;
      }

      if (pp->type == PTP_REPEATED && pp->repeated.sub->type == PTP_REPEATED) {
	 PQLASSERT(pp->repeated.pathfrominside == NULL);
	 PQLASSERT(pp->repeated.pathonoutside == NULL);
	 PQLASSERT(pp->bindobjafter == NULL);
	 PQLASSERT(pp->bindpath == NULL);

	 pp->repeated.sub->parens = pp->parens || pp->repeated.sub->parens;
	 pp->repeated.sub->dontmerge = pp->dontmerge ||
	    pp->repeated.sub->dontmerge;

	 //del = pp;
	 pp = pp->repeated.sub;
	 //ptpath_destroy(del); -- handled by region allocator

	 continue;
      }

      /* nothing matched; stop */
      break;
   }
   return pp;
}

////////////////////////////////////////////////////////////

/*
 * If a compound path element binds a path variable, we need a path
 * variable on every subpath, and expressions to paste them together.
 *
 * This function inserts the path variables needed, builds the
 * expressions (dropping them into the context) and returns a pointer
 * to the columnvar that describes the subpath. If the subpath is
 * itself a compound element, pp->bindpath will be left NULL.
 *
 * Returns a (counted) reference.
 */
static struct ptcolumnvar *ptpath_compose(struct norm *ctx, struct ptpath *pp){
   unsigned i, num;
   struct ptpath *sub;
   struct ptcolumnvar *myvar, *subvar;
   struct ptexpr *myexpr, *subexpr;

   /* Shortcut any trivial nodes that might appear */
   if (pp->type == PTP_SEQUENCE &&
       ptpatharray_num(&pp->sequence.items) == 1) {
      return ptpath_compose(ctx, ptpatharray_get(&pp->sequence.items, 0));
   }
   if (pp->type == PTP_ALTERNATES &&
       ptpatharray_num(&pp->alternates.items) == 1) {
      return ptpath_compose(ctx, ptpatharray_get(&pp->alternates.items, 0));
   }

   if (pp->bindpath != NULL) {
      myvar = pp->bindpath;
      pp->bindpath = NULL;
   }
   else {
      myvar = mkptcolumnvar_fresh(ctx->pql);
   }

   switch (pp->type) {
    case PTP_SEQUENCE:
     myexpr = NULL;
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->sequence.items, i);
	subvar = ptpath_compose(ctx, sub);
	subexpr = mkptexpr_readcolumnvar(ctx->pql, subvar);
	if (myexpr == NULL) {
	   myexpr = subexpr;
	}
	else {
	   myexpr = mkptexpr_bop(ctx->pql, myexpr, F_CONCAT, subexpr);
	}
     }
     norm_mkletbind(ctx, myvar, myexpr);
     break;

    case PTP_ALTERNATES:
     myexpr = NULL;
     num = ptpatharray_num(&pp->alternates.items);
     for (i=0; i<num; i++) {
	sub = ptpatharray_get(&pp->alternates.items, i);
	subvar = ptpath_compose(ctx, sub);
	subexpr = mkptexpr_readcolumnvar(ctx->pql, subvar);
	if (myexpr == NULL) {
	   myexpr = subexpr;
	}
	else {
	   myexpr = mkptexpr_bop(ctx->pql, myexpr, F_CHOOSE, subexpr);
	}
     }
     norm_mkletbind(ctx, myvar, myexpr);
     break;

    case PTP_OPTIONAL:
     subvar = ptpath_compose(ctx, pp->optional.sub);
     myexpr = mkptexpr_readcolumnvar(ctx->pql, subvar);
     norm_mkletbind(ctx, myvar, myexpr);
     break;

    case PTP_REPEATED:
     subvar = ptpath_compose(ctx, pp->repeated.sub);
     pp->repeated.pathfrominside = subvar;
     pp->repeated.pathonoutside = myvar;
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     /* don't need to touch the computed edge expression (if any) */

     /* just put myvar back in bindpath */
     pp->bindpath = myvar;
     break;
   }

   ptcolumnvar_incref(myvar);
   return myvar;
}

static void ptpath_trycompose(struct norm *ctx, struct ptpath *pp) {
   struct ptcolumnvar *pathvar;

   if (pp->bindpath != NULL) {
      pathvar = ptpath_compose(ctx, pp);
      PQLASSERT(pp->bindpath == NULL);
      //ptcolumnvar_decref(pathvar); -- done by region allocator
   }
}

////////////////////////////////////////////////////////////

static struct ptpath *ptpath_norm(struct norm *ctx, bool dontmerge,
				  struct ptpath *pp);
static struct ptexpr *ptexpr_norm(struct norm *ctx, struct ptexpr *pp);

static void ptexprarray_norm(struct norm *ctx, struct ptexprarray *arr) {
   unsigned i, num;
   struct ptexpr *pe;

   num = ptexprarray_num(arr);
   for (i=0; i<num; i++) {
      pe = ptexprarray_get(arr, i);
      pe = ptexpr_norm(ctx, pe);
      ptexprarray_set(arr, i, pe);
   }
}

/*
 * Common prefix code for path normalize.
 */
static void ptpath_common_norm(bool *dontmerge, struct ptpath *pp) {
   /* don't know how to deal with this */
   PQLASSERT(pp->bindobjbefore == NULL);

   *dontmerge = pp->dontmerge = *dontmerge || pp->dontmerge;
}

/*
 * Normalize a path that's a sequence.
 *
 * XXX there's too much cloned code between here and the alternates form.
 */
static struct ptpath *ptpath_sequence_norm(struct norm *ctx,
					   bool dontmerge, bool docombine,
					   struct ptpath *pp) {
   unsigned i, num;
   struct ptpath *sub;
   bool needcombine = false;

   ptpath_common_norm(&dontmerge, pp);
   PQLASSERT(pp->type == PTP_SEQUENCE);

   num = ptpatharray_num(&pp->sequence.items);

   /*
    * 1. Sequences shouldn't bind objects. The binding should be
    * moved to the last subpath.
    */
   if (pp->bindobjafter != NULL) {
      PQLASSERT(num > 0);
      sub = ptpatharray_get(&pp->sequence.items, num-1);
      if (sub->bindobjafter == NULL) {
	 /* move the var */
	 sub->bindobjafter = pp->bindobjafter;
      }
      else {
	 /* define it as the other var instead */
	 ptcolumnvar_incref(sub->bindobjafter);
	 norm_mkletbindvar(ctx, pp->bindobjafter, sub->bindobjafter);
      }
      pp->bindobjafter = NULL;
   }

   /*
    * 2. Sequences shouldn't be of length 1. Drop and return the subpath.
    */
   if (num == 1) {
      sub = ptpatharray_get(&pp->sequence.items, 0);
      ptpatharray_setsize(ctx->pql, &pp->sequence.items, 0);

      /* bindobjafter must already be null from the above */
      PQLASSERT(pp->bindobjafter == NULL);

      if (pp->bindpath != NULL) {
	 if (sub->bindpath == NULL) {
	    /* move the var */
	    sub->bindpath = pp->bindpath;
	 }
	 else {
	    /* define it as the other var instead */
	    ptcolumnvar_incref(sub->bindpath);
	    norm_mkletbindvar(ctx, pp->bindpath, sub->bindpath);
	 }
	 pp->bindpath = NULL;
      }
      
      if (pp->parens) {
	 sub->parens = true;
      }

      //ptpath_destroy(pp); -- done by region allocator
      return ptpath_norm(ctx, dontmerge, sub);
   }

   /*
    * 3. If we bind a path, we need to call pathcompose, which will
    * recursively set up path variables in the subpaths instead. The
    * variable that comes back is already let-bound so we don't need
    * it here.
    */
   ptpath_trycompose(ctx, pp);

   /*
    * 4. Recurse. If the subpath is a sequence, shortcut back into
    * this function and suppress combining, so we can do it all at
    * once. Otherwise, go to the main code.
    */
   for (i=0; i<num; i++) {
      sub = ptpatharray_get(&pp->sequence.items, i);
      if (sub->type == PTP_SEQUENCE) {
	 sub = ptpath_sequence_norm(ctx, dontmerge, false/*docombine*/, sub);
	 needcombine = true;
      }
      else {
	 sub = ptpath_norm(ctx, dontmerge, sub);
	 /*
	  * It might *become* a sequence if a bunch of crap gets swept
	  * away... don't need to recurse again, but do need to make
	  * sure we do a combine phase.
	  */
	 if (sub->type == PTP_SEQUENCE) {
	    needcombine = true;
	 }
      }
      ptpatharray_set(&pp->sequence.items, i, sub);
   }

   /*
    * 5. If we had subsequences, combine them into one sequence node.
    */
   if (docombine && needcombine) {
      struct ptpath *np;

      np = mkptpath_emptysequence(ctx->pql);
      ptpath_sequence_collect(ctx, np, pp);
      if (pp->parens) {
	 np->parens = true;
      }
      //ptpath_destroy(pp); -- done by region allocator
      pp = np;
   }
   return pp;
}

/*
 * Normalize a path that's a set of alternates.
 */
static struct ptpath *ptpath_alternates_norm(struct norm *ctx,
					     bool dontmerge, bool docombine,
					     struct ptpath *pp) {
   unsigned i, num;
   struct ptpath *sub;
   struct ptexpr *myexpr, *subexpr;
   bool needcombine = false;

   ptpath_common_norm(&dontmerge, pp);
   PQLASSERT(pp->type == PTP_ALTERNATES);

   num = ptpatharray_num(&pp->alternates.items);

   /*
    * 1. Alternates shouldn't bind objects. Each subpath should bind an
    * object and the collective result should be produced with choose().
    *    
    * A reference to the variable holding the result is placed in
    * ->tailvar for future reference.
    *
    * If we aren't trying to bind an object, create one. Otherwise the
    * tuplify code will not be able to move on to the next element
    * following the alternates.
    */
   if (pp->bindobjafter == NULL) {
      pp->bindobjafter = mkptcolumnvar_fresh(ctx->pql);
   }
   if (1 /*pp->bindobjafter != NULL*/) {
      myexpr = NULL;
      for (i=0; i<num; i++) {
	 sub = ptpatharray_get(&pp->alternates.items, i);
	 if (sub->bindobjafter == NULL) {
	    sub->bindobjafter = mkptcolumnvar_fresh(ctx->pql);
	 }
	 ptcolumnvar_incref(sub->bindobjafter);
	 subexpr = mkptexpr_readcolumnvar(ctx->pql, sub->bindobjafter);
	 if (myexpr == NULL) {
	    myexpr = subexpr;
	 }
	 else {
	    myexpr = mkptexpr_bop(ctx->pql, myexpr, F_CHOOSE, subexpr);
	 }
      }
      ptcolumnvar_incref(pp->bindobjafter);
      norm_mkletbind(ctx, pp->bindobjafter, myexpr);
      pp->alternates.tailvar = pp->bindobjafter;
      pp->bindobjafter = NULL;
   }

   /*
    * 2. Alternates shouldn't be of length 1. Drop and return the subpath.
    */
   if (num == 1) {
      sub = ptpatharray_get(&pp->alternates.items, 0);
      ptpatharray_setsize(ctx->pql, &pp->alternates.items, 0);

      /* bindobjafter must already be null from the above */
      PQLASSERT(pp->bindobjafter == NULL);

      if (pp->bindpath != NULL) {
	 if (sub->bindpath == NULL) {
	    /* move the var */
	    sub->bindpath = pp->bindpath;
	 }
	 else {
	    /* define it as the other var instead */
	    ptcolumnvar_incref(sub->bindpath);
	    norm_mkletbindvar(ctx, pp->bindpath, sub->bindpath);
	 }
	 pp->bindpath = NULL;
      }

      if (pp->parens) {
	 sub->parens = true;
      }

      //ptpath_destroy(pp); -- done by region allocator
      return ptpath_norm(ctx, dontmerge, sub);
   }

   /*
    * 3. If we bind a path, we need to call pathcompose, which will
    * recursively set up path variables in the subpaths instead. The
    * variable that comes back is already let-bound so we don't need
    * it here.
    */
   ptpath_trycompose(ctx, pp);

   /*
    * 4. Recurse. If the subpath is a set of alternates, shortcut back
    * into this function and suppress combining, so we can do it all
    * at once. Otherwise, go to the main code.
    */
   for (i=0; i<num; i++) {
      sub = ptpatharray_get(&pp->alternates.items, i);
      if (sub->type == PTP_ALTERNATES) {
	 sub = ptpath_alternates_norm(ctx, dontmerge, false/*docombine*/, sub);
	 needcombine = true;
      }
      else {
	 sub = ptpath_norm(ctx, dontmerge, sub);
	 /*
	  * It might *become* alternates if a bunch of crap gets swept
	  * away... don't need to recurse again, but do need to make
	  * sure we do a combine phase.
	  */
	 if (sub->type == PTP_ALTERNATES) {
	    needcombine = true;
	 }
      }
      ptpatharray_set(&pp->alternates.items, i, sub);
   }

   /*
    * 5. If we had subsequences, combine them into one sequence node.
    */
   if (docombine && needcombine) {
      struct ptpath *np;

      np = mkptpath_emptyalternates(ctx->pql);
      ptpath_alternates_collect(ctx, np, pp);
      if (pp->parens) {
	 np->parens = true;
      }
      np->alternates.tailvar = pp->alternates.tailvar;
      pp->alternates.tailvar = NULL;
      //ptpath_destroy(pp); -- done by region allocator
      pp = np;
   }
   return pp;
}

/*
 * Normalize a path.
 *
 * This does:
 *    - flatten sequences of sequences
 *    - flatten alternates of alternates
 *    - combine repetition of repetition
 *    - trigger pathcompose where necessary
 *    - ensure sequences and alternates don't (directly) bind objects
 *    - propagate any dontmerge on nonprimitive elements downwards
 *
 */
static struct ptpath *ptpath_norm(struct norm *ctx,
				  bool dontmerge,
				  struct ptpath *pp) {
   /*
    * Nothing should be placed here (before the switch) because the
    * sequence and alternates code skips it when handling a subpath of
    * the same type. Anything you want to put here should go in
    * ptpath_common_norm instead.
    */
   switch (pp->type) {
    case PTP_SEQUENCE:
     pp = ptpath_sequence_norm(ctx, dontmerge, true/*docombine*/, pp);
     break;

    case PTP_ALTERNATES:
     pp = ptpath_alternates_norm(ctx, dontmerge, true/*docombine*/, pp);
     break;

    case PTP_OPTIONAL:
     ptpath_common_norm(&dontmerge, pp);
     ptpath_trycompose(ctx, pp);
     pp->optional.sub = ptpath_norm(ctx, dontmerge, pp->optional.sub);
     if (pp->optional.sub->type == PTP_OPTIONAL ||
	 pp->optional.sub->type == PTP_REPEATED) {
	pp = ptpath_repetition_combine(pp);
     }
     break;

    case PTP_REPEATED:
     ptpath_common_norm(&dontmerge, pp);
     ptpath_trycompose(ctx, pp);
     pp->repeated.sub = ptpath_norm(ctx, dontmerge, pp->repeated.sub);
     if (pp->repeated.sub->type == PTP_OPTIONAL ||
	 pp->repeated.sub->type == PTP_REPEATED) {
	pp = ptpath_repetition_combine(pp);
     }
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     ptpath_common_norm(&dontmerge, pp);
     if (pp->edge.iscomputed) {
	pp->edge.computedname = ptexpr_norm(ctx, pp->edge.computedname);
     }
     break;
   }

   return pp;
}

/*
 * Normalize an expression.
 *
 * This does:
 *    - simplify vacuous selects (that is, select X from nothing where true)
 *    - simplify tuples of arity 1
 *
 * It is not meant to be an optimization pass, so we don't try that
 * hard, but we do fish a little.
 */
static struct ptexpr *ptexpr_norm(struct norm *ctx, struct ptexpr *pe) {
   bool infromsave;

   switch (pe->type) {
    case PTE_SELECT:
     pe->select.sub = ptexpr_norm(ctx, pe->select.sub);
     pe->select.result = ptexpr_norm(ctx, pe->select.result);

#if 0 /* it is WRONG! */
     /*
      * If no from or where clauses, just use the subexpression.
      * (Is this what we want though?)
      */
     if (pe->select.sub == NULL) {
	struct ptexpr *sub;

	sub = pe->select.result;
	pe->select.result = NULL;
	//ptexpr_destroy(pe); -- done by region allocator
	pe = sub;
     }
#endif
     break;

    case PTE_FROM:
     infromsave = ctx->infrom;
     ctx->infrom = true;
     ptexprarray_norm(ctx, pe->from);
     norm_getexprs_back(pe->from, ctx);
     ctx->infrom = infromsave;
     break;

    case PTE_WHERE:
     pe->where.sub = ptexpr_norm(ctx, pe->where.sub);
     pe->where.where = ptexpr_norm(ctx, pe->where.where);
     /*
      * "where true" = no where clause
      */
     if (pe->where.where->type == PTE_VALUE &&
	 pqlvalue_isbool(pe->where.where->value) &&
	 pqlvalue_bool_get(pe->where.where->value) == true) {
	//ptexpr_destroy(pe->where.where); -- done by region allocator
	pe->where.where = NULL;
	//ptexpr_destroy(pe); -- done by region allocator
	pe = pe->where.sub;
     }
     break;

    case PTE_GROUP:
     pe->group.sub = ptexpr_norm(ctx, pe->group.sub);
     if (pe->group.newvar == NULL) {
	pe->group.newvar = mkptcolumnvar_fresh(ctx->pql);
     }
     break;

    case PTE_UNGROUP:
     pe->ungroup.sub = ptexpr_norm(ctx, pe->ungroup.sub);
     break;

    case PTE_RENAME:
     pe->rename.sub = ptexpr_norm(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	pe->rename.computedname = ptexpr_norm(ctx, pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     pe->path.root = ptexpr_norm(ctx, pe->path.root);
     pe->path.body = ptpath_norm(ctx, false/*dontmerge*/, pe->path.body);
     if (!ctx->infrom) {
	norm_getexprs_forward(&pe->path.morebindings, ctx);
     }
     break;

    case PTE_TUPLE:
     ptexprarray_norm(ctx, pe->tuple);
     /*
      * Prune tuples of arity 1.
      */
     if (ptexprarray_num(pe->tuple) == 1) {
	struct ptexpr *sub;

	sub = ptexprarray_get(pe->tuple, 0);
	ptexprarray_setsize(ctx->pql, pe->tuple, 0);
	//ptexpr_destroy(pe); -- nope! handled by region allocator
	pe = sub;
     }
     break;

    case PTE_FORALL:
     pe->forall.set = ptexpr_norm(ctx, pe->forall.set);
     pe->forall.predicate = ptexpr_norm(ctx, pe->forall.predicate);
     break;

    case PTE_EXISTS:
     pe->exists.set = ptexpr_norm(ctx, pe->exists.set);
     pe->exists.predicate = ptexpr_norm(ctx, pe->exists.predicate);
     break;

    case PTE_MAP:
     pe->map.set = ptexpr_norm(ctx, pe->map.set);
     pe->map.result = ptexpr_norm(ctx, pe->map.result);
     break;

    case PTE_ASSIGN:
     pe->assign.value = ptexpr_norm(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	pe->assign.body = ptexpr_norm(ctx, pe->assign.body);
     }
     break;

    case PTE_BOP:
     pe->bop.l = ptexpr_norm(ctx, pe->bop.l);
     pe->bop.r = ptexpr_norm(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     pe->uop.sub = ptexpr_norm(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	ptexprarray_norm(ctx, pe->func.args);
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

struct ptexpr *normalize(struct pqlcontext *pql, struct ptexpr *pe) {
   struct norm ctx;

   norm_init(&ctx, pql);
   pe = ptexpr_norm(&ctx, pe);
   norm_cleanup(&ctx);

   return pe;
}
