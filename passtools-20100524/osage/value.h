/*
 * Copyright 2006, 2007
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

#ifndef VALUE_H
#define VALUE_H

#include <stdint.h>
#include "ptrarray.h"

struct expr;
struct builtin;


enum whichindex {
   INDEX_ID,
   INDEX_I2P,
   INDEX_NAME,
   INDEX_ARGV,
};

enum valuetypes {
   VT_INT,		/* immediate integer */
   VT_FLOAT,		/* immediate floating point */
   VT_STRING,		/* string */
   VT_PNODE,		/* pnode number */
   VT_RANGE,		/* range of integers */
   VT_TUPLE,		/* fixed-size polymorphic collection */
   VT_LIST,		/* variable-sized monomorphic collection */
   VT_ALL,		/* all objects in database */
   VT_INDEX,		/* identity of an index */
   VT_LAMBDA,		/* code */
   VT_BUILTIN,		/* builtin function */
};

struct value;

struct valuelist {
   valuetypes membertype;
   ptrarray<value> members;
};

struct value {
   valuetypes type;
   union {
      long intval;
      struct {
	 long left;
	 long right;
      } rangeval;
      double floatval;
      char *strval;
      uint64_t pnodeval;
      struct {
	 value **vals;
	 int nvals;
      } tupleval;
      valuelist *listval;
      whichindex indexval;
      struct {
	 int id;
	 expr *e;
      } lambdaval;
      const builtin *builtinval;
   };
};

value *value_int(long v);
value *value_float(double v);
value *value_str(const char *s);
value *value_str_bylen(const char *s, size_t len);
value *value_str_two(const char *s, const char *t);
value *value_pnode(uint64_t pnode);
value *value_range(long a, long b);
value *value_tuple(value **vals, int nvals);  // consumes vals!
value *value_all(void);
value *value_index(whichindex ix);
value *value_lambda(int id, expr *e);
value *value_builtin(const builtin *b);
value *value_list(void);

int value_istrue(value *v);
int value_eq(value *v, value *w);

value *value_clone(value *v);
void value_destroy(value *v);

void valuelist_add(valuelist *l, value *v);
void valuelist_sort(valuelist *l);
void valuelist_uniq(valuelist *l);
int valuelist_compare(const value *a, const value *b);

void value_dump(value *v);

const char *indexstr(whichindex ix);


#endif /* VALUE_H */
