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

#include <stdio.h> // for snprintf
#include <string.h>

#include "pql.h"
#include "array.h"
#include "layout.h"
#include "datatype.h"
#include "functions.h"

#define VALUE_INLINE
#include "pqlvalue.h"
#include "valuerep.h"

/*
 * Combined value representation.
 */

/* Physical representation types for values */
enum valuereptypes {
   VR_NIL,
   VR_CBOOL,
   VR_CINT,
   VR_CDOUBLE,
   VR_CSTRING,
   VR_ARRAY,		/* used for sets, sequences, tuples */
   VR_DUPARRAY,		/* used for sets, sequences */
   VR_DBOBJ,		/* database reference */
   VR_TREESET,		/* tree encoding of tuple set */

   VR_PATHELEMENT,
};

struct pqlvalue {
   struct pqlcontext *pql;
   struct datatype *datatype;
   enum valuereptypes reptype;
   union {
      /* nil -- nothing at all */
      struct vr_cbool cbool;
      struct vr_cint cint;
      struct vr_cdouble cdouble;
      struct vr_cstring cstring;
      struct vr_array array;
      struct vr_duparray duparray;
      struct vr_dbobj dbobj;
      struct vr_treeset treeset;
      struct {
	 struct pqlvalue *leftobj;
	 struct pqlvalue *edgename;
	 struct pqlvalue *rightobj;
      } pathelement;
   };
};

////////////////////////////////////////////////////////////
// Value creation by representation

/*
 * Baseline shared pqlvalue creation. Sets all the
 * representation-independent fields but does not touch the
 * representation.
 */

static struct pqlvalue *pqlvalue_create(struct pqlcontext *pql,
					struct datatype *datatype,
					enum valuereptypes reptype,
					void *caller) {
   struct pqlvalue *pv;

   pv = domallocfrom(pql, sizeof(*pv), caller);
   pv->pql = pql;
   pv->reptype = reptype;
   pv->datatype = datatype;

   return pv;
}

/*
 * Creation functions by representation type.
 */

static struct pqlvalue *pqlvalue_create_nil(struct pqlcontext *pql,
					    struct datatype *datatype) {
   return pqlvalue_create(pql, datatype, VR_NIL, GETCALLER());
}

static struct pqlvalue *pqlvalue_create_cbool(struct pqlcontext *pql,
					      struct datatype *datatype,
					      bool val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CBOOL, GETCALLER());
   vr_cbool_init(&ret->cbool);
   vr_cbool_set(&ret->cbool, val);
   return ret;
}

static struct pqlvalue *pqlvalue_create_cint(struct pqlcontext *pql,
					     struct datatype *datatype,
					     int val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CINT, GETCALLER());
   vr_cint_init(&ret->cint);
   vr_cint_set(&ret->cint, val);
   return ret;
}

static struct pqlvalue *pqlvalue_create_cdouble(struct pqlcontext *pql,
						struct datatype *datatype,
						double val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CDOUBLE, GETCALLER());
   vr_cdouble_init(&ret->cdouble);
   vr_cdouble_set(&ret->cdouble, val);
   return ret;
}

static struct pqlvalue *pqlvalue_create_cstring(struct pqlcontext *pql,
						struct datatype *datatype,
						const char *val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CSTRING, GETCALLER());
   vr_cstring_init(&ret->cstring);
   vr_cstring_set(pql, &ret->cstring, val);
   return ret;
}

static struct pqlvalue *pqlvalue_create_cstring_bylen(struct pqlcontext *pql,
						struct datatype *datatype,
						const char *val, size_t len) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CSTRING, GETCALLER());
   vr_cstring_init(&ret->cstring);
   vr_cstring_set_bylen(pql, &ret->cstring, val, len);
   return ret;
}

static struct pqlvalue *pqlvalue_create_cstring_consume(struct pqlcontext *pql,
						struct datatype *datatype,
						char *val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_CSTRING, GETCALLER());
   vr_cstring_init(&ret->cstring);
   vr_cstring_set_consume(pql, &ret->cstring, val);
   return ret;
}

static struct pqlvalue *pqlvalue_create_array(struct pqlcontext *pql,
					      struct datatype *datatype) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_ARRAY, GETCALLER());
   vr_array_init(&ret->array);
   return ret;
}

static struct pqlvalue *pqlvalue_create_duparray(struct pqlcontext *pql,
						 struct datatype *datatype) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_DUPARRAY, GETCALLER());
   vr_duparray_init(&ret->duparray);
   return ret;
   // XXX need to make it possible to actually use VR_DUPARRAY
   (void)pqlvalue_create_duparray;
}

static struct pqlvalue *pqlvalue_create_dbobj(struct pqlcontext *pql,
					      struct datatype *datatype,
					      int dbnum, pqloid_t oid,
					      pqlsubid_t subid) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_DBOBJ, GETCALLER());
   vr_dbobj_init(&ret->dbobj);
   vr_dbobj_setdbnum(&ret->dbobj, dbnum);
   vr_dbobj_setoid(&ret->dbobj, oid);
   vr_dbobj_setsubid(&ret->dbobj, subid);
   return ret;
}

static struct pqlvalue *pqlvalue_create_treeset(struct pqlcontext *pql,
						struct datatype *datatype) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, datatype, VR_TREESET, GETCALLER());
   vr_treeset_init(pql, &ret->treeset);
   return ret;
   // XXX need to make it possible to actually use VR_TREESET
   (void)pqlvalue_create_treeset;
}

static struct pqlvalue *pqlvalue_create_pathelement(struct pqlcontext *pql,
						    struct datatype *datatype,
						    struct pqlvalue *lo,
						    struct pqlvalue *en,
						    struct pqlvalue *ro) {
   struct pqlvalue *ret;

   /* XXX this should go away, let's just use array */
   ret = pqlvalue_create(pql, datatype, VR_PATHELEMENT, GETCALLER());
   //vr_pathelement_init(&ret->pathelement);
   //vr_pathelement_set(&ret->pathelement, lo, en, ro);
   ret->pathelement.leftobj = lo;
   ret->pathelement.edgename = en;
   ret->pathelement.rightobj = ro;
   return ret;
}

/*
 * clone: duplicate the representation
 */
struct pqlvalue *pqlvalue_clone(struct pqlcontext *pql,
				const struct pqlvalue *val) {
   struct pqlvalue *ret;

   ret = pqlvalue_create(pql, val->datatype, val->reptype, GETCALLER());

   switch (val->reptype) {
    case VR_NIL:
     /* nothing */
     break;
    case VR_CBOOL:
     vr_cbool_copy(&ret->cbool, &val->cbool);
     break;
    case VR_CINT:
     vr_cint_copy(&ret->cint, &val->cint);
     break;
    case VR_CDOUBLE:
     vr_cdouble_copy(&ret->cdouble, &val->cdouble);
     break;
    case VR_CSTRING:
     vr_cstring_copy(pql, &ret->cstring, &val->cstring);
     break;
    case VR_ARRAY:
     vr_array_copy(pql, &ret->array, &val->array);
     break;
    case VR_DUPARRAY:
     vr_duparray_copy(pql, &ret->duparray, &val->duparray);
     break;
    case VR_TREESET:
     vr_treeset_copy(pql, &ret->treeset, &val->treeset);
     break;
    case VR_DBOBJ:
     vr_dbobj_copy(&ret->dbobj, &val->dbobj);
     break;
    case VR_PATHELEMENT:
     ret->pathelement.leftobj = pqlvalue_clone(pql,val->pathelement.leftobj);
     ret->pathelement.edgename = pqlvalue_clone(pql,val->pathelement.edgename);
     ret->pathelement.rightobj = pqlvalue_clone(pql,val->pathelement.rightobj);
     break;
   }
   return ret;
}

////////////////////////////////////////////////////////////
// Value creation by data type

struct pqlvalue *pqlvalue_nil(struct pqlcontext *pql) {
   return pqlvalue_create_nil(pql, datatype_absbottom(pql));
}

/* like nil but has type dbobj */
struct pqlvalue *pqlvalue_dbnil(struct pqlcontext *pql) {
   return pqlvalue_create_nil(pql, datatype_absdbobj(pql));
}

struct pqlvalue *pqlvalue_bool(struct pqlcontext *pql, bool x) {
   return pqlvalue_create_cbool(pql, datatype_bool(pql), x);
}

struct pqlvalue *pqlvalue_int(struct pqlcontext *pql, int x) {
   return pqlvalue_create_cint(pql, datatype_int(pql), x);
}

struct pqlvalue *pqlvalue_float(struct pqlcontext *pql, double x) {
   return pqlvalue_create_cdouble(pql, datatype_double(pql), x);
}

struct pqlvalue *pqlvalue_string(struct pqlcontext *pql, const char *x) {
   return pqlvalue_create_cstring(pql, datatype_string(pql), x);
}

struct pqlvalue *pqlvalue_string_bylen(struct pqlcontext *pql,
				       const char *x, size_t xlen) {
   return pqlvalue_create_cstring_bylen(pql, datatype_string(pql), x, xlen);
}

struct pqlvalue *pqlvalue_string_consume(struct pqlcontext *pql, char *x) {
   return pqlvalue_create_cstring_consume(pql, datatype_string(pql), x);
}

struct pqlvalue *pqlvalue_struct(struct pqlcontext *pql,
				 int dbnum, pqloid_t oid,
				 pqlsubid_t subid) {
   /* for now at least all structs are dbobjs */
   return pqlvalue_create_dbobj(pql, datatype_struct(pql), dbnum, oid, subid);
}

struct pqlvalue *pqlvalue_pathelement(struct pqlcontext *pql,
				      struct pqlvalue *leftobj,
				      struct pqlvalue *edgename,
				      struct pqlvalue *rightobj) {
   return pqlvalue_create_pathelement(pql, datatype_pathelement(pql), 
				      leftobj, edgename, rightobj);
}

struct pqlvalue *pqlvalue_distinguisher(struct pqlcontext *pql, unsigned id) {
   /* XXX: use unsigned? */
   return pqlvalue_create_cint(pql, datatype_distinguisher(pql), id);
}

struct pqlvalue *pqlvalue_unit(struct pqlcontext *pql) {
   return pqlvalue_create_array(pql, datatype_unit(pql));
}   

struct pqlvalue *pqlvalue_pair(struct pqlcontext *pql,
			       struct pqlvalue *a,
			       struct pqlvalue *b) {
   struct datatype *t;
   struct pqlvalue *pv;

   t = datatype_tuple_pair(pql, a->datatype, b->datatype);
   pv = pqlvalue_create_array(pql, t);
   vr_array_add(pql, &pv->array, a);
   vr_array_add(pql, &pv->array, b);
   return pv;
}   

struct pqlvalue *pqlvalue_tuple_specific(struct pqlcontext *pql,
					 struct pqlvaluearray *vals) {
   struct pqlvalue *pv;
   struct datatypearray subtypes;
   struct datatype *datatype;
   unsigned arity, i;

   arity = pqlvaluearray_num(vals);

   datatypearray_init(&subtypes);
   for (i=0; i<arity; i++) {
      datatypearray_add(pql, &subtypes, pqlvaluearray_get(vals, i)->datatype, NULL);
   }
   datatype = datatype_tuple_specific(pql, &subtypes);
   datatypearray_setsize(pql, &subtypes, 0);
   datatypearray_cleanup(pql, &subtypes);

   pv = pqlvalue_create_array(pql, datatype);
   for (i=0; i<arity; i++) {
      vr_array_add(pql, &pv->array, pqlvaluearray_get(vals, i));
   }
   return pv;
}

struct pqlvalue *pqlvalue_tuple_begin(struct pqlcontext *pql, unsigned arity) {
   struct pqlvalue *pv;
   unsigned i;

   pv = pqlvalue_create_array(pql, datatype_absbottom(pql));
   for (i=0; i<arity; i++) {
      vr_array_add(pql, &pv->array, NULL);
   }
   return pv;
}

void pqlvalue_tuple_assign(struct pqlcontext *pql, struct pqlvalue *tuple,
			   unsigned slot, struct pqlvalue *val) {
   struct pqlvalue *old;

   (void)pql;

   PQLASSERT(tuple->reptype == VR_ARRAY);
   old = vr_array_replace(&tuple->array, slot, val);
   PQLASSERT(old == NULL);
}

void pqlvalue_tuple_end(struct pqlcontext *pql, struct pqlvalue *tuple) {
   struct datatypearray subtypes;
   unsigned arity, i;

   PQLASSERT(tuple->reptype == VR_ARRAY);
   arity = vr_array_num(&tuple->array);

   datatypearray_init(&subtypes);
   for (i=0; i<arity; i++) {
      datatypearray_add(pql, &subtypes,
			vr_array_get(&tuple->array, i)->datatype, NULL);
   }
   tuple->datatype = datatype_tuple_specific(pql, &subtypes);
   datatypearray_setsize(pql, &subtypes, 0);
   datatypearray_cleanup(pql, &subtypes);
}

struct pqlvalue *pqlvalue_emptyset(struct pqlcontext *pql) {
   return pqlvalue_create_array(pql,
				datatype_set(pql, datatype_absbottom(pql)));
}

struct pqlvalue *pqlvalue_emptysequence(struct pqlcontext *pql) {
   return pqlvalue_create_array(pql,
				datatype_sequence(pql,
						  datatype_absbottom(pql)));
}

////////////////////////////////////////////////////////////
// destroy

void pqlvaluearray_destroymembers(struct pqlcontext *pql,
				  struct pqlvaluearray *arr) {
   unsigned num, i;
   struct pqlvalue *member;

   num = pqlvaluearray_num(arr);
   for (i=0; i<num; i++) {
      member = pqlvaluearray_get(arr, i);
      /*
       * Ordinarily values are not null, but sometimes we replace()
       * the members out of a set and then destroy it, e.g. in nest.
       * So tolerate null here.
       */
      if (member != NULL) {
	 pqlvalue_destroy(member);
      }
   }
   pqlvaluearray_setsize(pql, arr, 0);
}

void pqlvalue_destroy(struct pqlvalue *val) {
   switch (val->reptype) {
    case VR_NIL:
     /* nothing */
     break;
    case VR_CBOOL:
     vr_cbool_cleanup(&val->cbool);
     break;
    case VR_CINT:
     vr_cint_cleanup(&val->cint);
     break;
    case VR_CDOUBLE:
     vr_cdouble_cleanup(&val->cdouble);
     break;
    case VR_CSTRING:
     vr_cstring_cleanup(val->pql, &val->cstring);
     break;
    case VR_ARRAY:
     vr_array_cleanup(val->pql, &val->array);
     break;
    case VR_DUPARRAY:
     vr_duparray_cleanup(val->pql, &val->duparray);
     break;
    case VR_TREESET:
     vr_treeset_cleanup(val->pql, &val->treeset);
     break;
    case VR_DBOBJ:
     vr_dbobj_cleanup(&val->dbobj);
     break;
    case VR_PATHELEMENT:
     pqlvalue_destroy(val->pathelement.leftobj);
     pqlvalue_destroy(val->pathelement.edgename);
     pqlvalue_destroy(val->pathelement.rightobj);
     break;
   }
   dofree(val->pql, val, sizeof(*val));
}

////////////////////////////////////////////////////////////
// type inspection

/*
 * We go by the data type, except for nil, because nil isn't a type.
 *
 * For types that have multiple representations (e.g. tuplesets
 * vs. sets of tuples) the actions allowed on values of a particular
 * type have to work on whatever representation they get.
 */

struct datatype *pqlvalue_datatype(const struct pqlvalue *val) {
   return val->datatype;
}

/*
 * The following functions are all exposed to the user.
 */

bool pqlvalue_isnil(const struct pqlvalue *val) {
   return val->reptype == VR_NIL;
}

bool pqlvalue_isbool(const struct pqlvalue *val) {
   return datatype_isbool(val->datatype);
}

bool pqlvalue_isint(const struct pqlvalue *val) {
   return datatype_isint(val->datatype);
}

bool pqlvalue_isfloat(const struct pqlvalue *val) {
   return datatype_isdouble(val->datatype);
}

bool pqlvalue_isstring(const struct pqlvalue *val) {
   return datatype_isstring(val->datatype);
}

bool pqlvalue_isstruct(const struct pqlvalue *val) {
   return datatype_isstruct(val->datatype);
}

bool pqlvalue_ispathelement(const struct pqlvalue *val) {
   return datatype_ispathelement(val->datatype);
}

bool pqlvalue_isdistinguisher(const struct pqlvalue *val) {
   return datatype_isdistinguisher(val->datatype);
}

bool pqlvalue_istuple(const struct pqlvalue *val) {
   return datatype_istuple(val->datatype);
}

bool pqlvalue_islambda(const struct pqlvalue *val) {
   return datatype_islambda(val->datatype);
}

bool pqlvalue_isset(const struct pqlvalue *val) {
   return datatype_isset(val->datatype);
}

bool pqlvalue_issequence(const struct pqlvalue *val) {
   return datatype_issequence(val->datatype);
}

////////////////////////////////////////////////////////////
// value extraction functions

bool pqlvalue_bool_get(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isbool(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_CBOOL:
     return vr_cbool_get(&val->cbool);
    default:
     PQLASSERT(!"Invalid representation of bool");
     break;
   }
   return false;
}

int pqlvalue_int_get(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isint(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_CINT:
     return vr_cint_get(&val->cint);
    default:
     PQLASSERT(!"Invalid representation of int");
     break;
   }
   return 0;
}

double pqlvalue_float_get(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isdouble(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_CDOUBLE:
     return vr_cdouble_get(&val->cdouble);
    default:
     PQLASSERT(!"Invalid representation of float");
     break;
   }
   return 0;
}

const char *pqlvalue_string_get(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isstring(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_CSTRING:
     return vr_cstring_get(&val->cstring);
    default:
     PQLASSERT(!"Invalid representation of string");
     break;
   }
   return 0;
}

unsigned pqlvalue_tuple_getarity(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_istuple(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
     return vr_array_num(&val->array);
    default:
     PQLASSERT(!"Invalid representation of tuple");
     break;
   }
   return 0;
}

const struct pqlvalue *
pqlvalue_tuple_get(const struct pqlvalue *val, unsigned n) {
   /* check for proper type */
   PQLASSERT(datatype_istuple(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
     return vr_array_get(&val->array, n);
    default:
     PQLASSERT(!"Invalid representation of tuple");
     break;
   }
   return 0;
}

/*
 * XXX this is mostly used for iteration; how to handle duparray?
 */
unsigned pqlvalue_set_getnum(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isset(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
     return vr_array_num(&val->array);
    case VR_TREESET:
      // XXX write this sometime
      PQLASSERT(0);
      return 0;
    default:
     PQLASSERT(!"Invalid representation of set");
     break;
   }
   return 0;
}

const struct pqlvalue *pqlvalue_set_get(const struct pqlvalue *val,
					unsigned n) {
   /* check for proper type */
   PQLASSERT(datatype_isset(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
      return vr_array_get(&val->array, n);
    case VR_TREESET:
      // XXX write this sometime
      PQLASSERT(0);
      return NULL;
    default:
     PQLASSERT(!"Invalid representation of set");
     break;
   }
   return NULL;
}

unsigned pqlvalue_sequence_getnum(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_issequence(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
     return vr_array_num(&val->array);
    default:
     PQLASSERT(!"Invalid representation of sequence");
     break;
   }
   return 0;
}

const struct pqlvalue *
pqlvalue_sequence_get(const struct pqlvalue *val, unsigned n) {
   /* check for proper type */
   PQLASSERT(datatype_issequence(val->datatype));

   /* check representation */
   switch (val->reptype) {
    case VR_ARRAY:
     return vr_array_get(&val->array, n);
    default:
     PQLASSERT(!"Invalid representation of sequence");
     break;
   }
   return 0;
}

int pqlvalue_struct_getdbnum(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isstruct(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_DBOBJ);

   return val->dbobj.dbnum;
}

pqloid_t pqlvalue_struct_getoid(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isstruct(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_DBOBJ);

   return val->dbobj.oid;
}

pqlsubid_t pqlvalue_struct_getsubid(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_isstruct(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_DBOBJ);

   return val->dbobj.subid;
}

const struct pqlvalue *
pqlvalue_pathelement_getleftobj(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_ispathelement(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_PATHELEMENT);

   return val->pathelement.leftobj;
}

const struct pqlvalue *
pqlvalue_pathelement_getedgename(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_ispathelement(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_PATHELEMENT);

   return val->pathelement.edgename;
}

const struct pqlvalue *
pqlvalue_pathelement_getrightobj(const struct pqlvalue *val) {
   /* check for proper type */
   PQLASSERT(datatype_ispathelement(val->datatype));

   /* check for expected representation */
   PQLASSERT(val->reptype == VR_PATHELEMENT);

   return val->pathelement.rightobj;
}

////////////////////////////////////////////////////////////
// polymorphic value extraction on "collections"

unsigned pqlvalue_coll_getnum(const struct pqlvalue *coll) {
   if (pqlvalue_isset(coll)) {
      return pqlvalue_set_getnum(coll);
   }
   else {
      PQLASSERT(pqlvalue_issequence(coll));
      return pqlvalue_sequence_getnum(coll);
   }
}

const struct pqlvalue *pqlvalue_coll_get(const struct pqlvalue *coll,
					 unsigned n) {
   if (pqlvalue_isset(coll)) {
      return pqlvalue_set_get(coll, n);
   }
   else {
      PQLASSERT(pqlvalue_issequence(coll));
      return pqlvalue_sequence_get(coll, n);
   }
}

void pqlvalue_coll_drop(struct pqlvalue *coll, unsigned index) {
   if (pqlvalue_isset(coll)) {
      return pqlvalue_set_drop(coll, index);
   }
   else {
      PQLASSERT(pqlvalue_issequence(coll));
      return pqlvalue_sequence_drop(coll, index);
   }
}

////////////////////////////////////////////////////////////
// basic value updating operations

/*
 * These modify the value. It is the user's responsibility to not
 * modify a value after it's been given to the engine. When/if we want
 * to keep cached value objects in the database layer, which might
 * need updating on the fly, we'll see about how to do that safely.
 */

/*
 * Common code for pqlvalue_tuple/set/sequence_replace
 */
static struct pqlvalue *pqlvalue_common_replace(struct pqlvalue *coll,
						unsigned ix,
						struct pqlvalue *newval) {
   struct pqlvalue *ret;

   /* check for expected representation */
   PQLASSERT(coll->reptype == VR_ARRAY || coll->reptype == VR_TREESET);
   if (coll->reptype == VR_ARRAY) {
      ret = vr_array_replace(&coll->array, ix, newval);
   }
   else {
      // XXX write this sometime
      // (user code shouldn't reach it though... well maybe...)
      PQLASSERT(0);
   }
   return ret;
}

struct pqlvalue *pqlvalue_tuple_replace(struct pqlvalue *tuple, unsigned ix,
					struct pqlvalue *newval) {
   /* check for proper types */
   PQLASSERT(datatype_istuple(tuple->datatype));

   /*
    * Like with sets, this doesn't work because of polymorphism. E.g.
    * suppose we have a tuple that functions as (int, set(T), dbobj)
    * but was created with a struct in the dbobj slot. Something might
    * legitimately replace that with e.g. an int.
    *
    * If we set the type implicitly when the tuple is created, and the
    * type reflects what's actually in the tuple, this assertion fails
    * when the replacement happens. If we don't, then we have to get
    * it from the abstract syntax or something, which isn't the point.
    *
    * I think what should happen is that a physical tuple should
    * always reflect what's exactly in it and not contain abstract
    * types. (A set, however, should if necessary have an abstract
    * member type if the members are not uniform.)
    */
#if 0 /* XXX */
   if (newval != NULL) {
      PQLASSERT(datatype_match_specialize(tuple->pql,
					  datatype_getnth(tuple->datatype, ix),
					  newval->datatype) != NULL);
   }
#endif
   // XXX should probably replace the type of slot IX with the
   // type of newval.

   return pqlvalue_common_replace(tuple, ix, newval);
}

struct pqlvalue *pqlvalue_set_replace(struct pqlvalue *set, unsigned ix,
				      struct pqlvalue *newval,
				      bool forcetype) {
   struct pqlcontext *pql;
   struct datatype *settype, *valtype;

   pql = set->pql;

   if (!forcetype) {
      /* Shouldn't need this, but we're updating sets in place sometimes */

      /* check for proper types */
      settype = set->datatype;
      PQLASSERT(datatype_isset(settype));
      if (newval != NULL) {
	 /*
	  * normally values aren't null, but we sometimes replace with
	  * null temporarily.
	  */
	 valtype = newval->datatype;
	 // XXX this blows up left and right because of polymorphism;
	 // needs comprehensive fix
	 //PQLASSERT(datatype_eq(datatype_set(pql, valtype), settype));
      }
   }

   return pqlvalue_common_replace(set, ix, newval);
}

/*
 * This should go away. Use replace.
 */
struct pqlvalue *pqlvalue_set_getformodify(struct pqlvalue *val, unsigned n) {
   /* check for proper type */
   PQLASSERT(datatype_isset(val->datatype));

   /* check representation */
   if (val->reptype == VR_ARRAY) {
      return vr_array_getformodify(&val->array, n);
   }
   else {
      PQLASSERT(val->reptype == VR_TREESET);
      // XXX write this sometime
      PQLASSERT(0);
      return NULL;
   }
}

/*
 * Common code for pqlvalue_tuple/set/sequence_add
 */
static void pqlvalue_common_add(struct pqlvalue *coll, struct pqlvalue *val) {
   /* check for expected representation */
   PQLASSERT(coll->reptype == VR_ARRAY || coll->reptype == VR_TREESET);
   if (coll->reptype == VR_ARRAY) {
      vr_array_add(coll->pql, &coll->array, val);
   }
   else {
      // XXX write this sometime
      // (user code shouldn't reach it though... well maybe...)
      PQLASSERT(0);
   }
}

/*
 * Consumes VAL; return value replaces TUPLE.
 */
struct pqlvalue *pqlvalue_tuple_add(struct pqlvalue *tuple,
				    struct pqlvalue *val) {
   struct pqlcontext *pql;
   struct pqlvalue *tmp;

   pql = val->pql;

   if (!pqlvalue_istuple(tuple)) {
      /* single value becomes a pair */
      tmp = pqlvalue_unit(pql);
      pqlvalue_common_add(tmp, tuple);
      tmp->datatype = tuple->datatype;
      tuple = tmp;
   }
   else if (pqlvalue_tuple_getarity(tuple) == 0) {
      /* unit becomes a single value */
      pqlvalue_destroy(tuple);
      return val;
   }

   /* adjust the specific type */
   tuple->datatype = datatype_tuple_append(pql,
					   tuple->datatype, val->datatype);

   pqlvalue_common_add(tuple, val);
   return tuple;
}

void pqlvalue_set_add(struct pqlvalue *set, struct pqlvalue *val) {
   struct pqlcontext *pql;
   struct datatype *settype, *valtype;

   pql = val->pql;
   settype = set->datatype;
   valtype = val->datatype;

   /* check for proper types */
   PQLASSERT(datatype_isset(settype));

   if (datatype_isabsbottom(datatype_set_member(settype))) {
      // XXX shouldn't need to do this explicitly, should just run generalize
      PQLASSERT(pqlvalue_set_getnum(set) == 0);
      set->datatype = datatype_set(pql, val->datatype);
   }
   else {
      // XXX this breaks because of polymorphism; needs comprehensive fix
      //PQLASSERT(datatype_eq(datatype_set(pql, valtype), settype));
   }

   pqlvalue_common_add(set, val);
}

void pqlvalue_sequence_add(struct pqlvalue *seq, struct pqlvalue *val) {
   struct pqlcontext *pql;
   struct datatype *seqtype, *valtype;

   pql = val->pql;
   seqtype = seq->datatype;
   valtype = val->datatype;

   /* check for proper types */
   PQLASSERT(datatype_issequence(seqtype));

   // XXX as above
   if (datatype_isabsbottom(datatype_sequence_member(seqtype))) {
      PQLASSERT(pqlvalue_sequence_getnum(seq) == 0);
      seq->datatype = datatype_sequence(pql, val->datatype);
   }
   else {
      PQLASSERT(datatype_eq(datatype_sequence(pql, valtype), seqtype));
   }

   pqlvalue_common_add(seq, val);
}

/*
 * Common code for pqlvalue_tuple_strip/pqlvalue_set/sequence_drop
 */
static void pqlvalue_common_drop(struct pqlvalue *coll, unsigned ix) {
   struct pqlvalue *entry;

   /* check for expected representation */
   PQLASSERT(coll->reptype == VR_ARRAY || coll->reptype == VR_TREESET);
   if (coll->reptype == VR_ARRAY) {
      entry = vr_array_drop(&coll->array, ix);
      if (entry != NULL) {
	 pqlvalue_destroy(entry);
      }
   }
   else {
      // XXX write this sometime
      // (user code shouldn't reach it though... well maybe...)
      PQLASSERT(0);
   }
}

struct pqlvalue *pqlvalue_tuple_strip(struct pqlvalue *tuple, unsigned col) {
   /* check for proper types */
   if (!datatype_istuple(tuple->datatype)) {
      struct pqlvalue *ret;

      PQLASSERT(col == 0);
      ret = pqlvalue_unit(tuple->pql);
      pqlvalue_destroy(tuple);
      return ret;
   }

   /* adjust the specific type */
   tuple->datatype = datatype_tuple_strip(tuple->pql, tuple->datatype, col);

   pqlvalue_common_drop(tuple, col);

   /* if result is a monople, unwrap it */
   PQLASSERT(tuple->reptype == VR_ARRAY);
   if (vr_array_num(&tuple->array) == 1) {
      struct pqlvalue *ret;

      ret = vr_array_replace(&tuple->array, 0, NULL);
      pqlvalue_destroy(tuple);
      return ret;
   }
   return tuple;
}

void pqlvalue_set_drop(struct pqlvalue *set, unsigned ix) {
   /* check for proper types */
   PQLASSERT(datatype_isset(set->datatype));

   pqlvalue_common_drop(set, ix);
}

void pqlvalue_sequence_drop(struct pqlvalue *set, unsigned ix) {
   /* check for proper types */
   PQLASSERT(datatype_issequence(set->datatype));

   pqlvalue_common_drop(set, ix);
}

////////////////////////////////////////////////////////////
// comparison/equality.

/*
 * Equality.
 *
 * Two objects are "really" equal if they have the same type and the
 * same value. This is checked by pqlvalue_identical.
 *
 * Two objects are also equal if they're promotable to the same type
 * and this creates the same value.
 *
 * The promotions we need to care about:
 *
 *    int -> float
 *    string -> int
 *    string -> float
 *    int -> bool
 *    float -> bool
 *    string -> bool
 *    sequence -> set
 *    T1 -> T2 => tuple(..., T1, ...) -> tuple(..., T2, ...)
 *    T1 -> T2 => set(T1) -> set(T2)
 *    T1 -> T2 => sequence(T1) -> sequence(T2)
 *
 * The function pqlvalue_eq checks this.
 */

static int compare_tupleset_and_array(const struct pqlvalue *a,
				      const struct pqlvalue *b,
				      int (*sub)(const struct pqlvalue *a,
						 const struct pqlvalue *b)) {
   (void)a;
   (void)b;
   (void)sub;
   PQLASSERT(a->reptype == VR_TREESET);
   PQLASSERT(a->reptype == VR_ARRAY);
   // XXX write me sometime
   PQLASSERT(0);
   return 0;
}

static int compare_same_representations(const struct pqlvalue *a,
					const struct pqlvalue *b,
					int (*sub)(const struct pqlvalue *a,
						   const struct pqlvalue *b)) {
   unsigned anum, bnum, i;
   int ret;

   ret = 0; // gcc 4.1

   PQLASSERT(a->reptype == b->reptype);

   switch (a->reptype) {
    case VR_NIL:
     return 0;
    case VR_CBOOL:
     return vr_cbool_cmp(&a->cbool, &b->cbool);
    case VR_CINT:
     return vr_cint_cmp(&a->cint, &b->cint);
    case VR_CDOUBLE:
     return vr_cdouble_cmp(&a->cdouble, &b->cdouble);
    case VR_CSTRING:
     return vr_cstring_cmp(&a->cstring, &b->cstring);
    case VR_ARRAY:
     anum = vr_array_num(&a->array);
     bnum = vr_array_num(&b->array);
     ret = NUMBER_CMP(anum, bnum);
     if (ret == 0) {
	for (i=0; i<anum; i++) {
	   ret = sub(vr_array_get(&a->array, i),
		     vr_array_get(&b->array, i));
	   if (ret != 0) {
	      return ret;
	   }
	}
     }
     break;
    case VR_DUPARRAY:
     // XXX write this
     PQLASSERT(0);
     break;
    case VR_TREESET:
     // XXX write me
     PQLASSERT(0);
     break;
    case VR_DBOBJ:
     ret = NUMBER_CMP(a->dbobj.dbnum, b->dbobj.dbnum);
     if (ret == 0) {
	ret = NUMBER_CMP(a->dbobj.oid, b->dbobj.oid);
     }
     if (ret == 0) {
	ret = NUMBER_CMP(a->dbobj.subid, b->dbobj.subid);
     }
     break;
    case VR_PATHELEMENT:
     ret = sub(a->pathelement.edgename, b->pathelement.edgename);
     if (ret == 0) {
	ret = sub(a->pathelement.leftobj, b->pathelement.leftobj);
     }
     if (ret == 0) {
	ret = sub(a->pathelement.rightobj, b->pathelement.rightobj);
     }
     break;
   }
   return ret;
}

static int compare_same_types(const struct pqlvalue *a,
			      const struct pqlvalue *b,
			      int (*sub)(const struct pqlvalue *a,
					 const struct pqlvalue *b)) {
   // Don't do this; it means we can't lie and use this for sets of
   // different member types below, even though it works perfectly
   // well.
   //PQLASSERT(datatype_eq(a->datatype, b->datatype));

   /* check for cases where different representations are allowed */
   if (a->reptype == VR_TREESET && b->reptype == VR_ARRAY) {
      return compare_tupleset_and_array(a, b, sub);
   }
   if (a->reptype == VR_ARRAY && b->reptype == VR_TREESET) {
      return compare_tupleset_and_array(b, a, sub);
   }

   return compare_same_representations(a, b, sub);
}

static int compare_identical(const struct pqlvalue *a,
			     const struct pqlvalue *b) {
   /*
    * CAUTION: this does not generate a real ordering and will lead to
    * nasal demons if used to sort. But it needs to be able to handle
    * any types, because the user can throw whatever at it.
    *
    * This function is only used for pqlvalue_identical; however, it
    * needs to return a comparison value anyway so we can use
    * compare_same_types.
    */
   if (!datatype_eq(a->datatype, b->datatype)) {
      return -1;
   }
   return compare_same_types(a, b, compare_identical);
}

bool pqlvalue_identical(const struct pqlvalue *a, const struct pqlvalue *b) {
   PQLASSERT(a->pql == b->pql);
   return compare_identical(a, b) == 0;
}

static int compare_convertible(const struct pqlvalue *a,
			       const struct pqlvalue *b) {
   unsigned ar, br, i;
   bool abool, bbool;
   int aint, bint;
   double afloat, bfloat;
   bool aisfloat, bisfloat;
   int ret;

   if (pqlvalue_isnil(a) && pqlvalue_isnil(b)) {
      return 0;
   }
   if (pqlvalue_isnil(a)) {
      return -1;
   }
   if (pqlvalue_isnil(b)) {
      return 1;
   }

   if (datatype_eq(a->datatype, b->datatype)) {
      return compare_same_types(a, b, compare_convertible);
   }

   if (datatype_isanynumber(a->datatype) ||
       datatype_isanynumber(b->datatype) ||
       datatype_isstring(a->datatype) ||
       datatype_isstring(b->datatype)) {
      if (convert_to_number(a, &aint, &afloat, &aisfloat) == 0 &&
	  convert_to_number(b, &bint, &bfloat, &bisfloat) == 0) {
	 if (!aisfloat && !bisfloat) {
	    return NUMBER_CMP(aint, bint);
	 }
	 else {
	    return NUMBER_CMP(afloat, bfloat);
	 }
      }
   }

   /*
    * I don't think '6 == "yes"' should be true, so don't do this unless
    * there's specifically at least one bool involved.
    */
   if ((pqlvalue_isbool(a) || pqlvalue_isbool(b)) &&
       convert_to_bool(a, &abool) == 0 &&
       convert_to_bool(b, &bbool) == 0) {
      return (abool == bbool) ? 0 : abool ? 1 : -1;
   }

   if (datatype_istuple(a->datatype) && datatype_istuple(b->datatype)) {
      /* two tuples that aren't the same */
      ar = datatype_arity(a->datatype);
      br = datatype_arity(b->datatype);
      ret = NUMBER_CMP(ar, br);
      if (ret != 0) {
	 return ret;
      }
      for (i=0; i<ar; i++) {
	 ret = pqlvalue_compare(pqlvalue_tuple_get(a, i),
				pqlvalue_tuple_get(b, i));
	 if (ret != 0) {
	    return ret;
	 }
      }
      return ret;
   }

   if ((datatype_isset(a->datatype) || datatype_issequence(a->datatype)) &&
       (datatype_isset(b->datatype) || datatype_issequence(b->datatype))) {
      /* sets or sequences that aren't the same type */
      /* lie and use compare_same_types, which does the right thing */
      return compare_same_types(a, b, pqlvalue_compare);
   }

   /*
    * Again, this doesn't necessarily create a real ordering.
    */

   return -1;
}

bool pqlvalue_eq(const struct pqlvalue *a, const struct pqlvalue *b) {
   PQLASSERT(a->pql == b->pql);
   return compare_convertible(a, b) == 0;
}

/*
 * Ordering.
 *
 * The function pqlvalue_compare is used for sorting.
 *
 * We can't use the language's < operator for this, because it
 * produces inconsistent results if you mix strings and numbers.
 *
 * Consider sorting the set {"10", 8, "7"}. The following expressions
 * are all true:
 *
 *     "7" < 8
 *     8 < "10"
 *     "10" < "7"
 *
 * and that really won't do.
 *
 * Normally, but not always, we'll be sorting sets of homogeneous
 * type. By decree, when we aren't, and what we have are atoms, we'll
 * sort by string representation. This will behave poorly given a set
 * of numbers and strings that are numbers (like the example above)
 * but tough. It's also expensive (XXX: the optimizer should see to
 * this) but tough.
 *
 * We can assume we won't be asked to sort a set of bottom, or the
 * like, such that types that aren't supposed to be miscible appear
 * together. But we may need to sort things that < rejects, like
 * structs.
 */

static void getstring(char *buf, size_t max, const struct pqlvalue *v) {
   // XXX should probably share code with tostring (but that's awkward)
   switch (v->reptype) {
    case VR_CBOOL:
     /* don't use strlcpy as loonix doesn't provide it */
     snprintf(buf, max, "%s", v->cbool.val ? "true" : "false");
     break;
    case VR_CINT:
     snprintf(buf, max, "%d", v->cint.val);
     break;
    case VR_CDOUBLE:
     snprintf(buf, max, "%g", v->cdouble.val);
     break;
    default:
     PQLASSERT(0);
     strcpy(buf, "???");
     break;
   }
}

static unsigned datatype_rank(struct datatype *t) {
   /* all atoms come first */
   if (datatype_isanyatom(t)) {
      return 0;
   }
   if (datatype_isstruct(t)) {
      return 1;
   }
   if (datatype_ispathelement(t)) {
      return 2;
   }
   if (datatype_isdistinguisher(t)) {
      return 3;
   }
   if (datatype_istuple(t)) {
      return 4;
   }
   PQLASSERT(!datatype_islambda(t));
   PQLASSERT(datatype_isset(t) || datatype_issequence(t));
   return 5;
}

int pqlvalue_compare(const struct pqlvalue *a, const struct pqlvalue *b) {
   unsigned ar, br, i;
   int ret;

   PQLASSERT(a->pql == b->pql);

   /* nil always goes first */
   if (pqlvalue_isnil(a) && pqlvalue_isnil(b)) {
      return 0;
   }
   if (pqlvalue_isnil(a)) {
      return -1;
   }
   if (pqlvalue_isnil(b)) {
      return 1;
   }

   if (datatype_eq(a->datatype, b->datatype)) {
      return compare_same_types(a, b, pqlvalue_compare);
   }
   if (datatype_isanyatom(a->datatype) && datatype_isanyatom(b->datatype)) {
      const char *as, *bs;
      char abuf[128], bbuf[128];

      if (datatype_isstring(a->datatype)) {
	 as = pqlvalue_string_get(a);
      }
      else {
	 getstring(abuf, sizeof(abuf), a);
	 as = abuf;
      }

      if (datatype_isstring(b->datatype)) {
	 bs = pqlvalue_string_get(b);
      }
      else {
	 getstring(bbuf, sizeof(bbuf), b);
	 bs = bbuf;
      }

      return strcmp(as, bs);
   }

   if (datatype_istuple(a->datatype) && datatype_istuple(b->datatype)) {
      /* two tuples that aren't the same */
      ar = datatype_arity(a->datatype);
      br = datatype_arity(b->datatype);
      ret = NUMBER_CMP(ar, br);
      if (ret != 0) {
	 return ret;
      }
      for (i=0; i<ar; i++) {
	 ret = pqlvalue_compare(pqlvalue_tuple_get(a, i),
				pqlvalue_tuple_get(b, i));
	 if (ret != 0) {
	    return ret;
	 }
      }
      return ret;
   }

   if ((datatype_isset(a->datatype) || datatype_issequence(a->datatype)) &&
       (datatype_isset(b->datatype) || datatype_issequence(b->datatype))) {
      /* sets or sequences that aren't the same type */
      /* lie and use compare_same_types, which does the right thing */
      return compare_same_types(a, b, pqlvalue_compare);
   }

   ar = datatype_rank(a->datatype);
   br = datatype_rank(b->datatype);
   return NUMBER_CMP(ar, br);
}

////////////////////////////////////////////////////////////
// tostring

static struct pqlvalue *pathelement_tostring(struct pqlcontext *pql,
					     const struct pqlvalue *val) {
   struct pqlvalue *retval;
   struct pqlvaluearray strings;

   PQLASSERT(val->reptype == VR_PATHELEMENT);
   PQLASSERT(datatype_ispathelement(val->datatype));

   pqlvaluearray_init(&strings);
   pqlvaluearray_setsize(pql, &strings, 3);

   pqlvaluearray_set(&strings, 0,
		     pqlvalue_tostring(pql, val->pathelement.leftobj));
   pqlvaluearray_set(&strings, 1,
		     pqlvalue_tostring(pql, val->pathelement.edgename));
   pqlvaluearray_set(&strings, 2,
		     pqlvalue_tostring(pql, val->pathelement.rightobj));

   /* format will be e.g. "{3:5}.count.6" */
   retval = pqlvalue_string_fromlist(pql, &strings, "", ".", "");

   pqlvaluearray_destroymembers(pql, &strings);
   pqlvaluearray_cleanup(pql, &strings);

   return retval;
}

static struct pqlvalue *array_tostring(struct pqlcontext *pql,
				       const struct pqlvalue *val) {
   unsigned num, i;
   const struct pqlvalue *ival;
   struct pqlvalue *retval;
   struct pqlvaluearray strings;

   PQLASSERT(val->reptype == VR_ARRAY);
   num = vr_array_num(&val->array);

   pqlvaluearray_init(&strings);
   pqlvaluearray_setsize(pql, &strings, num);
   for (i=0; i<num; i++) {
      ival = vr_array_get(&val->array, i);
      pqlvaluearray_set(&strings, i, pqlvalue_tostring(pql, ival));
   }

   // XXX this is probably wrong now
   if (datatype_istuple(val->datatype)) {
      PQLASSERT(datatype_arity(val->datatype) == num);
      retval = pqlvalue_string_fromlist(pql, &strings, "(", ", ", ")");
   }
   else if (datatype_isset(val->datatype)) {
      retval = pqlvalue_string_fromlist(pql, &strings, "{", ", ", "}");
   }
   else {
      PQLASSERT(datatype_issequence(val->datatype));
      retval = pqlvalue_string_fromlist(pql, &strings, "{", ", ", "}");
   }

   pqlvaluearray_destroymembers(pql, &strings);
   pqlvaluearray_cleanup(pql, &strings);

   return retval;
}

static struct pqlvalue *dbobj_tostring(struct pqlcontext *pql,
				       const struct pqlvalue *val) {
   char buf[256];
   unsigned long long oid, subid;

   PQLASSERT(val->reptype == VR_DBOBJ);
   /* datatype might conceivably be anything, so ignore it */

   /* unsigned long long is guaranteed to be big enough for a 64-bit oid */
   oid = val->dbobj.oid;
   subid = val->dbobj.subid;

   if (subid == 0) {
      snprintf(buf, sizeof(buf), "{%d.%llu}", val->dbobj.dbnum, oid);
   }
   else {
      snprintf(buf, sizeof(buf), "{%d.%llu.%llu}", val->dbobj.dbnum, oid,
	       subid);
   }

   return pqlvalue_string(pql, buf);
}

struct pqlvalue *pqlvalue_tostring(struct pqlcontext *pql,
				   const struct pqlvalue *val) {
   char buf[128];

   if (val->reptype == VR_NIL) {
     return pqlvalue_string(pql, "nil");
   }
   else if (datatype_isbool(val->datatype)) {
      return pqlvalue_string(pql, pqlvalue_bool_get(val) ? "true" : "false");
   }
   else if (datatype_isint(val->datatype)) {
      snprintf(buf, sizeof(buf), "%d", pqlvalue_int_get(val));
      return pqlvalue_string(pql, buf);
   }
   else if (datatype_isdouble(val->datatype)) {
      snprintf(buf, sizeof(buf), "%g", pqlvalue_float_get(val));
      return pqlvalue_string(pql, buf);
   }
   else if (datatype_isstring(val->datatype)) {
      return pqlvalue_clone(pql, val);
   }
   else if (datatype_ispathelement(val->datatype)) {
      return pathelement_tostring(pql, val);
   }
   else {
      // XXX this is going to be wrong soon if it isn't already
      if (val->reptype == VR_ARRAY) {
	 return array_tostring(pql, val);
      }
      else if (val->reptype == VR_DBOBJ) {
	 return dbobj_tostring(pql, val);
      }
      else if (val->reptype == VR_TREESET) {
	 return tupleset_tostring(pql, &val->treeset.val);
      }
      else {
	 PQLASSERT(0);
	 return NULL;
      }
   }

   PQLASSERT(0);
   return NULL;
}

struct layout *pqlvalue_layout(struct pqlcontext *pql,
			       const struct pqlvalue *val) {
   struct pqlvalue *str;
   struct layout *ret;

   str = pqlvalue_tostring(pql, val);
   PQLASSERT(str->reptype == VR_CSTRING);

   ret = mklayout_text_consume(pql, str->cstring.val);
   str->cstring.val = NULL;
   pqlvalue_destroy(str);

   return ret;
}

////////////////////////////////////////////////////////////
// some additional operations that result in value creation

/*static*/ struct pqlvalue *pqlvalue_string_fromlist(struct pqlcontext *pql,
						 struct pqlvaluearray *strings,
						 const char *leftdelim,
						 const char *separator,
						 const char *rightdelim) {
   size_t len, ldlen, seplen, rdlen;
   unsigned num, i;
   const struct pqlvalue *ival;
   const char *str;
   char *buf;

   ldlen = strlen(leftdelim);
   seplen = strlen(separator);
   rdlen = strlen(rightdelim);

   len = 0;
   num = pqlvaluearray_num(strings);
   for (i=0; i<num; i++) {
      ival = pqlvaluearray_get(strings, i);
      PQLASSERT(datatype_isstring(ival->datatype));
      str = pqlvalue_string_get(ival);
      len += strlen(str);
   }

   len += ldlen + rdlen;
   if (num > 0) {
      len += (num-1) * seplen;
   }

   buf = domalloc(pql, len+1);

   strcpy(buf, leftdelim);
   for (i=0; i<num; i++) {
      if (i > 0) {
	 strcat(buf, separator);
      }
      ival = pqlvaluearray_get(strings, i);
      str = pqlvalue_string_get(ival);
      strcat(buf, str);
   }
   strcat(buf, rightdelim);

   return pqlvalue_string_consume(pql, buf);
}

struct pqlvalue *pqlvalue_paste(struct pqlcontext *pql,
				const struct pqlvalue *t1,
				const struct pqlvalue *t2) {
   const struct pqlvalue *subval;
   struct pqlvalue *pv;
   struct datatype *datatype;
   unsigned arity1, arity2, i;

   arity1 = pqlvalue_istuple(t1) ? pqlvalue_tuple_getarity(t1) : 1;
   arity2 = pqlvalue_istuple(t2) ? pqlvalue_tuple_getarity(t2) : 1;

   if (arity1 == 0) {
      return pqlvalue_clone(pql, t2);
   }
   if (arity2 == 0) {
      return pqlvalue_clone(pql, t1);
   }

   datatype = datatype_tuple_concat(pql, t1->datatype, t2->datatype);

   pv = pqlvalue_create_array(pql, datatype);
   for (i=0; i<arity1; i++) {
      subval = pqlvalue_istuple(t1) ? pqlvalue_tuple_get(t1, i) : t1;
      vr_array_add(pql, &pv->array, pqlvalue_clone(pql, subval));
   }
   for (i=0; i<arity2; i++) {
      subval = pqlvalue_istuple(t2) ? pqlvalue_tuple_get(t2, i) : t2;
      vr_array_add(pql, &pv->array, pqlvalue_clone(pql, subval));
   }
   return pv;
}

////////////////////////////////////////////////////////////
// other actions on particular value types

/*
 * (see note above about modifying values)
 */

/*static*/ void pqlvalue_string_padonleft(struct pqlvalue *val, unsigned width) {
   size_t len, padlen;
   const char *str;
   char *nstring;

   PQLASSERT(val->reptype == VR_CSTRING);
   str = vr_cstring_get(&val->cstring);
   len = strlen(str);
   if (len < width) {
      padlen = width - len;
      nstring = domalloc(val->pql, width+1);
      memset(nstring, ' ', padlen);
      memcpy(nstring+padlen, str, len);
      nstring[width] = 0;
      vr_cstring_set_consume(val->pql, &val->cstring, nstring);
   }
}

/*
 * For each element, duplicate counts[i] times. Use column markcol to
 * distinguish the originals, by setting the markcol column to null
 * in the new entries. May destroy counts[].
 */
void pqlvalue_set_pryopen(struct pqlcontext *pql,
			  struct pqlvalue *set, 
			  unsigned markcol,
			  unsigned *counts) {
   unsigned oldnum, newnum, i, j, pos;
   const struct pqlvalue *templateval;
   struct pqlvalue *val, *tmp;
   

   PQLASSERT(pqlvalue_isset(set));

   if (set->reptype == VR_TREESET) {
      /*
       * write this sometime (whole point of VR_TREESET is to
       * support this)
       */
      PQLASSERT(0);
      return;
   }
   PQLASSERT(set->reptype == VR_ARRAY);

   oldnum = pqlvalue_set_getnum(set);
   newnum = 0;
   for (i=0; i<oldnum; i++) {
      newnum += counts[i];
   }

   /* If there are any rows going away entirely, nuke them up front */
   for (i=oldnum; i-- > 0; ) {
      if (counts[i] == 0) {
	 pqlvalue_set_drop(set, i);
	 memmove(counts+i, counts+i+1, (oldnum - (i+1))*sizeof(counts[0]));
	 oldnum--;
      }
   }

   /* (don't need to change newnum...) */

   /* make more space */
   vr_array_setsize(pql, &set->array, newnum);

   /* for robustness, clear the new space to NULL */
   for (i=oldnum; i<newnum; i++) {
      vr_array_set(&set->array, i, NULL);
   }

   /* move each of the existing elements into place */
   pos = newnum;
   for (i=oldnum; i-- > 0; ) {
      /* should not overflow if we counted right */
      PQLASSERT(pos >= counts[i]);
      pos -= counts[i];
      /* we aren't clobbering anything */
      PQLASSERT(pos >= i);
      /* move the element */
      val = pqlvalue_set_replace(set, i, NULL, false);
      val = pqlvalue_set_replace(set, pos, val, false);
      PQLASSERT(val == NULL);
   }

   /* now duplicate the existing elements as new elements */
   pos = 0;
   for (i=0; i<oldnum; i++) {
      templateval = pqlvalue_set_get(set, pos++);
      PQLASSERT(templateval != NULL);
      for (j=1; j<counts[i]; j++) {
	 /*
	  * XXX: would be better to avoid cloning and deleting the
	  * markcol value, as it's probably a set.
	  */
	 val = pqlvalue_clone(pql, templateval);
	 tmp = pqlvalue_tuple_replace(val, markcol, NULL);
	 pqlvalue_destroy(tmp);
	 val = pqlvalue_set_replace(set, pos++, val, false);
	 PQLASSERT(val == NULL);
      }
   }
   PQLASSERT(pos == newnum);
}

/*
 * XXX this is really rather gross
 */
void pqlvalue_set_updatetype(struct pqlvalue *val, struct datatype *t) {

   PQLASSERT(pqlvalue_isset(val));

#if 0
   if (pqlvalue_set_getnum(val) == 0) {
      t = datatype_absany(val->pql);
   }
   else {
      t = pqlvalue_set_get(val, 0)->datatype;
   }
   t = datatype_set(val->pql, t);
#endif
   val->datatype = t;
}

struct pqlvalue *pqlvalue_set_to_sequence(struct pqlcontext *pql,
					  struct pqlvalue *set) {
   PQLASSERT(datatype_isset(set->datatype));
   set->datatype = datatype_sequence(pql, datatype_set_member(set->datatype));

   PQLASSERT(set->reptype == VR_ARRAY);
   /* no need to change the representation! */
   return set;
}

struct pqlvalue *pqlvalue_sequence_to_set(struct pqlcontext *pql,
					  struct pqlvalue *seq) {
   PQLASSERT(datatype_issequence(seq->datatype));
   seq->datatype = datatype_set(pql, datatype_sequence_member(seq->datatype));

   PQLASSERT(seq->reptype == VR_ARRAY);
   /* no need to change the representation! */
   return seq;
}

////////////////////////////////////////////////////////////
// general actions, for the user

void pqlvalue_print(char *buf, size_t bufmax, const struct pqlvalue *val) {
   struct pqlvalue *s;

   /*
    * This is not ideal from a memory copying standpoint, but it'll do.
    */
   
   s = pqlvalue_tostring(val->pql, val);
   PQLASSERT(datatype_isstring(s->datatype));
   PQLASSERT(s->reptype == VR_CSTRING);

   /* don't use strlcpy as loonix doesn't provide it */
   snprintf(buf, bufmax, "%s", s->cstring.val);
   
   pqlvalue_destroy(s);
}

