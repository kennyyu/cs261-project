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

#ifndef DATATYPE_H
#define DATATYPE_H

#include <stdbool.h>
#include "array.h"

struct pqlcontext;


struct datatypemanager; /* Opaque. */
struct datatype; /* Opaque. */

#ifndef DATATYPE_INLINE
#define DATATYPE_INLINE INLINE
#endif
DECLARRAY(datatype);
DEFARRAY(datatype, DATATYPE_INLINE);


/* Manager. */
struct datatypemanager *datatypemanager_create(struct pqlcontext *);
void datatypemanager_destroy(struct datatypemanager *);

/*
 * Constructors.
 */

/* Concrete atom types. */
struct datatype *datatype_bool(struct pqlcontext *);
struct datatype *datatype_int(struct pqlcontext *);
struct datatype *datatype_double(struct pqlcontext *);
struct datatype *datatype_string(struct pqlcontext *);

/* Concrete compound types. */
struct datatype *datatype_struct(struct pqlcontext *);
struct datatype *datatype_pathelement(struct pqlcontext *);

/* Distinguisher. */
struct datatype *datatype_distinguisher(struct pqlcontext *);

/* Tuple type. */
struct datatype *datatype_unit(struct pqlcontext *);
struct datatype *datatype_tuple_pair(struct pqlcontext *,
				     struct datatype *t0,
				     struct datatype *t1);
struct datatype *datatype_tuple_triple(struct pqlcontext *,
				       struct datatype *t0,
				       struct datatype *t1,
				       struct datatype *t2);
struct datatype *datatype_tuple_specific(struct pqlcontext *,
					 const struct datatypearray *);
//					 unsigned arity,
//					 struct datatype *members[]);
struct datatype *datatype_tuple_append(struct pqlcontext *,
				       struct datatype *t0,
				       struct datatype *t1);
struct datatype *datatype_tuple_concat(struct pqlcontext *,
				       struct datatype *t0,
				       struct datatype *t1);
struct datatype *datatype_tuple_strip(struct pqlcontext *pql,
				      const struct datatype *, unsigned ix);
struct datatype *datatype_tupleset_strip(struct pqlcontext *pql,
					 const struct datatype *, unsigned ix);

/* Function type. */
struct datatype *datatype_lambda(struct pqlcontext *,
				 struct datatype *arg, struct datatype *res);

/* Collection types. */
struct datatype *datatype_set(struct pqlcontext *, struct datatype *t);
struct datatype *datatype_sequence(struct pqlcontext *, struct datatype *t);


/* Abstract types. */

// XXX rename uses of these to insert "abs"
struct datatype *datatype_dbobj(struct pqlcontext *);
struct datatype *datatype_dbedge(struct pqlcontext *);

struct datatype *datatype_absdbedge(struct pqlcontext *);
struct datatype *datatype_absnumber(struct pqlcontext *);
struct datatype *datatype_absatom(struct pqlcontext *);
struct datatype *datatype_absdbobj(struct pqlcontext *);
struct datatype *datatype_abstop(struct pqlcontext *);
struct datatype *datatype_absbottom(struct pqlcontext *);

/*
 * Tests for exact types.
 */

bool datatype_eq(const struct datatype *, const struct datatype *);

bool datatype_isbool(const struct datatype *);
bool datatype_isint(const struct datatype *);
bool datatype_isdouble(const struct datatype *);
bool datatype_isstring(const struct datatype *);
bool datatype_isstruct(const struct datatype *);
bool datatype_ispathelement(const struct datatype *);
bool datatype_isdistinguisher(const struct datatype *);
bool datatype_istuple(const struct datatype *);
bool datatype_islambda(const struct datatype *);
bool datatype_isset(const struct datatype *);
bool datatype_issequence(const struct datatype *);

// XXX rename uses of these to insert "abs"
bool datatype_isdbobj(const struct datatype *);
bool datatype_isdbedge(const struct datatype *);

bool datatype_isabsdbedge(const struct datatype *);
bool datatype_isabsnumber(const struct datatype *);
bool datatype_isabsatom(const struct datatype *);
bool datatype_isabsdbobj(const struct datatype *);
bool datatype_isabstop(const struct datatype *);
bool datatype_isabsbottom(const struct datatype *);

/*
 * Tests for type classes.
 */

bool datatype_isanydbedge(const struct datatype *);
bool datatype_isanynumber(const struct datatype *);
bool datatype_isanyatom(const struct datatype *);
bool datatype_isanydbobj(const struct datatype *);
//bool datatype_isanyany(const struct datatype *); -- pointless, use "true" :-)

/*
 * Matching.
 */

struct datatype *datatype_match_generalize(struct pqlcontext *pql,
					   struct datatype *t1,
					   struct datatype *t2);
struct datatype *datatype_match_specialize(struct pqlcontext *pql,
					   struct datatype *t1,
					   struct datatype *t2);

/*
 * Inspection functions.
 *
 * Note that all types are treated as tuples -- those that aren't
 * really tuples function as tuples of arity 1. This makes
 * datatype_arity and datatype_getnth work on everything and removes a
 * pile of special cases.
 */

/* polymorphic */
const char *datatype_getname(const struct datatype *);
unsigned datatype_arity(const struct datatype *);
struct datatype *datatype_getnth(/*const*/ struct datatype *, unsigned ix);
unsigned datatype_nonset_arity(const struct datatype *);

/* lambda */
struct datatype *datatype_lambda_argument(const struct datatype *);
struct datatype *datatype_lambda_result(const struct datatype *);

/* set/sequence */
struct datatype *datatype_set_member(const struct datatype *);
struct datatype *datatype_sequence_member(const struct datatype *);


#endif /* DATATYPE_H */
