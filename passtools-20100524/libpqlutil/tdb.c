#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "pql.h"
#include "pqlutil.h"

////////////////////////////////////////////////////////////
// malloc

static void *tdb_malloc(size_t size) {
   void *ret;

   ret = malloc(size);
   if (ret == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(1);
   }
   return ret;
}

////////////////////////////////////////////////////////////
// array

/*
 * Probably this should use libpql's typesafe arrays, but sharing them
 * is a pain and we don't really need the full generality. Also I
 * don't want to either pollute the linker namespace or require
 * setting up all the abi.txt crap libpql needs.
 */

struct tdbarray {
   unsigned num, max;
   void **v;
};

static inline void tdbarray_init(struct tdbarray *a) {
   a->num = a->max = 0;
   a->v = NULL;
}

static inline void tdbarray_cleanup(struct tdbarray *a) {
   assert(a->num == 0);
   free(a->v);
}

static inline unsigned tdbarray_num(const struct tdbarray *a) {
   return a->num;
}

static inline void *tdbarray_get(const struct tdbarray *a, unsigned index) {
   assert(index < a->num);
   return a->v[index];
}

static inline void tdbarray_set(const struct tdbarray *a, unsigned index,
				void *val) {
   assert(index < a->num);
   a->v[index] = val;
}

static void tdbarray_setsize(struct tdbarray *a, unsigned num) {
   unsigned newmax;
   void **newptr;

   if (num > a->max) {
      newmax = a->max;
      while (num > newmax) {
	 newmax = newmax ? newmax*2 : 4;
      }
      newptr = realloc(a->v, newmax*sizeof(*a->v));
      if (newptr == NULL) {
	 fprintf(stderr, "Out of memory\n");
	 exit(1);
      }
      a->v = newptr;
      a->max = newmax;
   }
   a->num = num;
}

static inline void tdbarray_add(struct tdbarray *a, void *val,
				unsigned *index_ret) {
   unsigned index;

   index = a->num;
   tdbarray_setsize(a, index+1);
   a->v[index] = val;
   if (index_ret != NULL) {
      *index_ret = index;
   }
}

////////////////////////////////////////////////////////////
// storage types

struct tdb_field {
   struct pqlvalue *edge;
   struct pqlvalue *val;
};

struct tdb_object {
   struct tdbarray fields;  /* array of tdb_field */
};

struct tdb {
   struct tdbarray objects; /* array of tdb_object */
};

////////////////////////////////////////////////////////////
// fields

static struct tdb_field *tdb_field_create(struct pqlcontext *pql,
					  const struct pqlvalue *e,
					  const struct pqlvalue *v) {
   struct tdb_field *f;

   f = tdb_malloc(sizeof(*f));
   f->edge = pqlvalue_clone(pql, e);
   f->val = pqlvalue_clone(pql, v);
   return f;
}

#if 0 /* unused */
static struct tdb_field *tdb_field_clone(const struct tdb_field *f) {
   return tdb_field_create(f->edge, f->val);
}
#endif

static void tdb_field_destroy(struct tdb_field *f) {
   pqlvalue_destroy(f->edge);
   pqlvalue_destroy(f->val);
   free(f);
}

////////////////////////////////////////////////////////////
// objects

static struct tdb_object *tdb_object_create(void) {
   struct tdb_object *obj;

   obj = tdb_malloc(sizeof(*obj));
   tdbarray_init(&obj->fields);
   return obj;
}

#if 0 // unused
static struct tdb_object *tdb_object_clone(struct tdb_object *oldobj) {
   struct tdb_object *newobj;
   unsigned i, num;

   num = tdbarray_num(&oldobj->fields);
   newobj = tdb_object_create();
   tdbarray_setsize(&newobj->fields, num);
   for (i=0; i<num; i++) {
      tdbarray_set(&newobj->fields, i, 
		   tdb_field_clone(tdbarray_get(&oldobj->fields, i)));
   }
   return newobj;
}
#endif

static void tdb_object_destroy(struct tdb_object *obj) {
   unsigned i, num;

   num = tdbarray_num(&obj->fields);
   for (i=0; i<num; i++) {
      tdb_field_destroy(tdbarray_get(&obj->fields, i));
   }
   tdbarray_setsize(&obj->fields, 0);
   tdbarray_cleanup(&obj->fields);
   free(obj);
}

////////////////////////////////////////////////////////////
// databases

struct tdb *tdb_create(void) {
   struct tdb *t;

   t = tdb_malloc(sizeof(*t));
   tdbarray_init(&t->objects);
   return t;
}

void tdb_destroy(struct tdb *t) {
   unsigned i, num;

   num = tdbarray_num(&t->objects);
   for (i=0; i<num; i++) {
      tdb_object_destroy(tdbarray_get(&t->objects, i));
   }
   tdbarray_setsize(&t->objects, 0);
   tdbarray_cleanup(&t->objects);
   free(t);
}

////////////////////////////////////////////////////////////
// interface

pqloid_t tdb_newobject(struct tdb *t) {
   struct tdb_object *obj;
   unsigned num;

   obj = tdb_object_create();
   tdbarray_add(&t->objects, obj, &num);
   return (pqloid_t)num;
}

int tdb_assign(struct pqlcontext *pql,
	       struct tdb *t, pqloid_t oid,
	       const struct pqlvalue *edge, const struct pqlvalue *val) {
   struct tdb_object *obj;
   unsigned index = (unsigned)oid;

   assert(oid < (unsigned)tdbarray_num(&t->objects));

   obj = tdbarray_get(&t->objects, index);
   tdbarray_add(&obj->fields, tdb_field_create(pql, edge, val), NULL);
   return 0;
}

struct pqlvalue *tdb_follow(struct pqlcontext *pql,
			    struct tdb *t,
			    pqloid_t oid,
			    const struct pqlvalue *edge,
			    bool reverse) {
   struct tdb_field *f;
   struct tdb_object *obj;
   unsigned index = (unsigned)oid;
   struct pqlvalue *ret;
   unsigned i, num;

   assert(!reverse); // XXX
   assert(oid < (unsigned)tdbarray_num(&t->objects));

   obj = tdbarray_get(&t->objects, index);

   ret = pqlvalue_emptyset(pql);
   num = tdbarray_num(&obj->fields);
   for (i=0; i<num; i++) {
      f = tdbarray_get(&obj->fields, i);
      if (pqlvalue_eq(edge, f->edge)) {
	 pqlvalue_set_add(ret, pqlvalue_clone(pql, f->val));
      }
   }
   return ret;
}

struct pqlvalue *tdb_followall(struct pqlcontext *pql,
			       struct tdb *t, pqloid_t oid, bool reverse) {
   struct tdb_field *f;
   struct tdb_object *obj;
   unsigned index = (unsigned)oid;
   struct pqlvalue *ret;
   struct pqlvalue *edge, *val;
   unsigned i, num;

   assert(!reverse); // XXX
   assert(oid < (unsigned)tdbarray_num(&t->objects));

   obj = tdbarray_get(&t->objects, index);

   ret = pqlvalue_emptyset(pql);
   num = tdbarray_num(&obj->fields);
   for (i=0; i<num; i++) {
      f = tdbarray_get(&obj->fields, i);
      edge = pqlvalue_clone(pql, f->edge);
      val = pqlvalue_clone(pql, f->val);
      pqlvalue_set_add(ret, pqlvalue_pair(pql, edge, val));
   }
   return ret;
}
