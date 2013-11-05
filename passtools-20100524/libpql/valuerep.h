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

#ifndef VALUEREP_H
#define VALUEREP_H

/*
 * Value representations.
 *
 * This file is only supposed to be included by valuerep*.c and pqlvalue.c.
 */

#include <string.h>

#include "pqlvalue.h"

#define NUMBER_CMP(a, b) (((a) < (b)) ? -1 : ((a) == (b)) ? 0 : 1)

////////////////////////////////////////////////////////////

/*
 * Base value types.
 *
 * All this type wrapping is probably not necessary but for the moment
 * it feels like it's helping structure things.
 */

struct vr_cbool {
   bool val;
};

struct vr_cint {
   int val;
};

struct vr_cdouble {
   double val;
};

void vr_cbool_init(struct vr_cbool *rep);
void vr_cbool_cleanup(struct vr_cbool *rep);
void vr_cbool_copy(struct vr_cbool *dst, const struct vr_cbool *src);
bool vr_cbool_get(const struct vr_cbool *rep);
void vr_cbool_set(struct vr_cbool *rep, bool val);
int vr_cbool_cmp(const struct vr_cbool *a, const struct vr_cbool *b);

void vr_cint_init(struct vr_cint *rep);
void vr_cint_cleanup(struct vr_cint *rep);
void vr_cint_copy(struct vr_cint *dst, const struct vr_cint *src);
int vr_cint_get(const struct vr_cint *rep);
void vr_cint_set(struct vr_cint *rep, int val);
int vr_cint_cmp(const struct vr_cint *a, const struct vr_cint *b);

void vr_cdouble_init(struct vr_cdouble *rep);
void vr_cdouble_cleanup(struct vr_cdouble *rep);
void vr_cdouble_copy(struct vr_cdouble *dst, const struct vr_cdouble *src);
double vr_cdouble_get(const struct vr_cdouble *rep);
void vr_cdouble_set(struct vr_cdouble *rep, double val);
int vr_cdouble_cmp(const struct vr_cdouble *a, const struct vr_cdouble *b);

VALUE_INLINE void vr_cbool_init(struct vr_cbool *rep) {
   rep->val = false;
}
VALUE_INLINE void vr_cbool_cleanup(struct vr_cbool *rep) {
   (void)rep;
}
VALUE_INLINE void vr_cbool_copy(struct vr_cbool *dst,
				const struct vr_cbool *src) {
   dst->val = src->val;
}
VALUE_INLINE bool vr_cbool_get(const struct vr_cbool *rep) {
   return rep->val;
}
VALUE_INLINE void vr_cbool_set(struct vr_cbool *rep, bool val) {
   rep->val = val;
}
VALUE_INLINE int vr_cbool_cmp(const struct vr_cbool *a,
			      const struct vr_cbool *b) {
   return (a->val == b->val) ? 0 : a->val ? 1 : -1;
}

VALUE_INLINE void vr_cint_init(struct vr_cint *rep) {
   rep->val = 0;
}
VALUE_INLINE void vr_cint_cleanup(struct vr_cint *rep) {
   (void)rep;
}
VALUE_INLINE void vr_cint_copy(struct vr_cint *dst, const struct vr_cint *src){
   dst->val = src->val;
}
VALUE_INLINE int vr_cint_get(const struct vr_cint *rep) {
   return rep->val;
}
VALUE_INLINE void vr_cint_set(struct vr_cint *rep, int val) {
   rep->val = val;
}
VALUE_INLINE int vr_cint_cmp(const struct vr_cint *a,
			     const struct vr_cint *b) {
   return NUMBER_CMP(a->val, b->val);
}

VALUE_INLINE void vr_cdouble_init(struct vr_cdouble *rep) {
   rep->val = 0.0;
}
VALUE_INLINE void vr_cdouble_cleanup(struct vr_cdouble *rep) {
   (void)rep;
}
VALUE_INLINE void vr_cdouble_copy(struct vr_cdouble *dst,
				  const struct vr_cdouble *src) {
   dst->val = src->val;
}
VALUE_INLINE double vr_cdouble_get(const struct vr_cdouble *rep) {
   return rep->val;
}
VALUE_INLINE void vr_cdouble_set(struct vr_cdouble *rep, double val) {
   rep->val = val;
}
VALUE_INLINE int vr_cdouble_cmp(const struct vr_cdouble *a,
				const struct vr_cdouble *b) {
   return NUMBER_CMP(a->val, b->val);
}

////////////////////////////////////////////////////////////

/*
 * Strings
 */

struct vr_cstring {
   char *val;
};

void vr_cstring_init(struct vr_cstring *rep);
void vr_cstring_cleanup(struct pqlcontext *pql, struct vr_cstring *rep);
void vr_cstring_copy(struct pqlcontext *pql,
		     struct vr_cstring *dst, const struct vr_cstring *src);
const char *vr_cstring_get(const struct vr_cstring *rep);
void vr_cstring_set(struct pqlcontext *pql,
		    struct vr_cstring *rep, const char *val);
void vr_cstring_set_bylen(struct pqlcontext *pql,
			  struct vr_cstring *rep, const char *val, size_t len);
void vr_cstring_set_consume(struct pqlcontext *pql,
			    struct vr_cstring *rep, char *val);
int vr_cstring_cmp(const struct vr_cstring *a, const struct vr_cstring *b);

VALUE_INLINE void vr_cstring_init(struct vr_cstring *rep) {
   rep->val = NULL;
}
VALUE_INLINE void vr_cstring_cleanup(struct pqlcontext *pql,
				     struct vr_cstring *rep) {
   dostrfree(pql, rep->val);
   rep->val = NULL;
}
VALUE_INLINE void vr_cstring_copy(struct pqlcontext *pql,
				  struct vr_cstring *dst,
				  const struct vr_cstring *src) {
   dst->val = dostrdup(pql, src->val);
}
VALUE_INLINE const char *vr_cstring_get(const struct vr_cstring *rep) {
   return rep->val;
}
VALUE_INLINE void vr_cstring_set(struct pqlcontext *pql,
				 struct vr_cstring *rep, const char *val) {
   dostrfree(pql, rep->val);
   rep->val = dostrdup(pql, val);
}
VALUE_INLINE void vr_cstring_set_bylen(struct pqlcontext *pql,
				       struct vr_cstring *rep, const char *val,
				       size_t len) {
   dostrfree(pql, rep->val);
   rep->val = dostrndup(pql, val, len);
}
VALUE_INLINE void vr_cstring_set_consume(struct pqlcontext *pql,
					 struct vr_cstring *rep, char *val) {
   dostrfree(pql, rep->val);
   rep->val = val;
}
VALUE_INLINE int vr_cstring_cmp(const struct vr_cstring *a,
				const struct vr_cstring *b) {
   return strcmp(a->val, b->val);
}

////////////////////////////////////////////////////////////

/*
 * Arrays of pqlvalues
 */

struct vr_array {
   struct pqlvaluearray vals;
};

void vr_array_init(struct vr_array *rep);
void vr_array_cleanup(struct pqlcontext *pql, struct vr_array *rep);
void vr_array_copy(struct pqlcontext *pql,
		   struct vr_array *dst, const struct vr_array *src);
unsigned vr_array_num(const struct vr_array *rep);
void vr_array_setsize(struct pqlcontext *pql, struct vr_array *rep, unsigned num);
const struct pqlvalue *vr_array_get(const struct vr_array *rep, unsigned ix);
struct pqlvalue *vr_array_getformodify(const struct vr_array *rep,unsigned ix);
void vr_array_set(struct vr_array *, unsigned ix, struct pqlvalue *);
struct pqlvalue *vr_array_replace(struct vr_array *, unsigned ix, 
				  struct pqlvalue *);
void vr_array_add(struct pqlcontext *pql,struct vr_array *, struct pqlvalue *);
struct pqlvalue *vr_array_drop(struct vr_array *, unsigned ix);

/*
 * initialize and clean up
 */

VALUE_INLINE void vr_array_init(struct vr_array *rep) {
   pqlvaluearray_init(&rep->vals);
}

VALUE_INLINE void vr_array_cleanup(struct pqlcontext *pql, struct vr_array *rep) {
   struct pqlvalue *val;
   unsigned i, num;

   num = pqlvaluearray_num(&rep->vals);
   for (i=0; i<num; i++) {
      val = pqlvaluearray_get(&rep->vals, i);
      if (val) {
	 pqlvalue_destroy(val);
      }
   }
   pqlvaluearray_setsize(pql, &rep->vals, 0);
   pqlvaluearray_cleanup(pql, &rep->vals);
}

/*
 * size operations
 */

VALUE_INLINE unsigned vr_array_num(const struct vr_array *rep) {
   return pqlvaluearray_num(&rep->vals);
}

VALUE_INLINE void vr_array_setsize(struct pqlcontext *pql, struct vr_array *rep, unsigned num) {
   // XXX what about checking for error?
   pqlvaluearray_setsize(pql, &rep->vals, num);
}

/*
 * Get entry.
 */
VALUE_INLINE const struct pqlvalue *vr_array_get(const struct vr_array *rep,
						 unsigned ix) {
   return pqlvaluearray_get(&rep->vals, ix);
}

/*
 * Get modifiable reference to an entry.
 * (should probably go away)
 */
VALUE_INLINE struct pqlvalue *vr_array_getformodify(const struct vr_array *rep,
						 unsigned ix) {
   return pqlvaluearray_get(&rep->vals, ix);
}

/*
 * Set entry. Consumes the reference to VAL.
 */
VALUE_INLINE void vr_array_set(struct vr_array *rep, unsigned ix,
			       struct pqlvalue *val) {
   pqlvaluearray_set(&rep->vals, ix, val);
}

/*
 * Replace an entry.
 *
 * Consumes NEWVAL and returns master reference to the old value, which
 * should (generally) be destroyed.
 *
 * Note that newval is sometimes null; the eval code uses an idiom where
 * a value is replaced with null while being modified, and then the
 * null is replaced with the updated value.
 */
VALUE_INLINE struct pqlvalue *vr_array_replace(struct vr_array *rep,
					       unsigned ix, 
					       struct pqlvalue *newval) {
   struct pqlvalue *oldval;

   oldval = pqlvaluearray_get(&rep->vals, ix);
   pqlvaluearray_set(&rep->vals, ix, newval);
   return oldval;
}

/*
 * Append an entry at the end of the array.
 *
 * Consumes VAL.
 */
VALUE_INLINE void vr_array_add(struct pqlcontext *pql,
			       struct vr_array *rep,
			       struct pqlvalue *val) {
   // XXX check for error
   pqlvaluearray_add(pql, &rep->vals, val, NULL);
}

/*
 * Remove an entry, including from the middle.
 * 
 * Returns value previously stored. (Which should generally be destroyed.)
 */
VALUE_INLINE struct pqlvalue *vr_array_drop(struct vr_array *rep, unsigned ix) {
   struct pqlvalue *ret;

   ret = pqlvaluearray_get(&rep->vals, ix);
   pqlvaluearray_remove(&rep->vals, ix);
   return ret;
}

/*
 * Clone the contents of SRC into DST.
 */
VALUE_INLINE void vr_array_copy(struct pqlcontext *pql,
				struct vr_array *dst,
				const struct vr_array *src) {
   unsigned i, num;

   vr_array_init(dst);
   num = vr_array_num(src);
   vr_array_setsize(pql, dst, num);
   for (i=0; i<num; i++) {
      vr_array_set(dst, i, pqlvalue_clone(pql, vr_array_get(src, i)));
   }
}

////////////////////////////////////////////////////////////

/*
 * Arrays of pqlvalues with duplicate counts
 * (Largely the same as the plain arrays; there are some unresolved
 * questions about iterating though.)
 */

struct vr_duparray {
   struct pqlvaluearray vals;
   unsigned *counts;
};

void vr_duparray_init(struct vr_duparray *rep);
void vr_duparray_cleanup(struct pqlcontext *pql, struct vr_duparray *rep);
void vr_duparray_copy(struct pqlcontext *pql,
		      struct vr_duparray *dst, const struct vr_duparray *src);
unsigned vr_duparray_num(const struct vr_duparray *rep);
void vr_duparray_setsize(struct pqlcontext *pql,
			 struct vr_duparray *rep, unsigned num);
const struct pqlvalue *vr_duparray_get(const struct vr_duparray *rep,
				       unsigned ix);
unsigned vr_duparray_getdups(const struct vr_duparray *rep, unsigned ix);
void vr_duparray_set(struct pqlcontext *,
		     struct vr_duparray *rep, unsigned ix,
		     const struct pqlvalue *val);
void vr_duparray_setdups(const struct vr_duparray *rep,
			 unsigned ix, unsigned val);
void vr_duparray_add(struct pqlcontext *pql,
		     struct vr_duparray *rep,
		     const struct pqlvalue *val, unsigned dups);


VALUE_INLINE void vr_duparray_init(struct vr_duparray *rep) {
   pqlvaluearray_init(&rep->vals);
   rep->counts = NULL;
}

VALUE_INLINE void vr_duparray_cleanup(struct pqlcontext *pql,
				      struct vr_duparray *rep) {
   unsigned i, num;

   num = pqlvaluearray_num(&rep->vals);
   for (i=0; i<num; i++) {
      pqlvalue_destroy(pqlvaluearray_get(&rep->vals, i));
   }
   pqlvaluearray_cleanup(pql, &rep->vals);
   dofree(pql, rep->counts, num * sizeof(rep->counts[0]));
   rep->counts = NULL;
}

VALUE_INLINE unsigned vr_duparray_num(const struct vr_duparray *rep) {
   return pqlvaluearray_num(&rep->vals);
}

VALUE_INLINE void vr_duparray_setsize(struct pqlcontext *pql,
				      struct vr_duparray *rep,
				      unsigned num) {
   unsigned oldnum;
   // XXX what about checking for error?
   oldnum = pqlvaluearray_num(&rep->vals);
   pqlvaluearray_setsize(pql, &rep->vals, num);
   rep->counts = dorealloc(pql, rep->counts, oldnum*sizeof(rep->counts[0]),
			   num*sizeof(rep->counts[0]));
   PQLASSERT(rep->counts != NULL);
}

VALUE_INLINE const struct pqlvalue *
vr_duparray_get(const struct vr_duparray *rep, unsigned ix) {
   return pqlvaluearray_get(&rep->vals, ix);
}

VALUE_INLINE unsigned vr_duparray_getdups(const struct vr_duparray *rep,
					  unsigned ix) {
   PQLASSERT(ix < pqlvaluearray_num(&rep->vals));
   return rep->counts[ix];
}

VALUE_INLINE void vr_duparray_set(struct pqlcontext *pql,
				  struct vr_duparray *rep, unsigned ix,
				  const struct pqlvalue *val) {
   struct pqlvalue *old;

   old = pqlvaluearray_get(&rep->vals, ix);
   if (old != NULL) {
      pqlvalue_destroy(old);
   }
   pqlvaluearray_set(&rep->vals, ix, pqlvalue_clone(pql, val));
}

VALUE_INLINE void vr_duparray_setdups(const struct vr_duparray *rep,
				      unsigned ix, unsigned val) {
   PQLASSERT(ix < pqlvaluearray_num(&rep->vals));
   rep->counts[ix] = val;
}

VALUE_INLINE void vr_duparray_add(struct pqlcontext *pql,
				  struct vr_duparray *rep,
				  const struct pqlvalue *val, unsigned dups) {
   unsigned num;

   // XXX what about checking for error?
   pqlvaluearray_add(pql, &rep->vals, pqlvalue_clone(pql, val), NULL);
   num = pqlvaluearray_num(&rep->vals);
   rep->counts = dorealloc(pql, rep->counts, (num-1)*sizeof(rep->counts[0]),
			   num*sizeof(rep->counts[0]));
   PQLASSERT(rep->counts != NULL);
   rep->counts[num-1] = dups;
}

VALUE_INLINE void vr_duparray_copy(struct pqlcontext *pql,
				   struct vr_duparray *dst,
				   const struct vr_duparray *src) {
   unsigned i, num;

   vr_duparray_init(dst);
   num = vr_duparray_num(src);
   vr_duparray_setsize(pql, dst, num);
   for (i=0; i<num; i++) {
      vr_duparray_set(pql,dst, i, pqlvalue_clone(pql,vr_duparray_get(src, i)));
      vr_duparray_setdups(dst, i, vr_duparray_getdups(src, i));
   }
}

////////////////////////////////////////////////////////////

/*
 * Objects out of the database
 */

struct vr_dbobj {
   int dbnum;
   pqloid_t oid;
   pqlsubid_t subid;
};

void vr_dbobj_init(struct vr_dbobj *rep);
void vr_dbobj_cleanup(struct vr_dbobj *rep);
void vr_dbobj_copy(struct vr_dbobj *dst, const struct vr_dbobj *src);
int vr_dbobj_getdbnum(const struct vr_dbobj *rep);
pqloid_t vr_dbobj_getoid(const struct vr_dbobj *rep);
pqlsubid_t vr_dbobj_getsubid(const struct vr_dbobj *rep);
void vr_dbobj_setdbnum(struct vr_dbobj *rep, int val);
void vr_dbobj_setoid(struct vr_dbobj *rep, pqloid_t val);
void vr_dbobj_setsubid(struct vr_dbobj *rep, pqlsubid_t val);
struct pqlvalue *vr_dbobj_lookup(struct vr_dbobj *rep);
void vr_dbobj_modify(struct vr_dbobj *rep, const struct pqlvalue *val);


VALUE_INLINE void vr_dbobj_init(struct vr_dbobj *rep) {
   rep->dbnum = -1;
   rep->oid = 0;
   rep->subid = 0;
}
VALUE_INLINE void vr_dbobj_cleanup(struct vr_dbobj *rep) {
   (void)rep;
}
VALUE_INLINE void vr_dbobj_copy(struct vr_dbobj *dst,
				const struct vr_dbobj *src) {
   *dst = *src;
}
VALUE_INLINE int vr_dbobj_getdbnum(const struct vr_dbobj *rep) {
   return rep->dbnum;
}
VALUE_INLINE pqloid_t vr_dbobj_getoid(const struct vr_dbobj *rep) {
   return rep->oid;
}
VALUE_INLINE pqlsubid_t vr_dbobj_getsubid(const struct vr_dbobj *rep) {
   return rep->subid;
}
VALUE_INLINE void vr_dbobj_setdbnum(struct vr_dbobj *rep, int val) {
   rep->dbnum = val;
}
VALUE_INLINE void vr_dbobj_setoid(struct vr_dbobj *rep, pqloid_t val) {
   rep->oid = val;
}
VALUE_INLINE void vr_dbobj_setsubid(struct vr_dbobj *rep, pqlsubid_t val) {
   rep->subid = val;
}

////////////////////////////////////////////////////////////

/*
 * Tree based tuple set representation.
 *
 * Given
 *
 *           / C3 - D4
 *        B2 - C2 - D3
 *      /         / D2
 *    A - B1 - C1 - D1
 *
 * we call a vertical section (e.g. B1 and B2) a "slice".
 *
 * struct tuplevalue holds a single element, which is a pqlvalue
 * plus neighbor pointers.
 *
 * struct tupleslice holds a slice, which is an array of tuplevalues.
 * Index 0 is at the "bottom" (as in the diagram).
 *
 * A whole tupleset is an array of tupleslices. Index 0 is at the
 * "left" (as in the diagram). Thus, the root value is
 * val->tupleset.slices[0]->elements[0].
 *
 * The number of elements in any particular slice depends on the tree
 * structure, except that the number of elements in the rightmost
 * slice is ipso facto the number of tuples in the tuple set.
 */

struct tuplevalue;
struct tupleslice;
DECLARRAY(tuplevalue);
DECLARRAY(tupleslice);

struct tuplevalue {
   struct tuplevalue *up;	/* Next value in same slice */
   struct tuplevalue *down;	/* Previous value in same slice */
   struct tuplevalue *left;	/* Corresponding value in parent slice */
   struct tuplevaluearray right;/* Corresponding values in child slices */
   unsigned sliceindex;		/* Index in this slice */

   struct pqlvalue *val;	/* Value found here */
};

struct tupleslice {
   struct tuplevaluearray elements;
   unsigned setindex;		/* Index in this set */
};

struct tupleset {
   struct tupleslicearray slices;
};

void tupleset_init(struct pqlcontext *pql, struct tupleset *);
void tupleset_cleanup(struct pqlcontext *pql, struct tupleset *);
struct pqlvalue *tupleset_tostring(struct pqlcontext *pql,
				   const struct tupleset *ts);

struct vr_treeset {
   struct tupleset val;
};

void vr_treeset_init(struct pqlcontext *pql, struct vr_treeset *rep);
void vr_treeset_cleanup(struct pqlcontext *pql, struct vr_treeset *rep);
void vr_treeset_copy(struct pqlcontext *pql,
		     struct vr_treeset *dst, const struct vr_treeset *src);

VALUE_INLINE void vr_treeset_init(struct pqlcontext *pql,
				  struct vr_treeset *rep) {
   tupleset_init(pql, &rep->val);
}
VALUE_INLINE void vr_treeset_cleanup(struct pqlcontext *pql,
				     struct vr_treeset *rep) {
   tupleset_cleanup(pql, &rep->val);
}

////////////////////////////////////////////////////////////

#endif /* VALUEREP_H */
