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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "utils.h"
#include "dump.h"
#include "value.h"
#include "ast.h" // for expr_clone
#include "builtins.h"

static value *mkvalue(valuetypes type) {
   value *v = new value;
   v->type = type;
   return v;
}

value *value_int(long i) {
   value *v = mkvalue(VT_INT);
   v->intval = i;
   return v;
}

value *value_range(long a, long b) {
   value *v = mkvalue(VT_RANGE);
   v->rangeval.left = a;
   v->rangeval.right = b;
   return v;
}

value *value_float(double d) {
   value *v = mkvalue(VT_FLOAT);
   v->floatval = d;
   return v;
}

value *value_str(const char *s) {
   value *v = mkvalue(VT_STRING);
   v->strval = xstrdup(s);
   return v;
}

value *value_str_bylen(const char *s, size_t len) {
   value *v = mkvalue(VT_STRING);
   v->strval = new char[len+1];
   memcpy(v->strval, s, len);
   v->strval[len] = 0;
   return v;
}

value *value_str_two(const char *s, const char *t) {
   value *v = mkvalue(VT_STRING);
   v->strval = new char[strlen(s)+strlen(t)+1];
   strcpy(v->strval, s);
   strcat(v->strval, t);
   return v;
}

value *value_pnode(uint64_t pnode) {
   value *v = mkvalue(VT_PNODE);
   v->pnodeval = pnode;
   return v;
}

value *value_all(void) {
   return mkvalue(VT_ALL);
}

value *value_index(whichindex ix) {
   value *v = mkvalue(VT_INDEX);
   v->indexval = ix;
   return v;
}

value *value_lambda(int id, expr *e) {
   value *v = mkvalue(VT_LAMBDA);
   v->lambdaval.id = id;
   v->lambdaval.e = e;
   return v;
}

value *value_builtin(const builtin *b) {
   value *v = mkvalue(VT_BUILTIN);
   v->builtinval = b;
   return v;
}

value *value_list(void) {
   value *v = mkvalue(VT_LIST);
   v->listval = new valuelist;
   v->listval->membertype = VT_ALL;	// means "any" in this context
   return v;
}

void valuelist_add(valuelist *l, value *v) {
   if (l->membertype == VT_ALL) {
      assert(l->members.num()==0);
      l->membertype = v->type;
   }
   else {
      assert(l->members.num()>0);
      if (l->membertype != v->type) {
	 whine(nowhere, "Type mismatch adding value to list");
	 return;
      }
   }
   l->members.add(v);
}

#define CMP(sym, type) \
   int sym##cmp(type a, type b) { return a<b ? -1 : a>b ? 1 : 0; }
CMP(int, int);
CMP(float, double);
CMP(pnode, uint64_t);
CMP(index, whichindex);

// this does not have to define a meaningful order, just *an* order.
int valuelist_compare(const value *a, const value *b) {
   int r;

   if (!a && !b) {
      return 0;
   }
   if (!a) {
      return -1;
   }
   if (!b) {
      return 1;
   }
   if (a->type != b->type) {
      return a->type < b->type ? -1 : 1;
   }
   switch (a->type) {
    case VT_INT: return intcmp(a->intval, b->intval);
    case VT_FLOAT: return floatcmp(a->floatval, b->floatval);
    case VT_STRING: return strcmp(a->strval, b->strval);
    case VT_PNODE: return pnodecmp(a->pnodeval, b->pnodeval);
    case VT_RANGE: 
     r = intcmp(a->rangeval.left, b->rangeval.left);
     if (r == 0) {
	r = intcmp(a->rangeval.right, b->rangeval.right);
     }
     return r;
    case VT_ALL: return 0;
    case VT_INDEX: return indexcmp(a->indexval, b->indexval);
    case VT_TUPLE:
     r = intcmp(a->tupleval.nvals, b->tupleval.nvals);
     if (r == 0) {
	for (int i=0; i<a->tupleval.nvals; i++) {
	   r = valuelist_compare(a->tupleval.vals[i], b->tupleval.vals[i]);
	   if (r) {
	      break;
	   }
	}
     }
     return r;
    case VT_LIST:
     r = intcmp(a->listval->members.num(), b->listval->members.num());
     if (r == 0) {
	for (int i=0; i<a->listval->members.num(); i++) {
	   r = valuelist_compare(a->listval->members[i],
				 b->listval->members[i]);
	   if (r) {
	      break;
	   }
	}
     }
     return r;
    case VT_LAMBDA: return intcmp(a->lambdaval.id, b->lambdaval.id);
    case VT_BUILTIN: return builtin_cmp(a->builtinval, b->builtinval);
   }

   assert(0); // ?
   return 0;
}

static int valuelist_compare_thunk(const void *a, const void *b) {
   const value *aa = *(const value *const *) a;
   const value *bb = *(const value *const *) b;
   return valuelist_compare(aa, bb);
}

void valuelist_sort(valuelist *vl) {
   qsort(vl->members.getdata(), vl->members.num(), sizeof(vl->members[0]),
	 valuelist_compare_thunk);
}

void valuelist_uniq(valuelist *vl) {
   int i=0, j=0, n = vl->members.num();
   while (i < n) {
      assert(j <= i);
      if (i==0 || !value_eq(vl->members[i-1], vl->members[i])) {
	 if (j < i) {
	    vl->members[j] = vl->members[i];
	 }
	 j++;
      }
      i++;
   }
   vl->members.setsize(j);
}

value *value_tuple(value **vals, int nvals) {
   value *v = mkvalue(VT_TUPLE);
   v->tupleval.vals = vals;
   v->tupleval.nvals = nvals;
   return v;
}

int value_istrue(value *v) {
   switch (v->type) {
    case VT_INT: return v->intval != 0;
    case VT_RANGE: return 1;
    case VT_FLOAT: return v->floatval != 0;
    case VT_PNODE: return 1;
    case VT_STRING: return *v->strval != 0;
    case VT_ALL: return 1;
    case VT_INDEX: return 1;
    case VT_TUPLE: return 1;
    case VT_LAMBDA: return 1;
    case VT_BUILTIN: return 1;
    case VT_LIST:
     return v->listval->members.num() > 0;
   }

   assert(0); // ?
   return 0;
}

int value_eq(value *v, value *w) {
   if (!v && !w) {
      return 1;
   }
   if (!v || !w) {
      return 0;
   }

   /*
    * Not clear if this should really be here or if it should be put in
    * eval.cc so it's limited to explicit user-written comparisons...
    */
   if (v->type == VT_INT && w->type == VT_FLOAT) {
      return (double)v->intval == w->floatval;
   }
   if (v->type == VT_FLOAT && w->type == VT_INT) {
      return v->floatval == (double)w->intval;
   }

   if (v->type != w->type) {
      return 0;
   }
   switch (v->type) {
    case VT_INT:
     return v->intval == w->intval;
    case VT_RANGE:
     return v->rangeval.left == w->rangeval.left &&
	v->rangeval.right == w->rangeval.right;
    case VT_FLOAT:
     return v->floatval == w->floatval;
    case VT_STRING:
     return !strcmp(v->strval, w->strval);
    case VT_PNODE:
     return v->pnodeval == w->pnodeval;
    case VT_ALL:
     return 1;
    case VT_INDEX:
     return v->indexval == w->indexval;
    case VT_TUPLE:
     if (v->tupleval.nvals != w->tupleval.nvals) {
	return 0;
     }
     for (int i=0; i<v->tupleval.nvals; i++) {
	if (!value_eq(v->tupleval.vals[i], w->tupleval.vals[i])) {
	   return 0;
	}
     }
     return 1;

    case VT_LIST:
     if (v->listval->membertype != w->listval->membertype) {
	return 0;
     }
     if (v->listval->members.num() != w->listval->members.num()) {
	return 0;
     }
     for (int i=0; i<v->listval->members.num(); i++) {
	if (!value_eq(v->listval->members[i], w->listval->members[i])) {
	   return 0;
	}
     }
     return 1;

    case VT_LAMBDA:
     return v->lambdaval.id == w->lambdaval.id;
    case VT_BUILTIN:
     return v->builtinval == w->builtinval;
   }

   assert(0); // ?
   return 0;
}

value *value_clone(value *v) {
   if (!v) {
      return NULL;
   }
   switch (v->type) {
    case VT_INT: return value_int(v->intval);
    case VT_RANGE: return value_range(v->rangeval.left, v->rangeval.right);
    case VT_FLOAT: return value_float(v->floatval);
    case VT_STRING: return value_str(v->strval);
    case VT_PNODE: return value_pnode(v->pnodeval);
    case VT_ALL: return value_all();
    case VT_INDEX: return value_index(v->indexval);
    case VT_TUPLE:
     {
	value **vals = new value* [v->tupleval.nvals];
	for (int i=0; i<v->tupleval.nvals; i++) {
	   vals[i] = value_clone(v->tupleval.vals[i]);
	}
	return value_tuple(vals, v->tupleval.nvals);
     }

    case VT_LIST:
     {
	value *nl = value_list();
	nl->listval->membertype = v->listval->membertype;
	nl->listval->members.setsize(v->listval->members.num());
	for (int i=0; i<v->listval->members.num(); i++) {
	   nl->listval->members[i] = value_clone(v->listval->members[i]);
	}
	return nl;
     }

    case VT_LAMBDA:
     return value_lambda(v->lambdaval.id, expr_clone(v->lambdaval.e));
    case VT_BUILTIN:
     return value_builtin(v->builtinval);
   }

   assert(0); // ?
   return 0;
}

void value_destroy(value *v) {
   if (!v) {
      return;
   }
   switch (v->type) {
    case VT_INT:
    case VT_RANGE:
    case VT_FLOAT:
    case VT_PNODE:
     break;
    case VT_STRING:
     delete []v->strval;
     break;
    case VT_ALL:
    case VT_INDEX:
     break;
    case VT_TUPLE:
     if (v->tupleval.vals) {
	for (int i=0; i<v->tupleval.nvals; i++) {
	   delete v->tupleval.vals[i];
	}
	delete []v->tupleval.vals;
     }
     break;
    case VT_LIST:
     for (int i=0; i<v->listval->members.num(); i++) {
	value_destroy(v->listval->members[i]);
     }
     v->listval->members.setsize(0);
     delete v->listval;
     break;
    case VT_LAMBDA:
     expr_destroy(v->lambdaval.e);
     break;
    case VT_BUILTIN:
     break;
   }
   delete v;
}

void value_dump(value *v) {
   if (!v) {
      dump("NULL");
      return;
   }
   switch (v->type) {
    case VT_INT:
     dump("%ld", v->intval);
     break;
    case VT_FLOAT:
     dump("%g", v->floatval);
     break;
    case VT_STRING:
     dump("\"");
     for (int i=0; v->strval[i]; i++) {
	if (v->strval[i] >= 32 && v->strval[i] < 127) {
	   dump("%c", v->strval[i]);
	}
	else if (v->strval[i]=='\n') {
	   dump("\\n");
	}
	else if (v->strval[i]=='\t') {
	   dump("\\t");
	}
	else {
	   dump("\(%02x)", v->strval[i]);
	}
     }
     dump("\"");
     break;
    case VT_PNODE:
     dump("(pnode %llu)", (unsigned long long) v->pnodeval);
     break;
    case VT_RANGE:
     dump("(%ld..%ld)", v->rangeval.left, v->rangeval.right);
     break;
    case VT_ALL:
     dump("**");
     break;
    case VT_INDEX:
     dump("<index %s>", indexstr(v->indexval));
     break;
    case VT_TUPLE:
     dump("(");
     for (int i=0; i<v->tupleval.nvals; i++) {
	if (i>0) dump(", ");
	value_dump(v->tupleval.vals[i]);
     }
     dump(")");
     break;
    case VT_LIST:
     dump("[");
     dump_indent();
     for (int i=0; i<v->listval->members.num(); i++) {
	if (i>0) dump(", ");
	value_dump(v->listval->members[i]);
     }
     dump_unindent();
     dump("]");
     break;
    case VT_LAMBDA:
     ast_dump(v->lambdaval.e);
     break;
    case VT_BUILTIN:
     builtin_dump(v->builtinval);
     break;
   }
}

const char *indexstr(whichindex ix) {
   switch (ix) {
    case INDEX_ID: return "(identity)";
    case INDEX_I2P: return "I2P";
    case INDEX_NAME: return "NAME";
    case INDEX_ARGV: return "ARGV";
   }
   assert(0); // ?
   return NULL;
}
