/*
 * Copyright 2008
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

#ifndef PQL_H
#define PQL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pqlcontext;
struct pqlquery;
struct pqlvalue;

typedef uint64_t pqloid_t;
typedef uint64_t pqlsubid_t;

#define PQLOID_INVALID ((pqloid_t)-1)

struct pqlbackend_ops {
   struct pqlvalue *(*read_global)(struct pqlcontext *pql, const char *name);
   struct pqlvalue *(*newobject)(struct pqlcontext *pql);
   int              (*assign)(struct pqlcontext *pql,
			      struct pqlvalue *obj,
			      const struct pqlvalue *edge,
			      const struct pqlvalue *val);
   struct pqlvalue *(*follow)(struct pqlcontext *pql,
			      const struct pqlvalue *obj,
			      const struct pqlvalue *edge, bool reversed);
   struct pqlvalue *(*followall)(struct pqlcontext *pql,
				 const struct pqlvalue *obj, bool reversed);
};

// assertions

typedef void (*pqlassertionhandler)
	(const char *expr, const char *file, int line, const char *func);
void pql_set_assertion_handler(pqlassertionhandler newhandler);

// contexts
// pqlcontext_destroy returns the amount of memory leaked
// (should of course be zero but might not be)

struct pqlcontext *pqlcontext_create(const struct pqlbackend_ops *);
size_t pqlcontext_destroy(struct pqlcontext *);

size_t pqlcontext_getmemorypeak(struct pqlcontext *);

void pqlcontext_dodumps(struct pqlcontext *, bool onoff);
void pqlcontext_dotrace(struct pqlcontext *, bool onoff);

unsigned pqlcontext_getnumdumps(struct pqlcontext *);
const char *pqlcontext_getdumpname(struct pqlcontext *, unsigned which);
const char *pqlcontext_getdumptext(struct pqlcontext *, unsigned which);
const char *pqlcontext_getdumpbyname(struct pqlcontext *, const char *name);
void pqlcontext_cleardumps(struct pqlcontext *pql);

unsigned pqlcontext_getnumtracelines(struct pqlcontext *pql);
const char *pqlcontext_gettraceline(struct pqlcontext *pql, unsigned num);
void pqlcontext_cleartrace(struct pqlcontext *pql);

unsigned pqlcontext_getnumerrors(struct pqlcontext *);
const char *pqlcontext_geterror(struct pqlcontext *, unsigned which);
void pqlcontext_clearerrors(struct pqlcontext *);

// queries

struct pqlquery *pqlquery_compile(struct pqlcontext *pql,
				  const char *textbuf, size_t len);
void pqlquery_destroy(struct pqlquery *);

struct pqlvalue *pqlquery_run(struct pqlcontext *pql, const struct pqlquery *);

// values: creation/destruction

struct pqlvalue *pqlvalue_nil(struct pqlcontext *);
struct pqlvalue *pqlvalue_bool(struct pqlcontext *, bool x);
struct pqlvalue *pqlvalue_int(struct pqlcontext *, int x);
struct pqlvalue *pqlvalue_float(struct pqlcontext *, double x);
struct pqlvalue *pqlvalue_string(struct pqlcontext *, const char *);
struct pqlvalue *pqlvalue_string_bylen(struct pqlcontext *,
				       const char *, size_t);
struct pqlvalue *pqlvalue_struct(struct pqlcontext *, int dbnum, pqloid_t,
				 pqlsubid_t);

// consumes A and B
struct pqlvalue *pqlvalue_pair(struct pqlcontext *,
			       struct pqlvalue *a, struct pqlvalue *b);
struct pqlvalue *pqlvalue_emptyset(struct pqlcontext *);

struct pqlvalue *pqlvalue_clone(struct pqlcontext *, const struct pqlvalue *);

void pqlvalue_destroy(struct pqlvalue *);

// The following constructors are provided for completeness and for
// use by the pqlpickle code. Storage backends should not create these
// or return them to the engine; doing so may cause the engine to
// assert or misbehave.
struct pqlvalue *pqlvalue_emptysequence(struct pqlcontext *pql);
struct pqlvalue *pqlvalue_pathelement(struct pqlcontext *pql,
				      struct pqlvalue *leftobj,
				      struct pqlvalue *edgename,
				      struct pqlvalue *rightobj);
struct pqlvalue *pqlvalue_tuple_begin(struct pqlcontext *pql, unsigned arity);
// consumes val
void pqlvalue_tuple_assign(struct pqlcontext *pql, struct pqlvalue *tuple,
			   unsigned slot, struct pqlvalue *val);
void pqlvalue_tuple_end(struct pqlcontext *pql, struct pqlvalue *tuple);


// values: type tests
//
// All value types are here for completeness, including those the user
// won't normally see.

bool pqlvalue_isnil(const struct pqlvalue *);
bool pqlvalue_isbool(const struct pqlvalue *);
bool pqlvalue_isint(const struct pqlvalue *);
bool pqlvalue_isfloat(const struct pqlvalue *);
bool pqlvalue_isstring(const struct pqlvalue *);
bool pqlvalue_isstruct(const struct pqlvalue *);
bool pqlvalue_ispathelement(const struct pqlvalue *);
bool pqlvalue_istuple(const struct pqlvalue *);
bool pqlvalue_isset(const struct pqlvalue *);
bool pqlvalue_issequence(const struct pqlvalue *);

// values: per-type get operations
//
// All value types for which the results are externally representable
// are included here for completeness, including some the user won't
// normally see.
//
// The value structures returned when inspecting path elements,
// tuples, sets, and sequences are internal to the representation and
// should not be pqlvalue_destroy'd.

bool pqlvalue_bool_get(const struct pqlvalue *);
int pqlvalue_int_get(const struct pqlvalue *);
double pqlvalue_float_get(const struct pqlvalue *);
const char *pqlvalue_string_get(const struct pqlvalue *);
int pqlvalue_struct_getdbnum(const struct pqlvalue *);
pqloid_t pqlvalue_struct_getoid(const struct pqlvalue *);
pqlsubid_t pqlvalue_struct_getsubid(const struct pqlvalue *);
const struct pqlvalue *
   pqlvalue_pathelement_getleftobj(const struct pqlvalue *);
const struct pqlvalue *
   pqlvalue_pathelement_getedgename(const struct pqlvalue *);
const struct pqlvalue *
   pqlvalue_pathelement_getrightobj(const struct pqlvalue *);
unsigned pqlvalue_tuple_getarity(const struct pqlvalue *);
const struct pqlvalue *pqlvalue_tuple_get(const struct pqlvalue *, unsigned n);
unsigned pqlvalue_set_getnum(const struct pqlvalue *);
const struct pqlvalue *pqlvalue_set_get(const struct pqlvalue *, unsigned n);
unsigned pqlvalue_sequence_getnum(const struct pqlvalue *);
const struct pqlvalue *
   pqlvalue_sequence_get(const struct pqlvalue *, unsigned n);

// values: other per-type operations

void pqlvalue_set_add(struct pqlvalue *, struct pqlvalue *);
void pqlvalue_sequence_add(struct pqlvalue *, struct pqlvalue *);

// values: other operations
//
// eq provides the language's equality operator, where e.g. "1.0" = 1.
// identical checks strict equality: same type, same value in that type.
// print generates the canonical string representation.

bool pqlvalue_eq(const struct pqlvalue *, const struct pqlvalue *);
bool pqlvalue_identical(const struct pqlvalue *, const struct pqlvalue *);
void pqlvalue_print(char *buf, size_t maxlen, const struct pqlvalue *);

#ifdef __cplusplus
}
#endif

#endif /* PQL_H */
