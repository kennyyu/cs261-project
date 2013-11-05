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
 * value functions not published in pql.h
 */

#ifndef PQLVALUE_H
#define PQLVALUE_H

#include "array.h"
#include "pql.h"

struct pqlcontext;
struct pqlvalue;

#ifndef VALUE_INLINE
#define VALUE_INLINE INLINE
#endif
DECLARRAY(pqlvalue);
DEFARRAY(pqlvalue, VALUE_INLINE);

/*
 * More value constructors, that are not exposed to the user for one
 * reason or another.
 */
struct pqlvalue *pqlvalue_string_consume(struct pqlcontext *pql, char *str);
struct pqlvalue *pqlvalue_distinguisher(struct pqlcontext *pql, unsigned id);
struct pqlvalue *pqlvalue_unit(struct pqlcontext *pql);
struct pqlvalue *pqlvalue_triple(struct pqlcontext *pql,
				 struct pqlvalue *v1,
				 struct pqlvalue *v2,
				 struct pqlvalue *v3);
struct pqlvalue *pqlvalue_paste(struct pqlcontext *pql,
				const struct pqlvalue *left,
				const struct pqlvalue *right);
struct pqlvalue *pqlvalue_tuple_specific(struct pqlcontext *pql,
					 struct pqlvaluearray *consumedvals);
struct pqlvalue *pqlvalue_dbnil(struct pqlcontext *pql);

/*
 * Type tests for types the user isn't supposed to see.
 */
bool pqlvalue_isdistinguisher(const struct pqlvalue *val);
bool pqlvalue_islambda(const struct pqlvalue *val);
struct datatype *pqlvalue_datatype(const struct pqlvalue *val);

/*
 * Operations on specific types that we don't allow to the user.
 */

/* Add an element to the end of a tuple (consumes VALUE) */
struct pqlvalue *pqlvalue_tuple_add(struct pqlvalue *tuple,
				    struct pqlvalue *value);

/* Remove a column from a tuple */
struct pqlvalue *pqlvalue_tuple_strip(struct pqlvalue *tuple, unsigned index);

/* Replace the specified element of a tuple and return the old value */
struct pqlvalue *pqlvalue_tuple_replace(struct pqlvalue *tuple, unsigned index,
					struct pqlvalue *newval);

/* like get, but returns a non-const value */
struct pqlvalue *pqlvalue_set_getformodify(struct pqlvalue *set,
					   unsigned index);
/* replace value at given index, return old value */
struct pqlvalue *pqlvalue_set_replace(struct pqlvalue *set, unsigned index,
				      struct pqlvalue *newval,
				      bool forcetype);

/* drop an entry from the set */
void pqlvalue_set_drop(struct pqlvalue *set, unsigned index);

/* duplicate each element counts[i] times, except leave COL null in new ones */
void pqlvalue_set_pryopen(struct pqlcontext *pql,
			  struct pqlvalue *set,
			  unsigned col, unsigned *counts);


struct pqlvalue *pqlvalue_set_to_sequence(struct pqlcontext *pql,
					  struct pqlvalue *set);
struct pqlvalue *pqlvalue_sequence_to_set(struct pqlcontext *pql,
					  struct pqlvalue *sequence);

void pqlvalue_sequence_drop(struct pqlvalue *set, unsigned ix);

/* polymorphic over sets and sequences */
unsigned pqlvalue_coll_getnum(const struct pqlvalue *);
const struct pqlvalue *pqlvalue_coll_get(const struct pqlvalue *, unsigned n);
void pqlvalue_coll_drop(struct pqlvalue *set, unsigned index);



/*
 * Other functions.
 */

/* XXX */
void pqlvalue_set_updatetype(struct pqlvalue *val, struct datatype *t);


int pqlvalue_compare(const struct pqlvalue *a, const struct pqlvalue *b);
struct pqlvalue *pqlvalue_tostring(struct pqlcontext *pql,
				   const struct pqlvalue *val);
struct layout *pqlvalue_layout(struct pqlcontext *pql,
			       const struct pqlvalue *val);

/*
 * These currently live in eval.c (probably should be moved)
 */

int convert_to_bool(const struct pqlvalue *val, bool *ret);
int convert_to_number(const struct pqlvalue *val,
		      int *ret_int, double *ret_float,
		      bool *ret_isfloat);

/*
 * these are not supposed to be exposed outside pqlvalue.c
 */
void pqlvalue_string_padonleft(struct pqlvalue *val, unsigned width);
struct pqlvalue *pqlvalue_string_fromlist(struct pqlcontext *pql,
					  struct pqlvaluearray *strings,
					  const char *leftdelim,
					  const char *separator,
					  const char *rightdelim);
void pqlvaluearray_destroymembers(struct pqlcontext *pql,
				  struct pqlvaluearray *arr);

#endif /* PQLVALUE_H */
