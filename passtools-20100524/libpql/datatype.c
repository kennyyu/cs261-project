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

#include <stdio.h> // for snprintf
#include <string.h>

#include "array.h"
#include "pqlcontext.h"

#define DATATYPE_INLINE
#include "datatype.h"


enum datatyperepresentations {
   DT_BASE,			/* one of the base types in the manager */
   DT_SET,			/* set(MEMBER) */
   DT_SEQUENCE,			/* sequence(MEMBER) */
   DT_PAIR,			/* (LEFT, RIGHT) */
   DT_LAMBDA,			/* (ARGUMENT -> RESULT) */
};
struct datatype {
   struct datatypemanager *dtm;
   char *name;

   enum datatyperepresentations rep;
   union {
      /* base - nothing */
      struct {
	 struct datatype *member;
      } set;
      struct {
	 struct datatype *member;
      } sequence;
      struct {
	 struct datatype *left;
	 struct datatype *right;
      } pair;
      struct {
	 struct datatype *argument;
	 struct datatype *result;
      } lambda;
   };

   /* Types derived from this type T */
   struct datatype *set_of;		/* set(T) */
   struct datatype *sequence_of;	/* sequence(T) */
   struct datatypearray pairs;		/* all (T, RIGHT) */
   struct datatypearray lambdas;	/* all (T -> RESULT) */
};

struct datatypemanager {
   struct pqlcontext *pql;

   struct datatype type_unit;
   struct datatype type_bool;
   struct datatype type_int;
   struct datatype type_double;
   struct datatype type_string;
   struct datatype type_struct;
   struct datatype type_pathelement;
   struct datatype type_distinguisher;

   struct datatype type_absdbedge;
   struct datatype type_absnumber;
   struct datatype type_absatom;
   struct datatype type_absdbobj;
   struct datatype type_abstop;
   struct datatype type_absbottom;
};

////////////////////////////////////////////////////////////

static void datatype_destroy(struct datatype *t);

static void datatype_init(struct datatypemanager *dtm,
			  struct datatype *t,
			  const char *name,
			  enum datatyperepresentations rep) {
   t->dtm = dtm;
   t->name = dostrdup(dtm->pql, name);
   t->rep = rep;
   t->set_of = t->sequence_of = NULL;
   datatypearray_init(&t->pairs);
   datatypearray_init(&t->lambdas);
}

static void datatype_cleanup(struct datatype *t) {
   unsigned i, num;

   dostrfree(t->dtm->pql, t->name);
   t->name = NULL;

#if 0 /* nothing to do */
   switch (t->type) {
    case DT_BASE:
    case DT_SET:
    case DT_SEQUENCE:
    case DT_PAIR:
    case DT_LAMBDA:
   }
#endif

   if (t->set_of != NULL) {
      datatype_destroy(t->set_of);
      t->set_of = NULL;
   }

   if (t->sequence_of != NULL) {
      datatype_destroy(t->sequence_of);
      t->sequence_of = NULL;
   }

   num = datatypearray_num(&t->pairs);
   for (i=0; i<num; i++) {
      datatype_destroy(datatypearray_get(&t->pairs, i));
   }
   datatypearray_setsize(t->dtm->pql, &t->pairs, 0);
   datatypearray_cleanup(t->dtm->pql, &t->pairs);

   num = datatypearray_num(&t->lambdas);
   for (i=0; i<num; i++) {
      datatype_destroy(datatypearray_get(&t->lambdas, i));
   }
   datatypearray_setsize(t->dtm->pql, &t->lambdas, 0);
   datatypearray_cleanup(t->dtm->pql, &t->lambdas);
}

static struct datatype *datatype_create(struct pqlcontext *pql,
					const char *name,
					enum datatyperepresentations rep) {
   struct datatype *t;

   t = domalloc(pql, sizeof(*t));
   datatype_init(pql->dtm, t, name, rep);
   return t;
}

static void datatype_destroy(struct datatype *t) {
   datatype_cleanup(t);
   dofree(t->dtm->pql, t, sizeof(*t));
}

////////////////////////////////////////////////////////////

struct datatypemanager *datatypemanager_create(struct pqlcontext *pql) {
   struct datatypemanager *dtm;

   dtm = domalloc(pql, sizeof(*dtm));

   dtm->pql = pql;

   datatype_init(dtm, &dtm->type_unit, "unit", DT_BASE);
   datatype_init(dtm, &dtm->type_bool, "bool", DT_BASE);
   datatype_init(dtm, &dtm->type_int, "int", DT_BASE);
   datatype_init(dtm, &dtm->type_double, "double", DT_BASE);
   datatype_init(dtm, &dtm->type_string, "string", DT_BASE);
   datatype_init(dtm, &dtm->type_struct, "struct", DT_BASE);
   datatype_init(dtm, &dtm->type_pathelement, "pathelement", DT_BASE);
   datatype_init(dtm, &dtm->type_distinguisher, "distinguisher", DT_BASE);

   datatype_init(dtm, &dtm->type_absdbedge, "dbedge", DT_BASE);
   datatype_init(dtm, &dtm->type_absnumber, "number", DT_BASE);
   datatype_init(dtm, &dtm->type_absatom, "atom", DT_BASE);
   datatype_init(dtm, &dtm->type_absdbobj, "dbobj", DT_BASE);
   datatype_init(dtm, &dtm->type_abstop, "top", DT_BASE);
   datatype_init(dtm, &dtm->type_absbottom, "bottom", DT_BASE);

   return dtm;
}

void datatypemanager_destroy(struct datatypemanager *dtm) {

   datatype_cleanup(&dtm->type_unit);
   datatype_cleanup(&dtm->type_bool);
   datatype_cleanup(&dtm->type_int);
   datatype_cleanup(&dtm->type_double);
   datatype_cleanup(&dtm->type_string);
   datatype_cleanup(&dtm->type_struct);
   datatype_cleanup(&dtm->type_pathelement);
   datatype_cleanup(&dtm->type_distinguisher);

   datatype_cleanup(&dtm->type_absdbedge);
   datatype_cleanup(&dtm->type_absnumber);
   datatype_cleanup(&dtm->type_absatom);
   datatype_cleanup(&dtm->type_absdbobj);
   datatype_cleanup(&dtm->type_abstop);
   datatype_cleanup(&dtm->type_absbottom);

   dofree(dtm->pql, dtm, sizeof(*dtm));
}

////////////////////////////////////////////////////////////

/*
 * Base types (kind "*")
 */

/* concrete */

struct datatype *datatype_unit(struct pqlcontext *pql) {
   return &pql->dtm->type_unit;
}

struct datatype *datatype_bool(struct pqlcontext *pql) {
   return &pql->dtm->type_bool;
}

struct datatype *datatype_int(struct pqlcontext *pql) {
   return &pql->dtm->type_int;
}

struct datatype *datatype_double(struct pqlcontext *pql) {
   return &pql->dtm->type_double;
}

struct datatype *datatype_string(struct pqlcontext *pql) {
   return &pql->dtm->type_string;
}

struct datatype *datatype_struct(struct pqlcontext *pql) {
   return &pql->dtm->type_struct;
}

struct datatype *datatype_pathelement(struct pqlcontext *pql) {
   return &pql->dtm->type_pathelement;
}

struct datatype *datatype_distinguisher(struct pqlcontext *pql) {
   return &pql->dtm->type_distinguisher;
}

/* abstract */

struct datatype *datatype_absdbedge(struct pqlcontext *pql) {
   return &pql->dtm->type_absdbedge;
}

struct datatype *datatype_absnumber(struct pqlcontext *pql) {
   return &pql->dtm->type_absnumber;
}

struct datatype *datatype_absatom(struct pqlcontext *pql) {
   return &pql->dtm->type_absatom;
}

struct datatype *datatype_absdbobj(struct pqlcontext *pql) {
   return &pql->dtm->type_absdbobj;
}

struct datatype *datatype_abstop(struct pqlcontext *pql) {
   return &pql->dtm->type_abstop;
}

struct datatype *datatype_absbottom(struct pqlcontext *pql) {
   return &pql->dtm->type_absbottom;
}

// XXX these two should be removed
struct datatype *datatype_dbobj(struct pqlcontext *pql) {
   return datatype_absdbobj(pql);
}

struct datatype *datatype_dbedge(struct pqlcontext *pql) {
   return datatype_absdbedge(pql);
}

////////////////////////////////////////////////////////////

/*
 * Collection types (kind "* -> *")
 */

struct datatype *datatype_set(struct pqlcontext *pql, struct datatype *t) {
   PQLASSERT(t->dtm == pql->dtm);
   if (t->set_of == NULL) {
      char buf[1024];

      snprintf(buf, sizeof(buf), "set(%s)", t->name);
      t->set_of = datatype_create(pql, buf, DT_SET);
      t->set_of->set.member = t;
   }
   return t->set_of;
}

struct datatype *datatype_sequence(struct pqlcontext *pql, struct datatype *t){
   PQLASSERT(t->dtm == pql->dtm);
   if (t->sequence_of == NULL) {
      char buf[1024];

      snprintf(buf, sizeof(buf), "seq(%s)", t->name);
      t->sequence_of = datatype_create(pql, buf, DT_SEQUENCE);
      t->sequence_of->sequence.member = t;
   }
   return t->sequence_of;
}

////////////////////////////////////////////////////////////

/*
 * Tuples
 *
 * Tuples are encoded as pairs (kind "(*, *) -> *").
 */


struct datatype *datatype_tuple_pair(struct pqlcontext *pql,
				     struct datatype *t0,
				     struct datatype *t1) {
   unsigned i, num;
   struct datatype *t;
   char buf[1024];
   size_t len;

   PQLASSERT(t0->dtm == pql->dtm);
   PQLASSERT(t1->dtm == pql->dtm);

   num = datatypearray_num(&t0->pairs);
   for (i=0; i<num; i++) {
      t = datatypearray_get(&t0->pairs, i);
      PQLASSERT(t->pair.left == t0);
      if (t->pair.right == t1) {
	 /* found it */
	 return t;
      }
   }

   /* not found, make one */

   if (t0->rep == DT_PAIR) {
      /* t0 is a tuple, extend the existing parens in the name */
      len = strlen(t0->name);
      PQLASSERT(len > 2);
      PQLASSERT(t0->name[0] == '(');
      PQLASSERT(t0->name[len-1] == ')');
      snprintf(buf, sizeof(buf), "(%.*s, %s)", (int)(len-2), t0->name+1,
	       t1->name);
   }
   else {
      snprintf(buf, sizeof(buf), "(%s, %s)", t0->name, t1->name);
   }

   t = datatype_create(pql, buf, DT_PAIR);
   t->pair.left = t0;
   t->pair.right = t1;
   datatypearray_add(t0->dtm->pql, &t0->pairs, t, NULL);

   return t;
}

/*
 * Recursive tuple builder.
 */
static struct datatype *datatype_tuple_build(struct pqlcontext *pql,
					     unsigned arity,
					     const struct datatypearray *
					       members) {
   struct datatype *t;

   if (arity == 0) {
      return datatype_unit(pql);
   }
   if (arity == 1) {
      t = datatypearray_get(members, 0);
      PQLASSERT(t->dtm == pql->dtm);
      // XXX: is this what we want?
      return t;
   }

   if (arity > 2) {
      t = datatype_tuple_build(pql, arity-1, members);
      return datatype_tuple_pair(pql, t, datatypearray_get(members, arity-1));
   }

   return datatype_tuple_pair(pql,
			      datatypearray_get(members, 0),
			      datatypearray_get(members, 1));
}

struct datatype *datatype_tuple_specific(struct pqlcontext *pql,
					 const struct datatypearray *members) {
   return datatype_tuple_build(pql, datatypearray_num(members), members);
}

struct datatype *datatype_tuple_triple(struct pqlcontext *pql,
				       struct datatype *t0,
				       struct datatype *t1,
				       struct datatype *t2) {
   struct datatype *pair;

   pair = datatype_tuple_pair(pql, t0, t1);
   return datatype_tuple_pair(pql, pair, t2);
}

struct datatype *datatype_tuple_concat(struct pqlcontext *pql,
				       struct datatype *t0,
				       struct datatype *t1) {
   if (t0 == datatype_unit(pql)) {
      return t1;
   }
   if (t1 == datatype_unit(pql)) {
      return t0;
   }
   if (t1->rep != DT_PAIR) {
      /* t1 isn't a tuple and can be consed onto the end */
      return datatype_tuple_pair(pql, t0, t1);
   }
   return datatype_tuple_pair(pql,
			      datatype_tuple_concat(pql, t0, t1->pair.left),
			      t1->pair.right);
}

struct datatype *datatype_tuple_append(struct pqlcontext *pql,
				       struct datatype *t0,
				       struct datatype *t1) {
   /* treat t1 as a non-tuple and cons it onto the end */
   if (t0 == datatype_unit(pql)) {
      return t1;
   }
   if (t1 == datatype_unit(pql)) {
      return t0;
   }
   return datatype_tuple_pair(pql, t0, t1);
}

struct datatype *datatype_tuple_strip(struct pqlcontext *pql,
				      const struct datatype *t, unsigned ix) {
   unsigned arity;
   struct datatype *left;

   arity = datatype_arity(t);
   PQLASSERT(ix < arity);

   if (arity == 1) {
      return datatype_unit(pql);
   }

   PQLASSERT(t->rep == DT_PAIR);

   if (ix == arity - 1) {
      return t->pair.left;
   }

   left = datatype_tuple_strip(pql, t->pair.left, ix);
   if (datatype_arity(left) == 0) {
      return t->pair.right;
   }
   return datatype_tuple_pair(pql,
			      left,
			      t->pair.right);
}

struct datatype *datatype_tupleset_strip(struct pqlcontext *pql,
					 const struct datatype *t,
					 unsigned ix) {
   struct datatype *t2;
   bool isset = false, issequence = false;

   if (datatype_isset(t)) {
      isset = true;
      t = datatype_set_member(t);
   }
   else if (datatype_issequence(t)) {
      issequence = true;
      t = datatype_sequence_member(t);
   }

   t2 = datatype_tuple_strip(pql, t, ix);

   if (issequence) {
      t2 = datatype_sequence(pql, t2);
   }
   else if (isset) {
      t2 = datatype_set(pql, t2);
   }

   return t2;
}

////////////////////////////////////////////////////////////

/*
 * lambdas
 */

struct datatype *datatype_lambda(struct pqlcontext *pql,
				 struct datatype *arg,
				 struct datatype *res) {
   unsigned i, num;
   struct datatype *t;
   char buf[1024];

   PQLASSERT(arg->dtm == pql->dtm);
   PQLASSERT(arg->dtm == pql->dtm);

   num = datatypearray_num(&arg->lambdas);
   for (i=0; i<num; i++) {
      t = datatypearray_get(&arg->lambdas, i);
      PQLASSERT(t->lambda.argument == arg);
      if (t->lambda.result == res) {
	 /* found it */
	 return t;
      }
   }

   /* not found, make one */
   snprintf(buf, sizeof(buf), "(%s -> %s)", arg->name, res->name);
   t = datatype_create(pql, buf, DT_LAMBDA);
   t->lambda.argument = arg;
   t->lambda.result = res;
   datatypearray_add(t->dtm->pql, &arg->lambdas, t, NULL);

   return t;
}

////////////////////////////////////////////////////////////

bool datatype_eq(const struct datatype *t0, const struct datatype *t1) {
   return t0 == t1;
}

bool datatype_isbool(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_bool);
}

bool datatype_isint(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_int);
}

bool datatype_isdouble(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_double);
}

bool datatype_isstring(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_string);
}

bool datatype_isstruct(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_struct);
}

bool datatype_ispathelement(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_pathelement);
}

bool datatype_isdistinguisher(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_distinguisher);
}

/*
 * A type is a tuple if (1) it's unit, or (2) its representation is a
 * pair.
 */
bool datatype_istuple(const struct datatype *t) {
   if (t == &t->dtm->type_unit) {
      return true;
   }
   return t->rep == DT_PAIR;
}

bool datatype_islambda(const struct datatype *t) {
   return t->rep == DT_LAMBDA;
}

bool datatype_isset(const struct datatype *t) {
   return t->rep == DT_SET;
}

bool datatype_issequence(const struct datatype *t) {
   return t->rep == DT_SEQUENCE;
}

bool datatype_isabsdbedge(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_absdbedge);
}

bool datatype_isabsnumber(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_absnumber);
}

bool datatype_isabsatom(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_absatom);
}

bool datatype_isabsdbobj(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_absdbobj);
}

bool datatype_isabstop(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_abstop);
}

bool datatype_isabsbottom(const struct datatype *t) {
   return datatype_eq(t, &t->dtm->type_absbottom);
}

// XXX these should be removed

bool datatype_isdbedge(const struct datatype *t) {
   return datatype_isabsdbedge(t);
}

bool datatype_isdbobj(const struct datatype *t) {
   return datatype_isabsdbobj(t);
}

////////////////////////////////////////////////////////////

/*
 * Database edges may be strings or integers.
 */
bool datatype_isanydbedge(const struct datatype *t) {
   return datatype_isabsbottom(t) || datatype_isabsdbedge(t) || datatype_isstring(t) || datatype_isint(t);
}

/*
 * Numbers are integers or floats.
 */
bool datatype_isanynumber(const struct datatype *t) {
   return datatype_isabsbottom(t) || datatype_isabsnumber(t) || datatype_isint(t) || datatype_isdouble(t);
}

/*
 * Atoms are numbers, strings, or booleans.
 */
bool datatype_isanyatom(const struct datatype *t) {
   return datatype_isabsbottom(t) ||
      datatype_isabsatom(t) ||
      datatype_isanynumber(t) ||
      datatype_isstring(t) || datatype_isbool(t);
}

/*
 * Database objects are atoms or structs.
 */
bool datatype_isanydbobj(const struct datatype *t) {
   return datatype_isabsbottom(t) ||
      datatype_isabsdbobj(t) ||
      datatype_isanyatom(t) || datatype_isstruct(t);
}

////////////////////////////////////////////////////////////

/*
 * Find the least common supertype of T1 and T2. In the worst case,
 * returns top.
 */
struct datatype *datatype_match_generalize(struct pqlcontext *pql,
					   struct datatype *t1,
					   struct datatype *t2) {
   struct datatype *result;

   if (datatype_eq(t1, t2)) {
      return t1;
   }

   if (datatype_isabsbottom(t1)) {
      return t2;
   }
   if (datatype_isabsbottom(t2)) {
      return t1;
   }

   /* Compound types */

   if (datatype_isset(t1) && datatype_isset(t2)) {
      result = datatype_match_generalize(pql,
					 datatype_set_member(t1),
					 datatype_set_member(t2));
      return datatype_set(pql, result);
   }
   if (datatype_issequence(t1) && datatype_issequence(t2)) {
      result = datatype_match_generalize(pql,
					 datatype_sequence_member(t1),
					 datatype_sequence_member(t2));
      return datatype_sequence(pql, result);
   }

   /*
    * Theoretically we ought to match
    *
    *    number -> number    =>  int -> number
    *    int -> number
    *
    * and
    *
    *    int -> int    =>  int -> number
    *    int -> float
    *
    * but I'm not going to bother: we don't allow the user to make
    * their own lambdas and the ones we generate should involve
    * exact-matching tuples.
    */
   if (datatype_islambda(t1) || datatype_islambda(t2)) {
      /* since they aren't the same, use top */
      return datatype_abstop(pql);
   }

   if (datatype_istuple(t1) && datatype_istuple(t2)) {
      struct datatypearray members;
      struct datatype *st1, *st2, *stm, *ret;
      unsigned i, arity;

      arity = datatype_arity(t1);
      if (arity != datatype_arity(t2)) {
	 return datatype_abstop(pql);
      }

      datatypearray_init(&members);
      datatypearray_setsize(t1->dtm->pql, &members, arity);

      for (i=0; i<arity; i++) {
	 st1 = datatype_getnth(t1, i);
	 st2 = datatype_getnth(t2, i);
	 stm = datatype_match_generalize(pql, st1, st2);
	 datatypearray_set(&members, i, stm);
      }

      ret = datatype_tuple_specific(pql, &members);
      
      datatypearray_setsize(t1->dtm->pql, &members, 0);
      datatypearray_cleanup(t1->dtm->pql, &members);

      return ret;
   }

   if (datatype_istuple(t1) || datatype_istuple(t2)) {
      /* since they aren't both tuples, use top */
      return datatype_abstop(pql);
   }

   /* Base types and abstract supertypes */

   /* Don't generate dbedge unless it's already present */
   if ((datatype_isabsdbedge(t1) && datatype_isanydbedge(t2)) ||
       (datatype_isanydbedge(t1) && datatype_isabsdbedge(t2))) {
      return datatype_absdbedge(pql);
   }

   /* number */
   if (datatype_isanynumber(t1) && datatype_isanynumber(t2)) {
      return datatype_absnumber(pql);
   }

   /* atom */
   if (datatype_isanyatom(t1) && datatype_isanyatom(t2)) {
      return datatype_absatom(pql);
   }

   /* dbobj */
   if (datatype_isanydbobj(t1) && datatype_isanydbobj(t2)) {
      return datatype_absdbobj(pql);
   }

   /* no luck */
   return datatype_abstop(pql);
}

/*
 * Find the greatest common subtype of T1 and T2. If none, returns
 * NULL.
 */
struct datatype *datatype_match_specialize(struct pqlcontext *pql,
					   struct datatype *t1,
					   struct datatype *t2) {
   struct datatype *result;

   if (datatype_eq(t1, t2)) {
      return t1;
   }

   if (datatype_isabsbottom(t1) || datatype_isabsbottom(t2)) {
      return datatype_absbottom(pql);
   }

   if (datatype_isabstop(t1)) {
      return t2;
   }
   if (datatype_isabstop(t2)) {
      return t1;
   }

   /* Compound types */

   if (datatype_isset(t1) && datatype_isset(t2)) {
      result = datatype_match_specialize(pql,
					 datatype_set_member(t1),
					 datatype_set_member(t2));
      if (result == NULL) {
	 return NULL;
      }
      return datatype_set(pql, result);
   }
   if (datatype_issequence(t1) && datatype_issequence(t2)) {
      result = datatype_match_specialize(pql,
					 datatype_sequence_member(t1),
					 datatype_sequence_member(t2));
      if (result == NULL) {
	 return NULL;
      }
      return datatype_sequence(pql, result);
   }

   /*
    * As in generalize, punt on lambdas.
    */
   if (datatype_islambda(t1) || datatype_islambda(t2)) {
      /* since they aren't the same, fail */
      return NULL;
   }

   if (datatype_istuple(t1) && datatype_istuple(t2)) {
      struct datatypearray members;
      struct datatype *st1, *st2, *stm, *ret;
      unsigned i, arity;

      arity = datatype_arity(t1);
      if (arity != datatype_arity(t2)) {
	 return NULL;
      }

      datatypearray_init(&members);
      datatypearray_setsize(t1->dtm->pql, &members, arity);

      for (i=0; i<arity; i++) {
	 st1 = datatype_getnth(t1, i);
	 st2 = datatype_getnth(t2, i);
	 stm = datatype_match_specialize(pql, st1, st2);
	 if (stm == NULL) {
	    datatypearray_setsize(t1->dtm->pql, &members, 0);
	    datatypearray_cleanup(t1->dtm->pql, &members);
	    return NULL;
	 }
	 datatypearray_set(&members, i, stm);
      }

      ret = datatype_tuple_specific(pql, &members);
      
      datatypearray_setsize(t1->dtm->pql, &members, 0);
      datatypearray_cleanup(t1->dtm->pql, &members);

      return ret;
   }

   if (datatype_istuple(t1) || datatype_istuple(t2)) {
      /* since they aren't the same, fail */
      return NULL;
   }

   /* Base types and abstract supertypes */

   /* Don't involve dbedge unless it's already present */
   if (datatype_isabsdbedge(t1) && datatype_isanydbedge(t2)) {
      return t2;
   }
   if (datatype_isanydbedge(t1) && datatype_isabsdbedge(t2)) {
      return t1;
   }

   /* number */
   if (datatype_isabsnumber(t1) && datatype_isanynumber(t2)) {
      return t2;
   }
   if (datatype_isanynumber(t1) && datatype_isabsnumber(t2)) {
      return t1;
   }

   /* atom */
   if (datatype_isabsatom(t1) && datatype_isanyatom(t2)) {
      return t2;
   }
   if (datatype_isanyatom(t1) && datatype_isabsatom(t2)) {
      return t1;
   }

   /* dbobj */
   if (datatype_isabsdbobj(t1) && datatype_isanydbobj(t2)) {
      return t2;
   }
   if (datatype_isanydbobj(t1) && datatype_isabsdbobj(t2)) {
      return t1;
   }

   /* no luck */
   // XXX should be bottom
   return NULL;
}

////////////////////////////////////////////////////////////

const char *datatype_getname(const struct datatype *t) {
   return t->name;
}

unsigned datatype_arity(const struct datatype *t) {
   if (datatype_eq(t, &t->dtm->type_unit)) {
      return 0;
   }
   if (t->rep != DT_PAIR) {
      return 1;
   }

   if (datatype_eq(t->pair.left, &t->dtm->type_unit)) {
      // XXX if left is unit, its arity is 0 and we'll end up returning
      // 1 instead of 2. Arguably the representation ought to be fixed
      // to not cause this... ugh.
      return 2;
   }

   return datatype_arity(t->pair.left) + 1;
}

struct datatype *datatype_getnth(/*const*/ struct datatype *t, unsigned n) {
   unsigned arity;

   /* unit has no members; therefore cannot get members from it */
   PQLASSERT(t != &t->dtm->type_unit);

   arity = datatype_arity(t);
   PQLASSERT(n < arity);

   /* allow getting index 0 from a non-tuple (monople) */
   if (arity == 1) {
      //PQLASSERT(n == 0); -- redundant
      return t;
   }

   PQLASSERT(t->rep == DT_PAIR);

   while (arity >= 2) {
      if (n == arity-1) {
	 return t->pair.right;
      }
      t = t->pair.left;
      arity--;
   }
   return t;
}

unsigned datatype_nonset_arity(const struct datatype *t) {
   if (datatype_isset(t)) {
      t = datatype_set_member(t);
   }
   if (datatype_issequence(t)) {
      t = datatype_sequence_member(t);
   }
   return datatype_arity(t);
}

struct datatype *datatype_lambda_argument(const struct datatype *t) {
   PQLASSERT(t->rep == DT_LAMBDA);
   return t->lambda.argument;
}

struct datatype *datatype_lambda_result(const struct datatype *t) {
   PQLASSERT(t->rep == DT_LAMBDA);
   return t->lambda.result;
}

struct datatype *datatype_set_member(const struct datatype *t) {
   PQLASSERT(t->rep == DT_SET);
   return t->set.member;
}

struct datatype *datatype_sequence_member(const struct datatype *t) {
   PQLASSERT(t->rep == DT_SEQUENCE);
   return t->sequence.member;
}
