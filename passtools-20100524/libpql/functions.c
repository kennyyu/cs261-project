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

#include <string.h>

#include "utils.h"
#include "functions.h"

/*
 * XXX this currently needs to be synchronized with ptlex.c
 */
enum functions function_getbyname(const char *name, size_t namelen) {
   if (namelen == 5 && !memcmp("ctime", name, namelen)) {
      return F_CTIME;
   }
   if (namelen == 8 && !memcmp("tostring", name, namelen)) {
      return F_TOSTRING;
   }
   PQLASSERT(0);
   return F_ADD;
}

/*
 * XXX this should have a table to drive it
 */
const char *function_getname(enum functions f) {
   switch (f) {
    case F_UNION: return "union";
    case F_INTERSECT: return "intersect";
    case F_EXCEPT: return "except";
    case F_UNIONALL: return "unionall";
    case F_INTERSECTALL: return "intersectall";
    case F_EXCEPTALL: return "exceptall";
    case F_IN: return "in";
    case F_NONEMPTY: return "nonempty";
    case F_MAKESET: return "set";
    case F_GETELEMENT: return "element";
    case F_COUNT: return "count";
    case F_SUM: return "sum";
    case F_AVG: return "avg";
    case F_MIN: return "min";
    case F_MAX: return "max";
    case F_ALLTRUE: return "alltrue";
    case F_ANYTRUE: return "anytrue";
    case F_AND: return "and";
    case F_OR: return "or";
    case F_NOT: return "not";
    case F_NEW: return "new";
    case F_CTIME: return "ctime";
    case F_EQ: return "=";
    case F_NOTEQ: return "<>";
    case F_LT: return "<";
    case F_GT: return ">";
    case F_LTEQ: return "<=";
    case F_GTEQ: return ">=";
    case F_LIKE: return "like";
    case F_GLOB: return "glob";
    case F_GREP: return "grep";
    case F_SOUNDEX: return "soundex";
    case F_TOSTRING: return "tostring";
    case F_CONCAT: return "++";
    case F_CHOOSE: return "choose";
    case F_ADD: return "+";
    case F_SUB: return "-";
    case F_MUL: return "*";
    case F_DIV: return "/";
    case F_MOD: return "mod";
    case F_NEG: return "neg";
    case F_ABS: return "abs";
   }
   PQLASSERT(0);
   return NULL;
}

bool function_commutes(enum functions f) {
   switch (f) {
    case F_UNION:
    case F_INTERSECT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_AND:
    case F_OR:
    case F_EQ:
    case F_NOTEQ:
    case F_CHOOSE:
    case F_ADD:
    case F_MUL:
     return true;

    case F_EXCEPT:
    case F_EXCEPTALL:
    case F_IN:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
    case F_CONCAT:
    case F_SUB:
    case F_DIV:
    case F_MOD:
     return false;

    case F_NONEMPTY:
    case F_MAKESET:
    case F_GETELEMENT:
    case F_COUNT:
    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
    case F_ALLTRUE:
    case F_ANYTRUE:
    case F_NOT:
    case F_NEW:
    case F_CTIME:
    case F_TOSTRING:
    case F_NEG:
    case F_ABS:
     /* not binary; question doesn't make sense */
     break;
   }
   PQLASSERT(0);
   return NULL;
}
