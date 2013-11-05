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

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stddef.h>
#include <stdbool.h>

struct pqlvalue;


enum functions {
   /* set */
   F_UNION,
   F_INTERSECT,
   F_EXCEPT,
   F_UNIONALL,
   F_INTERSECTALL,
   F_EXCEPTALL,
   F_IN,
   F_NONEMPTY,
   F_MAKESET,
   F_GETELEMENT,

   /* aggregator */
   F_COUNT,
   F_SUM,
   F_AVG,
   F_MIN,
   F_MAX,
   F_ALLTRUE,
   F_ANYTRUE,

   /* boolean */
   F_AND,
   F_OR,
   F_NOT,

   /* object */
   F_NEW,

   /* time */
   F_CTIME,

   /* comparison */
   F_EQ,
   F_NOTEQ,
   F_LT,
   F_GT,
   F_LTEQ,
   F_GTEQ,
   F_LIKE,
   F_GLOB,
   F_GREP,
   F_SOUNDEX,

   /* string */
   F_TOSTRING,

   /* string and sequence */
   F_CONCAT,

   /* nil */
   F_CHOOSE,

   /* numeric */
   F_ADD,
   F_SUB,
   F_MUL,
   F_DIV,
   F_MOD,
   F_NEG,
   F_ABS,
};


enum functions function_getbyname(const char *name, size_t namelen);

const char *function_getname(enum functions f);

bool function_commutes(enum functions f);


#endif /* FUNCTIONS_H */
