/*
 * Copyright 2009, 2010
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

#include "valuerep.h"

DEFARRAY(tuplevalue, /*no inline*/);
DEFARRAY(tupleslice, /*no inline*/);


static struct tuplevalue *tuplevalue_create(struct pqlcontext *pql) {
   struct tuplevalue *ret;

   ret = domalloc(pql, sizeof(*ret));

   ret->up = NULL;
   ret->down = NULL;
   ret->left = NULL;
   tuplevaluearray_init(&ret->right);
   ret->sliceindex = 0;

   ret->val = NULL;

   return ret;
}

static void tuplevalue_destroy(struct pqlcontext *pql, struct tuplevalue *tv) {
   /* directional pointers are unowned; don't free them */
   tuplevaluearray_setsize(pql, &tv->right, 0);
   tuplevaluearray_cleanup(pql, &tv->right);

   pqlvalue_destroy(tv->val);
   dofree(pql, tv, sizeof(*tv));
}

static struct tuplevalue *tuplevalue_clone(struct pqlcontext *pql,
					   struct tuplevalue *tv) {
   struct tuplevalue *ret;

   ret = tuplevalue_create(pql);
   ret->sliceindex = tv->sliceindex;
   ret->val = pqlvalue_clone(pql, tv->val);
   return ret;
}

static void tuplevalue_map_tostring(struct pqlcontext *pql,
				    struct tuplevalue *tv) {
   struct pqlvalue *string;

   string = pqlvalue_tostring(pql, tv->val);
   pqlvalue_destroy(tv->val);
   tv->val = string;
}

//////////////////////////////

static struct tupleslice *tupleslice_create(struct pqlcontext *pql) {
   struct tupleslice *ret;

   ret = domalloc(pql, sizeof(*ret));
   tuplevaluearray_init(&ret->elements);
   ret->setindex = 0;
   return ret;
}

static void tupleslice_cleanup(struct pqlcontext *pql, struct tupleslice *ts) {
   unsigned i, num;

   num = tuplevaluearray_num(&ts->elements);
   for (i=0; i<num; i++) {
      tuplevalue_destroy(pql, tuplevaluearray_get(&ts->elements, i));
   }
   tuplevaluearray_setsize(pql, &ts->elements, 0);
   tuplevaluearray_cleanup(pql, &ts->elements);
}

static void tupleslice_destroy(struct pqlcontext *pql, struct tupleslice *ts) {
   tupleslice_cleanup(pql, ts);
   dofree(pql, ts, sizeof(*ts));
}

static void tupleslice_restitch(struct pqlcontext *pql,
				const struct tupleslice *destleftslice,
				struct tupleslice *destslice,
				const struct tupleslice *srcslice) {
   unsigned i, num;
   struct tuplevalue *srcval, *destval, *srcleftval, *destleftval;

   num = tuplevaluearray_num(&srcslice->elements);
   PQLASSERT(num == tuplevaluearray_num(&destslice->elements));

   for (i=0; i<num; i++) {
      srcval = tuplevaluearray_get(&srcslice->elements, i);
      destval = tuplevaluearray_get(&destslice->elements, i);
      srcleftval = srcval->left;
      destleftval = tuplevaluearray_get(&destleftslice->elements,
					srcleftval->sliceindex);
      destval->left = destleftval;
      tuplevaluearray_add(pql, &destleftval->right, destval, NULL);
   }
}

static struct tupleslice *tupleslice_clone(struct pqlcontext *pql,
					   struct tupleslice *ts) {
   struct tupleslice *ret;
   struct tuplevalue *thisval, *lowerval;
   unsigned i, num;

   ret = tupleslice_create(pql);
   num = tuplevaluearray_num(&ts->elements);
   tuplevaluearray_setsize(pql, &ret->elements, num);
   for (i=0; i<num; i++) {
      thisval = tuplevalue_clone(pql, tuplevaluearray_get(&ts->elements, i));
      if (i > 0) {
	 lowerval = tuplevaluearray_get(&ret->elements, i-1);
	 thisval->down = lowerval;
	 lowerval->up = thisval;
      }
      tuplevaluearray_set(&ret->elements, i, thisval);
   }
   ret->setindex = ts->setindex;

   return ret;
}

static void tupleslice_map_tostring(struct pqlcontext *pql,
				    struct tupleslice *ts) {
   unsigned i, num;

   num = tuplevaluearray_num(&ts->elements);
   for (i=0; i<num; i++) {
      tuplevalue_map_tostring(pql, tuplevaluearray_get(&ts->elements, i));
   }
}

//////////////////////////////

void tupleset_init(struct pqlcontext *pql, struct tupleset *ts) {
   (void)pql;
   tupleslicearray_init(&ts->slices);
}

void tupleset_cleanup(struct pqlcontext *pql, struct tupleset *ts) {
   unsigned i, num;

   num = tupleslicearray_num(&ts->slices);
   for (i=0; i<num; i++) {
      tupleslice_destroy(pql, tupleslicearray_get(&ts->slices, i));
   }
   tupleslicearray_setsize(pql, &ts->slices, 0);
   tupleslicearray_cleanup(pql, &ts->slices);
}

/*
 * Copy SRC into DEST. DEST should be empty but should have had
 * tupleset_init called on it.
 */
static void tupleset_copy(struct pqlcontext *pql,
			  struct tupleset *dest, const struct tupleset *src) {
   unsigned i, num;
   struct tupleslice *srcslice, *destslice, *destleftslice;

   num = tupleslicearray_num(&src->slices);
   tupleslicearray_setsize(pql, &dest->slices, num);
   for (i=0; i<num; i++) {
      srcslice = tupleslicearray_get(&src->slices, i);
      destslice = tupleslice_clone(pql, srcslice);
      if (i > 0) {
	 destleftslice = tupleslicearray_get(&dest->slices, i-1);
	 tupleslice_restitch(pql, destleftslice, destslice, srcslice);
      }
      tupleslicearray_set(&dest->slices, i, destslice);
   }
}

static void tupleset_map_tostring(struct pqlcontext *pql,
				  struct tupleset *ts) {
   unsigned i, num;

   num = tupleslicearray_num(&ts->slices);
   for (i=0; i<num; i++) {
      tupleslice_map_tostring(pql, tupleslicearray_get(&ts->slices, i));
   }
}

/*
 * Print like this:
 *
 * {
 *    (A, B1, C1, D1),
 *    (   B2, C2, D2),
 *    (       C3, D3),
 *    (   B3, C4, D4)
 * }
 *
 * We will do this by first constructing a tupleset of strings,
 * then folding it to a single string.
 *
 * Separating tupleset_copy and tupleset_map_tostring requires
 * doing an unnecessary copy of every element, but we aren't doing
 * this often enough to be worth making tostring into a specialized
 * copy function. I think.
 */
struct pqlvalue *tupleset_tostring(struct pqlcontext *pql,
				   const struct tupleset *ts) {
   struct tupleset stringts;
   struct tuplevaluearray lastseen;
   struct pqlvaluearray blankstrings;
   struct pqlvaluearray rowstrings, valuestrings;
   struct tupleslice *islice, *lastslice;
   struct tuplevalue *jval;
   struct pqlvalue *val;
   unsigned *widths, len;
   unsigned numslices, numvalues, i, j;
   bool blanking;
   char *s;

   /* If the set is empty, bail early */
   numslices = tupleslicearray_num(&stringts.slices);
   if (numslices == 0) {
      return pqlvalue_string(pql, "{\n}");
   }

   /* Make a tupleset of strings. */
   tupleset_init(pql, &stringts);
   tupleset_copy(pql, &stringts, ts);
   tupleset_map_tostring(pql, &stringts);

   /* initialize lastseen[] */
   tuplevaluearray_init(&lastseen);
   tuplevaluearray_setsize(pql, &lastseen, numslices);
   for (i=0; i<numslices; i++) {
      tuplevaluearray_set(&lastseen, i, NULL);
   }

   /* compute width for each column */
   widths = domalloc(pql, numslices * sizeof(*widths));
   for (i=0; i<numslices; i++) {
      widths[i] = 0;

      /* get the slice for this column */
      islice = tupleslicearray_get(&stringts.slices, i);

      /* extract the maximum width found */
      numvalues = tuplevaluearray_num(&islice->elements);
      for (j=0; j<numvalues; j++) {
	 val = tuplevaluearray_get(&islice->elements, i)->val;
	 len = strlen(pqlvalue_string_get(val));
	 if (len > widths[i]) {
	    widths[i] = len;
	 }
      }
   }

   /* pad each column's string out to the width */
   for (i=0; i<numslices; i++) {
      islice = tupleslicearray_get(&stringts.slices, i);
      numvalues = tuplevaluearray_num(&islice->elements);
      for (j=0; j<numvalues; j++) {
	 val = tuplevaluearray_get(&islice->elements, i)->val;
	 pqlvalue_string_padonleft(val, widths[i]);
      }
   }

   /* make a blank string of the right length for each column */
   pqlvaluearray_init(&blankstrings);
   pqlvaluearray_setsize(pql, &blankstrings, numslices);
   for (i=0; i<numslices; i++) {
      s = domalloc(pql, widths[i]+1);
      memset(s, ' ', widths[i]);
      s[widths[i]] = 0;
      pqlvaluearray_set(&blankstrings, i, pqlvalue_string_consume(pql, s));
   }

   /* Fold to one string per element, then one string */
   pqlvaluearray_init(&rowstrings);
   pqlvaluearray_init(&valuestrings);

   lastslice = tupleslicearray_get(&stringts.slices, numslices-1);
   numvalues = tuplevaluearray_num(&lastslice->elements);

   pqlvaluearray_setsize(pql, &rowstrings, numvalues);
   pqlvaluearray_setsize(pql, &valuestrings, numslices);

   for (j=0; j<numvalues; j++) {
      blanking = false;

      /* start from leftmost value */
      jval = tuplevaluearray_get(&lastslice->elements, j);
      i = numslices-1;
      while (jval != NULL) {

	 /* is this the same guy we last saw in this column? */
	 if (jval == tuplevaluearray_get(&lastseen, i)) {
	    /* print just spaces here and to the left */
	    blanking = true;
	 }
	 else {
	    /* representation is a tree: once blanking stay blanking */
	    PQLASSERT(blanking == false);
	    /* remember who we saw here */
	    tuplevaluearray_set(&lastseen, i, jval);
	 }
	 /* pick the proper string */
	 pqlvaluearray_set(&valuestrings, i,
			   blanking ? pqlvaluearray_get(&blankstrings, i) :
			   jval->val);
	 /* next guy */
	 jval = jval->left;
	 i--;
      }
      /* make sure we came out even */
      PQLASSERT(i+1 == 0);

      /* make a string for this element */
      val = pqlvalue_string_fromlist(pql, &valuestrings, "(", ", ", ")");
      pqlvaluearray_set(&rowstrings, j, val);
   }

   /* make an overall value */
   val = pqlvalue_string_fromlist(pql, &rowstrings, "{\n   ", ",\n   ", "\n}");

   /* clean everything up */

   /* rowstrings and valuestrings are unowned pointers */
   pqlvaluearray_setsize(pql, &rowstrings, 0);
   pqlvaluearray_cleanup(pql, &rowstrings);
   pqlvaluearray_setsize(pql, &valuestrings, 0);
   pqlvaluearray_cleanup(pql, &valuestrings);

   /* same with lastseen */
   tuplevaluearray_setsize(pql, &lastseen, 0);
   tuplevaluearray_cleanup(pql, &lastseen);

   /* but not blankstrings */
   pqlvaluearray_destroymembers(pql, &blankstrings);
   pqlvaluearray_cleanup(pql, &blankstrings);

   dofree(pql, widths, numslices * sizeof(*widths));
   tupleset_cleanup(pql, &stringts);

   return val;
}

void vr_treeset_copy(struct pqlcontext *pql,
		     struct vr_treeset *dst,
		     const struct vr_treeset *src) {
   tupleset_init(pql, &dst->val);
   tupleset_copy(pql, &dst->val, &src->val);
}
