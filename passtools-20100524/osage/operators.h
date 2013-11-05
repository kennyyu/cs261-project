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

#ifndef OPERATORS_H
#define OPERATORS_H

// operators
enum ops {
   // reserved
   OP_NOP,

   // procedural
   OP_SORT,		// sort(x)          => sortby(compare(x))
   OP_REVSORT,		// sort(revsort(x)) => sortby(-compare(x))
   OP_LOOKUP,
   OP_FUNC,
   OP_FIELD,

   // structural
   OP_PATH,
   OP_LONGPATHZ,
   OP_LONGPATHNZ,

   // set
   OP_UNION,
   OP_INTERSECT,

   // list
   OP_CONS,

   // logical
   OP_LOGAND,
   OP_LOGOR,

   // comparison
   OP_EQ,
   OP_NE,
   OP_MATCH,
   OP_NOMATCH,
   OP_LT,
   OP_GT,
   OP_LE,
   OP_GE,
   OP_CONTAINS,

   // arithmetic
   OP_ADD,
   OP_SUB,
   OP_MUL,
   OP_DIV,
   OP_MOD,
   OP_STRCAT,

   // unary logical
   OP_LOGNOT,

   // unary arithmetic
   OP_NEG,

   // unary path element qualifiers
   OP_OPTIONAL,
   OP_REPEAT,
   OP_EXTRACT,
};

const char *opstr(ops op);
bool isunaryop(ops op);
bool islogicalop(ops op);

ops op_logical_flip(ops op);
ops op_leftright_flip(ops op);

#endif /* OPERATORS_H */
