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

#include <stdarg.h>
#include <stdio.h>      /* for snprintf */
#include <stdlib.h>     /* for qsort, strtol, strtod */
#include <string.h>
#include <errno.h>      /* for strtol, strtod */
#include <fnmatch.h>    /* for fnmatch */
#include <regex.h>      /* for regcomp/regexec */
#include <time.h>       /* for ctime */
#include <math.h>       /* for fmod, fabs */

#include "utils.h"
#include "datatype.h"
#include "columns.h"
#include "pqlvalue.h"
#include "tcalc.h"
#include "passes.h"
#include "pqlcontext.h"

// uncomment this to trace to stderr (not useful in production, 
// but useful for debugging PQL)
//#define TRACE_TO_STDERR

struct varbinding {
   const struct tcvar *var;
   const struct pqlvalue *value;
};
DECLARRAY(varbinding);
DEFARRAY(varbinding, /*noinline*/);

struct eval {
   struct pqlcontext *pql;
   struct varbindingarray bindings;

   /* trace state */
   unsigned traceindent;
};

static struct pqlvalue *tcexpr_eval(struct eval *ctx, struct tcexpr *te);
static struct pqlvalue *lambda_eval(struct eval *ctx, struct tcexpr *te,
				    const struct pqlvalue *arg);

////////////////////////////////////////////////////////////
// context management

static void eval_init(struct eval *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   varbindingarray_init(&ctx->bindings);
   ctx->traceindent = 0;
}

static void eval_cleanup(struct eval *ctx) {
   PQLASSERT(varbindingarray_num(&ctx->bindings) == 0);
   varbindingarray_cleanup(ctx->pql, &ctx->bindings);
   PQLASSERT(ctx->traceindent == 0);
}

static struct varbinding *varbinding_create(struct pqlcontext *pql,
					    struct tcvar *var,
					    const struct pqlvalue *val) {
   struct varbinding *b;

   b = domalloc(pql, sizeof(*b));
   b->var = var;
   b->value = val;
   return b;
}

static void varbinding_destroy(struct pqlcontext *pql, struct varbinding *b) {
   dofree(pql, b, sizeof(*b));
}

static unsigned eval_bind(struct eval *ctx,
			  struct tcvar *var, const struct pqlvalue *val) {
   struct varbinding *b;
   unsigned mark;

   mark = 0; // gcc 4.1

   b = varbinding_create(ctx->pql, var, val);
   varbindingarray_add(ctx->pql, &ctx->bindings, b, &mark);
   return mark;
}

static void eval_unbind(struct eval *ctx, unsigned mark) {
   struct varbinding *b;
   unsigned i, num;

   num = varbindingarray_num(&ctx->bindings);
   for (i=mark; i<num; i++) {
      b = varbindingarray_get(&ctx->bindings, i);
      varbinding_destroy(ctx->pql, b);
   }
   varbindingarray_setsize(ctx->pql, &ctx->bindings, mark);
}

static const struct pqlvalue *eval_readvar(struct eval *ctx,
					   struct tcvar *var) {
   struct varbinding *b;
   unsigned i, num;

   /*
    * This should be made smarter.
    */
   num = varbindingarray_num(&ctx->bindings);
   for (i=0; i<num; i++) {
      b = varbindingarray_get(&ctx->bindings, i);
      if (b->var == var) {
	 return b->value;
      }
   }
   return NULL;
}

////////////////////////////////////////////////////////////
// tracing

static void trace_indent(struct eval *ctx) {
   ctx->traceindent++;
}

static void trace_unindent(struct eval *ctx) {
   PQLASSERT(ctx->traceindent > 0);
   ctx->traceindent--;
}

static void trace(struct eval *ctx, const char *fmt, ...) PF(2, 3);

static void trace(struct eval *ctx, const char *fmt, ...) {
   char buf[8192 + 128];
   va_list ap;
   size_t pos;

   snprintf(buf, sizeof(buf), "%*s", ctx->traceindent*3, "");
   pos = strlen(buf);
   va_start(ap, fmt);
   vsnprintf(buf+pos, sizeof(buf)-pos, fmt, ap);
   va_end(ap);

#ifdef TRACE_TO_STDERR
   fprintf(stderr, "%s\n", buf);
#else
   pqlcontext_addtrace(ctx->pql, dostrdup(ctx->pql, buf));
#endif
}

static void trace_dump(struct eval *ctx, struct tcexpr *te) {
   char *txt;

   txt = tcdump(ctx->pql, te, false);
   trace(ctx, "%s", txt);
   dostrfree(ctx->pql, txt);
}

static void trace_value(struct eval *ctx, const struct pqlvalue *val) {
   char buf[8192];

   pqlvalue_print(buf, sizeof(buf), val);
   trace(ctx, "%s", buf);
}

static void trace_2value(struct eval *ctx, const struct pqlvalue *val1,
			 const struct pqlvalue *val2) {
   char buf1[8192];
   char buf2[8192];

   pqlvalue_print(buf1, sizeof(buf1), val1);
   pqlvalue_print(buf2, sizeof(buf2), val2);
   trace(ctx, "%s  %s", buf1, buf2);
}

static void trace_2string(struct eval *ctx, const char *val1,const char *val2){
   trace(ctx, "%s  %s", val1, val2);
}

static void trace_descvalue(struct eval *ctx, const struct pqlvalue *val,
			    const char *desc) {
   char buf[8192];

   pqlvalue_print(buf, sizeof(buf), val);
   trace(ctx, "%s: %s", desc, buf);
}

static void trace_desc2value(struct eval *ctx, const struct pqlvalue *val1,
			     const struct pqlvalue *val2,
			     const char *desc) {
   char buf1[8192];
   char buf2[8192];

   pqlvalue_print(buf1, sizeof(buf1), val1);
   pqlvalue_print(buf2, sizeof(buf2), val2);
   trace(ctx, "%s: %s  %s", desc, buf1, buf2);
}

static void trace_shortvalue(struct eval *ctx, const struct pqlvalue *val) {
   if (pqlvalue_isset(val)) {
      trace(ctx, "set of %u", pqlvalue_set_getnum(val));
   }
   else {
      trace_value(ctx, val);
   }
}

#define MAYBETRACE(ctx, ...)  ((ctx)->pql->dotrace ? __VA_ARGS__ : (void)0)

#define TRACE_INDENT(ctx)     MAYBETRACE(ctx, trace_indent(ctx))
#define TRACE_UNINDENT(ctx)   MAYBETRACE(ctx, trace_unindent(ctx))
#define TRACE(ctx, ...)       MAYBETRACE(ctx, trace(ctx, __VA_ARGS__))
#define TRACE_DUMP(ctx, te)   MAYBETRACE(ctx, trace_dump(ctx, te))
#define TRACE_VALUE(ctx, val) MAYBETRACE(ctx, trace_value(ctx, val))
#define TRACE_2VALUE(ctx, val1, val2) MAYBETRACE(ctx, trace_2value(ctx, val1, val2))
#define TRACE_2STRING(ctx, val1, val2) MAYBETRACE(ctx, trace_2string(ctx, val1, val2))
#define TRACE_DESCVALUE(ctx, val, desc) MAYBETRACE(ctx, trace_descvalue(ctx, val, desc))
#define TRACE_DESC2VALUE(ctx, val1, val2, desc) MAYBETRACE(ctx, trace_desc2value(ctx, val1, val2, desc))
#define TRACE_SHORTVALUE(ctx, val) MAYBETRACE(ctx, trace_shortvalue(ctx, val))

////////////////////////////////////////////////////////////
// column index tools

struct columnindexes {
   unsigned num;
   unsigned *list;
};

static void columnindexes_init(struct columnindexes *ci) {
   ci->num = 0;
   ci->list = NULL;
}

static void columnindexes_cleanup(struct eval *ctx,
				  struct columnindexes *ci) {
   if (ci->list != NULL) {
      dofree(ctx->pql, ci->list, ci->num * sizeof(*ci->list));
   }
}

static unsigned getcolumnindex(struct coltree *name, struct colname *col) {
   unsigned ret;

   if (!coltree_istuple(name)) {
      PQLASSERT(col == coltree_wholecolumn(name));
      return 0;
   }

   if (coltree_find(name, col, &ret) < 0) {
      PQLASSERT(!"Invalid column reference");
      return (unsigned)(-1);
   }
   return ret;
}

static void getcolumnindexes(struct eval *ctx,
			     struct columnindexes *ci,
			     struct coltree *name, struct colset *cols) {
   struct colname *col;
   unsigned i;

   PQLASSERT(ci->list == NULL);

   ci->num = colset_num(cols);
   ci->list = domalloc(ctx->pql, ci->num * sizeof(*ci->list));

   for (i=0; i<ci->num; i++) {
      col = colset_get(cols, i);
      ci->list[i] = getcolumnindex(name, col);
   }
}

static void columnindexes_one(struct eval *ctx,
			      struct columnindexes *ci,
			      struct coltree *name, struct colname *col) {
   PQLASSERT(ci->list == NULL);

   ci->num = 1;
   ci->list = domalloc(ctx->pql, ci->num * sizeof(*ci->list));
   ci->list[0] = getcolumnindex(name, col);
}

static void columnindexes_fixed(struct eval *ctx,
				struct columnindexes *ci, unsigned col) {
   PQLASSERT(ci->list == NULL);

   ci->num = 1;
   ci->list = domalloc(ctx->pql, ci->num * sizeof(*ci->list));
   ci->list[0] = col;
}

static void columnindexes_all(struct eval *ctx,
			      struct columnindexes *ci, unsigned num) {
   unsigned i;

   PQLASSERT(ci->list == NULL);

   ci->num = num;
   ci->list = domalloc(ctx->pql, ci->num * sizeof(*ci->list));
   for (i=0; i<ci->num; i++) {
      ci->list[i] = i;
   }
}

static int unsigned_sortfunc(const void *av, const void *bv) {
   unsigned a = *(const unsigned *)av;
   unsigned b = *(const unsigned *)bv;

   return (a < b) ? -1 : (a == b) ? 0 : 1;
}

static void columnindexes_sort(struct columnindexes *ci) {
   qsort(ci->list, ci->num, sizeof(*ci->list), unsigned_sortfunc);
}

static void columnindexes_complement(struct eval *ctx,
				     struct columnindexes *ci,
				     unsigned totnum) {
   unsigned i;
   unsigned *keep, numkeep, thiskeep;
   unsigned *skip, numskip, thisskip;

   columnindexes_sort(ci);

   numskip = ci->num;
   skip = ci->list;
   ci->num = 0;
   ci->list = NULL;

   PQLASSERT(numskip < totnum);

   numkeep = totnum - numskip;
   keep = domalloc(ctx->pql, numkeep * sizeof(*ci->list));
   thisskip = 0;
   thiskeep = 0;
   for (i=0; i<totnum; i++) {
      if (thisskip >= numskip || i < skip[thisskip]) {
	 keep[thiskeep++] = i;
      }
      else {
	 PQLASSERT(i == skip[thisskip]);
	 thisskip++;
      }
   }
   PQLASSERT(thisskip == numskip);
   PQLASSERT(thiskeep == numkeep);

   ci->num = numkeep;
   ci->list = keep;
}

////////////////////////////////////////////////////////////
// common/shared stuff

/*
 * Filter SET using PREDICATE.
 *
 * Note: consumes the set and returns a new one.
 * (Actually, modifies it in place, but that's equivalent.)
 */
static struct pqlvalue *dofilter(struct eval *ctx,
				 struct tcexpr *predicate,
				 struct pqlvalue *set) {
   unsigned i, num;
   const struct pqlvalue *subval;
   struct pqlvalue *result;

   TRACE(ctx, "filtering %u items", pqlvalue_set_getnum(set));
   TRACE_DUMP(ctx, predicate);
   TRACE_INDENT(ctx);

   PQLASSERT(pqlvalue_isset(set));
   num = pqlvalue_set_getnum(set);
   for (i=num; i-- > 0; ) {
      subval = pqlvalue_set_get(set, i);
      TRACE_VALUE(ctx, subval);
      result = lambda_eval(ctx, predicate, subval);
      TRACE_VALUE(ctx, result);
      PQLASSERT(pqlvalue_isnil(result) || pqlvalue_isbool(result));
      if (pqlvalue_isnil(result) ||
	  pqlvalue_bool_get(result) == false) {
	 pqlvalue_set_drop(set, i);
      }
      pqlvalue_destroy(result);
   }

   TRACE_UNINDENT(ctx);

   return set;
}

static struct pqlvalue *doproject_onetuple(struct eval *ctx,
					   const struct pqlvalue *tuple,
					   struct columnindexes *cols) {
   const struct pqlvalue *found;
   struct pqlvalue *ret;
   unsigned i;

   PQLASSERT(pqlvalue_istuple(tuple));
   if (cols->num == 1) {
      found = pqlvalue_tuple_get(tuple, cols->list[0]);
      ret = pqlvalue_clone(ctx->pql, found);
   }
   else {
      ret = pqlvalue_unit(ctx->pql);
      for (i=0; i<cols->num; i++) {
	 found = pqlvalue_tuple_get(tuple, cols->list[i]);
	 ret = pqlvalue_tuple_add(ret, pqlvalue_clone(ctx->pql, found));
      }
   }
   return ret;
}

static struct pqlvalue *doproject(struct eval *ctx,
				  const struct pqlvalue *subval,
				  struct columnindexes *cols) {
   struct pqlvalue *newtuple, *ret;
   unsigned i, num;

   if (!pqlvalue_isset(subval)) {
      TRACE(ctx, "project %u columns", cols->num);
      TRACE_INDENT(ctx);
      ret = doproject_onetuple(ctx, subval, cols);
      TRACE_VALUE(ctx, ret);
      TRACE_UNINDENT(ctx);
   }
   else {
      num = pqlvalue_set_getnum(subval);
      TRACE(ctx, "project %u columns from set of %u", cols->num, num);
      TRACE_INDENT(ctx);
      ret = pqlvalue_emptyset(ctx->pql);
      for (i=0; i<num; i++) {
	 newtuple = doproject_onetuple(ctx, pqlvalue_set_get(subval, i), cols);
	 TRACE_VALUE(ctx, newtuple);
	 pqlvalue_set_add(ret, newtuple);
      }
      TRACE_UNINDENT(ctx);
   }

   return ret;
}

static struct pqlvalue *dostrip_onetuple(struct eval *ctx,
					 struct pqlvalue *tuple,
					 struct columnindexes *cols) {
   unsigned i;

   (void)ctx;

   if (pqlvalue_istuple(tuple)) {
      for (i=cols->num; i-- > 0; ) {
	 tuple = pqlvalue_tuple_strip(tuple, cols->list[i]);
      }
   }
   else {
      pqlvalue_destroy(tuple);
      return pqlvalue_unit(ctx->pql);
   }

   return tuple;
}

static struct pqlvalue *dostrip(struct eval *ctx, struct pqlvalue *subval,
				struct columnindexes *cols) {
   struct pqlvalue *thisval;
   struct datatype *mytype;
   unsigned i, num;

   if (!pqlvalue_isset(subval)) {
      TRACE(ctx, "strip %u columns", cols->num);
      TRACE_INDENT(ctx);
      subval = dostrip_onetuple(ctx, subval, cols);
      TRACE_VALUE(ctx, subval);
      TRACE_UNINDENT(ctx);
   }
   else {
      num = pqlvalue_set_getnum(subval);
      TRACE(ctx, "strip %u columns from set of %u", cols->num, num);
      TRACE_INDENT(ctx);
      for (i=0; i<num; i++) {
	 thisval = pqlvalue_set_replace(subval, i, NULL, false);
	 thisval = dostrip_onetuple(ctx, thisval, cols);
	 pqlvalue_set_replace(subval, i, thisval, true);
	 TRACE_VALUE(ctx, thisval);
      }
      TRACE_UNINDENT(ctx);

      mytype = datatype_set_member(pqlvalue_datatype(subval));
      for (i=cols->num; i-- > 0; ) {
	 mytype = datatype_tuple_strip(ctx->pql, mytype, cols->list[i]);
      }
      mytype = datatype_set(ctx->pql, mytype);
      pqlvalue_set_updatetype(subval, mytype);
   }

   return subval;
}

static struct pqlvalue *dounnest(struct eval *ctx, struct pqlvalue *set,
				 unsigned targetcolindex,
				 struct datatype *resulttype) {
   struct pqlvalue *thistuple;
   const struct pqlvalue *target, *newtarget;
   const struct pqlvalue *unpacked, *subunpacked;
   unsigned i, j, k, num, arity;
   unsigned *counts;

   PQLASSERT(pqlvalue_isset(set));

   TRACE(ctx, "unnesting %u items on column %u", pqlvalue_set_getnum(set),
	 targetcolindex);
   TRACE_INDENT(ctx);

   /*
    * First find out how many values each element is going to expand
    * to.
    */

   num = pqlvalue_set_getnum(set);
   counts = domalloc(ctx->pql, num * sizeof(*counts));

   for (i=0; i<num; i++) {
      thistuple = pqlvalue_set_getformodify(set, i);
      PQLASSERT(pqlvalue_istuple(thistuple));

      target = pqlvalue_tuple_get(thistuple, targetcolindex);
      PQLASSERT(pqlvalue_isset(target));

      counts[i] = pqlvalue_set_getnum(target);
   }

   /*
    * Wedge in space for new values. The pqlvalue code does this
    * because it's representation-dependent.
    */
   pqlvalue_set_pryopen(ctx->pql, set, targetcolindex, counts);
   dofree(ctx->pql, counts, num * sizeof(*counts));

   /*
    * Now unpack the sets.
    */
   num = pqlvalue_set_getnum(set);
   target = NULL;
   j = 0;
   for (i=0; i<num; i++) {
      thistuple = pqlvalue_set_getformodify(set, i);
      PQLASSERT(pqlvalue_istuple(thistuple));

      newtarget = pqlvalue_tuple_get(thistuple, targetcolindex);
      if (newtarget != NULL) {
	 PQLASSERT(target == NULL || j == pqlvalue_set_getnum(target));
	 target = newtarget;
	 j = 0;
      }

      unpacked = pqlvalue_set_get(target, j++);
      if (pqlvalue_istuple(unpacked)) {
	 arity = pqlvalue_tuple_getarity(unpacked);
	 for (k=0; k<arity; k++) {
	    subunpacked = pqlvalue_tuple_get(unpacked, k);
	    thistuple = pqlvalue_tuple_add(thistuple,
			       pqlvalue_clone(ctx->pql, subunpacked));
	 }
      }
      else {
	 thistuple = pqlvalue_tuple_add(thistuple,
			    pqlvalue_clone(ctx->pql, unpacked));
      }
      pqlvalue_set_replace(set, i, thistuple, true);
   }

   for (i=0; i<num; i++) {
      thistuple = pqlvalue_set_getformodify(set, i);
      PQLASSERT(pqlvalue_istuple(thistuple));

      thistuple = pqlvalue_tuple_strip(thistuple, targetcolindex);
      pqlvalue_set_replace(set, i, thistuple, true); /* just in case */
      TRACE_VALUE(ctx, thistuple);
   }

   pqlvalue_set_updatetype(set, resulttype);

   TRACE_UNINDENT(ctx);
   return set;
}

static int column_compare(const struct pqlvalue *a, const struct pqlvalue *b,
			  const struct columnindexes *cols) {
   const struct pqlvalue *asub, *bsub;
   unsigned i;
   int ret;

   PQLASSERT(pqlvalue_istuple(a) == pqlvalue_istuple(b));

   if (!pqlvalue_istuple(a)) {
      PQLASSERT(cols->num == 1);
      PQLASSERT(cols->list[0] == 0);
      return pqlvalue_compare(a, b);
   }

   for (i=0; i<cols->num; i++) {
      asub = pqlvalue_tuple_get(a, cols->list[i]);
      bsub = pqlvalue_tuple_get(b, cols->list[i]);
      ret = pqlvalue_compare(asub, bsub);
      if (ret != 0) {
	 return ret;
      }
   }
   return 0;
}

/*
 * Roll our own qsort; we can't use the C library one because it
 * doesn't allow passing context through.
 */

/*
 * Sort the subrange SET[x] for low <= x < hi.
 */
static void doqsort(struct eval *ctx, struct pqlvalue *set,
		    unsigned low, unsigned hi,
		    struct pqlvaluearray *tmp,
		    const struct columnindexes *cols) {
   unsigned num, pivot, i, j, k;
   struct pqlvalue *val1, *val2;

   num = hi - low;
   if (num > 1) {
      pivot = num/2;
      doqsort(ctx, set, low, low+pivot, tmp, cols);
      doqsort(ctx, set, low+pivot, hi, tmp, cols);

      for (i=0; i<num; i++) {
	 val1 = pqlvalue_set_replace(set, low + i, NULL, false);
	 pqlvaluearray_set(tmp, low + i, val1);
      }

      i = 0;
      j = pivot;
      k = 0;
      while (i<pivot && j<num) {
	 val1 = pqlvaluearray_get(tmp, low + i);
	 val2 = pqlvaluearray_get(tmp, low + j);
	 if (column_compare(val1, val2, cols) < 0) {
	    pqlvalue_set_replace(set, low + k, val1, false);
	    i++;
	 }
	 else {
	    pqlvalue_set_replace(set, low + k, val2, false);
	    j++;
	 }
	 k++;
      }
      while (i<pivot) {
	 val1 = pqlvaluearray_get(tmp, low + i);
	 pqlvalue_set_replace(set, low + k, val1, false);
	 i++;
	 k++;
      }
      while (j<num) {
	 val2 = pqlvaluearray_get(tmp, low + j);
	 pqlvalue_set_replace(set, low + k, val2, false);
	 j++;
	 k++;
      }

      PQLASSERT(k == num);
   }
   else {
      PQLASSERT(num > 0);
      /* one element is always sorted */
   }
}

static struct pqlvalue *dosort(struct eval *ctx, struct pqlvalue *set,
			       const struct columnindexes *cols) {
   unsigned num;
   struct pqlvaluearray tmp;

   (void)ctx;

   PQLASSERT(pqlvalue_isset(set));
   num = pqlvalue_set_getnum(set);

   if (num > 1) {
      pqlvaluearray_init(&tmp);
      pqlvaluearray_setsize(ctx->pql, &tmp, num);

      doqsort(ctx, set, 0, num, &tmp, cols);

      pqlvaluearray_setsize(ctx->pql, &tmp, 0);
      pqlvaluearray_cleanup(ctx->pql, &tmp);
   }

   set = pqlvalue_set_to_sequence(ctx->pql, set);
   return set;
}

static struct pqlvalue *douniq(struct eval *ctx, struct pqlvalue *set,
			       const struct columnindexes *cols) {
   unsigned i, num;
   const struct pqlvalue *prev, *here;

   (void)ctx;

   // XXX theoretically only sequences can be uniq'd
   PQLASSERT(pqlvalue_isset(set) || pqlvalue_issequence(set));
   num = pqlvalue_coll_getnum(set);
   prev = NULL;
   for (i=num; i-- > 0; ) {
      here = pqlvalue_coll_get(set, i);
      if (prev != NULL && column_compare(prev, here, cols) == 0) {
	 pqlvalue_coll_drop(set, i);
      }
      else {
	 prev = here;
      }
   }

   return set;
}


////////////////////////////////////////////////////////////
// type conversion tools

int convert_to_bool(const struct pqlvalue *val, bool *ret) {
   if (pqlvalue_isbool(val)) {
      *ret = pqlvalue_bool_get(val);
      return 0;
   }

   if (pqlvalue_isint(val)) {
      *ret = pqlvalue_int_get(val) != 0;
      return 0;
   }

   if (pqlvalue_isfloat(val)) {
      *ret = pqlvalue_float_get(val) != 0.0;
      return 0;
   }

   if (pqlvalue_isstring(val)) {
      const char *str;

      str = pqlvalue_string_get(val);

      if (!strcasecmp(str, "true") ||
	  !strcasecmp(str, "yes") ||
	  !strcasecmp(str, "on") ||
	  !strcasecmp(str, "1")) {
	 *ret = true;
	 return 0;
      }
      if (!strcasecmp(str, "false") ||
	  !strcasecmp(str, "no") ||
	  !strcasecmp(str, "off") ||
	  !strcasecmp(str, "0")) {
	 *ret = false;
	 return 0;
      }
   }

   return -1;
}

int convert_to_number(const struct pqlvalue *val,
		      int *ret_int, double *ret_float,
		      bool *ret_isfloat) {

#if 0 // XXX: do we accept bool?
   if (pqlvalue_isbool(val)) {
      *ret_int = pqlvalue_bool_get(val) ? 1 : 0;
      *ret_float = *ret_int;
      *ret_isfloat = false;
      return 0;
   }
#endif

   if (pqlvalue_isint(val)) {
      *ret_int = pqlvalue_int_get(val);
      *ret_float = *ret_int;
      *ret_isfloat = false;
      return 0;
   }
   if (pqlvalue_isfloat(val)) {
      *ret_float = pqlvalue_float_get(val);
      *ret_int = *ret_float;
      *ret_isfloat = true;
      return 0;
   }

   if (pqlvalue_isstring(val)) {
      const char *str;
      char *t;
      long lval;
      double fval;

      str = pqlvalue_string_get(val);

      /* is it an integer? */
      errno = 0;
      lval = strtol(str, &t, 0);
#if INT_MAX < LONG_MAX
      if (lval > INT_MAX || lval < INT_MIN) {
	 errno = ERANGE;
      }
#endif
      if (*t == 0 && errno == 0) {
	 /* conversion succeeded */
	 *ret_int = lval;
	 *ret_float = *ret_int;
	 *ret_isfloat = false;
	 return 0;
      }

      errno = 0;
      fval = strtod(str, &t);
      if (*t == 0 && errno == 0) {
	 /* conversion succeeded */
	 *ret_float = fval;
	 *ret_int = *ret_float;
	 *ret_isfloat = true;
	 return 0;
      }

      /* oh well... */
   }

   return -1;
}

/*
 * XXX how aggressive is this supposed to be? For now we'll use
 * tostring, but I don't think we necessary want to do that in
 * all cases, like say sets or pathelements. (On the other hand,
 * the typechecker should prevent those from happening...)
 */
static struct pqlvalue *convert_to_string(struct pqlcontext *pql,
					  const struct pqlvalue *val) {
   return pqlvalue_tostring(pql, val);
}

////////////////////////////////////////////////////////////
// operators/builtin functions

/*
 * Set functions
 */

/*
 * Common code for union/intersect/except/subset/elementof.
 *
 * If EXCLUSION is not null, elements of HAYSTACK whose corresponding
 * entry in EXCLUSION is true are skipped, and elements that are
 * matched have this entry set to true upon return. This causes each
 * distinct member to be matched no more than once.
 *
 * XXX would be nice to be able to binary search if sorted
 * XXX what does this do with nil?
 */
static bool set_ismember(const struct pqlvalue *haystack,
			 const struct pqlvalue *needle,
			 bool *exclusion) {
   struct datatype *haystacktype, *needletype;
   const struct pqlvalue *subval;
   unsigned i, num;

   haystacktype = pqlvalue_datatype(haystack);
   needletype = pqlvalue_datatype(needle);

   PQLASSERT(datatype_isset(haystacktype));
   // XXX this fails due to polymorphism; a comprehensive solution is needed
   //PQLASSERT(datatype_eq(needletype, datatype_set_member(haystacktype)));

   // theoretically redundant
   PQLASSERT(pqlvalue_isset(haystack));

   num = pqlvalue_set_getnum(haystack);
   for (i=0; i<num; i++) {
      if (exclusion != NULL && exclusion[i]) {
	 continue;
      }
      subval = pqlvalue_set_get(haystack, i);
      if (pqlvalue_eq(needle, subval)) {
	 if (exclusion != NULL) {
	    exclusion[i] = true;
	 }
	 return true;
      }
   }
   return false;
}

/*
 * Union. This sucks. Among other things we should (upstream) insert a
 * suitable sort operation if we aren't already, and then arrange to
 * know what the order is here, so we can do this by scanning both
 * sets instead of a nested loop. Or whatever. XXX
 */
static struct pqlvalue *func_union(struct eval *ctx,
				   const struct pqlvalue *left,
				   const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (!set_ismember(ret, subval, NULL)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }
   for (i=0; i<rnum; i++) {
      subval = pqlvalue_set_get(right, i);
      if (!set_ismember(ret, subval, NULL)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }

   return ret;
}

/*
 * This could be done better too.
 */
static struct pqlvalue *func_intersect(struct eval *ctx,
				       const struct pqlvalue *left,
				       const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (set_ismember(right, subval, NULL)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }

   return ret;
}

/*
 * This could be done better too.
 */
static struct pqlvalue *func_except(struct eval *ctx,
				    const struct pqlvalue *left,
				    const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (!set_ismember(right, subval, NULL)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }

   return ret;
}

/*
 * Union all.
 */
static struct pqlvalue *func_unionall(struct eval *ctx,
				      const struct pqlvalue *left,
				      const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
   }
   for (i=0; i<rnum; i++) {
      subval = pqlvalue_set_get(right, i);
      pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
   }

   return ret;
}

/*
 * XXX this sucks too.
 */
static struct pqlvalue *func_intersectall(struct eval *ctx,
					  const struct pqlvalue *left,
					  const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;
   bool *used;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   used = domalloc(ctx->pql, rnum * sizeof(bool));
   for (i=0; i<rnum; i++) {
      used[i] = false;
   }

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (set_ismember(right, subval, used)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }

   dofree(ctx->pql, used, rnum * sizeof(bool));

   return ret;
}

/*
 * XXX this sucks too.
 */
static struct pqlvalue *func_exceptall(struct eval *ctx,
				       const struct pqlvalue *left,
				       const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   struct pqlvalue *ret;
   unsigned lnum, rnum, i;
   bool *used;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   used = domalloc(ctx->pql, rnum * sizeof(bool));
   for (i=0; i<rnum; i++) {
      used[i] = false;
   }

   ret = pqlvalue_emptyset(ctx->pql);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (!set_ismember(right, subval, used)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, subval));
      }
   }

   dofree(ctx->pql, used, rnum * sizeof(bool));

   return ret;
}

static struct pqlvalue *func_subsetof(struct eval *ctx,
				      const struct pqlvalue *left,
				      const struct pqlvalue *right) {
   const struct pqlvalue *subval;
   unsigned lnum, rnum, i;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   lnum = pqlvalue_set_getnum(left);
   rnum = pqlvalue_set_getnum(right);

   for (i=0; i<lnum; i++) {
      subval = pqlvalue_set_get(left, i);
      if (!set_ismember(right, subval, NULL)) {
	 return pqlvalue_bool(ctx->pql, false);
      }
   }

   return pqlvalue_bool(ctx->pql, true);
}

static struct pqlvalue *func_elementof(struct eval *ctx,
				       const struct pqlvalue *left,
				       const struct pqlvalue *right) {
   PQLASSERT(pqlvalue_isset(right));
   return pqlvalue_bool(ctx->pql, set_ismember(right, left, NULL));
}

static struct pqlvalue *func_nonempty(struct eval *ctx,
				      const struct pqlvalue *arg) {
   PQLASSERT(pqlvalue_isset(arg));
   return pqlvalue_bool(ctx->pql, pqlvalue_set_getnum(arg) > 0);
}

static struct pqlvalue *func_makeset(struct eval *ctx,
				     const struct pqlvalue *arg) {
   struct pqlvalue *ret;

   ret = pqlvalue_emptyset(ctx->pql);
   pqlvalue_set_add(ret, pqlvalue_clone(ctx->pql, arg));
   return ret;
}

static struct pqlvalue *func_getelement(struct eval *ctx,
					const struct pqlvalue *arg) {
   PQLASSERT(pqlvalue_isset(arg));
   if (pqlvalue_set_getnum(arg) == 1) {
      return pqlvalue_clone(ctx->pql, pqlvalue_set_get(arg, 0));
   }
   else {
      // XXX post a warning?
      return pqlvalue_nil(ctx->pql);
   }
}

/*
 * Aggregator functions
 */

static struct pqlvalue *func_count(struct eval *ctx,
				   const struct pqlvalue *arg) {
   PQLASSERT(pqlvalue_isset(arg));
   return pqlvalue_int(ctx->pql, pqlvalue_set_getnum(arg));
}

static int pqlvalue_sum(const struct pqlvalue *arg,
			int *int_ret, double *float_ret, bool *isfloat_ret) {
   const struct pqlvalue *subval;
   int thisint, totint;
   double thisfloat, totfloat;
   bool thisisfloat, havefloat;
   unsigned i, num;

   totint = 0;
   totfloat = 0.0;
   havefloat = false;

   PQLASSERT(pqlvalue_isset(arg));
   num = pqlvalue_set_getnum(arg);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(arg, i);
      if (convert_to_number(subval, &thisint, &thisfloat, &thisisfloat) < 0) {
	 /* oops, there isn't a well-defined sum */
	 return -1;
      }
      if (thisisfloat) {
	 totfloat += thisfloat;
	 havefloat = true;
      }
      else {
	 totint += thisint;
      }
   }

   if (havefloat) {
      *int_ret = totint + totfloat;
      *float_ret = totint + totfloat;
      *isfloat_ret = true;
   }
   else {
      *int_ret = totint;
      *float_ret = totint;
      *isfloat_ret = false;
   }
   
   return 0;
} 

static struct pqlvalue *func_sum(struct eval *ctx, const struct pqlvalue *arg){
   double fsum;
   int isum;
   bool havefloat;

   if (pqlvalue_sum(arg, &isum, &fsum, &havefloat) < 0) {
      return pqlvalue_nil(ctx->pql);
   }

   if (havefloat) {
      return pqlvalue_float(ctx->pql, fsum);
   }
   return pqlvalue_int(ctx->pql, isum);
}

static struct pqlvalue *func_avg(struct eval *ctx, const struct pqlvalue *arg){
   unsigned num;
   double fsum;
   int isum;
   bool havefloat;

   PQLASSERT(pqlvalue_isset(arg));

   num = pqlvalue_set_getnum(arg);
   if (num == 0) {
      return pqlvalue_float(ctx->pql, 0.0);
   }

   if (pqlvalue_sum(arg, &isum, &fsum, &havefloat) < 0) {
      return pqlvalue_nil(ctx->pql);
   }
   (void)isum;
   (void)havefloat;

   return pqlvalue_float(ctx->pql, fsum / num);
}

static struct pqlvalue *func_min(struct eval *ctx, const struct pqlvalue *arg){
   const struct pqlvalue *subval;
   unsigned i, num;
   int imin, icur;
   double fmin, fcur;
   bool haveint, havefloat, gotfloat;

   imin = 0.0; // gcc 4.1
   fmin = 0.0; // gcc 4.1

   haveint = false;
   havefloat = false;

   PQLASSERT(pqlvalue_isset(arg));
   num = pqlvalue_set_getnum(arg);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(arg, i);
      if (convert_to_number(subval, &icur, &fcur, &gotfloat) < 0) {
	 /* oops, there isn't a well-defined value */
	 return pqlvalue_nil(ctx->pql);
      }
      if (gotfloat) {
	 if (!havefloat) {
	    havefloat = true;
	    fmin = fcur;
	 }
	 else if (fcur < fmin) {
	    fmin = fcur;
	 }
      }
      else {
	 if (!haveint) {
	    haveint = true;
	    imin = icur;
	 }
	 else if (icur < imin) {
	    imin = icur;
	 }
      }
   }

   if (haveint && havefloat) {
      return pqlvalue_float(ctx->pql, fmin < imin ? fmin : imin);
   }
   if (havefloat) {
      return pqlvalue_float(ctx->pql, fmin);
   }
   if (haveint) {
      return pqlvalue_int(ctx->pql, imin);
   }
   return pqlvalue_nil(ctx->pql);
}

/*
 * Would be nice to combine this and min.
 */
static struct pqlvalue *func_max(struct eval *ctx, const struct pqlvalue *arg){
   const struct pqlvalue *subval;
   unsigned i, num;
   int imin, icur;
   double fmin, fcur;
   bool haveint, havefloat, gotfloat;

   imin = 0.0; // gcc 4.1
   fmin = 0.0; // gcc 4.1

   haveint = false;
   havefloat = false;

   PQLASSERT(pqlvalue_isset(arg));
   num = pqlvalue_set_getnum(arg);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(arg, i);
      if (convert_to_number(subval, &icur, &fcur, &gotfloat) < 0) {
	 /* oops, there isn't a well-defined value */
	 return pqlvalue_nil(ctx->pql);
      }
      if (gotfloat) {
	 if (!havefloat) {
	    havefloat = true;
	    fmin = fcur;
	 }
	 else if (fcur > fmin) {
	    fmin = fcur;
	 }
      }
      else {
	 if (!haveint) {
	    haveint = true;
	    imin = icur;
	 }
	 else if (icur > imin) {
	    imin = icur;
	 }
      }
   }

   if (haveint && havefloat) {
      return pqlvalue_float(ctx->pql, fmin < imin ? fmin : imin);
   }
   if (havefloat) {
      return pqlvalue_float(ctx->pql, fmin);
   }
   if (haveint) {
      return pqlvalue_int(ctx->pql, imin);
   }
   return pqlvalue_nil(ctx->pql);
}

static struct pqlvalue *func_alltrue(struct eval *ctx,
				     const struct pqlvalue *arg) {
   const struct pqlvalue *subval;
   unsigned i, num;
   bool t;

   PQLASSERT(pqlvalue_isset(arg));
   num = pqlvalue_set_getnum(arg);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(arg, i);
      if (convert_to_bool(subval, &t) < 0) {
	 return pqlvalue_nil(ctx->pql);
      }
      if (!t) {
	 return pqlvalue_bool(ctx->pql, false);
      }
   }
   return pqlvalue_bool(ctx->pql, true);
}

static struct pqlvalue *func_anytrue(struct eval *ctx,
				     const struct pqlvalue *arg) {
   const struct pqlvalue *subval;
   unsigned i, num;
   bool t;

   PQLASSERT(pqlvalue_isset(arg));
   num = pqlvalue_set_getnum(arg);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(arg, i);
      if (convert_to_bool(subval, &t) < 0) {
	 return pqlvalue_nil(ctx->pql);
      }
      if (t) {
	 return pqlvalue_bool(ctx->pql, true);
      }
   }
   return pqlvalue_bool(ctx->pql, false);
}

/*
 * Boolean functions
 */
static struct pqlvalue *func_and(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   bool lt, rt, lnil, rnil;

   lnil = convert_to_bool(left, &lt);
   rnil = convert_to_bool(right, &rt);

   if (lnil || rnil) {
      return pqlvalue_nil(ctx->pql);
   }
   return pqlvalue_bool(ctx->pql, lt && rt);
}

static struct pqlvalue *func_or(struct eval *ctx, const struct pqlvalue *left,
				const struct pqlvalue *right) {
   bool ret, lt, rt, lnil, rnil;

   lnil = convert_to_bool(left, &lt) < 0;
   rnil = convert_to_bool(right, &rt) < 0;

   if (lnil && rnil) {
      return pqlvalue_nil(ctx->pql);
   }

   if (lnil) {
      ret = rt;
   }
   else if (rnil) {
      ret = lt;
   }
   else {
      ret = lt || rt;
   }
   return pqlvalue_bool(ctx->pql, ret);
}

static struct pqlvalue *func_not(struct eval *ctx, const struct pqlvalue *arg){
   bool t;

   if (convert_to_bool(arg, &t) < 0) {
      return pqlvalue_nil(ctx->pql);
   }
   return pqlvalue_bool(ctx->pql, !t);
}

/* 
 * Object functions
 */

static struct pqlvalue *func_new(struct eval *ctx, const struct pqlvalue *arg){
   struct pqlvalue *newobj;
   struct pqlvalue *edgename;
   unsigned i, num;

   if (pqlvalue_isbool(arg) ||
       pqlvalue_isint(arg) ||
       pqlvalue_isfloat(arg) ||
       pqlvalue_isstring(arg) ||
       pqlvalue_isdistinguisher(arg)) {
      /* primitive values are unchanged by new */
      return pqlvalue_clone(ctx->pql, arg);
   }

   newobj = ctx->pql->ops->newobject(ctx->pql);

   if (pqlvalue_isstruct(arg)) {
      /* clone the struct */
      struct pqlvalue *set;
      const struct pqlvalue *pair, *edgename, *rightobj;

      set = ctx->pql->ops->followall(ctx->pql, arg, false);
      PQLASSERT(pqlvalue_isset(set));
      num = pqlvalue_set_getnum(set);
      for (i=0; i<num; i++) {
	 pair = pqlvalue_set_get(set, i);
	 PQLASSERT(pqlvalue_istuple(pair));
	 edgename = pqlvalue_tuple_get(pair, 0);
	 rightobj = pqlvalue_tuple_get(pair, 1);
	 if (ctx->pql->ops->assign(ctx->pql, newobj, edgename, rightobj) < 0) {
	    goto flail;
	 }
      }
      pqlvalue_destroy(set);
   }
   else if (pqlvalue_ispathelement(arg)) {
      edgename = pqlvalue_string(ctx->pql, "leftobj");
      if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				pqlvalue_pathelement_getleftobj(arg)) < 0) {
	 goto flail;
      }
      pqlvalue_destroy(edgename);

      edgename = pqlvalue_string(ctx->pql, "edgename");
      if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				pqlvalue_pathelement_getedgename(arg)) < 0) {
	 goto flail;
      }
      pqlvalue_destroy(edgename);

      edgename = pqlvalue_string(ctx->pql, "rightobj");
      if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				pqlvalue_pathelement_getrightobj(arg)) < 0) {
	 goto flail;
      }
      pqlvalue_destroy(edgename);
   }
   else if (pqlvalue_istuple(arg)) {
      // XXX all wrong, need the column/edge name from the tcexpr.
      //
      // Which we can't get because the tcexpr maybe got eval'd a long
      // time back. I guess this means we need column names on tuple
      // values? Bleh. think about it...
      //
      // Also I think we need to have values of type splatter...

      edgename = pqlvalue_string(ctx->pql, "default");
      num = pqlvalue_tuple_getarity(arg);
      for (i=0; i<num; i++) {
	 if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				   pqlvalue_tuple_get(arg, i)) < 0) {
	    goto flail;
	 }
      }
      pqlvalue_destroy(edgename);
   }
   else if (pqlvalue_islambda(arg)) {
      PQLASSERT(!"tried to new() a loose lambda expression");
   }
   else if (pqlvalue_isset(arg)) {
      /* what should the edge name be here, anyway? */
      edgename = pqlvalue_string(ctx->pql, "default");
      num = pqlvalue_set_getnum(arg);
      for (i=0; i<num; i++) {
	 if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				   pqlvalue_set_get(arg, i)) < 0) {
	    goto flail;
	 }
      }
      pqlvalue_destroy(edgename);
   }
   else if (pqlvalue_issequence(arg)) {
      num = pqlvalue_set_getnum(arg);
      for (i=0; i<num; i++) {
	 edgename = pqlvalue_int(ctx->pql, i);
	 if (ctx->pql->ops->assign(ctx->pql, newobj, edgename,
				   pqlvalue_sequence_get(arg, i)) < 0) {
	    goto flail;
	 }
	 pqlvalue_destroy(edgename);
      }
   }
   else {
      // value has dud type
      PQLASSERT(0);
   }

   return newobj;

 flail:
   /* now what? XXX */
   PQLASSERT(!"backend assign failed, sorry can't cope yet");
   return NULL;
}

/*
 * Time functions
 */

static struct pqlvalue *func_ctime(struct eval *ctx) {
   time_t timer;
   const char *str;

   time(&timer);
   str = ctime(&timer);
   return pqlvalue_string(ctx->pql, str);
}

/*
 * Comparison functions
 */

/*
 * Equality. Go to pqlvalue_eq().
 */
static struct pqlvalue *func_eq(struct eval *ctx, const struct pqlvalue *left,
				const struct pqlvalue *right) {
   // XXX is this what we want?
   if (pqlvalue_isnil(left) || pqlvalue_isnil(right)) {
      return pqlvalue_nil(ctx->pql);
   }

   return pqlvalue_bool(ctx->pql, pqlvalue_eq(left, right));
}

/*
 * Less-than.
 *
 * The following are the basic forms:
 *
 *    int < int
 *    float < float
 *    string < string
 *
 * We can promote int to float, and string to either float or int if it
 * has the right form.
 *
 * Comparisons on other types are false. Comparison of nil is nil.
 */
 
static struct pqlvalue *func_lt(struct eval *ctx,
				const struct pqlvalue *left,
				const struct pqlvalue *right) {
   double leftfloat, rightfloat;
   int leftint, rightint;
   bool leftisfloat, rightisfloat;
   bool ret;

   if (pqlvalue_isnil(left) || pqlvalue_isnil(right)) {
      return pqlvalue_nil(ctx->pql);
   }

   if (pqlvalue_isstring(left) && pqlvalue_isstring(right)) {
      ret = strcmp(pqlvalue_string_get(left), pqlvalue_string_get(right)) < 0;
   }
   else if (convert_to_number(left, &leftint, &leftfloat, &leftisfloat) < 0 ||
	    convert_to_number(right,&rightint,&rightfloat,&rightisfloat) < 0) {
      ret = false;
   }
   else if (!leftisfloat && !rightisfloat) {
      ret = leftint < rightint;
   }
   else {
      ret = leftfloat < rightfloat;
   }
   return pqlvalue_bool(ctx->pql, ret);
}

static struct pqlvalue *func_like(struct eval *ctx,
				  const struct pqlvalue *left,
				  const struct pqlvalue *right) {
   struct pqlvalue *leftstr, *rightstr;
   const char *txt;
   size_t i, j, len;
   char *glob;
   bool esc, ret;
   int ch;

   if (pqlvalue_isnil(left) || pqlvalue_isnil(right)) {
      return pqlvalue_nil(ctx->pql);
   }

   leftstr = convert_to_string(ctx->pql, left);
   rightstr = convert_to_string(ctx->pql, right);

   if (pqlvalue_isnil(leftstr) || pqlvalue_isnil(rightstr)) {
      pqlvalue_destroy(leftstr);
      pqlvalue_destroy(rightstr);
      return pqlvalue_nil(ctx->pql);
   }

   /*
    * In a "like" expression, _ matches any one character and % matches
    * zero or more characters, and \\ is the escape character. Convert
    * to a glob.
    */

   txt = pqlvalue_string_get(rightstr);
   len = strlen(txt);
   glob = domalloc(ctx->pql, len * 2 + 1);

   esc = false;
   for (i=j=0; i<len; i++) {
      ch = txt[i];
      if (ch == '\\' && !esc) {
	 esc = true;
	 continue;
      }
      if (ch == '_' && !esc) {
	 ch = '?';
      }
      else if (ch == '%' && !esc) {
	 ch = '*';
      }
      else if (ch == '*' || ch == '?' || ch == '[' || ch == ']') {
	 glob[j++] = '\\';
      }
      if (esc) {
	 glob[j++] = '\\';
      }
      glob[j++] = ch;
      esc = false;
   }
   glob[j] = 0;
   PQLASSERT(j < len * 2);

   TRACE_2STRING(ctx, pqlvalue_string_get(leftstr), glob);

   ret = fnmatch(glob, pqlvalue_string_get(leftstr), 0) == 0;

   dofree(ctx->pql, glob, len * 2 + 1);
   pqlvalue_destroy(leftstr);
   pqlvalue_destroy(rightstr);
   return pqlvalue_bool(ctx->pql, ret);
}

static struct pqlvalue *func_glob(struct eval *ctx,
				  const struct pqlvalue *left,
				  const struct pqlvalue *right) {
   struct pqlvalue *leftstr, *rightstr;
   bool ret;

   if (pqlvalue_isnil(left) || pqlvalue_isnil(right)) {
      return pqlvalue_nil(ctx->pql);
   }

   leftstr = convert_to_string(ctx->pql, left);
   rightstr = convert_to_string(ctx->pql, right);

   if (pqlvalue_isnil(leftstr) || pqlvalue_isnil(rightstr)) {
      pqlvalue_destroy(leftstr);
      pqlvalue_destroy(rightstr);
      return pqlvalue_nil(ctx->pql);
   }

   TRACE_2VALUE(ctx, leftstr, rightstr);

   ret = fnmatch(pqlvalue_string_get(right),
		 pqlvalue_string_get(left), 0) == 0;

   pqlvalue_destroy(leftstr);
   pqlvalue_destroy(rightstr);
   return pqlvalue_bool(ctx->pql, ret);
}

/*
 * XXX we should figure out how to run regcomp once if we're going to
 * do this on a whole list of results.
 *
 * XXX also if regcmp fails because the regexp is invalid, we ought to
 * kvetch at query compile time.
 */
static struct pqlvalue *func_grep(struct eval *ctx,
				  const struct pqlvalue *left,
				  const struct pqlvalue *right) {
   struct pqlvalue *leftstr, *rightstr;
   const char *pattern, *text;
   regex_t rx;
   int result;

   if (pqlvalue_isnil(left) || pqlvalue_isnil(right)) {
      return pqlvalue_nil(ctx->pql);
   }

   leftstr = convert_to_string(ctx->pql, left);
   rightstr = convert_to_string(ctx->pql, right);

   if (pqlvalue_isnil(leftstr) || pqlvalue_isnil(rightstr)) {
      pqlvalue_destroy(leftstr);
      pqlvalue_destroy(rightstr);
      return pqlvalue_nil(ctx->pql);
   }

   pattern = pqlvalue_string_get(rightstr);
   text = pqlvalue_string_get(leftstr);

   result = regcomp(&rx, pattern, REG_EXTENDED|REG_NOSUB);
   if (result == 0) {
      result = regexec(&rx, text, 0, NULL, 0);
   }
   regfree(&rx);

   pqlvalue_destroy(leftstr);
   pqlvalue_destroy(rightstr);

   if (result == 0) {
      return pqlvalue_bool(ctx->pql, true);
   }
   if (result == REG_NOMATCH) {
      return pqlvalue_bool(ctx->pql, false);
   }
   return pqlvalue_nil(ctx->pql);
}

static struct pqlvalue *func_soundex(struct eval *ctx,
				     const struct pqlvalue *left,
				     const struct pqlvalue *right) {
   struct pqlvalue *leftstr, *rightstr;

   leftstr = convert_to_string(ctx->pql, left);
   rightstr = convert_to_string(ctx->pql, right);

   if (pqlvalue_isnil(leftstr) || pqlvalue_isnil(rightstr)) {
      pqlvalue_destroy(leftstr);
      pqlvalue_destroy(rightstr);
      return pqlvalue_nil(ctx->pql);
   }

   PQLASSERT(!"no soundex implementation");

   pqlvalue_destroy(leftstr);
   pqlvalue_destroy(rightstr);

   return pqlvalue_nil(ctx->pql);
}

/*
 * String functions
 */

/* (none here - use pqlvalue_tostring for tostring) */


/*
 * String and sequence functions
 */

/*
 * XXX we should arrange upstream to have a ++= version so we can
 * append to LEFT in place when that's appropriate.
 *
 * XXX the sequence and string versions should be separated out
 * upstream of here.
 */
static struct pqlvalue *func_concat(struct eval *ctx,
				    const struct pqlvalue *left,
				    const struct pqlvalue *right) {
   struct pqlvalue *ret, *tmp;

   if (pqlvalue_isnil(left)) {
      ret = pqlvalue_clone(ctx->pql, right);
      if (pqlvalue_ispathelement(ret)) {
	 tmp = pqlvalue_emptysequence(ctx->pql);
	 pqlvalue_sequence_add(tmp, ret);
	 ret = tmp;
      }
      return ret; 
   }
   if (pqlvalue_isnil(right)) {
      ret = pqlvalue_clone(ctx->pql, left);
      if (pqlvalue_ispathelement(ret)) {
	 tmp = pqlvalue_emptysequence(ctx->pql);
	 pqlvalue_sequence_add(tmp, ret);
	 ret = tmp;
      }
      return ret;
   }

   if ((pqlvalue_issequence(left) || pqlvalue_ispathelement(left)) && 
       (pqlvalue_issequence(right) || pqlvalue_ispathelement(right))) {
      const struct pqlvalue *subval;
      unsigned i, num;

      ret = pqlvalue_emptysequence(ctx->pql);

      if (pqlvalue_issequence(left)) {
	 num = pqlvalue_sequence_getnum(left);
	 for (i=0; i<num; i++) {
	    subval = pqlvalue_sequence_get(left, i);
	    pqlvalue_sequence_add(ret, pqlvalue_clone(ctx->pql, subval));
	 }
      }
      else {
	 pqlvalue_sequence_add(ret, pqlvalue_clone(ctx->pql, left));
      }

      if (pqlvalue_issequence(right)) {
	 num = pqlvalue_sequence_getnum(right);
	 for (i=0; i<num; i++) {
	    subval = pqlvalue_sequence_get(right, i);
	    pqlvalue_sequence_add(ret, pqlvalue_clone(ctx->pql, subval));
	 }
      }
      else {
	 pqlvalue_sequence_add(ret, pqlvalue_clone(ctx->pql, right));
      }
   }
   else {
      struct pqlvalue *leftstr, *rightstr;
      const char *l, *r;
      char *s;

      leftstr = convert_to_string(ctx->pql, left);
      rightstr = convert_to_string(ctx->pql, right);

      if (pqlvalue_isnil(leftstr) || pqlvalue_isnil(rightstr)) {
	 pqlvalue_destroy(leftstr);
	 pqlvalue_destroy(rightstr);
	 return pqlvalue_nil(ctx->pql);
      }

      l = pqlvalue_string_get(leftstr);
      r = pqlvalue_string_get(rightstr);
      s = domalloc(ctx->pql, strlen(l) + strlen(r) + 1);
      strcpy(s, l);
      strcat(s, r);
      ret = pqlvalue_string_consume(ctx->pql, s);

      pqlvalue_destroy(leftstr);
      pqlvalue_destroy(rightstr);
   }

   return ret;
}

/*
 * Nil functions
 */
static struct pqlvalue *func_choose(struct eval *ctx,
				    const struct pqlvalue *left,
				    const struct pqlvalue *right) {
   if (pqlvalue_isnil(left)) {
      return pqlvalue_clone(ctx->pql, right);
   }
   PQLASSERT(pqlvalue_isnil(right));
   return pqlvalue_clone(ctx->pql, left);
}

/*
 * Numeric functions
 */

static struct pqlvalue *numberop(struct eval *ctx,
				 const struct pqlvalue *left,
				 const struct pqlvalue *right,
				 bool inhibit_zero_on_right,
				 double (*f_func)(double, double),
				 int (*i_func)(int, int)) {
   int leftint, rightint;
   double leftfloat, rightfloat;
   bool leftisfloat, rightisfloat;

   if (pqlvalue_isnil(left) || pqlvalue_isnil(right) ||
       convert_to_number(left, &leftint, &leftfloat, &leftisfloat) < 0 ||
       convert_to_number(right, &rightint, &rightfloat, &rightisfloat) < 0) {
      return pqlvalue_nil(ctx->pql);
   }

   if (inhibit_zero_on_right) {
      if (rightisfloat ? (rightfloat == 0.0) : (rightint == 0)) {
	 return pqlvalue_nil(ctx->pql);
      }
   }

   if (!leftisfloat && !rightisfloat) {
      return pqlvalue_int(ctx->pql, i_func(leftint, rightint));
   }
   else {
      return pqlvalue_float(ctx->pql, f_func(leftfloat, rightfloat));
   }
}

#define MAKEOP(NAME, OP) \
     static double float_##NAME(double a, double b) { return a OP b; } \
     static int int_##NAME(int a, int b) { return a OP b; }

MAKEOP(add, +);
MAKEOP(sub, -);
MAKEOP(mul, *);
MAKEOP(div, /);

static double float_mod(double a, double b) { return fmod(a, b); }
static int int_mod(int a, int b) { return a % b; }

static struct pqlvalue *func_add(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   return numberop(ctx, left, right, false, float_add, int_add);
}

static struct pqlvalue *func_sub(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   return numberop(ctx, left, right, false, float_sub, int_sub);
}

static struct pqlvalue *func_mul(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   return numberop(ctx, left, right, false, float_mul, int_mul);
}

static struct pqlvalue *func_div(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   return numberop(ctx, left, right, true, float_div, int_div);
}

static struct pqlvalue *func_mod(struct eval *ctx, const struct pqlvalue *left,
				 const struct pqlvalue *right) {
   return numberop(ctx, left, right, true, float_mod, int_mod);
}

static struct pqlvalue *func_neg(struct eval *ctx, const struct pqlvalue *arg){
   int ival;
   double fval;
   bool isfloat;

   if (convert_to_number(arg, &ival, &fval, &isfloat) < 0) {
      return pqlvalue_nil(ctx->pql);
   }

   if (!isfloat) {
      return pqlvalue_int(ctx->pql, -ival);
   }
   else {
      return pqlvalue_float(ctx->pql, -fval);
   }
}

static struct pqlvalue *func_abs(struct eval *ctx, const struct pqlvalue *arg){
   int ival;
   double fval;
   bool isfloat;

   if (convert_to_number(arg, &ival, &fval, &isfloat) < 0) {
      return pqlvalue_nil(ctx->pql);
   }

   if (!isfloat) {
      return pqlvalue_int(ctx->pql, abs(ival));
   }
   else {
      return pqlvalue_float(ctx->pql, fabs(fval));
   }
}

/*
 * Dispatchers
 */

static struct pqlvalue *do_binary_function(struct eval *ctx,
					   enum functions op,
					   const struct pqlvalue *left,
					   const struct pqlvalue *right) {
   struct pqlvalue *ret, *tmp;

   switch (op) {
    case F_UNION:
     ret = func_union(ctx, left, right);
     break;
    case F_INTERSECT:
     ret = func_intersect(ctx, left, right);
     break;
    case F_EXCEPT:
     ret = func_except(ctx, left, right);
     break;
    case F_UNIONALL:
     ret = func_unionall(ctx, left, right);
     break;
    case F_INTERSECTALL:
     ret = func_intersectall(ctx, left, right);
     break;
    case F_EXCEPTALL:
     ret = func_exceptall(ctx, left, right);
     break;
    case F_IN:
     /*
      * XXX need to distinguish these upstream - this test can be
      * hosed by polymorphism; e.g if you properly have two sets of dbobj,
      * you can end up with *values* that are sets of int and string, and
      * then you try to do elementof.
      */
     if (datatype_eq(pqlvalue_datatype(left), pqlvalue_datatype(right))) {
	ret = func_subsetof(ctx, left, right);
     }
     else {
	ret = func_elementof(ctx, left, right);
     }
     break;
    case F_AND:
     ret = func_and(ctx, left, right);
     break;
    case F_OR:
     ret = func_or(ctx, left, right);
     break;
    case F_EQ:
     ret = func_eq(ctx, left, right);
     break;
    case F_NOTEQ:
     tmp = func_eq(ctx, left, right);
     ret = func_not(ctx, tmp);
     pqlvalue_destroy(tmp);
     break;
    case F_LT:
     ret = func_lt(ctx, left, right);
     break;
    case F_GT:
     ret = func_lt(ctx, right, left);
     break;
    case F_LTEQ:
     tmp = func_lt(ctx, right, left);
     ret = func_not(ctx, tmp);
     pqlvalue_destroy(tmp);
     break;
    case F_GTEQ:
     tmp = func_lt(ctx, left, right);
     ret = func_not(ctx, tmp);
     pqlvalue_destroy(tmp);
     break;
    case F_LIKE:
     ret = func_like(ctx, left, right);
     break;
    case F_GLOB:
     ret = func_glob(ctx, left, right);
     break;
    case F_GREP:
     ret = func_grep(ctx, left, right);
     break;
    case F_SOUNDEX:
     ret = func_soundex(ctx, left, right);
     break;
    case F_CONCAT:
     ret = func_concat(ctx, left, right);
     break;
    case F_CHOOSE:
     ret = func_choose(ctx, left, right);
     break;
    case F_ADD:
     ret = func_add(ctx, left, right);
     break;
    case F_SUB:
     ret = func_sub(ctx, left, right);
     break;
    case F_MUL:
     ret = func_mul(ctx, left, right);
     break;
    case F_DIV:
     ret = func_div(ctx, left, right);
     break;
    case F_MOD:
     ret = func_mod(ctx, left, right);
     break;

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
     /* these are not binary operators and should not come here */
     PQLASSERT(0);
     ret = pqlvalue_nil(ctx->pql);
     break;
   }

   return ret;
}

static struct pqlvalue *do_unary_function(struct eval *ctx,
					  enum functions op,
					  const struct pqlvalue *sub) {
   struct pqlvalue *ret;

   switch (op) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_IN:
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
    case F_CONCAT:
    case F_CHOOSE:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
    case F_CTIME:
     /* these are not unary operators and should not come here */
     ret = pqlvalue_nil(ctx->pql);
     break;

    case F_NONEMPTY:
     ret = func_nonempty(ctx, sub);
     break;
    case F_MAKESET:
     ret = func_makeset(ctx, sub);
     break;
    case F_GETELEMENT:
     ret = func_getelement(ctx, sub);
     break;
    case F_COUNT:
     ret = func_count(ctx, sub);
     break;
    case F_SUM:
     ret = func_sum(ctx, sub);
     break;
    case F_AVG:
     ret = func_avg(ctx, sub);
     break;
    case F_MIN:
     ret = func_min(ctx, sub);
     break;
    case F_MAX:
     ret = func_max(ctx, sub);
     break;
    case F_ALLTRUE:
     ret = func_alltrue(ctx, sub);
     break;
    case F_ANYTRUE:
     ret = func_anytrue(ctx, sub);
     break;
    case F_NOT:
     ret = func_not(ctx, sub);
     break;
    case F_NEW:
     ret = func_new(ctx, sub);
     break;
    case F_TOSTRING:
     ret = pqlvalue_tostring(ctx->pql, sub);
     break;
    case F_NEG:
     ret = func_neg(ctx, sub);
     break;
    case F_ABS:
     ret = func_abs(ctx, sub);
     break;
   }
   return ret;
}

static struct pqlvalue *do_nullary_function(struct eval *ctx,
					    enum functions op) {
   struct pqlvalue *ret;

   switch (op) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_IN:
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
    case F_CONCAT:
    case F_CHOOSE:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
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
    case F_TOSTRING:
    case F_NEG:
    case F_ABS:
     /* these are not nullary and should not come here */
     ret = pqlvalue_nil(ctx->pql);
     break;

    case F_CTIME:
     ret = func_ctime(ctx);
     break;
   }
   return ret;
}


static struct pqlvalue *do_function(struct eval *ctx,
				    enum functions op,
				    const struct pqlvaluearray *args) {
   struct pqlvalue *ret;

   switch (op) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_IN:
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
    case F_CONCAT:
    case F_CHOOSE:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     PQLASSERT(pqlvaluearray_num(args) == 2);
     ret = do_binary_function(ctx, op, pqlvaluearray_get(args, 0),
			      pqlvaluearray_get(args, 1));
     break;

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
    case F_TOSTRING:
    case F_NEG:
    case F_ABS:
     PQLASSERT(pqlvaluearray_num(args) == 1);
     return do_unary_function(ctx, op, pqlvaluearray_get(args, 0));
     break;

    case F_CTIME:
     PQLASSERT(pqlvaluearray_num(args) == 0);
     return do_nullary_function(ctx, op);
     break;
   }

   return ret;
}

////////////////////////////////////////////////////////////
// eval for various expression types

/*
 * Filter. Hand off to dofilter(), which is shared with join.
 */
static struct pqlvalue *filter_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *sub;

   sub = tcexpr_eval(ctx, te->filter.sub);
   return dofilter(ctx, te->filter.predicate, sub);
}

/*
 * Project.
 */

static struct pqlvalue *project_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval, *ret;
   struct columnindexes cols;
   unsigned arity;

   arity = coltree_arity(te->project.sub->colnames);

   subval = tcexpr_eval(ctx, te->project.sub);
   //PQLASSERT(arity == pqlvalue_arity(subval));

   columnindexes_init(&cols);
   getcolumnindexes(ctx, &cols, te->project.sub->colnames, te->project.cols);

   PQLASSERT(cols.num > 0);
   PQLASSERT(cols.num <= arity);
   if (arity == 1 && cols.num == arity) {
      ret = subval;
   }
   else {
      ret = doproject(ctx, subval, &cols);
      pqlvalue_destroy(subval);
   }

   columnindexes_cleanup(ctx, &cols);
   return ret;
}

/*
 * Strip. (Complement of project.)
 * Should probably replace strip with project upstream of here.
 */
static struct pqlvalue *strip_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval, *ret;
   unsigned arity;
   struct columnindexes cols;

   arity = coltree_arity(te->strip.sub->colnames);

   subval = tcexpr_eval(ctx, te->strip.sub);
   if (pqlvalue_isset(subval) && pqlvalue_set_getnum(subval) == 0) {
      /*
       * Shortcut empty sets. Note: don't return the existing empty
       * set as its type may have been fixed to something with more
       * columns than we're supposed to have. A new empty set will
       * have type set(bottom). XXX sets that become empty ought to
       * have their type revert to set(bottom).
       */
      pqlvalue_destroy(subval);
      return pqlvalue_emptyset(ctx->pql);
   }

   //PQLASSERT(arity == pqlvalue_arity(subval));

   columnindexes_init(&cols);
   getcolumnindexes(ctx, &cols, te->strip.sub->colnames, te->strip.cols);
   columnindexes_sort(&cols);

   PQLASSERT(cols.num > 0);
   PQLASSERT(cols.num <= arity);

   /*
    * note: the cols.num == arity (stripping all columns) case happens
    * if you have an unused path in the from-clause. It gives a set of
    * unit that gives us multiple copies of the results. The optimizer
    * should (but currently doesn't, XXX) convert this to "clone unit
    * count(sub) times" or something, which can then be used to
    * suppress as much as possible of the subexpression.
    */

   ret = dostrip(ctx, subval, &cols);

   columnindexes_cleanup(ctx, &cols);
   return ret;
}

/*
 * Rename.
 *
 * Since for now at least values don't have column names, this is a
 * nop.
 */
static struct pqlvalue *rename_eval(struct eval *ctx, struct tcexpr *te) {
   return tcexpr_eval(ctx, te->rename.sub);
}

/*
 * Vanilla join.
 *
 * XXX this sucks. Should eval the condition on the left and right before
 * pasting them together. Also should eval the condition on each pair as
 * we generate it instead of consing the whole set and filtering it.
 */
static struct pqlvalue *join_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *leftval, *rightval, *ret, *newitem;
   const struct pqlvalue *leftitem, *rightitem;
   unsigned leftnum, rightnum, i, j;

   leftval = tcexpr_eval(ctx, te->join.left);
   rightval = tcexpr_eval(ctx, te->join.right);

   TRACE(ctx, "join {%u} x {%u}",
	 pqlvalue_set_getnum(leftval), pqlvalue_set_getnum(rightval));

   PQLASSERT(pqlvalue_isset(leftval));
   PQLASSERT(pqlvalue_isset(rightval));
   leftnum = pqlvalue_set_getnum(leftval);
   rightnum = pqlvalue_set_getnum(rightval);

   ret = pqlvalue_emptyset(ctx->pql);
   for (i=0; i<leftnum; i++) {
      leftitem = pqlvalue_set_get(leftval, i);
      for (j=0; j<rightnum; j++) {
	 rightitem = pqlvalue_set_get(rightval, j);
	 newitem = pqlvalue_paste(ctx->pql, leftitem, rightitem);
	 pqlvalue_set_add(ret, newitem);
      }
   }
   if (te->join.predicate != NULL) {
      ret = dofilter(ctx, te->join.predicate, ret);
   }
   pqlvalue_destroy(leftval);
   pqlvalue_destroy(rightval);
   return ret;
}

/*
 * Order.
 *
 * If columns are listed, compare by those; if not, compare by something
 * convenient that involves all columns.
 */
static struct pqlvalue *order_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval, *ret;
   struct columnindexes ix;

   subval = tcexpr_eval(ctx, te->order.sub);
   TRACE(ctx, "order {%u}", pqlvalue_set_getnum(subval));

   columnindexes_init(&ix);
   if (colset_num(te->order.cols) == 0) {
      columnindexes_all(ctx, &ix, coltree_arity(te->order.sub->colnames));
   }
   else {
      getcolumnindexes(ctx, &ix, te->order.sub->colnames, te->order.cols);
   }
   ret = dosort(ctx, subval, &ix);
   columnindexes_cleanup(ctx, &ix);
   return ret;
}

/*
 * Uniq.
 *
 * If no columns are given, compare all.
 */
static struct pqlvalue *uniq_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval;
   struct columnindexes ix;
   unsigned arity;

   subval = tcexpr_eval(ctx, te->uniq.sub);
   arity = coltree_arity(te->uniq.sub->colnames);
   //PQLASSERT(arity == pqlvalue_arity(subval));

   TRACE(ctx, "uniq {%u}", pqlvalue_coll_getnum(subval));

   columnindexes_init(&ix);
   if (colset_num(te->uniq.cols) == 0) {
      columnindexes_all(ctx, &ix, arity);
   }
   else {
      getcolumnindexes(ctx, &ix, te->uniq.sub->colnames, te->uniq.cols);
      columnindexes_sort(&ix);
   }

   douniq(ctx, subval, &ix);
   columnindexes_cleanup(ctx, &ix);
   return subval;
}

/*
 * Nest.
 *
 * Nest_C,E (set) as G turns
 *
 *         A   B   C   D   E   F
 *       ( 1 , 2 , 3 , 4 , 5 , 6  )
 *       ( 1 , 2 , 0 , 4 , 0 , 6  )
 *       ( 0 , 0 , 0 , 0 , 0 , 0  )
 *
 * into
 *
 *         A   B   D   F           G
 *       ( 1 , 2 , 4 , 6, { (3, 5) , (0, 0) } )
 *       ( 0 , 0 , 0 , 0, { (0, 0)          } )
 *
 */
static struct pqlvalue *nest_eval(struct eval *ctx, struct tcexpr *te) {
   struct columnindexes keep;		/* columns we keep */
   struct columnindexes move;		/* columns we nest into a set */
   struct columnindexes movesorted;	/* sorted copy of move[] */
   struct pqlvalue *oldset;		/* input set */
   struct pqlvalue *thistuple;		/* tuple we're examining */
   struct pqlvalue *val;		/* temporary value */
   struct pqlvalue *prevtuple;		/* tuple of current group */
   struct pqlvalue *newtuple;		/* element for nested set */
   struct pqlvalue *insideset;		/* set we nest into */
   struct pqlvalue *ret;		/* output set */
   unsigned i, j, num;

   /* Evaluate the subexpression to get the input. */
   oldset = tcexpr_eval(ctx, te->nest.sub);

   PQLASSERT(pqlvalue_isset(oldset));

   TRACE(ctx, "nest %u items", pqlvalue_set_getnum(oldset));
   TRACE_INDENT(ctx);

   /* Set up the lists of column indexes. */
   columnindexes_init(&keep);
   getcolumnindexes(ctx, &keep, te->nest.sub->colnames, te->nest.cols);
   columnindexes_complement(ctx, &keep, coltree_arity(te->nest.sub->colnames));

   columnindexes_init(&move);
   getcolumnindexes(ctx, &move, te->nest.sub->colnames, te->nest.cols);

   columnindexes_init(&movesorted);
   getcolumnindexes(ctx, &movesorted, te->nest.sub->colnames, te->nest.cols);
   columnindexes_sort(&movesorted);

   /*
    * First we have to sort, because we have to combine by values that
    * are the same outside the columns we're nesting, and doing that
    * sanely requires sorting. XXX: this sort should appear explicitly
    * in TE so it can be munged by the optimizer.
    *
    * Sort by the columns we're keeping.
    */
   oldset = dosort(ctx, oldset, &keep);

   /*
    * Now do it.
    */
   ret = pqlvalue_emptyset(ctx->pql);
   insideset = NULL;
   prevtuple = NULL;
   num = pqlvalue_set_getnum(oldset);
   for (i=0; i<num; i++) {
      /* Remove the tuple from the old set. */
      thistuple = pqlvalue_set_replace(oldset, i, NULL, false);
      PQLASSERT(pqlvalue_istuple(thistuple));
      TRACE_VALUE(ctx, thistuple);

      /* Move the values to be moved to a new tuple, in the user's order. */
      newtuple = pqlvalue_unit(ctx->pql);
      for (j=0; j<move.num; j++) {
	 val = pqlvalue_tuple_replace(thistuple, move.list[j], NULL);
	 newtuple = pqlvalue_tuple_add(newtuple, val);
      }
      /* Now delete those columns, in order so the indexes don't change. */
      for (j=movesorted.num; j-- > 0; ) {
	 thistuple = pqlvalue_tuple_strip(thistuple, movesorted.list[j]);
      }

      if (prevtuple == NULL || !pqlvalue_eq(prevtuple, thistuple)) {
	 if (prevtuple != NULL) {
	    /* finish old group */
	    prevtuple = pqlvalue_tuple_add(prevtuple, insideset);
	    pqlvalue_set_add(ret, prevtuple);
	    TRACE_VALUE(ctx, prevtuple);
	 }
	 /* start a new group */
	 insideset = pqlvalue_emptyset(ctx->pql);
	 prevtuple = thistuple;
      }
      else {
	 /* same group */
	 pqlvalue_destroy(thistuple);
      }
      /* either way, add the peeled-off stuff to the target set */
      pqlvalue_set_add(insideset, newtuple);
   }
   if (prevtuple != NULL) {
      /* finish last group */
      prevtuple = pqlvalue_tuple_add(prevtuple, insideset);
      pqlvalue_set_add(ret, prevtuple);
      TRACE_VALUE(ctx, prevtuple);
   }

   TRACE_UNINDENT(ctx);

   /* don't need the remnants of oldset */
   pqlvalue_destroy(oldset);

   columnindexes_cleanup(ctx, &keep);
   columnindexes_cleanup(ctx, &move);
   columnindexes_cleanup(ctx, &movesorted);
   return ret;
}

static struct pqlvalue *unnest_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval, *ret;
   unsigned ix;

   subval = tcexpr_eval(ctx, te->unnest.sub);
   ix = getcolumnindex(te->unnest.sub->colnames, te->unnest.col);
   ret = dounnest(ctx, subval, ix, te->datatype);

   return ret;
}

static struct pqlvalue *distinguish_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *ret, *thistuple, *dist;
   unsigned i, num;

   ret = tcexpr_eval(ctx, te->distinguish.sub);
   PQLASSERT(pqlvalue_isset(ret));

   TRACE(ctx, "distinguish %u items", pqlvalue_set_getnum(ret));
   TRACE_INDENT(ctx);

   num = pqlvalue_set_getnum(ret);
   for (i=0; i<num; i++) {
      thistuple = pqlvalue_set_getformodify(ret, i);
      dist = pqlvalue_distinguisher(ctx->pql, i);
      thistuple = pqlvalue_tuple_add(thistuple, dist);
      pqlvalue_set_replace(ret, i, thistuple, true);
      TRACE_VALUE(ctx, thistuple);
   }
   TRACE_UNINDENT(ctx);
   pqlvalue_set_updatetype(ret, te->datatype);
   return ret;
}

static struct pqlvalue *adjoin_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval;
   struct pqlvalue *ret, *subresult;
   unsigned i, num;

   ret = tcexpr_eval(ctx, te->adjoin.left);
   //PQLASSERT(pqlvalue_isset(ret));

   if (!pqlvalue_isset(ret)) {
      num = 1;
   }
   else {
      num = pqlvalue_set_getnum(ret);
   }

   TRACE(ctx, "adjoining %u items", num);
   TRACE_DUMP(ctx, te->adjoin.func);
   TRACE_INDENT(ctx);

   if (!pqlvalue_isset(ret)) {
      TRACE_VALUE(ctx, ret);
      subresult = lambda_eval(ctx, te->adjoin.func, ret);
      TRACE_VALUE(ctx, subresult);
      PQLASSERT(!pqlvalue_istuple(subresult));
      ret = pqlvalue_tuple_add(ret, subresult);
   }
   else {
      for (i=0; i<num; i++) {
	 subval = pqlvalue_set_getformodify(ret, i);
	 TRACE_VALUE(ctx, subval);
	 subresult = lambda_eval(ctx, te->adjoin.func, subval);
	 TRACE_VALUE(ctx, subresult);
	 PQLASSERT(!pqlvalue_istuple(subresult));
	 subval = pqlvalue_tuple_add(subval, subresult);
	 pqlvalue_set_replace(ret, i, subval, true);
      }
      pqlvalue_set_updatetype(ret, te->datatype);
   }

   TRACE_UNINDENT(ctx);
   return ret;
}

static struct pqlvalue *step_eval(struct eval *ctx, struct tcexpr *te) {
   const struct pqlvalue *leftobj;
   struct pqlvalue *thistuple;
   struct pqlvalue *leftval, *edgename, *rightset;
   unsigned subcolindex, i, num;
   unsigned oldarity;
   struct datatype *mytype;

   leftval = tcexpr_eval(ctx, te->step.sub);
   /*
    * It's ok for the left side not to be a set; this happens e.g. for
    * the root of a path in a nested select, not nested in the
    * from-clause, where the root is a variable bound in the outer
    * select. (XXX should we have something upstream wrap these with
    * set() instead?)
    */
   if (!pqlvalue_isset(leftval)) {
      struct pqlvalue *tmp;

      tmp = pqlvalue_emptyset(ctx->pql);
      pqlvalue_set_add(tmp, leftval);
      leftval = tmp;
   }

   oldarity = coltree_arity(te->step.sub->colnames);

   subcolindex = getcolumnindex(te->step.sub->colnames, te->step.subcolumn);
   num = pqlvalue_set_getnum(leftval);

   TRACE(ctx, "step (%d items, column %s)", num,
	 colname_getname(ctx->pql, te->step.subcolumn));
   TRACE_INDENT(ctx);
   if (te->step.edgename != NULL) {
      TRACE_DESCVALUE(ctx, te->step.edgename, "edge name");
   }
   else {
      TRACE(ctx, "all edges");
   }

   for (i=0; i<num; i++) {
      thistuple = pqlvalue_set_getformodify(leftval, i);
      if (pqlvalue_istuple(thistuple)) {
	 PQLASSERT(pqlvalue_tuple_getarity(thistuple) == oldarity);
	 leftobj = pqlvalue_tuple_get(thistuple, subcolindex);
      }
      else {
	 PQLASSERT(oldarity == 1);
	 PQLASSERT(subcolindex == 0);
	 leftobj = thistuple;
      }

      TRACE_DESCVALUE(ctx, leftobj, "from");
      if (te->step.edgename != NULL) {
	 if (pqlvalue_isstruct(leftobj)) {
	    rightset = ctx->pql->ops->follow(ctx->pql, leftobj,
					     te->step.edgename,
					     te->step.reversed);
	    edgename = pqlvalue_clone(ctx->pql, te->step.edgename);

	    /* Make sure the backend returned a set of the right format */
	    PQLASSERT(pqlvalue_isset(rightset));
	    if (pqlvalue_set_getnum(rightset) > 0) {
	       struct datatype *backend_result_type;

	       backend_result_type = pqlvalue_datatype(rightset);
	       backend_result_type = datatype_set_member(backend_result_type);
	       PQLASSERT(datatype_arity(backend_result_type) == 1);
	    }
	 }
	 else {
	    rightset = pqlvalue_emptyset(ctx->pql);
	    edgename = pqlvalue_nil(ctx->pql);
	 }
	 TRACE_DESC2VALUE(ctx, edgename, rightset, "got");
      }
      else {
	 edgename = NULL;
	 if (pqlvalue_isstruct(leftobj)) {
	    rightset = ctx->pql->ops->followall(ctx->pql, leftobj,
						te->step.reversed);
	    /* Make sure the backend returned a set of the right format */
	    PQLASSERT(pqlvalue_isset(rightset));
	    if (pqlvalue_set_getnum(rightset) > 0) {
	       struct datatype *backend_result_type;

	       backend_result_type = pqlvalue_datatype(rightset);
	       backend_result_type = datatype_set_member(backend_result_type);
	       PQLASSERT(datatype_arity(backend_result_type) == 2);
	    }
	 }
	 else {
	    rightset = pqlvalue_emptyset(ctx->pql);
	 }
	 TRACE_DESCVALUE(ctx, rightset, "got");
      }

      thistuple = pqlvalue_tuple_add(thistuple,
				     pqlvalue_clone(ctx->pql, leftobj));
      if (edgename != NULL) {
	 thistuple = pqlvalue_tuple_add(thistuple, edgename);
      }
      thistuple = pqlvalue_tuple_add(thistuple, rightset);
      pqlvalue_set_replace(leftval, i, thistuple, true);
   }

   mytype = datatype_set_member(pqlvalue_datatype(leftval));
   mytype = datatype_tuple_append(ctx->pql, mytype, datatype_absdbobj(ctx->pql));
   mytype = datatype_tuple_append(ctx->pql, mytype, datatype_absdbedge(ctx->pql));
   mytype = datatype_tuple_append(ctx->pql, mytype, datatype_absdbobj(ctx->pql));
   mytype = datatype_set(ctx->pql, mytype);
   pqlvalue_set_updatetype(leftval, mytype);

   /* unnest on rightset */
   if (te->step.edgename != NULL) {
      leftval = dounnest(ctx, leftval, oldarity+2, te->datatype);
   }
   else {
      leftval = dounnest(ctx, leftval, oldarity+1, te->datatype);
   }

   TRACE_UNINDENT(ctx);

   if (te->step.predicate != NULL) {
      leftval = dofilter(ctx, te->step.predicate, leftval);
   }

   return leftval;
}

/*
 * Join LEFT and RIGHT on LEFTCOL = RIGHTCOL.
 * Emits only the left-hand copy of the common column.
 *
 * (Supports the repeat code.)
 *
 * XXX: this should be merged with the regular join code.
 */
static struct pqlvalue *donaturaljoin(struct eval *ctx,
				      const struct pqlvalue *left,
				      unsigned leftcol,
				      const struct pqlvalue *right,
				      unsigned rightcol) {
   struct pqlvalue *ret, *newtuple, *newitem;
   const struct pqlvalue *lefttuple, *leftitem;
   const struct pqlvalue *righttuple, *rightitem;
   unsigned i, leftnum, j, rightnum, k, rightarity;

   PQLASSERT(pqlvalue_isset(left));
   PQLASSERT(pqlvalue_isset(right));

   leftnum = pqlvalue_set_getnum(left);
   rightnum = pqlvalue_set_getnum(right);

   ret = pqlvalue_emptyset(ctx->pql);
   if (leftnum == 0 || rightnum == 0) {
      return ret;
   }

   TRACE(ctx, "naturaljoin");
   TRACE_INDENT(ctx);

   for (i=0; i<leftnum; i++) {
      lefttuple = pqlvalue_set_get(left, i);
      if (!pqlvalue_istuple(lefttuple)) {
	 PQLASSERT(leftcol == 0);
	 leftitem = lefttuple;
      }
      else {
	 leftitem = pqlvalue_tuple_get(lefttuple, leftcol);
      }

      for (j=0; j<rightnum; j++) {
	 righttuple = pqlvalue_set_get(right, j);
	 if (!pqlvalue_istuple(righttuple)) {
	    PQLASSERT(rightcol == 0);
	    rightitem = righttuple;
	 }
	 else {
	    rightitem = pqlvalue_tuple_get(righttuple, rightcol);
	 }

	 if (pqlvalue_eq(leftitem, rightitem)) {
	    newtuple = pqlvalue_clone(ctx->pql, lefttuple);
	    rightarity = pqlvalue_tuple_getarity(righttuple);
	    for (k=0; k<rightarity; k++) {
	       if (k != rightcol) {
		  newitem = pqlvalue_clone(ctx->pql,
					   pqlvalue_tuple_get(righttuple, k));
		  newtuple = pqlvalue_tuple_add(newtuple, newitem);
	       }
	    }
	    pqlvalue_set_add(ret, newtuple);
	    TRACE_VALUE(ctx, newtuple);
	 }
      }
   }

   TRACE_UNINDENT(ctx);
   return ret;
}

/*
 * Strip two or three columns from a datatype.
 * This is a helper for repeat.
 */
static struct datatype *datatype_strip_2or3(struct pqlcontext *pql,
					    struct datatype *t,
					    unsigned col1,
					    unsigned col2,
					    unsigned col3,
					    bool docol3) {
   unsigned cols[3], ncols, i;

   cols[0] = col1;
   cols[1] = col2;
   if (docol3) {
      cols[2] = col3;
      ncols = 3;
   }
   else {
      ncols = 2;
   }
   qsort(cols, ncols, sizeof(*cols), unsigned_sortfunc);

   PQLASSERT(datatype_arity(t) >= ncols);
   for (i=ncols; i-- > 0; ) {
      t = datatype_tuple_strip(pql, t, cols[i]);
   }

   return t;
}

/*
 * Strip two or three columns from a tuple.
 * This is also a helper for repeat.
 */
static struct pqlvalue *tuple_strip_2or3(struct pqlvalue *val,
					 unsigned col1,
					 unsigned col2,
					 unsigned col3,
					 bool docol3) {
   unsigned cols[3], ncols, i;

   cols[0] = col1;
   cols[1] = col2;
   if (docol3) {
      cols[2] = col3;
      ncols = 3;
   }
   else {
      ncols = 2;
   }
   qsort(cols, ncols, sizeof(*cols), unsigned_sortfunc);

   PQLASSERT(pqlvalue_tuple_getarity(val) >= ncols);
   for (i=ncols; i-- > 0; ) {
      val = pqlvalue_tuple_strip(val, cols[i]);
   }

   return val;
}

/*
 * Fixed column numbers for the repeat accumulator set.
 */
#define REP_COL_START     0
#define REP_COL_OUTPUTSEQ 1
#define REP_COL_PATHSEQ   2
#define REP_COL_ZAPPER    3
#define REP_COL_CURRENT   4
#define REP_COL_END       5
#define REP_ARITY         6

/*
 * Set up the accumulator set, as described below. We get a single-column
 * set holding the start column; for each entry we adjoin what else we
 * need as the starting state.
 */
static struct pqlvalue *repeat_initaccum(struct eval *ctx,
					 struct pqlvalue *accum,
					 struct datatype *resulttype) {
   struct pqlvalue *item, *curobj, *curset;
   unsigned i, num;
   struct datatype *t1;
   struct datatype *t2;
   struct datatype *t3;
   struct datatype *t4;
   struct datatype *t5;
   struct datatype *t6;
   struct datatype *accumtype;

   PQLASSERT(pqlvalue_isset(accum));

   t1 = datatype_absdbobj(ctx->pql);
   t2 = datatype_sequence(ctx->pql, resulttype);
   t3 = datatype_sequence(ctx->pql, datatype_pathelement(ctx->pql));
   t4 = datatype_set(ctx->pql, datatype_absdbobj(ctx->pql));
   t5 = datatype_absdbobj(ctx->pql);
   t6 = datatype_absdbobj(ctx->pql);

   accumtype = datatype_tuple_append(ctx->pql, t1, t2);
   accumtype = datatype_tuple_append(ctx->pql, accumtype, t3);
   accumtype = datatype_tuple_append(ctx->pql, accumtype, t4);
   accumtype = datatype_tuple_append(ctx->pql, accumtype, t5);
   accumtype = datatype_tuple_append(ctx->pql, accumtype, t6);
   accumtype = datatype_set(ctx->pql, accumtype);


   TRACE(ctx, "repeat: initial state");
   TRACE_INDENT(ctx);

   num = pqlvalue_set_getnum(accum);
   for (i=0; i<num; i++) {
      item = pqlvalue_set_replace(accum, i, NULL, false);
      PQLASSERT(!pqlvalue_istuple(item));

      curobj = pqlvalue_clone(ctx->pql, item);
      curset = pqlvalue_emptyset(ctx->pql);
      pqlvalue_set_add(curset, pqlvalue_clone(ctx->pql, curobj));

      item = pqlvalue_tuple_add(item, pqlvalue_emptysequence(ctx->pql));
      item = pqlvalue_tuple_add(item, pqlvalue_emptysequence(ctx->pql));
      item = pqlvalue_tuple_add(item, curset);
      item = pqlvalue_tuple_add(item, curobj);
      item = pqlvalue_tuple_add(item, pqlvalue_dbnil(ctx->pql));
      pqlvalue_set_replace(accum, i, item, true);

      TRACE_VALUE(ctx, item);
      PQLASSERT(pqlvalue_tuple_getarity(item) == REP_ARITY);
   }

   TRACE_UNINDENT(ctx);

   pqlvalue_set_updatetype(accum, accumtype);

   return accum;
}

/*
 * Check if a repeated path has looped back on itself.
 *
 * We have a loop if the same physical path (that is, the same tuple)
 * returns to the same current object in the same regexp state. Since
 * the regexp state is "at the head of the repeat loop", all we need
 * to do is check to see if we've seen the same object before, and if
 * not, remember it for later.
 */
static bool repeat_isloop(struct eval *ctx, struct pqlvalue *accum_tuple) {
   const struct pqlvalue *curobj;
   struct pqlvalue *zapper;
   bool ret;

   PQLASSERT(pqlvalue_istuple(accum_tuple));
   PQLASSERT(pqlvalue_tuple_getarity(accum_tuple) == REP_ARITY);

   curobj = pqlvalue_tuple_get(accum_tuple, REP_COL_CURRENT);
   if (pqlvalue_isnil(curobj) || !pqlvalue_isstruct(curobj)) {
      return false;
   }

   zapper = pqlvalue_tuple_replace(accum_tuple, REP_COL_ZAPPER, NULL);
   PQLASSERT(pqlvalue_isset(zapper));
   ret = set_ismember(zapper, curobj, NULL);
   if (!ret) {
      pqlvalue_set_add(zapper, pqlvalue_clone(ctx->pql, curobj));
   }
   pqlvalue_tuple_replace(accum_tuple, REP_COL_ZAPPER, zapper);

   return ret;
}


/*
 * Insert a set of repeat results into the accumulator set.
 * Destroys (maybe) ACCUM and returns a new value for it.
 * RESULT is untouched.
 *
 * Entries that can't match any further are put in FINISHED.
 *
 * If FIRST is true, unmatched preexisting values are not retained.
 * (Without this logic we implement * instead of +. But we want +.)
 *
 * This essentially takes ACCUM |x| RESULT, then munges each tuple to
 * produce something in the same format as ACCUM but with more stuff
 * in its sequence members. Except that it's a left outer join.
 *
 * We could do this in place using pqlvalue_pryopen, but for the moment
 * that seems difficult and not necessarily worthwhile.
 *
 * XXX in the long run some of this logic should be emitted as tuple
 * calculus by tuplify so the optimizer can grind it.
 */
static struct pqlvalue *repeat_accumresults(
	struct eval *ctx,			/* eval context */
	struct pqlvalue *accum,			/* input accumulator set */
	const struct pqlvalue *result,		/* results from loop body */
	unsigned result_start_col,		/* col# for result start pt */
	unsigned result_end_col,		/* col# for result endpoint */
	unsigned result_path_col,		/* col# for result path */
	bool doingpaths,			/* extract & combine paths? */
	bool first,				/* first iteration? */
	struct pqlvalue *finished		/* completed matches */
) {

   struct pqlvalue *ret;			/* return (new accum) set */
   struct pqlvalue *ret_tuple;			/* tuple for return set */

   const struct pqlvalue *acc_tuple;		/* tuple from accum set */
   const struct pqlvalue *res_tuple;		/* tuple from result set */

   const struct pqlvalue *acc_oldcur;		/* previous current object */
   const struct pqlvalue *res_oldcur;		/* previous current object */

   struct pqlvalue *res_end;			/* end object from result */

   struct pqlvalue *res_path;			/* path object from result */
   struct pqlvalue *path_seq;			/* accum's path sequence */

   struct pqlvalue *res_copy;			/* working copy of res_tuple */
   struct pqlvalue *output_seq;			/* accum's output sequence */

   struct pqlvalue *tmp;			/* scratch value */
   unsigned i, anum, j, rnum;
   bool matched;

   PQLASSERT(pqlvalue_isset(accum));
   PQLASSERT(pqlvalue_isset(result));

   TRACE(ctx, "repeat: accumulate");
   TRACE_INDENT(ctx);

   anum = pqlvalue_set_getnum(accum);
   rnum = pqlvalue_set_getnum(result);

   ret = pqlvalue_emptyset(ctx->pql);
   /* in case the output is empty; avoids tripping on set(bottom) later */
   pqlvalue_set_updatetype(ret, pqlvalue_datatype(accum));

   for (i=0; i<anum; i++) {
      acc_tuple = pqlvalue_set_get(accum, i);
      PQLASSERT(pqlvalue_istuple(acc_tuple));
      PQLASSERT(pqlvalue_tuple_getarity(acc_tuple) == REP_ARITY);

      acc_oldcur = pqlvalue_tuple_get(acc_tuple, REP_COL_CURRENT);
      PQLASSERT(!pqlvalue_isnil(acc_oldcur));

      matched = false;

      if (!pqlvalue_isstruct(acc_oldcur)) {
	 goto skip; // XXX should not be a goto
      }

      for (j=0; j<rnum; j++) {
	 res_tuple = pqlvalue_set_get(result, j);
	 res_oldcur = pqlvalue_tuple_get(res_tuple, result_start_col);
	 if (!pqlvalue_eq(acc_oldcur, res_oldcur)) {
	    continue;
	 }
	 matched = true;
	 PQLASSERT(pqlvalue_isstruct(res_oldcur));

	 /* Clone each tuple. Yes, this clones each whole sequence. Suck. */
	 ret_tuple = pqlvalue_clone(ctx->pql, acc_tuple);
	 res_copy = pqlvalue_clone(ctx->pql, res_tuple);

	 /* Get result endpoint and move it to the CURRENT position */
	 res_end = pqlvalue_tuple_replace(res_copy, result_end_col, NULL);
	 tmp = pqlvalue_tuple_replace(ret_tuple, REP_COL_CURRENT, res_end);
	 pqlvalue_destroy(tmp);

	 /* If we're collecting a path... */
	 if (doingpaths) {
	    /* Get the path out and concatenate it to the accumulated path. */
	    path_seq = pqlvalue_tuple_replace(ret_tuple, REP_COL_PATHSEQ,NULL);
	    res_path = pqlvalue_tuple_replace(res_copy, result_path_col, NULL);
	    tmp = path_seq;
	    path_seq = func_concat(ctx, path_seq, res_path);
	    pqlvalue_destroy(tmp);
	    pqlvalue_destroy(res_path);
	    pqlvalue_tuple_replace(ret_tuple, REP_COL_PATHSEQ, path_seq);
	 }

	 /* Strip the start, endpoint, and path (if appropriate) columns */
	 res_copy = tuple_strip_2or3(res_copy, result_start_col,
				     result_end_col,
				     result_path_col, doingpaths);

	 /* Add what's left (if anything) in res_copy to the output sequence */
	 if (!pqlvalue_istuple(res_copy) ||
	     pqlvalue_tuple_getarity(res_copy) > 0) {
	    output_seq = pqlvalue_tuple_replace(ret_tuple, REP_COL_OUTPUTSEQ,
						NULL);
	    pqlvalue_sequence_add(output_seq, res_copy);
	    pqlvalue_tuple_replace(ret_tuple, REP_COL_OUTPUTSEQ, output_seq);
	 }
	 else {
	    pqlvalue_destroy(res_copy);
	 }

	 /* Check for loops. */
	 if (repeat_isloop(ctx, ret_tuple)) {
	    /* stop here; swap as below to indicate a finished match */
	    // XXX do we do this, or kill this match off entirely?
	    tmp = pqlvalue_tuple_replace(ret_tuple, REP_COL_CURRENT, NULL);
	    tmp = pqlvalue_tuple_replace(ret_tuple, REP_COL_END, tmp);
	    PQLASSERT(pqlvalue_isnil(tmp));
	    pqlvalue_tuple_replace(ret_tuple, REP_COL_CURRENT, tmp);

	    /* Add ret_tuple to the finished list. */
	    pqlvalue_set_add(finished, ret_tuple);
	    TRACE_DESCVALUE(ctx, ret_tuple, "looped");
	 }
	 else {
	    /* Add ret_tuple to the output. */
	    pqlvalue_set_add(ret, ret_tuple);
	    TRACE_DESCVALUE(ctx, ret_tuple, "contin");
	 }
      }

  skip:
      if (!matched && !first) {
	 /* Nothing matched acc_tuple. */

	 /* Take it out of the old accum */
	 ret_tuple = pqlvalue_set_replace(accum, i, NULL, false);

	 if (!pqlvalue_isnil(pqlvalue_tuple_get(ret_tuple, REP_COL_CURRENT))) {
	    /*
	     * Move its CURRENT to its END, and nil to CURRENT.
	     * Since END should be nil, we can do this by exchange.
	     */
	    tmp = pqlvalue_tuple_replace(ret_tuple, REP_COL_CURRENT, NULL);
	    tmp = pqlvalue_tuple_replace(ret_tuple, REP_COL_END, tmp);
	    PQLASSERT(pqlvalue_isnil(tmp));
	    pqlvalue_tuple_replace(ret_tuple, REP_COL_CURRENT, tmp);
	    /* Add to finished list */
	    pqlvalue_set_add(finished, ret_tuple);
	    TRACE_DESCVALUE(ctx, ret_tuple, "ending");
	 }
	 else {
	    /* Add to results */
	    /* should not have stayed in accum last time */
	    PQLASSERT(0);
	    pqlvalue_set_add(finished, ret_tuple);
	    TRACE_DESCVALUE(ctx, ret_tuple, "ended.");
	 }
      }
   }

   TRACE_UNINDENT(ctx);

   pqlvalue_destroy(accum);

   return ret;
}


/*
 * Repeat.
 *
 * Repeat can generate (for each matched physical path)
 *
 *   - optionally, a sequence of values for bound variables ("output")
 *   - optionally, a sequence of path elements
 *   - always, an endpoint object
 *
 * It returns these joined to the results of its input path ("sub").
 *
 * In the code below, "result" is something that arises from
 * evaluating the body expression once.
 */
static struct pqlvalue *repeat_eval(struct eval *ctx, struct tcexpr *te) {

   struct pqlvalue *subval;			/* value of sub expr */
   struct pqlvalue *accum;			/* result accumulator */
   struct pqlvalue *finished;			/* completed matches */
   struct pqlvalue *current;			/* column of current objects */
   struct pqlvalue *thisresult;			/* yield from loop body */
   struct pqlvalue *ret;			/* return value (new set) */

   unsigned subval_end_col;			/* subval's endpoint col */
   struct columnindexes subval_end_cols;	/* subval's endpoint col */
   struct columnindexes first_cols;		/* column #0 */
   struct columnindexes accum_outputseq_cols;	/* accum's OUTPUTSEQ col */
   struct columnindexes accum_pathseq_cols;	/* accum's PATHSEQ col */
   struct columnindexes accum_zapper_cols;	/* accum's ZAPPER col */
   struct columnindexes accum_current_cols;	/* accum's CURRENT col */
   unsigned result_start_col;			/* result's START column */
   unsigned result_end_col;			/* result's END column */
   unsigned result_path_col;			/* result's PATH column */
   bool doingoutput;
   bool doingpaths;
   bool first;

   unsigned mark, count = 0;
   struct datatype *resulttype;

   
   first = true;

   /*
    * Compute column indexes.
    */
   columnindexes_init(&subval_end_cols);
   columnindexes_one(ctx, &subval_end_cols, te->repeat.sub->colnames,
		     te->repeat.subendcolumn);
   subval_end_col = subval_end_cols.list[0];
   columnindexes_init(&first_cols);
   columnindexes_fixed(ctx, &first_cols, 0);
   columnindexes_init(&accum_outputseq_cols);
   columnindexes_fixed(ctx, &accum_outputseq_cols, REP_COL_OUTPUTSEQ);
   columnindexes_init(&accum_pathseq_cols);
   columnindexes_fixed(ctx, &accum_pathseq_cols, REP_COL_PATHSEQ);
   columnindexes_init(&accum_zapper_cols);
   columnindexes_fixed(ctx, &accum_zapper_cols, REP_COL_ZAPPER); 
   columnindexes_init(&accum_current_cols);
   columnindexes_fixed(ctx, &accum_current_cols, REP_COL_CURRENT); 

   result_start_col = getcolumnindex(te->repeat.body->colnames,
				   te->repeat.bodystartcolumn);
   if (te->repeat.bodypathcolumn != NULL) {
      result_path_col = getcolumnindex(te->repeat.body->colnames,
				       te->repeat.bodypathcolumn);
      doingpaths = true;
   }
   else {
      result_path_col = 0;
      doingpaths = false;
   }
   result_end_col = getcolumnindex(te->repeat.body->colnames,
				   te->repeat.bodyendcolumn);

   /*
    * First, eval the stuff leading up to the repeat.
    */
   subval = tcexpr_eval(ctx, te->repeat.sub);
   PQLASSERT(pqlvalue_isset(subval));

   /*
    * Get the datatype for (one element of) the result we accumulate.
    * XXX this should be stored in the tcalc.
    */
   resulttype = te->repeat.body->datatype;
   PQLASSERT(datatype_isset(resulttype));
   resulttype = datatype_set_member(resulttype);
   resulttype = datatype_strip_2or3(ctx->pql, resulttype,
				    result_start_col, result_end_col,
				    result_path_col, doingpaths);
   if (datatype_arity(resulttype) == 0) {
      doingoutput =false;
   }
   else {
      doingoutput = true;
   }

   TRACE(ctx, "repeat");
   TRACE_INDENT(ctx);

   /*
    * Create the accumulator set. This is a tuple set of arity 4, which
    * contains for each match hypothesis:
    *    (0) the start object,
    *    (1) a result sequence (which begins empty),
    *    (2) a path sequence (which begins empty),
    *    (3) the current object, which starts as the start object.
    *    (4) the end object, which starts as nil.
    *
    * The starting point is the end column of the start expression,
    * which we get via ->repeat.startcolumn.
    */
   if (datatype_arity(datatype_set_member(te->repeat.sub->datatype)) > 1) {
      accum = doproject(ctx, subval, &subval_end_cols);
   }
   else {
      accum = subval;
      subval = NULL;
   }
   accum = repeat_initaccum(ctx, accum, resulttype);

   finished = pqlvalue_emptyset(ctx->pql);
   pqlvalue_set_updatetype(finished, pqlvalue_datatype(accum));

   /* Loop until done. */
   while (1) {

      /* Get the current object out. */
      current = doproject(ctx, accum, &accum_current_cols);

      // for now at least values don't have column names, so don't need this
      //current = dorename(ctx, current, te->repeat.startcolumn,
      //                 te->repeat.loopincolumn);

      /*
       * We need to uniq(current) here. If some object K is repeated
       * in current, there'll be multiple copies of things after
       * joining the results back.
       *
       * Say K is found twice in current and K leads to three objects
       * X, Y, Z. The output will then have (K, X), (K, Y), and (K, Z)
       * twice each, once for each K. When this is joined back to
       * current, each K in current will be paired with all six such
       * pairs, resulting in twice as many outputs as intended.
       *
       * For even a small whole-graph search (like "John.#" in the
       * test database) this duplication causes a huge exponential
       * blowup.
       */
      current = dosort(ctx, current, &first_cols);
      current = douniq(ctx, current, &first_cols);
      current = pqlvalue_sequence_to_set(ctx->pql, current);

      /* bind the iteration variable */
      mark = eval_bind(ctx, te->repeat.loopvar, current);

      TRACE(ctx, "repeat: body");
      TRACE_INDENT(ctx);

      /* run the body */
      thisresult = tcexpr_eval(ctx, te->repeat.body);
      PQLASSERT(pqlvalue_isset(thisresult));

      TRACE_UNINDENT(ctx);

      /* remove the variable binding */
      eval_unbind(ctx, mark);
      pqlvalue_destroy(current);

      TRACE(ctx, "repeat: loop #%u", count);
      count++;

      TRACE(ctx, "repeat: %u in finished, %u in accum, %u results",
	    pqlvalue_set_getnum(finished),
	    pqlvalue_set_getnum(accum),
	    pqlvalue_set_getnum(thisresult));

      /* collect the results into accum */
      accum = repeat_accumresults(ctx, accum, thisresult,
				  result_start_col, result_end_col,
				  result_path_col, doingpaths, first,
				  finished);
      first = false;

      /* check if we're done */
      if (pqlvalue_set_getnum(thisresult) == 0) {
	 pqlvalue_destroy(thisresult);
	 break;
      }

      pqlvalue_destroy(thisresult);
   }

   PQLASSERT(pqlvalue_set_getnum(accum) == 0);
   pqlvalue_destroy(accum);
   accum = finished;

   /*
    * Note that the following strips have to be done right to left in the
    * tuple, or the column indexes break.
    */

   // for now at least values don't have column names, so don't need this
   //accum = dorename(ctx, accum, REP_COL_END, te->repeat.endcolumn);

   /* purge the current position column */
   accum = dostrip(ctx, accum, &accum_current_cols);

   /* purge the loop-zapper column */
   accum = dostrip(ctx, accum, &accum_zapper_cols);

   /* if we aren't collecting paths, purge that column */
   if (!doingpaths) {
      accum = dostrip(ctx, accum, &accum_pathseq_cols);
   }
   else {
      // for now at least values don't have column names, so don't need this
      //accum = dorename(ctx, current, REP_COL_PATHSEQ, te->repeat.endcolumn2);
   }

   /* if we aren't generating output, purge that column */
   if (!doingoutput) {
      accum = dostrip(ctx, accum, &accum_outputseq_cols);
   }

   /*
    * Clean up the various column lists.
    */
   columnindexes_cleanup(ctx, &subval_end_cols);
   columnindexes_cleanup(ctx, &first_cols);
   columnindexes_cleanup(ctx, &accum_current_cols);
   columnindexes_cleanup(ctx, &accum_outputseq_cols);
   columnindexes_cleanup(ctx, &accum_pathseq_cols);
   columnindexes_cleanup(ctx, &accum_zapper_cols);

   /* now join accum to subval */
   if (subval != NULL) {
      ret = donaturaljoin(ctx, subval, subval_end_col, accum, REP_COL_START);
      pqlvalue_destroy(subval);
      pqlvalue_destroy(accum);
      pqlvalue_set_updatetype(ret, te->datatype);
   }
   else {
      ret = accum;
   }

   TRACE_UNINDENT(ctx);

   return ret;
}

static struct pqlvalue *bop_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *leftval, *rightval, *ret;

   leftval = tcexpr_eval(ctx, te->bop.left);
   rightval = tcexpr_eval(ctx, te->bop.right);

   TRACE(ctx, "binary operator %s", function_getname(te->bop.op));
   TRACE_INDENT(ctx);

   ret = do_binary_function(ctx, te->bop.op, leftval, rightval);
   pqlvalue_destroy(leftval);
   pqlvalue_destroy(rightval);

   TRACE_UNINDENT(ctx);
   TRACE_VALUE(ctx, ret);

   return ret;
}

static struct pqlvalue *uop_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *subval, *ret;

   subval = tcexpr_eval(ctx, te->uop.sub);

   TRACE(ctx, "unary operator %s", function_getname(te->uop.op));
   TRACE_INDENT(ctx);

   ret = do_unary_function(ctx, te->uop.op, subval);
   pqlvalue_destroy(subval);

   TRACE_UNINDENT(ctx);
   TRACE_VALUE(ctx, ret);

   return ret;
}

static struct pqlvalue *func_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *ret, *val;
   struct pqlvaluearray vals;
   unsigned i, num;

   num = tcexprarray_num(&te->func.args);
   pqlvaluearray_init(&vals);
   pqlvaluearray_setsize(ctx->pql, &vals, num);
   for (i=0; i<num; i++) {
      val = tcexpr_eval(ctx, tcexprarray_get(&te->func.args, i));
      pqlvaluearray_set(&vals, i, val);
   }

   TRACE(ctx, "function %s", function_getname(te->func.op));
   TRACE_INDENT(ctx);

   ret = do_function(ctx, te->func.op, &vals);

   TRACE_UNINDENT(ctx);
   TRACE_VALUE(ctx, ret);

   for (i=0; i<num; i++) {
      pqlvalue_destroy(pqlvaluearray_get(&vals, i));
   }

   pqlvaluearray_setsize(ctx->pql, &vals, 0);
   pqlvaluearray_cleanup(ctx->pql, &vals);

   return ret;
}

static struct pqlvalue *map_eval(struct eval *ctx, struct tcexpr *te) {
   const struct pqlvalue *subval;
   struct pqlvalue *set, *ret, *subresult;
   unsigned mark, i, num;

   set = tcexpr_eval(ctx, te->map.set);
   PQLASSERT(pqlvalue_isset(set));

   TRACE(ctx, "map");
   TRACE_INDENT(ctx);

   ret = pqlvalue_emptyset(ctx->pql);
   num = pqlvalue_set_getnum(set);
   for (i=0; i<num; i++) {
      subval = pqlvalue_set_get(set, i);

      TRACE(ctx, "map-binding .K%u", te->map.var->id);
      TRACE_SHORTVALUE(ctx, subval);

      mark = eval_bind(ctx, te->map.var, subval);
      subresult = tcexpr_eval(ctx, te->map.result);
      pqlvalue_set_add(ret, subresult);
      eval_unbind(ctx, mark);
   }

   TRACE_UNINDENT(ctx);

   pqlvalue_destroy(set);
   pqlvalue_set_updatetype(ret, te->datatype);
   return ret;
}

static struct pqlvalue *let_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *val, *ret;
   unsigned mark;

   val = tcexpr_eval(ctx, te->let.value);

   TRACE(ctx, "let-binding .K%u", te->let.var->id);
   TRACE_SHORTVALUE(ctx, val);

   mark = eval_bind(ctx, te->let.var, val);
   ret = tcexpr_eval(ctx, te->let.body);
   eval_unbind(ctx, mark);

   pqlvalue_destroy(val);
   return ret;
}

static struct pqlvalue *lambda_eval(struct eval *ctx, struct tcexpr *te,
				    const struct pqlvalue *arg) {
   struct pqlvalue *ret;
   unsigned mark;

   TRACE(ctx, "lambda-binding .K%u", te->readvar->id);
   TRACE_SHORTVALUE(ctx, arg);

   mark = eval_bind(ctx, te->lambda.var, arg);
   ret = tcexpr_eval(ctx, te->lambda.body);
   eval_unbind(ctx, mark);
   
   return ret;
}

static struct pqlvalue *apply_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *arg, *ret;

   PQLASSERT(te->apply.lambda->type == TCE_LAMBDA);

   TRACE(ctx, "apply");

   arg = tcexpr_eval(ctx, te->apply.arg);
   ret = lambda_eval(ctx, te->apply.lambda, arg);
   pqlvalue_destroy(arg);

   return ret;
}

static struct pqlvalue *readvar_eval(struct eval *ctx, struct tcexpr *te) {
   const struct pqlvalue *val;

   TRACE(ctx, "readvar .K%u", te->readvar->id);

   val = eval_readvar(ctx, te->readvar);

   /* null == "not bound", which would be a serious error on our part */
   PQLASSERT(val != NULL);

   TRACE_SHORTVALUE(ctx, val);

   return pqlvalue_clone(ctx->pql, val);
}

static struct pqlvalue *readglobal_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *ret, *tmp;

   TRACE(ctx, "readglobal %s", te->readglobal->name);

   ret = ctx->pql->ops->read_global(ctx->pql, te->readglobal->name);

   // XXX should this be necessary?
   if (datatype_isset(te->datatype) && !pqlvalue_isset(ret)) {
      tmp = pqlvalue_emptyset(ctx->pql);
      pqlvalue_set_add(tmp, ret);
      TRACE_VALUE(ctx, tmp);
      return tmp;
   }

   TRACE_VALUE(ctx, ret);
   return ret;
}

static struct pqlvalue *createpathelement_eval(struct eval *ctx,
					       struct tcexpr *te) {
   struct pqlvalue *val, *ret;

   val = tcexpr_eval(ctx, te->createpathelement);
   PQLASSERT(pqlvalue_istuple(val));
   PQLASSERT(pqlvalue_tuple_getarity(val) == 3);

   ret = pqlvalue_pathelement(ctx->pql,
			      pqlvalue_clone(ctx->pql,
					     pqlvalue_tuple_get(val, 0)),
			      pqlvalue_clone(ctx->pql,
					     pqlvalue_tuple_get(val, 1)),
			      pqlvalue_clone(ctx->pql,
					     pqlvalue_tuple_get(val, 2)));
   pqlvalue_destroy(val);
   return ret;
}

static struct pqlvalue *splatter_eval(struct eval *ctx, struct tcexpr *te) {
   // XXX for now just get the values and ignore the names
   return tcexpr_eval(ctx, te->splatter.value);
}

static struct pqlvalue *tuple_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *ret, *val;
   struct pqlvaluearray vals;
   unsigned i, num;

   // XXX we need to return the column names somehow

   num = tcexprarray_num(&te->tuple.exprs);
   pqlvaluearray_init(&vals);
   pqlvaluearray_setsize(ctx->pql, &vals, num);
   for (i=0; i<num; i++) {
      val = tcexpr_eval(ctx, tcexprarray_get(&te->tuple.exprs, i));
      pqlvaluearray_set(&vals, i, val);
   }
   ret = pqlvalue_tuple_specific(ctx->pql, &vals);
   pqlvaluearray_setsize(ctx->pql, &vals, 0);
   pqlvaluearray_cleanup(ctx->pql, &vals);

   return ret;
}

static struct pqlvalue *value_eval(struct eval *ctx, struct tcexpr *te) {
   return pqlvalue_clone(ctx->pql, te->value);
}

////////////////////////////////////////////////////////////
// recursive traversal

static struct pqlvalue *tcexpr_eval(struct eval *ctx, struct tcexpr *te) {
   struct pqlvalue *ret;

   ret = NULL; // gcc 4.1

   PQLASSERT(te->datatype != NULL);

   switch (te->type) {
    case TCE_FILTER:            ret = filter_eval(ctx, te);            break;
    case TCE_PROJECT:           ret = project_eval(ctx, te);           break;
    case TCE_STRIP:             ret = strip_eval(ctx, te);             break;
    case TCE_RENAME:            ret = rename_eval(ctx, te);            break;
    case TCE_JOIN:              ret = join_eval(ctx, te);              break;
    case TCE_ORDER:             ret = order_eval(ctx, te);             break;
    case TCE_UNIQ:              ret = uniq_eval(ctx, te);              break;
    case TCE_NEST:              ret = nest_eval(ctx, te);              break;
    case TCE_UNNEST:            ret = unnest_eval(ctx, te);            break;
    case TCE_DISTINGUISH:       ret = distinguish_eval(ctx, te);       break;
    case TCE_ADJOIN:            ret = adjoin_eval(ctx, te);            break;
    case TCE_STEP:              ret = step_eval(ctx, te);              break;
    case TCE_REPEAT:            ret = repeat_eval(ctx, te);            break;
    case TCE_BOP:               ret = bop_eval(ctx, te);               break;
    case TCE_UOP:               ret = uop_eval(ctx, te);               break;
    case TCE_FUNC:              ret = func_eval(ctx, te);              break;
    case TCE_MAP:               ret = map_eval(ctx, te);               break;
    case TCE_LET:               ret = let_eval(ctx, te);               break;
    case TCE_APPLY:             ret = apply_eval(ctx, te);             break;
    case TCE_READVAR:           ret = readvar_eval(ctx, te);           break;
    case TCE_READGLOBAL:        ret = readglobal_eval(ctx, te);        break;
    case TCE_CREATEPATHELEMENT: ret = createpathelement_eval(ctx, te); break;
    case TCE_SPLATTER:          ret = splatter_eval(ctx, te);          break;
    case TCE_TUPLE:             ret = tuple_eval(ctx, te);             break;
    case TCE_VALUE:             ret = value_eval(ctx, te);             break;

    case TCE_LAMBDA:
     PQLASSERT(!"tried to eval a loose lambda expression");
     ret = pqlvalue_nil(ctx->pql);
     break;
    case TCE_SCAN:
     PQLASSERT(!"all scans are supposed to be removed by the optimizer");
     ret = pqlvalue_nil(ctx->pql);
     break;
   }

   PQLASSERT(datatype_match_specialize(ctx->pql,
				       pqlvalue_datatype(ret),
				       te->datatype) != NULL);
   // should assert something, but it's harder than this (XXX)
   //PQLASSERT(pqlvalue_arity(ret) == coltree_arity(te->colnames));

   return ret;
}

////////////////////////////////////////////////////////////
// entry point

struct pqlvalue *eval(struct pqlcontext *pql, struct tcexpr *te) {
   struct eval ctx;
   struct pqlvalue *ret;

   eval_init(&ctx, pql);
   ret = tcexpr_eval(&ctx, te);
   eval_cleanup(&ctx);

   return ret;
}
