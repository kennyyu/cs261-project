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

#include <stdio.h>
#include <string.h>

#include "pqlcontext.h"
#include "layout.h"   // XXX: do we want this in here?
#include "columns.h"

/*
 * Each column in a tuple or tuple set has a column name. These names
 * are used in project, nest, and other relational operations to
 * choose portions of the tuple set to operate on.
 *
 * If you project a single column out of a tuple set, it retains its
 * name. This appears to be necessary, although maybe it isn't really.
 *
 * A named column may have another tuple set nested inside it, with
 * its own column names. For this reason the complete name information
 * is tree-structured. (Not graph-structured, though.)
 *
 * If you project such a column out, it ends up with a name-as-
 * singleton as well as member names.
 *
 * Column names appear in the source language as named variables, but
 * not every column comes with a user-supplied name or even a name
 * supplied by the "fresh" name generator upstream of tuplify. We need
 * additional fresh columns for various things at various points.
 * These are numbered.
 *
 * Furthermore, sets and sequences are (mostly) irrelevant to column
 * handling; a tuple (A, B) has two columns, and so does a tuple set
 * {( A, B )}.
 *
 * These factors govern the layout of the data structures.
 *
 * There has been some question about whether the name-as-singleton
 * creates ambiguity or confusion when applied to a value that also
 * has columns. In particular, if you write A |x| B, under what
 * circumstances do you mean to treat B like a set of one column and
 * under what circumstances do you mean to treat it like a set of
 * multiple columns?
 *
 * I think there is no problem and that the current confusion arises
 * from ignoring the set types in here. Because nested tuples aren't
 * allowed (only nested tuple sets/sequences) if you have a set of
 * pairs, you can't join it to another tuple set as a single column,
 * because that gives a column whose members are pairs, not sets of
 * pairs. If you have a set of *sets* of pairs, then it can *only* be
 * joined as a single column, which ends up containing one set in each
 * row of the output.
 *
 * I think this means we should represent sets in here as a wrapper
 * layer in the data structure, so we can tell where we have top-level
 * columns: they show through one layer of set but not two.
 *
 * This should be done sometime. (XXX)
 */

/*
 * Turn this on to maintain coltree->name, useful from inside the
 * debugger.
 */
#define COLTREE_NAME

struct colname {
   char *name;
   unsigned id;
   unsigned refcount;
};

struct colset {
   struct colnamearray cols;
   bool tocomplement;
};

struct coltree {
   struct colname *wholecolumn;
   bool istuple;
   struct coltreearray subnames;
#ifdef COLTREE_NAME
   char *name;
#endif
};

DEFARRAY(colname, );
DEFARRAY(coltree, );


////////////////////////////////////////////////////////////
// colname ops

struct colname *mkcolname(struct pqlcontext *pql, const char *name) {
   struct colname *tccol;

   tccol = domalloc(pql, sizeof(*tccol));
   tccol->name = dostrdup(pql, name);
   /* XXX: should we share the columnid if copying a colnamevar? */
   /* XXX: and if not should we be sharing the same ID counter? */
   tccol->id = pql->nextcolumnid++;
   tccol->refcount = 1;

   return tccol;
}

struct colname *mkcolname_fresh(struct pqlcontext *pql) {
   struct colname *tccol;

   tccol = domalloc(pql, sizeof(*tccol));
   tccol->name = NULL;
   /* XXX: should we be sharing the same ID counter as colnamevars? */
   tccol->id = pql->nextcolumnid++;
   tccol->refcount = 1;

   return tccol;
}

void colname_incref(struct colname *tccol) {
   PQLASSERT(tccol != NULL);
   tccol->refcount++;
}

void colname_decref(struct pqlcontext *pql, struct colname *tccol) {
   PQLASSERT(tccol != NULL);
   PQLASSERT(tccol->refcount > 0);

   tccol->refcount--;
   if (tccol->refcount == 0) {
      dostrfree(pql, tccol->name);
      dofree(pql, tccol, sizeof(*tccol));
   }
}

const char *colname_getname(struct pqlcontext *pql, struct colname *tc) {
   if (tc == NULL) {
      return "<nocolumn>";
   }
   if (tc->name == NULL) {
      // XXX this needs to match what's in tcalc.c for the dumps
      // XXX (why don't we always materialize the name?)
      char buf[32];
      snprintf(buf, sizeof(buf), ".C%u", tc->id);
      tc->name = dostrdup(pql, buf);
   }
   return tc->name;
}

struct layout *colname_layout(struct pqlcontext *pql, struct colname *col) {
   if (col == NULL) {
      // XXX should assert here -- why are we getting this?
      return mklayout_text(pql, "???");
   }

   return mklayout_text(pql, colname_getname(pql, col));
}

////////////////////////////////////////////////////////////
// ops on colnamearrays

/*
 * Check if ARR contains COL.
 */
static bool colnamearray_contains(struct colnamearray *arr,
				   struct colname *col) {
   unsigned i, num;

   num = colnamearray_num(arr);
   for (i=0; i<num; i++) {
      if (colnamearray_get(arr, i) == col) {
	 return true;
      }
   }
   return false;
}

/*
 * Copy FROM into TO, increasing refcounts.
 */
static void colnamearray_copy(struct pqlcontext *pql,
			      struct colnamearray *to,
			      const struct colnamearray *from) {
   unsigned i, num;
   struct colname *col;

   num = colnamearray_num(from);
   colnamearray_setsize(pql, to, num);
   for (i=0; i<num; i++) {
      col = colnamearray_get(from, i);
      colname_incref(col);
      colnamearray_set(to, i, col);
   }
}

/*
 * Replace OLDCOL with NEWCOL in ARR.
 */
static void colnamearray_replace(struct pqlcontext *pql,
				 struct colnamearray *arr,
				 struct colname *oldcol,
				 struct colname *newcol) {
   unsigned i, num;

   num = colnamearray_num(arr);
   for (i=0; i<num; i++) {
      if (colnamearray_get(arr, i) == oldcol) {
	 colname_decref(pql, oldcol);
	 colname_incref(newcol);
	 colnamearray_set(arr, i, newcol);
      }
   }
}

/*
 * Remove COL from ARR.
 */
static void colnamearray_removeone(struct colnamearray *arr,
				    struct colname *col) {
   unsigned i, num;

   num = colnamearray_num(arr);
   for (i=0; i<num; i++) {
      if (colnamearray_get(arr, i) == col) {
	 colnamearray_remove(arr, i);
	 return;
      }
   }
   PQLASSERT(0);
}


/*
 * Move columns from FROM to TO.
 */
static void colnamearray_moveappend(struct pqlcontext *pql,
				    struct colnamearray *to,
				    struct colnamearray *from) {
   unsigned i, num;
   struct colname *col;

   num = colnamearray_num(from);
   for (i=0; i<num; i++) {
      col = colnamearray_get(from, i);
      colnamearray_add(pql, to, col, NULL);
   }
   colnamearray_setsize(pql, from, 0);
}

/*
 * Pretty-print.
 */
static struct layout *colnamearray_layout(struct pqlcontext *pql,
					   struct colnamearray *cols) {
   unsigned i, num;
   struct layout *ret = NULL, *sublayout;

   num = colnamearray_num(cols);
   for (i=0; i<num; i++) {
      sublayout = colname_layout(pql, colnamearray_get(cols, i));
      if (ret != NULL) {
	 ret = mklayout_triple(pql, ret, mklayout_text(pql, ","),
			       sublayout);
      }
      else {
	 ret = sublayout;
      }
   }
   if (ret == NULL) {
      // XXX should do something better than this
      ret = mklayout_text(pql, "--");
   }
   return ret;
}

////////////////////////////////////////////////////////////
// colset ops

static struct colset *colset_create(struct pqlcontext *pql) {
   struct colset *cs;

   (void)pql;

   cs = domalloc(pql, sizeof(*cs));
   colnamearray_init(&cs->cols);
   cs->tocomplement = false;

   return cs;
}

struct colset *colset_empty(struct pqlcontext *pql) {
   return colset_create(pql);
}

struct colset *colset_singleton(struct pqlcontext *pql, struct colname *col) {
   struct colset *cs;

   cs = colset_create(pql);
   colnamearray_add(pql, &cs->cols, col, NULL);
   return cs;
}

struct colset *colset_pair(struct pqlcontext *pql, struct colname *col1,
			   struct colname *col2) {
   struct colset *cs;

   cs = colset_create(pql);
   colnamearray_add(pql, &cs->cols, col1, NULL);
   colnamearray_add(pql, &cs->cols, col2, NULL);
   return cs;
}

struct colset *colset_triple(struct pqlcontext *pql, struct colname *col1,
			     struct colname *col2, struct colname *col3) {
   struct colset *cs;

   cs = colset_create(pql);
   colnamearray_add(pql, &cs->cols, col1, NULL);
   colnamearray_add(pql, &cs->cols, col2, NULL);
   colnamearray_add(pql, &cs->cols, col3, NULL);
   return cs;
}

struct colset *colset_fromcoltree(struct pqlcontext *pql,
				  const struct coltree *oldct) {
   struct colset *newset;
   struct colname *name;
   unsigned i, num;

   newset = colset_create(pql);
   if (!oldct->istuple) {
      colname_incref(oldct->wholecolumn);
      colnamearray_add(pql, &newset->cols, oldct->wholecolumn, NULL);
   }
   else {
      num = coltree_num(oldct);
      for (i=0; i<num; i++) {
	 name = coltree_get(oldct, i);
	 colname_incref(name);
	 colnamearray_add(pql, &newset->cols, name, NULL);
      }
   }
   return newset;
}

struct colset *colset_clone(struct pqlcontext *pql, struct colset *oldset) {
   struct colset *newset;

   newset = colset_create(pql);
   colnamearray_copy(pql, &newset->cols, &oldset->cols);
   newset->tocomplement = oldset->tocomplement;
   return newset;
}

void colset_destroy(struct pqlcontext *pql, struct colset *cs) {
   struct colname *name;
   unsigned i, num;

   num = colnamearray_num(&cs->cols);
   for (i=0; i<num; i++) {
      name = colnamearray_get(&cs->cols, i);
      if (name != NULL) {
	 colname_decref(pql, name);
      }
   }
   colnamearray_setsize(pql, &cs->cols, 0);
   colnamearray_cleanup(pql, &cs->cols);
   dofree(pql, cs, sizeof(*cs));
}

unsigned colset_num(const struct colset *cs) {
   PQLASSERT(!cs->tocomplement);
   return colnamearray_num(&cs->cols);
}

struct colname *colset_get(const struct colset *cs, unsigned index) {
   PQLASSERT(!cs->tocomplement);
   return colnamearray_get(&cs->cols, index);
}

// XXX should this handle the refcount itself?
void colset_set(struct colset *cs, unsigned index, struct colname *col) {
   PQLASSERT(!cs->tocomplement);
   colnamearray_set(&cs->cols, index, col);
}

void colset_add(struct pqlcontext *pql, struct colset *cs, struct colname *col) {
   PQLASSERT(!cs->tocomplement);
   colnamearray_add(pql, &cs->cols, col, NULL);
}

void colset_setsize(struct pqlcontext *pql, struct colset *cs, unsigned newarity) {
   unsigned i, oldarity;

   PQLASSERT(!cs->tocomplement);
   oldarity = colnamearray_num(&cs->cols);
   colnamearray_setsize(pql, &cs->cols, newarity);
   for (i=oldarity; i<newarity; i++) {
      colnamearray_set(&cs->cols, i, NULL);
   }
}

bool colset_contains(struct colset *cs, struct colname *col) {
   PQLASSERT(!cs->tocomplement);
   return colnamearray_contains(&cs->cols, col);
}

bool colset_eq(struct colset *as, struct colset *bs) {
   unsigned i, num;

   PQLASSERT(!as->tocomplement);
   PQLASSERT(!bs->tocomplement);

   num = colnamearray_num(&as->cols);
   if (num != colnamearray_num(&bs->cols)) {
      return false;
   }
   for (i=0; i<num; i++) {
      if (colnamearray_get(&as->cols, i) != colnamearray_get(&bs->cols, i)) {
	 return false;
      }
   }
   return true;
}

/*
 * Find COL in CS; if found return 0 and leave index in IX_RET,
 * if not found return -1.
 */
int colset_find(struct colset *cs, struct colname *col, unsigned *ix_ret) {
   unsigned num, i;

   PQLASSERT(!cs->tocomplement);

   num = colset_num(cs);
   for (i=0; i<num; i++) {
      if (colset_get(cs, i) == col) {
	 if (ix_ret != NULL) {
	    *ix_ret = i;
	 }
	 return 0;
      }
   }
   return -1;
}

void colset_moveappend(struct pqlcontext *pql,
		       struct colset *to, struct colset *from) {
   PQLASSERT(!to->tocomplement);
   PQLASSERT(!from->tocomplement);
   colnamearray_moveappend(pql, &to->cols, &from->cols);
}

void colset_replace(struct pqlcontext *pql,
		    struct colset *cs,
		    struct colname *oldcol, struct colname *newcol) {
   PQLASSERT(!cs->tocomplement);
   colnamearray_replace(pql, &cs->cols, oldcol, newcol);
}

void colset_remove(struct colset *cs, struct colname *col) {
   PQLASSERT(!cs->tocomplement);
   colnamearray_removeone(&cs->cols, col);
}

void colset_removebyindex(struct colset *cs, unsigned which) {
   PQLASSERT(!cs->tocomplement);
   colnamearray_remove(&cs->cols, which);
}

void colset_complement(struct pqlcontext *pql, struct colset *cs,
		       const struct coltree *context) {
   struct colset *ts;
   struct colname *n;
   unsigned i, num;

   PQLASSERT(!cs->tocomplement);

   /*
    * Not a particularly smart operation but a straightforward way to
    * do it.
    */

   ts = colset_fromcoltree(pql, context);

   num = colnamearray_num(&cs->cols);
   for (i=0; i<num; i++) {
      n = colnamearray_get(&cs->cols, i);
      colset_remove(ts, n);
      /* once for the ref in ts, once for the ref in cs->cols */
      colname_decref(pql, n);
      colname_decref(pql, n);
   }

   num = colnamearray_num(&ts->cols);
   colnamearray_setsize(pql, &cs->cols, num);
   for (i=0; i<num; i++) {
      n = colnamearray_get(&ts->cols, i);
      colname_incref(n);
      colset_set(cs, i, n);
   }

   colset_destroy(pql, ts);
}

void colset_mark_tocomplement(struct colset *cs) {
   cs->tocomplement = true;
}

void colset_resolve_tocomplement(struct pqlcontext *pql, struct colset *cs,
				 const struct coltree *context) {
   if (cs->tocomplement) {
      cs->tocomplement = false;
      colset_complement(pql, cs, context);
   }
}

struct layout *colset_layout(struct pqlcontext *pql, struct colset *cs) {
   struct layout *l;

   l = colnamearray_layout(pql, &cs->cols);
   if (cs->tocomplement) {
      l = mklayout_triple(pql, mklayout_text(pql, "~("), l,
			  mklayout_text(pql, ")"));
   }
   return l;
}

////////////////////////////////////////////////////////////
// coltree constructors

#ifdef COLTREE_NAME
/*
 * Update ct->name.
 */
static void coltree_setname(struct pqlcontext *pql, struct coltree *ct) {
   unsigned i, num;
   struct coltree *subtree;
   char buf[4096];

   if (!ct->istuple) {
      strlcpy(buf, colname_getname(pql, ct->wholecolumn), sizeof(buf));
   }
   else {
      strcpy(buf, "(");
      num = coltreearray_num(&ct->subnames);
      for (i=0; i<num; i++) {
	 subtree = coltreearray_get(&ct->subnames, i);
	 coltree_setname(pql, subtree);
	 if (i>0) {
	    strlcat(buf, ", ", sizeof(buf));
	 }
	 strlcat(buf, subtree->name, sizeof(buf));
      }
      strlcat(buf, ")", sizeof(buf));
   }

   dostrfree(pql, ct->name);
   ct->name = dostrdup(pql, buf);
}
#endif

/*
 * Create a coltree with no subnames at all. For use by other
 * constructors that are going to add things.
 */
static struct coltree *coltree_create_unfilled(struct pqlcontext *pql,
					       struct colname *wholecolumn) {
   struct coltree *ret;

   ret = domalloc(pql, sizeof(*ret));
   if (wholecolumn != NULL) {
      colname_incref(wholecolumn);
   }
   ret->wholecolumn = wholecolumn;
   ret->istuple = true;
   coltreearray_init(&ret->subnames);
#ifdef COLTREE_NAME
   ret->name = dostrdup(pql, "");
#endif

   return ret;
}

/*
 * Create a coltree for a non-tuple.
 */
struct coltree *coltree_create_scalar(struct pqlcontext *pql,
				      struct colname *wholecolumn) {
   struct coltree *ret;

   ret = coltree_create_unfilled(pql, wholecolumn);
   ret->istuple = false;
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

/*
 * Same, but calls mkcolname_fresh().
 */
struct coltree *coltree_create_scalar_fresh(struct pqlcontext *pql) {
   return coltree_create_scalar(pql, mkcolname_fresh(pql));
}

/*
 * Create a coltree for unit.
 */
struct coltree *coltree_create_unit(struct pqlcontext *pql,
				    struct colname *wholecolumn) {
   struct coltree *ret;

   ret = coltree_create_unfilled(pql, wholecolumn);
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

/*
 * Consumes the pair arguments. (hence mk...)
 */
static struct coltree *mkcoltree_pair(struct pqlcontext *pql,
				      struct coltree *a, struct coltree *b) {
   struct coltree *ret;

   ret = coltree_create_unfilled(pql, NULL);
   coltreearray_add(pql, &ret->subnames, a, NULL);
   coltreearray_add(pql, &ret->subnames, b, NULL);
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif
   
   return ret;
}


struct coltree *coltree_create_triple(struct pqlcontext *pql,
				      struct colname *wholecolumn,
				      struct colname *member0,
				      struct colname *member1,
				      struct colname *member2) {
   struct coltree *ret;

   ret = coltree_create_unfilled(pql, wholecolumn);
   coltreearray_add(pql, &ret->subnames, coltree_create_scalar(pql, member0), NULL);
   coltreearray_add(pql, &ret->subnames, coltree_create_scalar(pql, member1), NULL);
   coltreearray_add(pql, &ret->subnames, coltree_create_scalar(pql, member2), NULL);
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

struct coltree *coltree_create_tuple(struct pqlcontext *pql,
				     struct colname *wholecolumn,
				     struct colset *members) {
   struct colname *thismember;
   struct coltree *ret;
   unsigned i, num;

   ret = coltree_create_unfilled(pql, wholecolumn);
   num = colset_num(members);
   for (i=0; i<num; i++) {
      thismember = colset_get(members, i);
      coltreearray_add(pql,
		       &ret->subnames, coltree_create_scalar(pql, thismember),
		       NULL);
   }
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

struct coltree *coltree_clone(struct pqlcontext *pql, struct coltree *src) {
   struct coltree *ret, *thisname;
   unsigned i, num;

   if (src == NULL) {
      return NULL;
   }

   ret = coltree_create_unfilled(pql, src->wholecolumn);
   if (src->istuple) {
      num = coltreearray_num(&src->subnames);
      coltreearray_setsize(pql, &ret->subnames, num);
      for (i=0; i<num; i++) {
	 thisname = coltreearray_get(&src->subnames, i);
	 coltreearray_set(&ret->subnames, i, coltree_clone(pql, thisname));
      }
   }
   else {
      ret->istuple = false;
   }
#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

void coltree_destroy(struct pqlcontext *pql, struct coltree *ct) {
   unsigned i, num;

   if (ct->wholecolumn != NULL) {
      colname_decref(pql, ct->wholecolumn);
   }
   num = coltreearray_num(&ct->subnames);
   for (i=0; i<num; i++) {
      coltree_destroy(pql, coltreearray_get(&ct->subnames, i));
   }
   coltreearray_setsize(pql, &ct->subnames, 0);
   coltreearray_cleanup(pql, &ct->subnames);
#ifdef COLTREE_NAME
   dostrfree(pql, ct->name);
#endif
   dofree(pql, ct, sizeof(*ct));
}

////////////////////////////////////////////////////////////
// coltree misc ops

const char *coltree_getname(struct pqlcontext *pql, struct coltree *n) {
   if (n == NULL || n->wholecolumn == NULL) {
      return "NULL";
   }
   return colname_getname(pql, n->wholecolumn);
}

unsigned coltree_arity(struct coltree *ct) {
   PQLASSERT(ct != NULL);
   return ct->istuple ? coltreearray_num(&ct->subnames) : 1;
}

bool coltree_istuple(const struct coltree *ct) {
   return ct->istuple;
}

struct colname *coltree_wholecolumn(const struct coltree *ct) {
   return ct->wholecolumn;
}

unsigned coltree_num(const struct coltree *ct) {
   return coltreearray_num(&ct->subnames);
}

struct coltree *coltree_getsubtree(const struct coltree *ct, unsigned index) {
   return coltreearray_get(&ct->subnames, index);
}

struct colname *coltree_get(const struct coltree *ct, unsigned index) {
   return coltree_getsubtree(ct, index)->wholecolumn;
}

/*
 * Check if NAME contains COL in a position where it can be joined.
 */
bool coltree_contains_toplevel(const struct coltree *name,
			       struct colname *col) {
   struct colname *subcol;
   unsigned i, num;

   if (coltree_istuple(name)) {
      num = coltree_num(name);
      for (i=0; i<num; i++) {
	 subcol = coltree_get(name, i);
	 if (subcol == col) {
	    return true;
	 }
      }
   }
   else {
      if (coltree_wholecolumn(name) == col) {
	 return true;
      }
   }
   return false;
}

/*
 * Find COL in CT; if found return 0 and leave index in IX_RET,
 * if not found return -1.
 */
int coltree_find(struct coltree *ct, struct colname *col, unsigned *ix_ret) {
   struct coltree *subname;
   unsigned num, i;

   num = coltreearray_num(&ct->subnames);
   for (i=0; i<num; i++) {
      subname = coltreearray_get(&ct->subnames, i);
      PQLASSERT(subname != NULL);
      if (subname->wholecolumn == col) {
	 if (ix_ret != NULL) {
	    *ix_ret = i;
	 }
	 return 0;
      }
   }
   return -1;
}

bool coltree_eq_col(struct coltree *ct1, struct colname *cn2) {
   struct colname *wc;

   if (ct1 == NULL && cn2 == NULL) {
      return true;
   }

   /*
    * Check the wholecolumn first. Even if ct1 is a whole structure of
    * nested tuples, it's still the same as cn2 if cn2 names it.
    */
   wc = coltree_wholecolumn(ct1);
   if (wc != NULL && wc == cn2) {
      return true;
   }
   /* If ct1 is structured and has no overall name, they cannot be equal. */
   if (coltree_istuple(ct1)) {
      return false;
   }
   /*
    * This can be true only in the case that ct1 is an anonymous
    * scalar (so wc is null) and cn2 is also null.
    *
    * XXX I'm not sure that it makes sense to call that true. I'm also
    * not sure that it makes sense to pass null as cn2, either - part
    * of the point of giving everything (even scalars) a name was to
    * make sure we didn't have null names floating around.
    */
   return wc == cn2;
}

bool coltree_eq(struct coltree *ct1, struct coltree *ct2) {
   unsigned i, num1, num2;

   if (ct1 == NULL && ct2 == NULL) {
      return true;
   }
   if (ct1 == NULL || ct2 == NULL) {
      return false;
   }

   if (ct1->wholecolumn != ct2->wholecolumn) {
      return false;
   }

   num1 = coltreearray_num(&ct1->subnames);
   num2 = coltreearray_num(&ct2->subnames);

   if (!ct1->istuple && !ct2->istuple) {
      PQLASSERT(num1 == 0);
      PQLASSERT(num2 == 0);
      return true;
   }
   if (!ct1->istuple || !ct2->istuple) {
      return false;
   }

   if (num1 != num2) {
      return false;
   }

   for (i=0; i<num1; i++) {
      if (!coltree_eq(coltreearray_get(&ct1->subnames, i),
		      coltreearray_get(&ct2->subnames, i))) {
	 return false;
      }
   }

   return true;
}

/*
 * Remove column WHICH from CT, in place. Should not reduce the arity
 * to 1 (or 0). Caller must handle that case.
 */
void coltree_removebyindex(struct pqlcontext *pql, struct coltree *ct,
			   unsigned which) {
   struct coltree *victim;

   PQLASSERT(coltreearray_num(&ct->subnames) > 2);

   victim = coltreearray_get(&ct->subnames, which);
   coltreearray_remove(&ct->subnames, which);

   coltree_destroy(pql, victim);

#ifdef COLTREE_NAME
   coltree_setname(pql, ct);
#endif
}

/*
 * Replace OLDCOL with NEWCOL in CT, including recursively.
 * Handles refcounts.
 */
void coltree_replace(struct pqlcontext *pql, struct coltree *ct,
		     struct colname *oldcol, struct colname *newcol) {
   struct coltree *sub;
   unsigned i, num;

   if (ct->wholecolumn == oldcol) {
      if (oldcol != NULL) {
	 colname_decref(pql, oldcol);
      }
      colname_incref(newcol);
      ct->wholecolumn = newcol;
   }
   if (ct->istuple) {
      num = coltreearray_num(&ct->subnames);
      for (i=0; i<num; i++) {
	 sub = coltreearray_get(&ct->subnames, i);
	 coltree_replace(pql, sub, oldcol, newcol);
      }
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ct);
#endif
}

////////////////////////////////////////////////////////////
// coltree relational ops

struct coltree *coltree_project(struct pqlcontext *pql, struct coltree *src,
				struct colset *keep) {
   struct coltree *ret, *thisname;
   struct colname *thiscol;
   unsigned i, j, snum, knum;
   bool found;

   ret = coltree_create_unfilled(pql, src->wholecolumn);

   knum = colset_num(keep);

   if (!src->istuple) {
      for (i=0; i<knum; i++) {
	 if (colset_get(keep, i) == src->wholecolumn) {
	    ret->istuple = false;
	    break;
	 }
      }
#ifdef COLTREE_NAME
      coltree_setname(pql, ret);
#endif
      /* if we don't match this will be unit */
      return ret;
   }

   snum = coltreearray_num(&src->subnames);

   for (j=0; j<knum; j++) {
      thiscol = colset_get(keep, j);
      found = false;
      for (i=0; i<snum; i++) {
	 thisname = coltreearray_get(&src->subnames, i);
	 PQLASSERT(thisname != NULL);
	 if (thiscol == thisname->wholecolumn) {
	    found = true;
	    break;
	 }
      }
      if (!found) {
	 continue;
      }
      coltreearray_add(pql, &ret->subnames, coltree_clone(pql, thisname), NULL);
   }

   if (coltreearray_num(&ret->subnames) == 1 && ret->wholecolumn == NULL) {
      thisname = coltreearray_get(&ret->subnames, 0);
      ret->wholecolumn = thisname->wholecolumn;
      thisname->wholecolumn = NULL;
      coltree_destroy(pql, thisname);
      coltreearray_setsize(pql, &ret->subnames, 0);
      ret->istuple = false;
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif
   
   return ret;
}

struct coltree *coltree_strip(struct pqlcontext *pql, struct coltree *src,
			      struct colset *remove) {
   struct coltree *ret, *thisname;
   struct colname *thiscol;
   unsigned i, j, snum, rnum;
   bool found;

   ret = coltree_create_unfilled(pql, src->wholecolumn);

   rnum = colset_num(remove);

   if (!src->istuple) {
      ret->istuple = false;
      for (i=0; i<rnum; i++) {
	 if (colset_get(remove, i) == src->wholecolumn) {
	    /* make the result be unit */
	    ret->istuple = true;
	    break;
	 }
      }
#ifdef COLTREE_NAME
      coltree_setname(pql, ret);
#endif
      return ret;
   }

   snum = coltreearray_num(&src->subnames);

   for (i=0; i<snum; i++) {
      thisname = coltreearray_get(&src->subnames, i);
      PQLASSERT(thisname != NULL);
      found = false;
      for (j=0; j<rnum; j++) {
	 thiscol = colset_get(remove, j);
	 if (thiscol == thisname->wholecolumn) {
	    found = true;
	    break;
	 }
      }
      if (found) {
	 continue;
      }
      coltreearray_add(pql, &ret->subnames, coltree_clone(pql, thisname), NULL);
   }
   
   if (coltreearray_num(&ret->subnames) == 1 && ret->wholecolumn == NULL) {
      thisname = coltreearray_get(&ret->subnames, 0);
      ret->wholecolumn = thisname->wholecolumn;
      thisname->wholecolumn = NULL;
      coltree_destroy(pql, thisname);
      coltreearray_setsize(pql, &ret->subnames, 0);
      ret->istuple = false;
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif
   
   return ret;
}

struct coltree *coltree_rename(struct pqlcontext *pql, struct coltree *src,
			       struct colname *oldcol, struct colname *newcol){
   struct coltree *ret, *thisname;
   unsigned i, num;

   if (src == NULL) {
      return NULL;
   }

   ret = coltree_create_unfilled(pql, src->wholecolumn);

   if (ret->wholecolumn == oldcol) {
      if (oldcol != NULL) {
	 colname_decref(pql, oldcol);
      }
      if (newcol != NULL) {
	 colname_incref(newcol);
      }
      ret->wholecolumn = newcol;
   }

   if (src->istuple) {
      num = coltreearray_num(&src->subnames);
      coltreearray_setsize(pql, &ret->subnames, num);
      for (i=0; i<num; i++) {
	 thisname = coltreearray_get(&src->subnames, i);
	 thisname = coltree_rename(pql, thisname, oldcol, newcol);
	 coltreearray_set(&ret->subnames, i, thisname);
      }
   }
   else {
      ret->istuple = false;
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif
   
   return ret;
}

struct coltree *coltree_join(struct pqlcontext *pql,
			     struct coltree *left, struct coltree *right) {
   unsigned i, leftnum, rightnum;
   struct coltree *ret, *thisname;

   ret = coltree_create_unfilled(pql, NULL);

   leftnum = coltreearray_num(&left->subnames);
   rightnum = coltreearray_num(&right->subnames);

   if (!left->istuple) {
      coltreearray_add(pql, &ret->subnames,
		       coltree_create_scalar(pql, left->wholecolumn), NULL);
   }
   else {
      for (i=0; i<leftnum; i++) {
	 thisname = coltreearray_get(&left->subnames, i);
	 coltreearray_add(pql, &ret->subnames, coltree_clone(pql, thisname),
			  NULL);
      }
   }

   if (!right->istuple) {
      coltreearray_add(pql, &ret->subnames,
		       coltree_create_scalar(pql, right->wholecolumn), NULL);
   }
   else {
      for (i=0; i<rightnum; i++) {
	 thisname = coltreearray_get(&right->subnames, i);
	 coltreearray_add(pql, &ret->subnames, coltree_clone(pql, thisname),
			  NULL);
      }
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif
   
   return ret;
}

struct coltree *coltree_adjoin_coltree(struct pqlcontext *pql,
				       struct coltree *src,
				       struct coltree *newstuff) {
   struct coltree *ret;

   ret = coltree_clone(pql, src);

   if (ret->istuple) {
      coltreearray_add(pql, &ret->subnames, newstuff, NULL);
   }
   else {
      ret = mkcoltree_pair(pql, ret, newstuff);
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

struct coltree *coltree_adjoin(struct pqlcontext *pql, struct coltree *src,
			       struct colname *newcol) {
   return coltree_adjoin_coltree(pql, src, coltree_create_scalar(pql, newcol));
}

struct coltree *coltree_nest(struct pqlcontext *pql, struct coltree *src,
			     struct colset *remove, struct colname *add) {
   struct coltree *ret, *nest;

   ret = coltree_strip(pql, src, remove);
   nest = coltree_project(pql, src, remove);

   if (nest->wholecolumn != NULL) {
      colname_decref(pql, nest->wholecolumn);
   }
   if (add != NULL) {
      colname_incref(add);
   }
   nest->wholecolumn = add;

   if (ret->istuple) {
      coltreearray_add(pql, &ret->subnames, nest, NULL);
   }
   else {
      ret = mkcoltree_pair(pql, ret, nest);
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}

struct coltree *coltree_unnest(struct pqlcontext *pql, struct coltree *src,
			       struct colname *expand) {
   struct coltree *ret, *unnest, *keep;
   struct colset *tmp;

   colname_incref(expand);
   tmp = colset_singleton(pql, expand);

   keep = coltree_strip(pql, src, tmp);
   unnest = coltree_project(pql, src, tmp);

   colset_destroy(pql, tmp);

   if (unnest->wholecolumn != NULL) {
      colname_decref(pql, unnest->wholecolumn);
   }

   ret = coltree_join(pql, keep, unnest);
   coltree_destroy(pql, unnest);
   coltree_destroy(pql, keep);

   PQLASSERT(ret->wholecolumn == NULL);
   if (src->wholecolumn != NULL) {
      colname_incref(src->wholecolumn);
      ret->wholecolumn = src->wholecolumn;
   }

#ifdef COLTREE_NAME
   coltree_setname(pql, ret);
#endif

   return ret;
}
