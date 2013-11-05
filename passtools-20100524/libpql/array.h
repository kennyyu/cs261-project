/*
 * Copyright (c) 2009 David A. Holland.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Author nor the names of any contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef ARRAY_H
#define ARRAY_H

#include "inlinedefs.h"
#include "memdefs.h"

#define ARRAYS_CHECKED

#ifdef ARRAYS_CHECKED
#include "utils.h"
#define arrayassert PQLASSERT
#else
#define arrayassert(x) ((void)(x))
#endif

////////////////////////////////////////////////////////////
// type and base operations

struct array {
   void **v;
   unsigned num, max;
};

struct array *array_create(struct pqlcontext *);
void array_destroy(struct pqlcontext *, struct array *);
void array_init(struct array *);
void array_cleanup(struct pqlcontext *, struct array *);
unsigned array_num(const struct array *);
void *array_get(const struct array *, unsigned index);
void array_set(const struct array *, unsigned index, void *val);
int array_setsize(struct pqlcontext *pql, struct array *, unsigned num);
int array_add(struct pqlcontext *pql, struct array *, void *val, unsigned *index_ret);
int array_insert(struct pqlcontext *pql, struct array *a, unsigned index);
void array_remove(struct array *a, unsigned index);

////////////////////////////////////////////////////////////
// inlining for base operations

#ifndef ARRAYINLINE
#define ARRAYINLINE INLINE
#endif

ARRAYINLINE unsigned array_num(const struct array *a) {
   return a->num;
}

ARRAYINLINE void *array_get(const struct array *a, unsigned index) {
   arrayassert(index < a->num);
   return a->v[index];
}

ARRAYINLINE void array_set(const struct array *a, unsigned index, void *val) {
   arrayassert(index < a->num);
   a->v[index] = val;
}

ARRAYINLINE int array_add(struct pqlcontext *pql, struct array *a, void *val, unsigned *index_ret) {
   unsigned index = a->num;
   if (array_setsize(pql, a, index+1)) {
      return -1;
   }
   a->v[index] = val;
   if (index_ret != NULL) {
      *index_ret = index;
   }
   return 0;
}

////////////////////////////////////////////////////////////
// bits for declaring and defining typed arrays

/*
 * Usage:
 *
 * DECLARRAY_BYTYPE(foo, bar) declares "struct foo", which is
 * an array of pointers to "bar", plus the operations on it.
 *
 * DECLARRAY(foo) is equivalent to DECLARRAY_BYTYPE(fooarray, struct foo).
 *
 * DEFARRAY_BYTYPE and DEFARRAY are the same as DECLARRAY except that
 * they define the operations, and both take an extra argument INLINE.
 * For C99 this should be INLINE in header files and empty in the
 * master source file, the same as the usage of ARRAYINLINE above and
 * in array.c.
 *
 * Example usage in e.g. item.h of some game:
 * 
 * DECLARRAY_BYTYPE(stringarray, char);
 * DECLARRAY(potion);
 * DECLARRAY(sword);
 *
 * #ifndef ITEMINLINE
 * #define ITEMINLINE INLINE
 * #endif
 *
 * DEFARRAY_BYTYPE(stringarray, char, ITEMINLINE);
 * DEFARRAY(potion, ITEMINLINE);
 * DEFARRAY(sword, ITEMINLINE);
 *
 * Then item.c would do "#define ITEMINLINE" before including item.h.
 */

#define DECLARRAY_BYTYPE(ARRAY, T) \
	struct ARRAY {						\
	   struct array arr;					\
	};							\
								\
	struct ARRAY *ARRAY##_create(struct pqlcontext *pql);	\
	void ARRAY##_destroy(struct pqlcontext *pql, struct ARRAY *a); \
	void ARRAY##_init(struct ARRAY *a);			\
	void ARRAY##_cleanup(struct pqlcontext *, struct ARRAY *a); \
	unsigned ARRAY##_num(const struct ARRAY *a);		\
	T *ARRAY##_get(const struct ARRAY *a, unsigned index);	\
	void ARRAY##_set(struct ARRAY *a, unsigned index, T *val); \
	int ARRAY##_setsize(struct pqlcontext *pql, struct ARRAY *a, unsigned num);	\
	int ARRAY##_add(struct pqlcontext *pql, struct ARRAY *a, T *val, unsigned *index_ret); \
	int ARRAY##_insert(struct pqlcontext *pql, struct ARRAY *a, unsigned index);	\
	void ARRAY##_remove(struct ARRAY *a, unsigned index)


#define DEFARRAY_BYTYPE(ARRAY, T, INLINE) \
	INLINE void ARRAY##_init(struct ARRAY *a) {		\
	   array_init(&a->arr);					\
	}							\
								\
	INLINE void ARRAY##_cleanup(struct pqlcontext *pql, struct ARRAY *a){ \
	   array_cleanup(pql, &a->arr);				\
	}							\
								\
	INLINE struct ARRAY *ARRAY##_create(struct pqlcontext *pql) { \
	   struct ARRAY *a = domalloc(pql, sizeof(*a));		\
	   if (a == NULL) {					\
	      return NULL;					\
	   }							\
	   ARRAY##_init(a);					\
	   return a;						\
	}							\
								\
	INLINE void ARRAY##_destroy(struct pqlcontext *pql, struct ARRAY *a) {\
	   ARRAY##_cleanup(pql, a);				\
	   dofree(pql, a, sizeof(*a));				\
	}							\
								\
	INLINE unsigned ARRAY##_num(const struct ARRAY *a) {	\
	   return array_num(&a->arr);				\
	}							\
								\
	INLINE T *ARRAY##_get(const struct ARRAY *a, unsigned index) { \
	   return (T *)array_get(&a->arr, index);		\
	}							\
								\
	INLINE void ARRAY##_set(struct ARRAY *a, unsigned index, T *val) { \
	   array_set(&a->arr, index, (void *)val);		\
	}							\
								\
	INLINE int ARRAY##_setsize(struct pqlcontext *pql, struct ARRAY *a, unsigned num) { \
	   return array_setsize(pql, &a->arr, num);			\
	}							\
								\
	INLINE int ARRAY##_add(struct pqlcontext *pql, struct ARRAY *a, T *val, unsigned *ret) { \
	   return array_add(pql, &a->arr, (void *)val, ret);		\
	}							\
								\
	INLINE int ARRAY##_insert(struct pqlcontext *pql, struct ARRAY *a, unsigned index) { \
	   return array_insert(pql, &a->arr, index);		\
	}							\
								\
	INLINE void ARRAY##_remove(struct ARRAY *a, unsigned index) { \
	   return array_remove(&a->arr, index);			\
	}

#define DECLARRAY(T) DECLARRAY_BYTYPE(T##array, struct T)
#define DEFARRAY(T, INLINE) DEFARRAY_BYTYPE(T##array, struct T, INLINE)

////////////////////////////////////////////////////////////
// basic array types

DECLARRAY_BYTYPE(stringarray, char);
DEFARRAY_BYTYPE(stringarray, char, ARRAYINLINE);

#endif /* ARRAY_H */
