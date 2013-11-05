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

#include <stdio.h>
#include <ctype.h> // XXX shouldn't use
#include "ast.h"
#include "value.h"
#include "builtins.h"
#include "dbops.h"
#include "main.h"

static int g_long_pnodes = 1;

////////////////////////////////////////////////////////////

static expr *make_eval_all_code(void) {
   // for x = ** in x, without optimizing it
   var *x = var_create();
   expr *e = expr_for(x, expr_val(value_all()), NULL, expr_ref(x));
   return e;
}

static value *eval_all(void) {
   expr *e = make_eval_all_code();
   value *v = eval(e);
   expr_destroy(e);
   return v;
}

////////////////////////////////////////////////////////////

static void printfullvalue(value *v, int indent);
static void printonevalue(value *v, int indent);

static void doindent(int level) {
   int len = level*3;
   printf("%*s", len, "");
}

static void printtype(value *v) {
   if (!v) {
      printf("null");
      return;
   }
   switch (v->type) {
    case VT_INT:     printf("int"); break;
    case VT_FLOAT:   printf("float"); break;
    case VT_INDEX:   printf("database-index"); break;
    case VT_BUILTIN: printf("builtin"); break;
    case VT_STRING:  printf("string"); break;
    case VT_PNODE:   printf("object"); break;
    case VT_RANGE:   printf("range of int"); break;
    case VT_ALL:     printf("list of object"); break;
    case VT_LAMBDA:  printf("lambda"); break;
    case VT_TUPLE:   
     printf("tuple of");
     for (int i=0; i<v->tupleval.nvals; i++) {
	printf(" ");
	printtype(v->tupleval.vals[i]);
     }
     break;
    case VT_LIST:
     if (v->listval->members.num() == 0) {
	printf("list of nothing");
     }
     else {
	printf("list of %d*", v->listval->members.num());
	printtype(v->listval->members[0]);
     }
     break;
   }
}

static void printindex(whichindex ix) {
   switch (ix) {
    case INDEX_ID: printf("<id>"); break;
    case INDEX_I2P: printf("<i2p>"); break;
    case INDEX_NAME: printf("<name>"); break;
    case INDEX_ARGV: printf("<argv>"); break;
    default: assert(0); break;
   }
}

static void printstring(value *v) {
   assert(v->type == VT_STRING);
   const char *s = v->strval;
   putchar('"');
   for (int i=0; s[i]; i++) {
      unsigned char ch = s[i];
      if (isprint(ch) && !isspace(ch) && ch != '"' && ch != '\\') {
	 putchar(ch);
      }
      else {
	 printf("\\%03o", ch);
      }
   }
   putchar('"');
}

static void printrange(value *v) {
   assert(v->type == VT_RANGE);
   printf("%ld to %ld", v->rangeval.left, v->rangeval.right);
}

static void printpnode(value *v, int indent) {
   if (!g_long_pnodes) {
      printf("pnode-%llu", v->pnodeval);
      return;
   }

   doindent(indent);
   printf("pnode %llu:\n", v->pnodeval);

   value *w = db_get_allattr(v->pnodeval);
   assert(w->type == VT_LIST);
   for (int i=0; i<w->listval->members.num(); i++) {
      value *x = w->listval->members[i];
      assert(x->type == VT_TUPLE);
      assert(x->tupleval.nvals == 2);
      value *a = x->tupleval.vals[0];
      value *b = x->tupleval.vals[1];
      assert(a->type == VT_STRING);
      doindent(indent+1);
      printf("%s: ", a->strval);
      printonevalue(b, indent+2);
      printf("\n");
   }
}

static void printtuple(value *v, int indent) {
   doindent(indent);
   printf("(");
   for (int i=0; i<v->tupleval.nvals; i++) {
      printf(" ");
      printonevalue(v->tupleval.vals[i], indent+1);
   }
   printf(" )\n");
}

static void printlist(value *v, int indent) {
   value *sorted = value_list();
   for (int i=0; i<v->listval->members.num(); i++) {
      valuelist_add(sorted->listval, v->listval->members[i]);
   }

   valuelist_sort(sorted->listval);

   for (int i=0; i<sorted->listval->members.num(); i++) {

      // XXX this should NOT be here - if people want unique output
      // they should use the not-yet-existing uniquify builtin.

      if (i>0 && 
	  value_eq(sorted->listval->members[i], 
		   sorted->listval->members[i-1])) {
	 continue;
      }
      printfullvalue(sorted->listval->members[i], indent);
   }

   // since we didn't clone the values, take them out before deleting
   sorted->listval->members.setsize(0);
   value_destroy(sorted);
}

//////////////////////////////

static void printonevalue(value *v, int indent) {
   if (!v) {
      printf("[NULL]");
      return;
   }
   switch (v->type) {
    case VT_INT:     printf("%ld", v->intval); break;
    case VT_FLOAT:   printf("%g", v->floatval); break;
    case VT_STRING:  printstring(v); break;
    case VT_RANGE:   printrange(v); break;
    case VT_INDEX:   printindex(v->indexval); break;
    case VT_BUILTIN: printf("%s", builtin_name(v->builtinval)); break;

    case VT_PNODE:
     if (!g_long_pnodes) {
	printpnode(v, indent);
	break;
     }
     /* FALLTHROUGH */
    case VT_TUPLE:
    case VT_LIST:
    case VT_LAMBDA:
     printfullvalue(v, indent+1);
     break;

    case VT_ALL:
     assert(0);
     break;
   }
}

static void printfullvalue(value *v, int indent) {
   if (!v) {
      printf("[NULL]\n");
      return;
   }
   switch (v->type) {
    case VT_INT:
    case VT_FLOAT:
    case VT_STRING:
    case VT_RANGE:
    case VT_INDEX:
    case VT_BUILTIN:
     doindent(indent);
     printonevalue(v, indent+1);
     printf("\n");
     return;

    case VT_PNODE:
     printpnode(v, indent);
     if (!g_long_pnodes) {
	printf("\n");
     }
     return;
   
    case VT_TUPLE:
     printtuple(v, indent);
     return;

    case VT_LIST:
     printlist(v, indent);
     return;

    case VT_ALL:
     {
	value *v1 = eval_all();
	printlist(v1, indent);
	value_destroy(v1);
     }
     return;

    case VT_LAMBDA:
     ast_dump(v->lambdaval.e);
     return;

    default:
     assert(0);
     break;
   }
   return;
}

void output_result(value *v) {
   if (!v) {
      printf("NOTHING\n");
      return;
   }

   printf("RESULT: ");
   printtype(v);
   printf("\n");

   printfullvalue(v, 0);
}
