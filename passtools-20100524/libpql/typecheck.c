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
 * Check the types (and column names) for tuple calculus.
 */

#include <stdarg.h>
#include <stdio.h>

#include "datatype.h"
#include "columns.h"
#include "pqlvalue.h"
#include "tcalc.h"
#include "passes.h"
#include "pqlcontext.h"

struct tc {
   struct pqlcontext *pql;
   bool failed;
};

////////////////////////////////////////////////////////////
// context management

static void tc_init(struct tc *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   ctx->failed = false;
}

static void tc_cleanup(struct tc *ctx) {
   (void)ctx;
}

static void tc_fail(struct tc *ctx) {
   ctx->failed = true;
}

static void tc_say(struct tc *ctx, const char *fmt, ...) PF(2,3);

static void tc_say(struct tc *ctx, const char *fmt, ...) {
   char buf[4096];
   va_list ap;
   
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   // XXX should get line numbers
   // XXX should add/use vcomplain
   complain(ctx->pql, 0, 0, "%s", buf);
}

////////////////////////////////////////////////////////////
// tools

////////////////////////////////////////////////////////////
// common type checks

/*
 * Check a lambda expression and return its type:
 *
 *    LAMBDATYPE :: ARGTYPE -> RESULTTYPE
 *
 * Skip the check on any of the arguments that are null. On failure,
 * make up something for the return value if necessary so it can be
 * used for subsequent checks.
 */
static struct datatype *tc_check_lambda(struct tc *ctx,
					struct datatype *lambdatype,
					struct datatype *argtype,
					struct datatype *resulttype) {
   if (!datatype_islambda(lambdatype)) {
      tc_say(ctx, "Expected lambda expression; found %s",
	     datatype_getname(lambdatype));
      tc_fail(ctx);
      goto makeitup;
   }

   if (argtype != NULL &&
       !datatype_eq(datatype_lambda_argument(lambdatype), argtype)) {
      tc_say(ctx, "Expected lambda accepting %s; found %s",
	     datatype_getname(argtype),
	     datatype_getname(datatype_lambda_argument(lambdatype)));
      tc_fail(ctx);
      goto makeitup;
   }

   if (resulttype != NULL &&
       !datatype_eq(datatype_lambda_result(lambdatype), resulttype)) {
      tc_say(ctx, "Expected lambda producing %s; found %s",
	     datatype_getname(resulttype),
	     datatype_getname(datatype_lambda_result(lambdatype)));
      tc_fail(ctx);
      goto makeitup;
   }

   return lambdatype;

 makeitup:
   /*
    * The Sybil Trelawney school of type-checking.
    */
   if (argtype == NULL) {
      argtype = datatype_absbottom(ctx->pql);
   }
   if (resulttype == NULL) {
      resulttype = datatype_absbottom(ctx->pql);
   }
   return datatype_lambda(ctx->pql, argtype, resulttype);
}

/*
 * Check a filtering expression.
 *
 * Given "result = sub where filter", check RESULTTYPE, SUBTYPE, and
 * FILTERTYPE as follows:
 *
 *   1. RESULTTYPE == SUBTYPE
 *   2. exists T. SUBTYPE :: set(T)
 *   3. FILTERTYPE :: T -> bool
 */
static void tc_check_filter(struct tc *ctx,
			    struct datatype *resulttype,
			    struct datatype *subtype,
			    struct datatype *filtertype) {
   struct datatype *t;

   if (!datatype_eq(resulttype, subtype)) {
      tc_say(ctx, "Filter operation changes type of its operand set");
      tc_fail(ctx);
      /* don't give up (yet) */
   }
   if (!datatype_isset(subtype)) {
      tc_say(ctx, "Filter operation applied to non-set");
      tc_fail(ctx);
      /* Give up, because we need T for the next test */
      return;
   }

   t = datatype_set_member(subtype);
   tc_check_lambda(ctx, filtertype, t, datatype_bool(ctx->pql));
}

////////////////////////////////////////////////////////////
// column checks

/*
 * Check that SUBSET is a subset of SET.
 */
static void tc_check_columnlist_subset(struct tc *ctx,
				       const char *what,
				       struct colset *subset,
				       struct coltree *set) {
   struct colname *tccol;
   unsigned i, num;

   num = colset_num(subset);
   for (i=0; i<num; i++) {
      tccol = colset_get(subset, i);
      if (coltree_find(set, tccol, NULL) < 0) {
	 tc_say(ctx, "%s tried to use nonexistent column %s", what,
		colname_getname(ctx->pql, tccol));
	 tc_fail(ctx);
      }
   }
}

/*
 * Check that BOTH = LEFT x { RIGHT }.
 */
static void tc_check_columnlist_appended(struct tc *ctx, 
					 const char *what,
					 struct coltree *both,
					 struct colset *left,
					 struct colname *right) {
   unsigned i, numboth, j, numleft;
   struct coltree *bname;
   struct colname *thisleft;

   numboth = coltree_num(both);
   numleft = colset_num(left);

   if (numboth != numleft + 1) {
      tc_say(ctx, "%s columns do not match up (computed %u columns, found %u)",
	     what, numleft + 1, numboth);
      tc_fail(ctx);
   }
   else {
      i = 0;
      for (j=0; j<numleft; j++, i++) {
	 bname = coltree_getsubtree(both, i);
	 thisleft = colset_get(left, j);
	 if (!coltree_eq_col(bname, thisleft)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(at index %u, computed %s, found %s)", what, j,
		   colname_getname(ctx->pql, thisleft),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
      }
      bname = coltree_getsubtree(both, numleft);
      if (!coltree_eq_col(bname, right)) {
	 tc_say(ctx, "%s columns do not match up "
		"(at index %u, computed %s, found %s)", what, numleft,
		colname_getname(ctx->pql, right),
		coltree_getname(ctx->pql, bname));
	 tc_fail(ctx);
      }
   }
}

/*
 * Same but LEFT is a coltree.
 */
static void tc_check_columnlist_coltree_appended(struct tc *ctx, 
					 const char *what,
					 struct coltree *both,
					 struct coltree *left,
					 struct colname *right) {
   unsigned i, numboth, numleft;
   struct coltree *bname, *lname;

   numboth = coltree_arity(both);
   numleft = coltree_arity(left);

   if (numboth != numleft + 1) {
      tc_say(ctx, "%s columns do not match up (computed %u columns, found %u)",
	     what, numleft + 1, numboth);
      tc_fail(ctx);
      return;
   }

   PQLASSERT(coltree_istuple(both));

   if (coltree_istuple(left)) {
      for (i=0; i<numleft; i++) {
	 bname = coltree_getsubtree(both, i);
	 lname = coltree_getsubtree(left, i);
	 if (!coltree_eq(bname, lname)) {
	    tc_say(ctx, "%s column %u does not match (computed %s, found %s)",
		   what, i, coltree_getname(ctx->pql, lname),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
      }
   }
   else {
      PQLASSERT(numleft == 1);
      PQLASSERT(numboth == 2);
      bname = coltree_getsubtree(both, 0);
      if (!coltree_eq(bname, left)) {
	 tc_say(ctx, "%s column 0 does not match (computed %s, found %s)",
		what, coltree_getname(ctx->pql, left),
		coltree_getname(ctx->pql, bname));
	 tc_fail(ctx);
      }
   }
   bname = coltree_getsubtree(both, numleft);
   if (!coltree_eq_col(bname, right)) {
      tc_say(ctx, "%s column %u does not match up (computed %s, found %s)",
	     what, numleft, colname_getname(ctx->pql, right),
	     coltree_getname(ctx->pql, bname));
      tc_fail(ctx);
   }
}

/*
 * Check that BOTH = LEFT x RIGHT.
 */
static void tc_check_columnlist_pasted(struct tc *ctx, 
				       const char *what,
				       struct coltree *both,
				       struct coltree *left,
				       struct coltree *right) {
   unsigned i, numboth, j, numleft, numright, extraleft, extraright;
   struct coltree *bname, *lname, *rname;

   numboth = coltree_num(both);
   numleft = coltree_num(left);
   numright = coltree_num(right);
   extraleft = (!coltree_istuple(left) && coltree_wholecolumn(left) != NULL);
   extraright = (!coltree_istuple(right) && coltree_wholecolumn(right) != NULL);

   if (numboth != numleft + extraleft + numright + extraright) {
      tc_say(ctx, "%s columns do not match up (computed %u columns, found %u)",
	     what, numleft + extraleft + numright + extraright, numboth);
      tc_fail(ctx);
   }
   else {
      i = 0;
      for (j=0; j<numleft; j++, i++) {
	 bname = coltree_getsubtree(both, i);
	 lname = coltree_getsubtree(left, j);
	 if (!coltree_eq(bname, lname)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, from %u on left; computed %s, found %s)",
		   what, i, j, coltree_getname(ctx->pql, lname),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
      }
      if (extraleft) {
	 bname = coltree_getsubtree(both, i);
	 if (!coltree_eq_col(bname, coltree_wholecolumn(left))) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, from 0 on left; computed %s, found %s)",
		   what, i, coltree_getname(ctx->pql, left),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
	 i++;
      }
      for (j=0; j<numright; j++, i++) {
	 bname = coltree_getsubtree(both, i);
	 rname = coltree_getsubtree(right, j);
	 if (!coltree_eq(bname, rname)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, from %u on right; computed %s, found %s)",
		   what, i, j, coltree_getname(ctx->pql, rname),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
      }
      if (extraright) {
	 bname = coltree_getsubtree(both, i);
	 if (!coltree_eq_col(bname, coltree_wholecolumn(right))) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, from 0 on right; computed %s, found %s)",
		   what, i, coltree_getname(ctx->pql, right),
		   coltree_getname(ctx->pql, bname));
	    tc_fail(ctx);
	 }
	 i++;
      }
      PQLASSERT(i == numboth);
   }
}

/*
 * Special mondo version of the previous for repeat expressions.
 *
 * FOUND is supposed to be BASECOLS x seq[MORECOLS] x EXTRACOL1 x EXTRACOL2
 * skipping EXTRACOL1/2 if null.
 */
static void tc_check_columnlist_multipasted(struct tc *ctx, 
					    const char *what,
					    struct coltree *found,
					    struct coltree *basecols,
					    struct colset *morecols,
					    struct colname *extracol1,
					    struct colname *extracol2) {
   unsigned i, numfound;
   unsigned j, numbase, nummore, extra1, extra2, numcomputed;
   struct coltree *foundname, *computedname, *seqname;
   struct colname *computedcol;

   numfound = coltree_arity(found);
   numbase = coltree_arity(basecols);
   nummore = colset_num(morecols);
   extra1 = (extracol1 != NULL);
   extra2 = (extracol2 != NULL);
   numcomputed = numbase + (nummore > 0 ? 1 : 0) + extra1 + extra2;

   if (numfound != numcomputed) {
      tc_say(ctx, "%s columns do not match up (computed %u columns, found %u)",
	     what, numcomputed, numfound);
      tc_fail(ctx);
   }
   else {
      i = 0;

      /* base */
      if (coltree_istuple(basecols)) {
	 for (j=0; j<numbase; j++, i++) {
	    foundname = coltree_getsubtree(found, i);
	    computedname = coltree_getsubtree(basecols, j);
	    if (!coltree_eq(foundname, computedname)) {
	       tc_say(ctx, "%s columns do not match up "
		      "(index %u, from %u in input; computed %s, found %s)",
		      what, i, j, coltree_getname(ctx->pql, computedname),
		      coltree_getname(ctx->pql, foundname));
	       tc_fail(ctx);
	    }
	 }
      }
      else {
	 foundname = coltree_getsubtree(found, i);
	 computedname = basecols;
	 if (!coltree_eq(foundname, computedname)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, from 0 in input; computed %s, found %s)",
		   what, i, coltree_getname(ctx->pql, computedname),
		   coltree_getname(ctx->pql, foundname));
	    tc_fail(ctx);
	 }
	 i++;
      }

      /* more (which are supposed to be nested) */
      if (colset_num(morecols) > 0) {
	 seqname = coltree_getsubtree(found, i);
	 if (coltree_arity(seqname) != nummore) {
	    tc_say(ctx, "%s sequence-output has the wrong number of columns "
		   "(computed %u, found %u)", what, nummore,
		   coltree_num(seqname));
	    tc_fail(ctx);
	 }
	 else {
	    if (coltree_istuple(seqname)) {
	       if (coltree_wholecolumn(seqname) != NULL) {
		  tc_say(ctx, "%s sequence-output has an unexpected name %s",
			 what, colname_getname(ctx->pql,
					       coltree_wholecolumn(seqname)));
		  tc_fail(ctx);
	       }
	       for (j=0; j<nummore; j++) {
		  foundname = coltree_getsubtree(seqname, j);
		  computedcol = colset_get(morecols, j);
		  if (!coltree_eq_col(foundname, computedcol)) {
		     tc_say(ctx, "%s sequence columns do not match up "
			    "(index %u; computed %s, found %s)",
			    what, j, colname_getname(ctx->pql, computedcol),
			    coltree_getname(ctx->pql, foundname));
		     tc_fail(ctx);
		  }
	       }
	    }
	    else {
	       PQLASSERT(nummore == 1);
	       foundname = seqname;
	       computedcol = colset_get(morecols, 0);
	       if (!coltree_eq_col(foundname, computedcol)) {
		  tc_say(ctx, "%s sequence-output columns do not match up "
			 "(index 0; computed %s, found %s)",
			 what, colname_getname(ctx->pql, computedcol),
			 coltree_getname(ctx->pql, foundname));
		  tc_fail(ctx);
	       }
	    }
	 }
	 i++;
      }

      /* extras */
      if (extra1) {
	 foundname = coltree_getsubtree(found, i);
	 computedcol = extracol1;
	 if (!coltree_eq_col(foundname, computedcol)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, output of body; computed %s, found %s)",
		   what, i, colname_getname(ctx->pql, computedcol),
		   coltree_getname(ctx->pql, foundname));
	    tc_fail(ctx);
	 }
	 i++;
      }
      if (extra2) {
	 foundname = coltree_getsubtree(found, i);
	 computedcol = extracol2;
	 if (!coltree_eq_col(foundname, computedcol)) {
	    tc_say(ctx, "%s columns do not match up "
		   "(index %u, path output of body; computed %s, found %s)",
		   what, i, colname_getname(ctx->pql, computedcol),
		   coltree_getname(ctx->pql, foundname));
	    tc_fail(ctx);
	 }
	 i++;
      }
      PQLASSERT(i == numfound);
   }
}

/*
 * Check that CL1 and CL2 are the same.
 */
static void tc_check_columnlist_same(struct tc *ctx,
				     const char *name,
				     struct coltree *cl1,
				     struct colset *cl2) {
   unsigned i, num;
   struct colname *cn1, *cn2;

   if (coltree_istuple(cl1)) {
      num = coltree_num(cl1);
   }
   else {
      num = 1;
   }
   if (num != colset_num(cl2)) {
      tc_say(ctx, "Number of column names in %s is wrong "
	     "(computed %u, have %u)",
	     name, colset_num(cl2), num);
      tc_fail(ctx);
      /* give up */
      return;
   }
   for (i=0; i<num; i++) {
      if (coltree_istuple(cl1)) {
	 cn1 = coltree_get(cl1, i);
      }
      else {
	 cn1 = coltree_wholecolumn(cl1);
      }
      cn2 = colset_get(cl2, i);
      if (cn1 != cn2) {
	 tc_say(ctx, "Column names in %s do not match at index %u "
		"(computed %s, found %s)", name, i,
		colname_getname(ctx->pql, cn2),
		colname_getname(ctx->pql, cn1));
	 tc_fail(ctx);
      }
   }
}

static void tc_check_coltree_scalar(struct tc *ctx,
				   const char *subname,
				   const char *name,
				   struct coltree *cols) {
   if (cols == NULL) {
      tc_say(ctx, "%s %s has a null column name (non-tuple expected)",
	     subname, name);
      tc_fail(ctx);
      return;
   }
   if (coltree_istuple(cols)) {
      tc_say(ctx, "%s %s has wrong arity (computed 1, found %u)",
	     subname, name, coltree_arity(cols));
      tc_fail(ctx);
      return;
   }
}

static void tc_check_coltree_nothing(struct tc *ctx,
				     const char *subname,
				     const char *name,
				     struct coltree *cols) {
   if (cols == NULL) {
      return;
   }
   if (!coltree_istuple(cols) || coltree_arity(cols) != 0) {
      tc_say(ctx, "%s %s has wrong arity (computed 0, found %u)",
	     subname, name, coltree_arity(cols));
      tc_fail(ctx);
      return;
   }
}

static void tc_check_coltree_same(struct tc *ctx,
				  const char *name,
				  unsigned index,
				  struct coltree *n1,
				  struct coltree *n2,
				  bool skiptop) {
   unsigned num1, num2, i;

   if (n1 == NULL && n2 == NULL) {
      /* ok */
      return;
   }
   if (n1 == NULL || n2 == NULL) {
      tc_say(ctx, "Column names in %s do not match at index %u "
	     "(computed %s, found %s)", name, index,
	     coltree_getname(ctx->pql, n2), coltree_getname(ctx->pql, n1));
      tc_fail(ctx);
      return;
   }
   if (!skiptop && coltree_wholecolumn(n1) != coltree_wholecolumn(n2)) {
      tc_say(ctx, "Column names in %s do not match at index %u "
	     "(computed %s, found %s)", name, index,
	     coltree_getname(ctx->pql, n2), coltree_getname(ctx->pql, n1));
      tc_fail(ctx);
      return;
   }

   num1 = coltree_num(n1);
   num2 = coltree_num(n2);

   if (num1 != num2) {
      tc_say(ctx, "Column names in %s do not match "
	     "(computed %u columns, found %u)", name, num2, num1);
      tc_fail(ctx);
      return;
   }

   for (i=0; i<num1; i++) {
      tc_check_coltree_same(ctx, name, i,
			   coltree_getsubtree(n1, i),
			   coltree_getsubtree(n2, i),
			   false);
   }
}

/*
 * Given "from", of type FROMTYPE with columns FROMCOLS, check that
 * CHOICES are all found in FROMCOLS, and produce the following:
 *
 *   - CHOSENTYPE, the type of CHOICES
 *   - UNCHOSENTYPE, the type of UNCHOSENMEMBERS
 *   - UNCHOSENMEMBERS, the columns not in CHOICES
 *
 * CHOSENTYPE, UNCHOSENTYPE, and/or UNCHOSENMEMBERS can be null if
 * that data isn't required.
 *
 * WHAT describes the expression this is part of, for complaints.
 */
static void tc_check_columnchoices(struct tc *ctx,
				   const char *what,
				   struct datatype *fromtype,
				   struct coltree *fromcols,
				   struct colset *choices,
				   struct datatype **chosentype,
				   struct datatype **unchosentype,
				   struct colset *unchosenmembers) {
   struct colname *tccol;
   struct datatype *membertype, *t;
   unsigned numfrom, numchoices, i, j;
   bool isset = false, isseq = false;

   numfrom = coltree_arity(fromcols);
   numchoices = colset_num(choices);

   /* top can have any number of columns */
   PQLASSERT(datatype_isabstop(fromtype) ||
	     numfrom == datatype_nonset_arity(fromtype));

   if (datatype_isset(fromtype)) {
      membertype = datatype_set_member(fromtype);
      isset = true;
   }
   else if (datatype_issequence(fromtype)) {
      membertype = datatype_sequence_member(fromtype);
      isseq = true;
   }
   else {
      membertype = fromtype;
   }

   if (datatype_arity(membertype) == 1 &&
       !coltree_istuple(fromcols) &&
       colset_num(choices) == 1 &&
       colset_get(choices, 0) == coltree_wholecolumn(fromcols)) {
      if (chosentype != NULL) {
	 *chosentype = fromtype;
      }
      if (unchosentype) {
	 *unchosentype = datatype_unit(ctx->pql);
	 if (isset) {
	    *unchosentype = datatype_set(ctx->pql, *unchosentype);
	 } else if (isseq) {
	    *unchosentype = datatype_sequence(ctx->pql, *unchosentype);
	 }
      }
      /* leave unchosenmembers empty */
      return;
   }

   if (!datatype_istuple(membertype)) {
      tc_say(ctx, "%s applied to non-tuple", what);
      tc_fail(ctx);
      /* make it up */
      if (chosentype != NULL) {
	 *chosentype = fromtype;
      }
      if (unchosentype != NULL) {
	 *unchosentype = datatype_unit(ctx->pql);
	 if (isset) {
	    *unchosentype = datatype_set(ctx->pql, *unchosentype);
	 } else if (isseq) {
	    *unchosentype = datatype_sequence(ctx->pql, *unchosentype);
	 }
      }
      /* leave unchosenmembers empty */
      return;
   }

   if (chosentype != NULL) {
      struct datatypearray types;

      datatypearray_init(&types);
      datatypearray_setsize(ctx->pql, &types, numchoices);

      for (i=0; i<numchoices; i++) {
	 tccol = colset_get(choices, i);
	 if (coltree_find(fromcols, tccol, &j) < 0) {
	    tc_say(ctx, "%s tried to use nonexistent column %s", what,
		   colname_getname(ctx->pql, tccol));
	    tc_fail(ctx);
	    /* use bottom */
	    t = datatype_absbottom(ctx->pql);
	 }
	 else {
	    t = datatype_getnth(membertype, j);
	 }
	 datatypearray_set(&types, i, t);
      }

      *chosentype = datatype_tuple_specific(ctx->pql, &types);

      datatypearray_setsize(ctx->pql, &types, 0);
      datatypearray_cleanup(ctx->pql, &types);
   }
   else {
      tc_check_columnlist_subset(ctx, what, choices, fromcols);
   }

   /*
    * Do this separately so we get it in the same order as
    * FROMMEMBERS, but get CHOSENTYPE in the order of CHOICES, which
    * isn't necessarily the same.
    */
   if (unchosentype != NULL || unchosenmembers != NULL) {
      struct datatypearray types;

      datatypearray_init(&types);

      for (i=0; i<numfrom; i++) {
	 tccol = coltree_get(fromcols, i);
	 if (colset_find(choices, tccol, NULL) == 0) {
	    continue;
	 }
	 if (unchosentype != NULL) {
	    datatypearray_add(ctx->pql,
			      &types, datatype_getnth(membertype, i), NULL);
	 }
	 if (unchosenmembers != NULL) {
	    // don't incref as everything we build in this module is transient
	    // and we shouldn't be changing the input expression.
	    //colname_incref(tccol);
	    colset_add(ctx->pql, unchosenmembers, tccol);
	 }
      }

      if (unchosentype != NULL) {
	 *unchosentype = datatype_tuple_specific(ctx->pql, &types);
	 datatypearray_setsize(ctx->pql, &types, 0);
      }
      datatypearray_cleanup(ctx->pql, &types);
   }

   if (isset) {
      if (chosentype != NULL) {
	 *chosentype = datatype_set(ctx->pql, *chosentype);
      }
      if (unchosentype != NULL) {
	 *unchosentype = datatype_set(ctx->pql, *unchosentype);
      }
   }
   else if (isseq) {
      if (chosentype != NULL) {
	 *chosentype = datatype_sequence(ctx->pql, *chosentype);
      }
      if (unchosentype != NULL) {
	 *unchosentype = datatype_sequence(ctx->pql, *unchosentype);
      }
   }
}

/*
 * Check that TE has *no* name-as-a-single-column.
 */
static void tc_check_no_name(struct tc *ctx, const char *what,
			     struct tcexpr *te) {
   if (te->colnames != NULL && coltree_wholecolumn(te->colnames) != NULL) {
      tc_say(ctx, "%s unexpectedly has its own name", what);
      tc_fail(ctx);
   }
}

/*
 * Check that TE1 and TE2 have the same name-as-a-single-column.
 *
 * XXX the messages here are incomprehensible and as written cannot be
 * related to anything in the input or even in the previous dump.
 */
static void tc_check_same_names(struct tc *ctx, const char *name,
				struct tcexpr *te1, struct tcexpr *te2) {
   struct colname *c1, *c2;

   if (te1->colnames == NULL) {
      c1 = NULL;
   }
   else {
      c1 = coltree_wholecolumn(te1->colnames);
   }
   if (te2->colnames == NULL) {
      c2 = NULL;
   }
   else {
      c2 = coltree_wholecolumn(te2->colnames);
   }

   if (c1 != c2) {
      tc_say(ctx, "Expression column name changed in %s"
	     " (computed %s, have %s)", name,
	     colname_getname(ctx->pql, c2), colname_getname(ctx->pql, c1));
      tc_fail(ctx);
   }
}

#if 0 /* no longer used */
/*
 * Check that TE has no column names and no members.
 */
static void tc_check_no_members(struct tc *ctx, const char *name,
				struct tcexpr *te) {
   tc_check_coltree_scalar(ctx, "Result of", name, te->colnames);
}
#endif

/*
 * Check that TE1 and TE2 have the same column names for their
 * members.
 *
 * XXX can we assume they're in the same order? I doubt it...
 *
 * XXX the messages here are incomprehensible and as written cannot be
 * related to anything in the input or even in the previous dump.
 */
static void tc_check_same_members(struct tc *ctx, const char *name,
				  struct tcexpr *te1,
				  struct tcexpr *te2) {
   tc_check_coltree_same(ctx, name, 0, te1->colnames, te2->colnames, true);
}

/*
 * Check both column names and members.
 */
static void tc_check_same_columns(struct tc *ctx,
				  const char *name,
				  struct tcexpr *te1,
				  struct tcexpr *te2) {
   tc_check_same_names(ctx, name, te1, te2);
   tc_check_same_members(ctx, name, te1, te2);
}

/*
 * XXX the argument ordering is bogus (but matches various other functions,
 * all of which need cleaning up)
 */
static void tc_check_rename(struct tc *ctx,
			    struct coltree *n1, struct coltree *n2,
			    struct colname *oldcol, struct colname *newcol,
			    bool *found) {
   unsigned num1, num2, i;

   PQLASSERT(n1 != NULL);
   PQLASSERT(n2 != NULL);

   if (coltree_wholecolumn(n2) == oldcol) {
      if (coltree_wholecolumn(n1) != newcol) {
	tc_say(ctx, "Rename of %s generated wrong result (computed %s, found %s)",
	       colname_getname(ctx->pql, oldcol),
	       colname_getname(ctx->pql, coltree_wholecolumn(n2)),
	       colname_getname(ctx->pql, coltree_wholecolumn(n1)));
	tc_fail(ctx);
      }
      PQLASSERT(!*found);
      *found = true;
   }
   else if (coltree_wholecolumn(n2) != coltree_wholecolumn(n1)) {
      tc_say(ctx, "Rename of %s caused %s to change to %s",
	     colname_getname(ctx->pql, oldcol),
	     colname_getname(ctx->pql, coltree_wholecolumn(n2)),
	     colname_getname(ctx->pql, coltree_wholecolumn(n1)));
      tc_fail(ctx);
   }

   num1 = coltree_num(n1);
   num2 = coltree_num(n2);
   if (num1 != num2) {
      tc_say(ctx, "Rename of %s caused subtuple %s/%s to change arity from %u to %u",
	     colname_getname(ctx->pql, oldcol),
	     colname_getname(ctx->pql, coltree_wholecolumn(n2)),
	     colname_getname(ctx->pql, coltree_wholecolumn(n1)),
	     num2, num1);
      tc_fail(ctx);
   }

   for (i=0; i<num1 && i<num2; i++) {
      tc_check_rename(ctx,
		      coltree_getsubtree(n1, i),
		      coltree_getsubtree(n2, i),
		      oldcol, newcol, found);
   }
}

static void tc_collect_columns(struct tc *ctx, struct coltree *cols,
			       struct colset *fill) {
   unsigned i, num;

   if (coltree_wholecolumn(cols) != NULL) {
      colset_add(ctx->pql, fill, coltree_wholecolumn(cols));
   }
   if (coltree_istuple(cols)) {
      num = coltree_num(cols);
      for (i=0; i<num; i++) {
	 tc_collect_columns(ctx, coltree_getsubtree(cols, i), fill);
      }
   }
}

static void tc_check_duplicatecolumns(struct tc *ctx, struct coltree *cols) {
   struct colset *collect;
   struct colname *thiscol, *othercol;
   unsigned i, j, num;

   if (!coltree_istuple(cols)) {
      /* shortcut a common case */
      return;
   }

   collect = colset_empty(ctx->pql);
   tc_collect_columns(ctx, cols, collect);

   /* this should really be done by sort and linear compare */

   num = colset_num(collect);
   for (i=0; i<num; i++) {
      thiscol = colset_get(collect, i);
      for (j=i+1; j<num; j++) {
	 othercol = colset_get(collect, j);
	 if (thiscol == othercol) {
	    tc_say(ctx, "Column name %s duplicated",
		   colname_getname(ctx->pql, thiscol));
	    tc_fail(ctx);
	    /* don't bother checking thiscol any more */
	    break;
	 }
      }
   }

   // drop the pointers as we didn't take references to them
   colset_setsize(ctx->pql, collect, 0);
   colset_destroy(ctx->pql, collect);
}

////////////////////////////////////////////////////////////
// builtin functions

static void tc_check_bool(struct tc *ctx,
			  const char *subname,
			  const char *name,
			  struct datatype *t) {
   if (!datatype_match_specialize(ctx->pql, t, datatype_bool(ctx->pql))) {
      tc_say(ctx, "%s %s is not boolean (found %s)",
	     subname, name, datatype_getname(t));
      tc_fail(ctx);
   }
}

static void tc_check_string(struct tc *ctx,
			    const char *subname,
			    const char *name,
			    struct datatype *t) {
   if (!datatype_match_specialize(ctx->pql, t, datatype_string(ctx->pql))) {
      tc_say(ctx, "%s %s is not a string (found %s)",
	     subname, name, datatype_getname(t));
      tc_fail(ctx);
   }
}

static void tc_check_numeric(struct tc *ctx,
			     const char *subname,
			     const char *name,
			     struct datatype *t) {
   if (!datatype_match_specialize(ctx->pql, t, datatype_absnumber(ctx->pql))) {
      tc_say(ctx, "%s %s is not a number (found %s)",
	     subname, name, datatype_getname(t));
      tc_fail(ctx);
   }
}

static struct datatype *tc_check_set(struct tc *ctx,
				     const char *subname,
				     const char *name,
				     struct datatype *t) {
   if (!datatype_isset(t)) {
      tc_say(ctx, "%s %s is not a set (found %s)",
	     subname, name, datatype_getname(t));
      tc_fail(ctx);
      return t;
   }
   return datatype_set_member(t);
}

static int tc_check_numargs(struct tc *ctx, const char *name,
			    struct datatypearray *argtypes, unsigned numargs) {
   unsigned arity;

   arity = datatypearray_num(argtypes);
   if (arity != numargs) {
      tc_say(ctx, "Wrong number of arguments for %s (found %u, expected %u)",
	     name, arity, numargs);
      tc_fail(ctx);
      return -1;
   }
   return 0;
}

static void tc_check_bop(struct tc *ctx, const char *name,
			 struct datatypearray *argtypes,
			 struct datatype **left_ret,
			 struct datatype **right_ret) {
   if (tc_check_numargs(ctx, name, argtypes, 2) < 0) {
      *left_ret = *right_ret = datatype_absbottom(ctx->pql);
   }
   else {
      *left_ret = datatypearray_get(argtypes, 0);
      *right_ret = datatypearray_get(argtypes, 1);
   }
}

static void tc_check_uop(struct tc *ctx, const char *name,
			 struct datatypearray *argtypes,
			 struct datatype **arg_ret) {
   if (tc_check_numargs(ctx, name, argtypes, 1) < 0) {
      *arg_ret = datatype_absbottom(ctx->pql);
   }
   else {
      *arg_ret = datatypearray_get(argtypes, 0);
   }
}

static void tc_check_noargs(struct tc *ctx, const char *name,
			    struct datatypearray *argtypes) {
   tc_check_numargs(ctx, name, argtypes, 0);
}

static struct datatype *function_tc(struct tc *ctx, enum functions f,
				    struct datatypearray *argtypes) {
   struct datatype *left, *right, *leftmember, *rightmember;
   struct datatype *type, *member;
   struct datatype *result;
   const char *name;

   result = NULL; // gcc 4.1

   name = function_getname(f);

   switch (f) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
     /* set(T) x set(T) -> set(T) */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     leftmember = tc_check_set(ctx, "Left argument of", name, left);
     rightmember = tc_check_set(ctx, "Right argument of", name, right);
     result = datatype_match_generalize(ctx->pql, leftmember, rightmember);
     PQLASSERT(result != NULL);
     result = datatype_set(ctx->pql, result);
     break;

    case F_IN:
     /* set(T) x set(T) -> bool  OR  T x set(T) -> bool */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     rightmember = tc_check_set(ctx, "Right argument of", name, right);
     result = datatype_match_specialize(ctx->pql, left, rightmember);
     if (result != NULL) {
	/* element-of */
     }
     else {
	leftmember = tc_check_set(ctx, "Left argument of", name, left);
	result = datatype_match_specialize(ctx->pql, leftmember, rightmember);
	if (result != NULL) {
	   /* subset-of */
	}
	else {
	   tc_say(ctx, "Illegal types for %s: %s, %s", name,
		  datatype_getname(left), datatype_getname(right));
	   tc_fail(ctx);
	}
     }
     result = datatype_bool(ctx->pql);
     break;

    case F_NONEMPTY:
     /* set(T) -> bool */
     tc_check_uop(ctx, name, argtypes, &type);
     member = tc_check_set(ctx, "Argument of", name, type);
     result = datatype_bool(ctx->pql);
     break;

    case F_MAKESET:
     /* T -> set(T) */
     tc_check_uop(ctx, name, argtypes, &type);
     result = datatype_set(ctx->pql, type);
     break;

    case F_GETELEMENT:
     /* set(T) -> T */
     tc_check_uop(ctx, name, argtypes, &type);
     result = tc_check_set(ctx, "Argument of", name, type);
     break;

    case F_COUNT:
     /* set(T) -> int */
     tc_check_uop(ctx, name, argtypes, &type);
     member = tc_check_set(ctx, "Argument of", name, type);
     result = datatype_int(ctx->pql);
     break;

    case F_SUM:
    case F_MIN:
    case F_MAX:
     /* set(T) -> T, T numeric */
     tc_check_uop(ctx, name, argtypes, &type);
     member = tc_check_set(ctx, "Argument of", name, type);
     tc_check_numeric(ctx, "Argument of", name, member);
     result = member;
     break;

    case F_AVG:
     /* set(T) -> float, T numeric */
     tc_check_uop(ctx, name, argtypes, &type);
     member = tc_check_set(ctx, "Argument of", name, type);
     tc_check_numeric(ctx, "Argument of", name, member);
     result = datatype_double(ctx->pql);
     break;

    case F_ALLTRUE:
    case F_ANYTRUE:
     /* set(bool) -> bool */
     tc_check_uop(ctx, name, argtypes, &type);
     member = tc_check_set(ctx, "Argument of", name, type);
     tc_check_bool(ctx, "Argument of", name, member);
     result = datatype_bool(ctx->pql);
     break;

    case F_AND:
    case F_OR:
     /* bool x bool -> bool */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     tc_check_bool(ctx, "Left argument of", name, left);
     tc_check_bool(ctx, "Right argument of", name, right);
     result = datatype_bool(ctx->pql);
     break;

    case F_NOT:
     /* bool -> bool */
     tc_check_uop(ctx, name, argtypes, &type);
     tc_check_bool(ctx, "Argument of", name, type);
     result = datatype_bool(ctx->pql);
     break;

    case F_NEW:
     /* set OR sequence OR pathelement OR tuple -> struct  OR  T -> T */
     tc_check_uop(ctx, name, argtypes, &type);
     if (datatype_isset(type) ||
	 datatype_issequence(type) ||
	 datatype_ispathelement(type) ||
	 datatype_istuple(type)) {
	result = datatype_struct(ctx->pql);
     }
     else {
	result = type;
     }
     break;

    case F_CTIME:
     /* unit -> string */
     // XXX shouldn't there be a date type?
     tc_check_noargs(ctx, name, argtypes);
     result = datatype_string(ctx->pql);
     break;

    case F_EQ:
    case F_NOTEQ:
     /* T x T -> bool */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     result = datatype_match_specialize(ctx->pql, left, right);
     if (result == NULL) {
	tc_say(ctx, "Illegal types for %s: %s, %s", name,
	       datatype_getname(left), datatype_getname(right));
	tc_fail(ctx);
     }
     result = datatype_bool(ctx->pql);
     break;

    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
     /* T x T -> bool, T numeric */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     tc_check_numeric(ctx, "Left argument of", name, left);
     tc_check_numeric(ctx, "Right argument of", name, right);
     result = datatype_match_specialize(ctx->pql, left, right);
     if (result == NULL) {
	tc_say(ctx, "Illegal types for %s: %s, %s", name,
	       datatype_getname(left), datatype_getname(right));
	tc_fail(ctx);
     }
     result = datatype_bool(ctx->pql);
     break;

    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
     /* string x string -> bool */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     tc_check_string(ctx, "Left argument of", name, left);
     tc_check_string(ctx, "Right argument of", name, right);
     result = datatype_bool(ctx->pql);
     break;

    case F_TOSTRING:
     /* T -> string */
     tc_check_uop(ctx, name, argtypes, &type);
     result = datatype_string(ctx->pql);
     break;

    case F_CONCAT:
     /*
      * string x string -> string  OR  seq(T) x seq(T) -> seq(T)
      * OR pathelemeent x pathelement -> seq(pathelement)
      *
      * (XXX: should we allow seq(pathelement) ++ pathelement like
      * this, or should we insist that the upstream code wrap single
      * pathelements in sequences if it's going to concat them?
      */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     if (datatype_issequence(left) && datatype_issequence(right)) {
	leftmember = datatype_sequence_member(left);
	rightmember = datatype_sequence_member(right);
	result = datatype_match_generalize(ctx->pql, leftmember, rightmember);
	PQLASSERT(result != NULL);
	result = datatype_sequence(ctx->pql, result);
     }
     else if (datatype_isstring(left) && datatype_isstring(right)) {
	result = left;
     }
     else if (datatype_ispathelement(left) && 
	      datatype_ispathelement(right)) {
	result = datatype_sequence(ctx->pql, left);
     }
     else if (datatype_ispathelement(left) && 
	      datatype_issequence(right) &&
	      datatype_ispathelement(datatype_sequence_member(right))) {
	result = right;
     }
     else if (datatype_issequence(left) &&
	      datatype_ispathelement(datatype_sequence_member(left)) &&
	      datatype_ispathelement(right)) {
	result = left;
     }
     else {
	tc_say(ctx, "Illegal types for %s: %s, %s", name,
	       datatype_getname(left), datatype_getname(right));
	tc_fail(ctx);
	result = left;
     }
     break;

    case F_CHOOSE:
     /* T x T -> T */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     result = datatype_match_generalize(ctx->pql, left, right);
     if (result == NULL) {
	tc_say(ctx, "Illegal types for %s: %s, %s", name,
	       datatype_getname(left), datatype_getname(right));
	tc_fail(ctx);
	result = left;
     }
     break;

    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     /* T x T -> T, T numeric */
     tc_check_bop(ctx, name, argtypes, &left, &right);
     tc_check_numeric(ctx, "Left argument of", name, left);
     tc_check_numeric(ctx, "Right argument of", name, right);
     result = datatype_match_generalize(ctx->pql, left, right);
#if 0 /* wrong */
     result = datatype_match_specialize(ctx->pql, left, right);
     if (result == NULL) {
#if 0 /* tc_check_numeric already issues a message */
	tc_say(ctx, "Illegal types for %s: %s, %s", name,
	       datatype_getname(left), datatype_getname(right));
	tc_fail(ctx);
#else
	PQLASSERT(ctx->failed);
#endif
	result = datatype_absany(ctx->pql);
     }
#endif
     break;

    case F_NEG:
    case F_ABS:
     /* T -> T, T numeric */
     tc_check_uop(ctx, name, argtypes, &type);
     tc_check_numeric(ctx, "Argument of", name, type);
     result = type;
     break;
   }

   return result;
}

static void function_colcheck(struct tc *ctx, enum functions f,
			      struct coltreearray *subcols,
			      struct coltree *resultcols) {
   struct coltree *left, *right, *argcols;
   const char *name;

   left = coltreearray_num(subcols) > 0 ? coltreearray_get(subcols, 0) : NULL;
   right = coltreearray_num(subcols) > 1 ? coltreearray_get(subcols, 1) : NULL;
   argcols = left;

   name = function_getname(f);

   switch (f) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
     if (coltree_arity(left) != coltree_arity(right)) {
	tc_say(ctx, "Left and right sides of %s have different arity "
	       "(%u vs. %u)", name, coltree_arity(left), coltree_arity(right));
	tc_fail(ctx);
     }
     else {
	tc_check_coltree_same(ctx, name, 0, left, resultcols, false);
	// If we take a union of two tuple sets with different column
	// names, we keep the column names from the first. The second
	// might have different column names. Note that the types have
	// to agree regardless.
	//
	// 20091211 dholland: no, we need to make sure the column
	// names are the same. Otherwise we run into trouble silently
	// if one side has the same columns but they get into a
	// different order. We'll insert renames somewhere for handling
	// any user-generated nonuniform names.
	tc_check_coltree_same(ctx, name, 0, right, resultcols, false);
     }
     break;

    case F_CONCAT:
    case F_CHOOSE:
     if (coltree_arity(left) != coltree_arity(right)) {
	tc_say(ctx, "Left and right sides of %s have different arity "
	       "(%u vs. %u)", name, coltree_arity(left), coltree_arity(right));
	tc_fail(ctx);
     }
     else if (coltree_arity(left) > 1) {
	tc_check_coltree_same(ctx, name, 0, left, resultcols, false);
	tc_check_coltree_same(ctx, name, 0, right, resultcols, false);
     }
     else {
	tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     }
     break;

    case F_IN:
     tc_check_coltree_same(ctx, name, 0, left, right, false);
     tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     break;

    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
    case F_ALLTRUE:
    case F_ANYTRUE:
    case F_NOT:
    case F_NEG:
    case F_ABS:
     tc_check_coltree_scalar(ctx, "Argument of", name, argcols);
     tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     break;

    case F_MAKESET:
    case F_GETELEMENT:
     tc_check_coltree_same(ctx, name, 0, argcols, resultcols, false);
     break;

    case F_AND:
    case F_OR:
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
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     /* bool x bool -> bool */
     tc_check_coltree_scalar(ctx, "Left argument of", name, left);
     tc_check_coltree_scalar(ctx, "Left argument of", name, right);
     tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     break;

    case F_NONEMPTY:
    case F_COUNT:
    case F_NEW:
    case F_TOSTRING:
     /* argument can be anything */
     tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     break;

    case F_CTIME:
     tc_check_coltree_nothing(ctx, "Argument of", name, argcols);
     tc_check_coltree_scalar(ctx, "Result of", name, resultcols);
     break;
   }
}

////////////////////////////////////////////////////////////
// recursive traversal

static void tcexpr_tc(struct tc *ctx, struct tcexpr *te) {
   tc_check_duplicatecolumns(ctx, te->colnames);

   switch (te->type) {
    case TCE_FILTER:
     tcexpr_tc(ctx, te->filter.sub);
     tcexpr_tc(ctx, te->filter.predicate);
     tc_check_filter(ctx, te->datatype, te->filter.sub->datatype,
		     te->filter.predicate->datatype);
     tc_check_same_columns(ctx, "filter expression", te, te->filter.sub);
     break;

    case TCE_PROJECT:
     tcexpr_tc(ctx, te->project.sub);
     {
	struct datatype *resulttype;
	unsigned num;

	tc_check_columnchoices(ctx, "Project expression",
			       te->project.sub->datatype,
			       te->project.sub->colnames,
			       te->project.cols,
			       &resulttype,
			       NULL, NULL);

	if (!datatype_eq(resulttype, te->datatype)) {
	   tc_say(ctx, "Project result has wrong type (computed %s, found %s)",
		  datatype_getname(resulttype),
		  datatype_getname(te->datatype));
	   tc_fail(ctx);
	}

	num = colset_num(te->project.cols);
	PQLASSERT(num > 0);
	tc_check_columnlist_same(ctx, "project expression result",
				 te->colnames, te->project.cols);
	if (colset_num(te->project.cols) != 1) {
	   tc_check_same_names(ctx, "project result", te, te->project.sub);
	}
     }
     break;

    case TCE_STRIP:
     tcexpr_tc(ctx, te->strip.sub);
     {
	struct datatype *resulttype;
	struct colset *resultcols;

	resultcols = colset_empty(ctx->pql);

	tc_check_columnchoices(ctx, "Strip expression",
			       te->strip.sub->datatype,
			       te->strip.sub->colnames,
			       te->strip.cols,
			       NULL,
			       &resulttype, resultcols);

	if (!datatype_eq(resulttype, te->datatype)) {
	   tc_say(ctx, "Strip result has wrong type");
	   tc_fail(ctx);
	}
	tc_check_columnlist_same(ctx, "strip expression result",
				 te->colnames, resultcols);
	if (colset_num(resultcols) != 1) {
	   tc_check_same_names(ctx, "strip expression result", te,
			       te->strip.sub);
	}

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, resultcols, 0);
	colset_destroy(ctx->pql, resultcols);
     }
     break;

    case TCE_RENAME:
     tcexpr_tc(ctx, te->rename.sub);
     if (!datatype_eq(te->datatype, te->rename.sub->datatype)) {
	tc_say(ctx, "Rename operation changed type of expression");
	tc_fail(ctx);
     }
     {
	bool found = false;

	tc_check_rename(ctx, te->colnames, te->rename.sub->colnames,
			te->rename.oldcol, te->rename.newcol, &found);
	if (!found) {
	   tc_say(ctx, "Target column %s of rename operation not found",
		  colname_getname(ctx->pql, te->rename.oldcol));
	   tc_fail(ctx);
	}
     }
     break;

    case TCE_JOIN:
     tcexpr_tc(ctx, te->join.left);
     tcexpr_tc(ctx, te->join.right);
     if (te->join.predicate != NULL) {
	tcexpr_tc(ctx, te->join.predicate);
     }
     {
	struct datatype *left, *right, *t;
	bool isset = false;

	left = te->join.left->datatype;
	right = te->join.right->datatype;
	if (datatype_isset(left)) {
	   left = datatype_set_member(left);
	   isset = true;
	}
	if (datatype_isset(right)) {
	   right = datatype_set_member(right);
	   isset = true;
	}
	t = datatype_tuple_concat(ctx->pql, left, right);
	if (isset) {
	   t = datatype_set(ctx->pql, t);
	}
	if (te->join.predicate != NULL) {
	   tc_check_filter(ctx, te->datatype, t,
			   te->join.predicate->datatype);
	}
	else {
	   /* subsumed by tc_check_filter in other case */
	   if (!datatype_eq(t, te->datatype)) {
	      tc_say(ctx, "Result of join has wrong type");
	      tc_fail(ctx);
	   }
	}
     }
     PQLASSERT(te->colnames != NULL);
     tc_check_no_name(ctx, "Join expression", te);
     tc_check_columnlist_pasted(ctx, "Join result",
				te->colnames,
				te->join.left->colnames,
				te->join.right->colnames);
     break;

    case TCE_ORDER:
     tcexpr_tc(ctx, te->order.sub);
     tc_check_same_columns(ctx, "order expression", te, te->order.sub);
     tc_check_columnlist_subset(ctx, "Order expression",
				te->order.cols, te->colnames);
     if (!datatype_isset(te->order.sub->datatype)) {
	tc_say(ctx, "Order applied to non-set");
	tc_fail(ctx);
	/* give up */
	break;
     }
     if (!datatype_issequence(te->datatype)) {
	tc_say(ctx, "Order result is not a sequence");
	tc_fail(ctx);
	/* give up */
	break;
     }
     if (!datatype_eq(datatype_set_member(te->order.sub->datatype),
		      datatype_sequence_member(te->datatype))) {
	tc_say(ctx, "Order changes type of operand");
	tc_fail(ctx);
     }
     break;

    case TCE_UNIQ:
     tcexpr_tc(ctx, te->uniq.sub);
     tc_check_same_columns(ctx, "uniq expression", te, te->uniq.sub);
     tc_check_columnlist_subset(ctx, "Uniq expression",
				te->uniq.cols, te->colnames);
     if (!datatype_issequence(te->uniq.sub->datatype)) {
	tc_say(ctx, "Uniq applied to non-sequence");
	tc_fail(ctx);
	/* give up */
	break;
     }
     if (!datatype_eq(te->order.sub->datatype, te->datatype)) {
	tc_say(ctx, "Uniq changes type of operand");
	tc_fail(ctx);
     }
     break;

    case TCE_NEST:
     tcexpr_tc(ctx, te->nest.sub);
     tc_check_same_names(ctx, "nest expression", te, te->nest.sub);
     {
	struct datatype *chosentype, *unchosentype, *resulttype;
	struct colset *unchosenmembers;

	unchosenmembers = colset_empty(ctx->pql);

	tc_check_columnchoices(ctx, "Nest expression",
			       te->nest.sub->datatype,
			       te->nest.sub->colnames,
			       te->nest.cols,
			       &chosentype,
			       &unchosentype,
			       unchosenmembers);

	/* tc_check_columnchoices conses the results back up; undo that */
	if (datatype_isset(te->nest.sub->datatype)) {
	   PQLASSERT(datatype_isset(chosentype));
	   PQLASSERT(datatype_isset(unchosentype));
	   chosentype = datatype_set_member(chosentype);
	   unchosentype = datatype_set_member(unchosentype);
	}
	else if (datatype_issequence(te->nest.sub->datatype)) {
	   PQLASSERT(datatype_issequence(chosentype));
	   PQLASSERT(datatype_issequence(unchosentype));
	   chosentype = datatype_sequence_member(chosentype);
	   unchosentype = datatype_sequence_member(unchosentype);
	}

	tc_check_columnlist_appended(ctx, "Nest expression", te->colnames,
				     unchosenmembers, te->nest.newcol);

	resulttype = datatype_tuple_concat(ctx->pql,
					   unchosentype,
					   datatype_set(ctx->pql, chosentype));

	if (datatype_isset(te->nest.sub->datatype)) {
	   resulttype = datatype_set(ctx->pql, resulttype);
	}
	else if (datatype_issequence(te->nest.sub->datatype)) {
	   resulttype = datatype_sequence(ctx->pql, resulttype);
	}


	if (!datatype_eq(te->datatype, resulttype)) {
	   tc_say(ctx, "Nest result type is wrong (computed %s, found %s)",
		  datatype_getname(resulttype),
		  datatype_getname(te->datatype));
	   tc_fail(ctx);
	}

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, unchosenmembers, 0);
	colset_destroy(ctx->pql, unchosenmembers);
     }
     break;

    case TCE_UNNEST:
     tcexpr_tc(ctx, te->unnest.sub);
     tc_check_same_names(ctx, "unnest expression", te, te->unnest.sub);
     {
	struct colset *cols, *unchosenmembers;
	struct datatype *chosentype, *unchosentype;
	struct datatype *unnestedtype, *resulttype;

	cols = colset_singleton(ctx->pql, te->unnest.col);
	unchosenmembers = colset_empty(ctx->pql);

	tc_check_columnchoices(ctx, "Unnest expression",
			       te->unnest.sub->datatype,
			       te->unnest.sub->colnames,
			       cols,
			       &chosentype,
			       &unchosentype,
			       unchosenmembers);

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, cols, 0);
	colset_destroy(ctx->pql, cols);

	/* tc_check_columnchoices conses the results back up; undo that */
	if (datatype_isset(te->unnest.sub->datatype)) {
	   PQLASSERT(datatype_isset(chosentype));
	   PQLASSERT(datatype_isset(unchosentype));
	   chosentype = datatype_set_member(chosentype);
	   unchosentype = datatype_set_member(unchosentype);
	}
	else if (datatype_issequence(te->unnest.sub->datatype)) {
	   PQLASSERT(datatype_issequence(chosentype));
	   PQLASSERT(datatype_issequence(unchosentype));
	   chosentype = datatype_sequence_member(chosentype);
	   unchosentype = datatype_sequence_member(unchosentype);
	}

	if (datatype_isset(chosentype)) {
	   unnestedtype = datatype_set_member(chosentype);
	}
	else if (datatype_issequence(chosentype)) {
	   unnestedtype = datatype_sequence_member(chosentype);
	}
	else {
	   tc_say(ctx, "Cannot unnest non-set column %s",
		  colname_getname(ctx->pql, te->unnest.col));
	   tc_fail(ctx);
	   /* give up */
	   colset_setsize(ctx->pql, unchosenmembers, 0);
	   colset_destroy(ctx->pql, unchosenmembers);
	   break;
	}

	resulttype = datatype_tuple_concat(ctx->pql,
					   unchosentype, unnestedtype);

	if (datatype_isset(te->unnest.sub->datatype)) {
	   resulttype = datatype_set(ctx->pql, resulttype);
	}
	else if (datatype_issequence(te->unnest.sub->datatype)) {
	   resulttype = datatype_sequence(ctx->pql, resulttype);
	}

	if (!datatype_eq(resulttype, te->datatype)) {
	   tc_say(ctx, "Unnest result type is wrong (computed %s, found %s)",
		  datatype_getname(resulttype),
		  datatype_getname(te->datatype));
	   tc_fail(ctx);
	}

	/* XXX where do we get the column names of the nested set? */
	//tc_check_columnlist_pasted(ctx, "Unnest result", &te->members,
	//			     &unchosenmembers, &unnestedmembers);
	tc_check_columnlist_subset(ctx, "Unnest expression",
				   unchosenmembers, te->colnames);

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, unchosenmembers, 0);
	colset_destroy(ctx->pql, unchosenmembers);
     }
     break;

    case TCE_DISTINGUISH:
     tcexpr_tc(ctx, te->distinguish.sub);
     if (coltree_arity(te->distinguish.sub->colnames) > 1) {
	tc_check_same_names(ctx, "distinguish expression",
			    te, te->distinguish.sub);
     }
     else {
	tc_check_no_name(ctx, "Distinguish expression", te);
     }
     {
	struct datatype *subtype, *resulttype;
	bool isset;
	
	subtype = te->distinguish.sub->datatype;
	isset = datatype_isset(subtype);
	if (isset) {
	   subtype = datatype_set_member(subtype);
	}

	resulttype = datatype_tuple_append(ctx->pql,
					   subtype,
					   datatype_distinguisher(ctx->pql));
	if (isset) {
	   resulttype = datatype_set(ctx->pql, resulttype);
	}
	if (!datatype_eq(te->datatype, resulttype)) {
	   tc_say(ctx, "Result of distinguish has wrong type");
	   tc_fail(ctx);
	}
     }
     tc_check_columnlist_coltree_appended(ctx, "Distinguish expression",
				  te->colnames,
				  te->distinguish.sub->colnames,
				  te->distinguish.newcol);
     break;

    case TCE_ADJOIN:
     tcexpr_tc(ctx, te->adjoin.left);
     tcexpr_tc(ctx, te->adjoin.func);
     if (coltree_arity(te->distinguish.sub->colnames) > 1) {
	tc_check_same_names(ctx, "adjoin expression", te, te->adjoin.left);
     }
     else {
	tc_check_no_name(ctx, "Adjoin expression", te);
     }
     {
	struct datatype *membertype, *lambdatype, *newcoltype, *resulttype;
	bool isset;

	membertype = te->adjoin.left->datatype;
	isset = datatype_isset(membertype);
	if (isset) {
	   membertype = datatype_set_member(membertype);
	}

	lambdatype = te->adjoin.func->datatype;
	if (!datatype_islambda(lambdatype)) {
	   tc_say(ctx, "Adjoin function not a lambda expression");
	   tc_fail(ctx);
	   /* use bottom */
	   newcoltype = datatype_absbottom(ctx->pql);
	}
	else {
	   if (!datatype_eq(datatype_lambda_argument(lambdatype), membertype)){
	      tc_say(ctx, "Adjoin function expects %s, got %s",
		     datatype_getname(datatype_lambda_argument(lambdatype)),
		     datatype_getname(membertype));
	      tc_fail(ctx);
	   }
	   newcoltype = datatype_lambda_result(lambdatype);
	}

	// XXX should this check be part of datatype_tuple_append?
	if (datatype_isabstop(membertype)) {
	   resulttype = datatype_abstop(ctx->pql);
	}
	else {
	   resulttype = datatype_tuple_append(ctx->pql, membertype,newcoltype);
	}
	if (isset) {
	   resulttype = datatype_set(ctx->pql, resulttype);
	}
	if (!datatype_eq(te->datatype, resulttype)) {
	   tc_say(ctx, "Result of adjoin has wrong type "
		  "(computed %s, found %s)", datatype_getname(resulttype),
		  datatype_getname(te->datatype));
	   tc_fail(ctx);
	}
     }
     tc_check_columnlist_coltree_appended(ctx, "Adjoin expression",
				  te->colnames,
				  te->adjoin.left->colnames,
				  te->adjoin.newcol);
     break;

    case TCE_STEP:
     tcexpr_tc(ctx, te->step.sub);
     if (te->step.predicate != NULL) {
	tcexpr_tc(ctx, te->step.predicate);
     }
     {
	struct datatype *left, *right, *t;

	left = te->step.sub->datatype;
	if (datatype_isset(left)) {
	   left = datatype_set_member(left);
	}
	right = datatype_tuple_triple(ctx->pql,
				      datatype_dbobj(ctx->pql),
				      datatype_dbedge(ctx->pql),
				      datatype_dbobj(ctx->pql));
	t = datatype_tuple_concat(ctx->pql, left, right);
	t = datatype_set(ctx->pql, t);

	if (te->step.predicate != NULL) {
	   tc_check_filter(ctx, te->datatype, t,
			   te->step.predicate->datatype);
	}
	else {
	   /* subsumed by tc_check_filter in other case */
	   if (!datatype_eq(t, te->datatype)) {
	      tc_say(ctx, "Result of scan has wrong type");
	      tc_fail(ctx);
	   }
	}
     }
     PQLASSERT(te->colnames != NULL);
     tc_check_no_name(ctx, "Step-join expression", te);
     {
	struct coltree *right;

	right = coltree_create_triple(ctx->pql, NULL,
				     te->step.leftobjcolumn,
				     te->step.edgecolumn,
				     te->step.rightobjcolumn);

	tc_check_columnlist_pasted(ctx, "Join result",
				   te->colnames,
				   te->step.sub->colnames,
				   right);

	coltree_destroy(ctx->pql, right);
     }
     break;

    case TCE_REPEAT:
     {
	struct datatype *chosentype, *unchosentype, *starttype;
	struct datatype *outputtype, *outputpathtype;
	struct colset *cols, *unchosenmembers;

	/* first, check the start expression */
	tcexpr_tc(ctx, te->repeat.sub);
	starttype = te->repeat.sub->datatype;
	if (!datatype_isset(starttype)) {
	   tc_say(ctx, "Repeat start expression is not a set "
		  "(found %s)",
		  datatype_getname(starttype));
	   tc_fail(ctx);
	}
	else {
	   starttype = datatype_set_member(starttype);
	}

	/* check that the start column is valid */
	cols = colset_singleton(ctx->pql, te->repeat.subendcolumn);
	tc_check_columnchoices(ctx, "Repeat expression",
			       te->repeat.sub->datatype,
			       te->repeat.sub->colnames,
			       cols,
			       &chosentype, NULL, NULL);
	if (!datatype_eq(chosentype, te->repeat.loopvar->datatype)) {
	   tc_say(ctx, "Repeat expression iteration var has wrong type "
		  "(computed %s, found %s)",
		  datatype_getname(chosentype),
		  datatype_getname(te->repeat.loopvar->datatype));
	   tc_fail(ctx);
	}
	tc_check_coltree_scalar(ctx,
			       "Iteration variable of", "repeat expression",
			       te->repeat.loopvar->colnames);
	if (coltree_wholecolumn(te->repeat.loopvar->colnames) !=
	    te->repeat.bodystartcolumn) {
	   tc_say(ctx, "Repeat expression input column inconsistent "
		  "(found %s and %s)",
		  coltree_getname(ctx->pql, te->repeat.loopvar->colnames),
		  colname_getname(ctx->pql, te->repeat.bodystartcolumn));
	   tc_fail(ctx);
	}

	tcexpr_tc(ctx, te->repeat.body);

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, cols, 0);
	colset_destroy(ctx->pql, cols);
	cols = colset_empty(ctx->pql);

	colset_add(ctx->pql, cols, te->repeat.bodystartcolumn);
	if (te->repeat.bodypathcolumn != NULL) {
	   colset_add(ctx->pql, cols, te->repeat.bodypathcolumn);
	}
	colset_add(ctx->pql, cols, te->repeat.bodyendcolumn);

	unchosenmembers = colset_empty(ctx->pql);

	tc_check_columnchoices(ctx, "Repeat expression",
			       te->repeat.body->datatype,
			       te->repeat.body->colnames,
			       cols,
			       &chosentype,
			       &unchosentype,
			       unchosenmembers);

	PQLASSERT(datatype_isset(chosentype));
	PQLASSERT(datatype_isset(unchosentype));

	chosentype = datatype_set_member(chosentype);
	unchosentype = datatype_set_member(unchosentype);

	if (te->repeat.bodypathcolumn != NULL) {
	   PQLASSERT(datatype_arity(chosentype) == 3);
	   outputpathtype = datatype_getnth(chosentype, 1);
	   /* if already a sequence, leave it alone, otherwise make it one */
	   if (!datatype_issequence(outputpathtype)) {
	      outputpathtype = datatype_sequence(ctx->pql, outputpathtype);
	   }
	   outputtype = datatype_getnth(chosentype, 2);
	}
	else {
	   PQLASSERT(datatype_arity(chosentype) == 2);
	   outputtype = datatype_getnth(chosentype, 1);
	   outputpathtype = NULL;
	}

	/* if we'd get sequence(unit), skip it */
	if (datatype_arity(unchosentype) > 0) {
	   unchosentype = datatype_sequence(ctx->pql, unchosentype);
	}

	if (outputpathtype != NULL) {
	   unchosentype = datatype_tuple_append(ctx->pql, unchosentype,
						outputpathtype);
	}

	unchosentype = datatype_tuple_append(ctx->pql, unchosentype,
					     outputtype);

	unchosentype = datatype_tuple_concat(ctx->pql, starttype,unchosentype);

	if (datatype_isset(te->repeat.body->datatype)) {
	   unchosentype = datatype_set(ctx->pql, unchosentype);
	}

	if (!datatype_eq(unchosentype, te->datatype)) {
	   tc_say(ctx, "Repeat expression has wrong type "
		  "(computed %s, found %s)",
		  datatype_getname(unchosentype),
		  datatype_getname(te->datatype));
	   tc_fail(ctx);
	}

	tc_check_columnlist_multipasted(ctx, "Repeat expression",
					te->colnames,
					te->repeat.sub->colnames,
					unchosenmembers,
					te->repeat.repeatpathcolumn,
					te->repeat.repeatendcolumn);

	// drop the pointers as we didn't take references to them
	colset_setsize(ctx->pql, cols, 0);
	colset_setsize(ctx->pql, unchosenmembers, 0);
	colset_destroy(ctx->pql, cols);
	colset_destroy(ctx->pql, unchosenmembers);

	tc_check_no_name(ctx, "Repeat expression", te);
     }
     break;

    case TCE_SCAN:
     if (te->scan.predicate != NULL) {
	tcexpr_tc(ctx, te->scan.predicate);
     }
     tc_check_no_name(ctx, "Scan expression", te);
     {
	struct datatype *t;

	t = datatype_tuple_triple(ctx->pql,
				  datatype_dbobj(ctx->pql),
				  datatype_dbedge(ctx->pql),
				  datatype_dbobj(ctx->pql));
	t = datatype_set(ctx->pql, t);
	if (te->scan.predicate != NULL) {
	   tc_check_filter(ctx, te->datatype, t,
			   te->scan.predicate->datatype);
	}
	else {
	   /* subsumed by tc_check_filter in other case */
	   if (!datatype_eq(t, te->datatype)) {
	      tc_say(ctx, "Result of scan has wrong type");
	      tc_fail(ctx);
	   }
	}
	/* Assert this because it takes real work to get it screwed up */
	PQLASSERT(coltree_num(te->colnames) == 3);
	{
	   struct coltree *l, *e, *r;

	   l = coltree_getsubtree(te->colnames, 0);
	   e = coltree_getsubtree(te->colnames, 1);
	   r = coltree_getsubtree(te->colnames, 2);
	   PQLASSERT(coltree_wholecolumn(l) == te->scan.leftobjcolumn);
	   PQLASSERT(coltree_wholecolumn(e) == te->scan.edgecolumn);
	   PQLASSERT(coltree_wholecolumn(r) == te->scan.rightobjcolumn);
	   PQLASSERT(coltree_num(l) == 0);
	   PQLASSERT(coltree_num(e) == 0);
	   PQLASSERT(coltree_num(r) == 0);
	}
     }
     break;

    case TCE_BOP:
     tcexpr_tc(ctx, te->bop.left);
     tcexpr_tc(ctx, te->bop.right);
     {
	struct datatypearray subtypes;
	struct coltreearray subcols;
	struct datatype *rettype;

	datatypearray_init(&subtypes);
	coltreearray_init(&subcols);

	datatypearray_add(ctx->pql, &subtypes, te->bop.left->datatype, NULL);
	datatypearray_add(ctx->pql, &subtypes, te->bop.right->datatype, NULL);
	coltreearray_add(ctx->pql, &subcols, te->bop.left->colnames, NULL);
	coltreearray_add(ctx->pql, &subcols, te->bop.right->colnames, NULL);

	rettype = function_tc(ctx, te->bop.op, &subtypes);
	if (!datatype_eq(rettype, te->datatype)) {
	   tc_say(ctx, "Result of %s has wrong type (computed %s, had %s)",
		  function_getname(te->bop.op),
		  datatype_getname(rettype), datatype_getname(te->datatype));
	   tc_fail(ctx);
	}

	function_colcheck(ctx, te->bop.op, &subcols, te->colnames);

	datatypearray_setsize(ctx->pql, &subtypes, 0);
	coltreearray_setsize(ctx->pql, &subcols, 0);
	datatypearray_cleanup(ctx->pql, &subtypes);
	coltreearray_cleanup(ctx->pql, &subcols);
     }
     break;

    case TCE_UOP:
     tcexpr_tc(ctx, te->uop.sub);
     {
	struct datatypearray subtypes;
	struct coltreearray subcols;
	struct datatype *rettype;

	datatypearray_init(&subtypes);
	coltreearray_init(&subcols);

	datatypearray_add(ctx->pql, &subtypes, te->uop.sub->datatype, NULL);
	coltreearray_add(ctx->pql, &subcols, te->uop.sub->colnames, NULL);

	rettype = function_tc(ctx, te->uop.op, &subtypes);
	if (!datatype_eq(rettype, te->datatype)) {
	   tc_say(ctx, "Result of %s has wrong type",
		  function_getname(te->uop.op));
	   tc_fail(ctx);
	}
	function_colcheck(ctx, te->uop.op, &subcols, te->colnames);

	datatypearray_setsize(ctx->pql, &subtypes, 0);
	coltreearray_setsize(ctx->pql, &subcols, 0);
	datatypearray_cleanup(ctx->pql, &subtypes);
	coltreearray_cleanup(ctx->pql, &subcols);
     }
     break;


    case TCE_FUNC:
     {
	unsigned i, num;
	struct tcexpr *subexpr;
	struct datatypearray subtypes;
	struct coltreearray subcols;
	struct datatype *rettype;

	datatypearray_init(&subtypes);
	coltreearray_init(&subcols);

	num = tcexprarray_num(&te->func.args);
	datatypearray_setsize(ctx->pql, &subtypes, num);
	coltreearray_setsize(ctx->pql, &subcols, num);

	for (i=0; i<num; i++) {
	   subexpr = tcexprarray_get(&te->func.args, i);
	   tcexpr_tc(ctx, subexpr);
	   datatypearray_set(&subtypes, i, subexpr->datatype);
	   coltreearray_set(&subcols, i, subexpr->colnames);
	}

	rettype = function_tc(ctx, te->func.op, &subtypes);
	if (!datatype_eq(rettype, te->datatype)) {
	   tc_say(ctx, "Result of %s has wrong type",
		  function_getname(te->func.op));
	   tc_fail(ctx);
	}
	function_colcheck(ctx, te->func.op, &subcols, te->colnames);

	datatypearray_setsize(ctx->pql, &subtypes, 0);
	coltreearray_setsize(ctx->pql, &subcols, 0);
	datatypearray_cleanup(ctx->pql, &subtypes);
	coltreearray_cleanup(ctx->pql, &subcols);
     }
     break;

    case TCE_MAP:
     tcexpr_tc(ctx, te->map.set);
     tcexpr_tc(ctx, te->map.result);
     tc_check_same_columns(ctx, "map expression result", te, te->map.result);
     tc_check_coltree_same(ctx, "map expression variable", 0,
			  te->map.var->colnames, te->map.set->colnames, false);
     {
	struct datatype *membertype, *resulttype;

	if (!datatype_isset(te->map.set->datatype)) {
	   tc_say(ctx, "Map operator or quantifier applied to non-set");
	   tc_fail(ctx);
	   membertype = te->map.set->datatype;
	}
	else {
	   membertype = datatype_set_member(te->map.set->datatype);
	}

	if (!datatype_eq(te->map.var->datatype, membertype)) {
	   tc_say(ctx, "Quantifier or map variable has wrong type");
	   tc_fail(ctx);
	}
	resulttype = datatype_set(ctx->pql, te->map.result->datatype);
	if (!datatype_eq(te->datatype, resulttype)) {
	   tc_say(ctx, "Quantifier or map expression has wrong result type");
	   tc_fail(ctx);
	}
     }
     break;

    case TCE_LET:
     tcexpr_tc(ctx, te->let.value);
     tcexpr_tc(ctx, te->let.body);
     tc_check_same_columns(ctx, "let expression result", te, te->map.result);
     tc_check_coltree_same(ctx, "let expression variable", 0,
			  te->let.var->colnames, te->let.value->colnames,
			  false);
     if (!datatype_eq(te->let.var->datatype, te->let.value->datatype)) {
	tc_say(ctx, "Let-bound variable has wrong type");
	tc_fail(ctx);
     }
     if (!datatype_eq(te->datatype, te->let.body->datatype)) {
	tc_say(ctx, "Let expression has wrong result type");
	tc_fail(ctx);
     }
     break;

    case TCE_LAMBDA:
     tcexpr_tc(ctx, te->lambda.body);
     tc_check_no_name(ctx, "Lambda expression", te);
     tc_check_coltree_scalar(ctx, "Result of", "lambda expression", te->colnames);
     if (!datatype_islambda(te->datatype)) {
	tc_say(ctx, "Lambda expression does not have lambda type");
	tc_fail(ctx);
	/* give up */
	break;
     }
     if (!datatype_eq(te->lambda.var->datatype,
		      datatype_lambda_argument(te->datatype))) {
	tc_say(ctx, "Lambda-bound variable has wrong type");
	tc_fail(ctx);
     }
     if (!datatype_eq(te->lambda.body->datatype,
		      datatype_lambda_result(te->datatype))) {
	tc_say(ctx, "Lambda-bound variable has wrong type");
	tc_fail(ctx);
     }
     break;

    case TCE_APPLY:
     /* XXX shouldn't APPLY and UOP be merged? */
     tcexpr_tc(ctx, te->apply.lambda);
     tcexpr_tc(ctx, te->apply.arg);
     tc_check_same_columns(ctx, "lambda application result",
			   te, te->lambda.body);
     if (!datatype_islambda(te->apply.lambda->datatype)) {
	tc_say(ctx, "Call of non-function");
	tc_fail(ctx);
	/* give up */
	break;
     }
     if (!datatype_eq(te->apply.arg->datatype,
		      datatype_lambda_argument(te->apply.lambda->datatype))) {
	tc_say(ctx, "Argument to lambda-expression has wrong type");
	tc_fail(ctx);
     }
     if (!datatype_eq(te->datatype,
		      datatype_lambda_result(te->apply.lambda->datatype))) {
	tc_say(ctx, "Apply expression has wrong type");
	tc_fail(ctx);
     }
     if (te->apply.lambda->type == TCE_LAMBDA) {
	tc_check_coltree_same(ctx, "lambda application argument", 0,
			     te->apply.arg->colnames,
			     te->apply.lambda->lambda.var->colnames,
			     false);
     }
     break;

    case TCE_READVAR:
     tc_check_coltree_same(ctx, "readvar result", 0,
			  te->colnames, te->readvar->colnames, false);
     if (!datatype_eq(te->datatype, te->readvar->datatype)) {
	tc_say(ctx, "Read-variable expression has wrong type");
	tc_fail(ctx);
     }
     break;

    case TCE_READGLOBAL:
     if (!datatype_eq(te->datatype, datatype_set(ctx->pql,
						 datatype_dbobj(ctx->pql)))) {
	tc_say(ctx, "Read-global-variable expression has wrong type");
	tc_fail(ctx);
     }
     break;

    case TCE_CREATEPATHELEMENT:
     tcexpr_tc(ctx, te->createpathelement);
     {
	struct datatype *t0, *t1, *t2;

	if (datatype_istuple(te->createpathelement->datatype)) {
	   t0 = datatype_getnth(te->createpathelement->datatype, 0);
	   t1 = datatype_getnth(te->createpathelement->datatype, 1);
	   t2 = datatype_getnth(te->createpathelement->datatype, 2);
	}
	else {
	   t0 = t1 = t2 = NULL;
	}
	if (!datatype_eq(t0, datatype_dbobj(ctx->pql)) ||
	    !datatype_eq(t1, datatype_dbedge(ctx->pql)) ||
	    !datatype_eq(t2, datatype_dbobj(ctx->pql))) {
	   tc_say(ctx, "Argument to pathelement constructor has wrong type");
	   tc_fail(ctx);
	}
	if (!datatype_eq(te->datatype, datatype_pathelement(ctx->pql))) {
	   tc_say(ctx, "Result of pathelement constructor has wrong type");
	   tc_fail(ctx);
	}
     }
     break;

    case TCE_SPLATTER:
     tcexpr_tc(ctx, te->splatter.value);
     tcexpr_tc(ctx, te->splatter.name);
     if (!datatype_eq(te->splatter.value->datatype, te->datatype)) {
	tc_say(ctx, "Result of splatter has wrong type");
	tc_fail(ctx);
     }
     if (datatype_match_specialize(ctx->pql, te->splatter.name->datatype,
				   datatype_absdbedge(ctx->pql)) == NULL) {
	tc_say(ctx, "Edge name in splatter has wrong type");
	tc_fail(ctx);
     }
     tc_check_no_name(ctx, "Splatter expression", te);
     tc_check_same_columns(ctx, "splatter result", te, te->splatter.value);
     break;

    case TCE_TUPLE:
     {
	unsigned i, num;
	struct tcexpr *subexpr;
	struct datatype *subtype;

	num = tcexprarray_num(&te->tuple.exprs);
	PQLASSERT(num == colset_num(te->tuple.columns));
	PQLASSERT(num == datatype_arity(te->datatype));

	for (i=0; i<num; i++) {
	   subexpr = tcexprarray_get(&te->tuple.exprs, i);
	   subtype = datatype_getnth(te->datatype, i);
	   tcexpr_tc(ctx, subexpr);
	   if (!datatype_eq(subexpr->datatype, subtype)) {
	      tc_say(ctx, "Tuple element %u has wrong type "
		     "(computed %s, have %s)", i,
		     datatype_getname(subexpr->datatype),
		     datatype_getname(subtype));
	      tc_fail(ctx);
	   }
	}
     }
     tc_check_no_name(ctx, "Tuple expression", te);
     tc_check_columnlist_same(ctx, "tuple expression",
			      te->colnames, te->tuple.columns);
     break;

    case TCE_VALUE:
     if (!datatype_eq(te->datatype, pqlvalue_datatype(te->value))) {
	tc_say(ctx, "Constant expression has wrong type");
	tc_fail(ctx);
     }
     break;
   }
}

////////////////////////////////////////////////////////////
// entry point

int typecheck(struct pqlcontext *pql, struct tcexpr *te) {
   struct tc ctx;
   int ret;

   tc_init(&ctx, pql);
   
   tcexpr_tc(&ctx, te);

   ret = ctx.failed ? -1 : 0;
   tc_cleanup(&ctx);
   return ret;
}
