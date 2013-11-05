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

#include <string.h>
#include <assert.h>
#include "operators.h"

const char *opstr(ops op) {
   switch (op) {
    case OP_NOP: return "<nop>";
    case OP_PATH: return "->";
    case OP_LONGPATHZ: return "->*";
    case OP_LONGPATHNZ: return "->+";
    case OP_UNION: return "union";
    case OP_INTERSECT: return "intersect";
    case OP_LOGAND: return "&&";
    case OP_LOGOR: return "||";
#if 0
    case OP_LOGXOR: return "^^";
#endif
    case OP_CONS: return ":";
    case OP_EQ: return "==";
    case OP_NE: return "!=";
    case OP_MATCH: return "~";
    case OP_NOMATCH: return "!~";
    case OP_LT: return "<";
    case OP_GT: return ">";
    case OP_LE: return "<=";
    case OP_GE: return ">=";
#if 0
    case OP_SUBSET: return "<<=";
    case OP_SUPERSET: return ">>=";
    case OP_PROPERSUBSET: return "<<";
    case OP_PROPERSUPERSET: return ">>";
#endif
    case OP_CONTAINS: return "contains";
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_STRCAT: return "++";
    case OP_LOGNOT: return "!";
    case OP_NEG: return "-";
    case OP_OPTIONAL: return "?";
    case OP_REPEAT: return "...";
    case OP_EXTRACT: return "->";

    case OP_SORT: return "<sort>";
    case OP_REVSORT: return "<revsort>";
    case OP_LOOKUP: return "<lookup>";
    case OP_FUNC: return "<func>";
    case OP_FIELD: return "@";
   }
   assert(0);
   return NULL;
}

bool islogicalop(ops op) {
   switch (op) {
    case OP_LOGAND:
    case OP_LOGOR:
#if 0
    case OP_LOGXOR:
#endif
    case OP_LOGNOT:
     return true;
    default:
     break;
   }
   return false;
}

bool isunaryop(ops op) {
   switch (op) {
    case OP_LOGNOT:
    case OP_NEG:
     return true;
    default:
     break;
   }
   return false;
}

ops op_logical_flip(ops op) {
   switch (op) {
    case OP_EQ: return OP_NE;
    case OP_NE: return OP_EQ;
    case OP_MATCH: return OP_NOMATCH;
    case OP_NOMATCH: return OP_MATCH;
    case OP_LT: return OP_GE;
    case OP_GT: return OP_LE;
    case OP_LE: return OP_GT;
    case OP_GE: return OP_LT;
#if 0
    case OP_SUBSET: return OP_PROPERSUPERSET;
    case OP_SUPERSET: return OP_PROPERSUBSET;
    case OP_PROPERSUBSET: return OP_SUPERSET;
    case OP_PROPERSUPERSET: return OP_SUBSET;
#endif
    default:
     break;
   }
   // invalid/undefined
   return OP_NOP;
}

ops op_leftright_flip(ops op) {
   switch (op) {
    case OP_UNION:
    case OP_LOGAND:
    case OP_LOGOR:
#if 0
    case OP_LOGXOR:
#endif
    case OP_EQ:
    case OP_NE:
    case OP_ADD:
    case OP_MUL:
     return op;
    case OP_LT: return OP_GT;
    case OP_GT: return OP_LT;
    case OP_LE: return OP_GE;
    case OP_GE: return OP_LE;
#if 0
    case OP_SUBSET: return OP_SUPERSET;
    case OP_SUPERSET: return OP_SUBSET;
    case OP_PROPERSUBSET: return OP_PROPERSUPERSET;
    case OP_PROPERSUPERSET: return OP_PROPERSUBSET;
#endif
    default:
     break;
   }
   // invalid/undefined
   return OP_NOP;
}
