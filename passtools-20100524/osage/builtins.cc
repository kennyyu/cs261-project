/*
 * Copyright 2007
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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "utils.h"
#include "dump.h"
#include "ast.h"
#include "value.h"
#include "builtins.h"
#include "main.h"

struct builtin {
   const char *name;
   value *(*func)(value *args);
};

////////////////////////////////////////////////////////////

static int alldigits(const char *s) {
   return (strspn(s, "0123456789") == strlen(s));
}

////////////////////////////////////////////////////////////

static value *fn_first(value *args) {
   if (args->type != VT_TUPLE) {
      whine(nowhere, "Wrong type argument to first()");
      return NULL;
   }
   assert(args->tupleval.nvals > 0);
   return value_clone(args->tupleval.vals[0]);
}

static value *fn_second(value *args) {
   if (args->type != VT_TUPLE) {
      whine(nowhere, "Wrong type argument to second()");
      return NULL;
   }
   assert(args->tupleval.nvals > 1);
   return value_clone(args->tupleval.vals[1]);
}

static value *fn_third(value *args) {
   if (args->type != VT_TUPLE) {
      whine(nowhere, "Wrong type argument to third()");
      return NULL;
   }
   if (args->tupleval.nvals < 3) {
      whine(nowhere, "Tuple not wide enough in third()");
      return NULL;
   }
   return value_clone(args->tupleval.vals[2]);
}

static value *fn_filter(value *args) {
   if (args->type != VT_TUPLE || args->tupleval.nvals != 2 ||
       args->tupleval.vals[0]->type != VT_LAMBDA ||
       args->tupleval.vals[1]->type != VT_LIST) {
      whine(nowhere, "Wrong type arguments to filter()");
      return NULL;
   }

   expr *pred = args->tupleval.vals[0]->lambdaval.e;
   valuelist *guys = args->tupleval.vals[1]->listval;

   var *ivar = var_create();
   expr *e = expr_op(OP_FUNC, pred, expr_ref(ivar));

   value *output = value_list();
   for (int i=0; i<guys->members.num(); i++) {
      value *guy = guys->members[i];

      ivar->val = guy;
      value *result = eval(e);
      ivar->val = NULL;
      if (value_istrue(result)) {
	 valuelist_add(output->listval, guy);
      }
      value_destroy(result);
   }

   e->op.left = NULL;
   expr_destroy(e);
   var_destroy(ivar);

   return output;
}

static value *fn_ctime(value *args) {
   time_t timer;
   if (args->type == VT_INT) {
      timer = args->intval;
   }
   else if (args->type == VT_STRING && alldigits(args->strval)) {
      timer = atoll(args->strval);
   }
   else {
      whine(nowhere, "Wrong type argument to ctime()");
      return NULL;
   }

   struct tm *t = localtime(&timer);
   char tmp[128];
   strftime(tmp, sizeof(tmp), "%c", t);
   return value_str(tmp);
}

////////////////////////////////////////////////////////////

static const builtin builtins[] = {
   // tuple ops
   { "first",    fn_first },
   { "second",   fn_second },
   { "third",    fn_third },

   // list ops
   { "filter",   fn_filter },

   // time ops
   { "ctime",    fn_ctime },
};
static unsigned numbuiltins = sizeof(builtins) / sizeof(builtins[0]);

////////////////////////////////////////////////////////////

const builtin *builtin_lookup(const char *name) {
   for (unsigned i=0; i<numbuiltins; i++) {
      if (!strcmp(builtins[i].name, name)) {
	 return &builtins[i];
      }
   }
   return NULL;
}

value *builtin_exec(const builtin *b, value *args) {
   return b->func(args);
}

int builtin_cmp(const builtin *a, const builtin *b) {
   return strcmp(a->name, b->name);
}

const char *builtin_name(const builtin *b) {
   return b->name;
}

void builtin_dump(const builtin *b) {
   dump("builtin %s", b->name);
}
