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
#include <fnmatch.h>
#include <math.h>
#include "utils.h"
#include "dump.h"
#include "ast.h"
#include "builtins.h"
#include "dbops.h"
#include "main.h"

////////////////////////////////////////////////////////////

static value *eval_lookup(value *table, value *key) {
   if (table->type != VT_INDEX) {
      whine(nowhere, "Lookup applied to non-database");
      return NULL;
   }
   switch (table->indexval) {
    case INDEX_ID:
     return value_clone(key);
    case INDEX_I2P:
     if (key->type != VT_INT) {
	whine(nowhere, "Lookup in I2P requires integer key");
	return NULL;
     }
     return db_get_i2p(key->intval);
    case INDEX_NAME:
     if (key->type != VT_STRING) {
	whine(nowhere, "Lookup in NAME requires string key");
	return NULL;
     }
     return db_get_name(key->strval);
    case INDEX_ARGV:
     if (key->type != VT_STRING) {
	whine(nowhere, "Lookup in ARGV requires string key");
	return NULL;
     }
     return db_get_argv(key->strval);
   }

   assert(0); // ?
   return NULL;
}

static value *eval_sort(expr *sortees, expr *sortfields) {
   (void)sortees;
   (void)sortfields;
   assert(!"sorry, no sort yet");
   return NULL;
}

static value *eval_field(value *obj, value *field) {
   if (field->type != VT_STRING) {
      whine(nowhere, "Invalid field reference - field name not a string?");
      return NULL;
   }
   if (obj->type != VT_PNODE) {
      whine(nowhere, "Invalid field reference - field of non-object");
      return NULL;
   }

   return db_get_attr(obj->pnodeval, field->strval);
}

static value *eval_bind(expr *e, value *bound) {
   e->ref->val = bound;
   value *guard = eval(e->bind.suchthat);
   if (!guard) {
      e->ref->val = NULL;
      return NULL;
   }
   if (!value_istrue(guard)) {
      e->ref->val = NULL;
      value_destroy(guard);
      return NULL;
   }
   value *ret = eval(e->bind.in);
   value_destroy(guard);
   e->ref->val = NULL;
   return ret;
}

static void eval_bind_to_list(value *list, expr *e, value *bound) {
   value *item = eval_bind(e, bound);
   if (item) {
      valuelist_add(list->listval, item);
   }
}

static value *eval_for_range(expr *e, value *range) {
   value *ret = value_list();
   value *tmp = value_int(0);
   if (range->rangeval.left <= range->rangeval.right) {
      for (long x = range->rangeval.left; x <= range->rangeval.right; x++) {
	 tmp->intval = x;
	 eval_bind_to_list(ret, e, tmp);
      }
   }
   else {
      for (long x = range->rangeval.left; x >= range->rangeval.right; x--) {
	 tmp->intval = x;
	 eval_bind_to_list(ret, e, tmp);
      }
   }
   value_destroy(tmp);
   return ret;
}

static value *eval_for_list(expr *e, value *list) {
   value *ret = value_list();
   for (int i=0; i<list->listval->members.num(); i++) {
      value *bound = list->listval->members[i];
      eval_bind_to_list(ret, e, bound);
   }
   return ret;
}

static value *eval_for_tuple(expr *e, value *tuple) {
   value *ret = value_list();
   for (int i=0; i<tuple->tupleval.nvals; i++) {
      value *bound = tuple->tupleval.vals[i];
      eval_bind_to_list(ret, e, bound);
   }
   return ret;
}

static value *eval_for_all(expr *e) {
   return db_evalallprov(e->bind.ref, e->bind.suchthat, e->bind.in);
}

static value *eval_for(expr *e) {
   if (g_dotrace) {
      dump("for $%d = ...\n", e->bind.ref->id);
      dump_indent();
   }

   value *bound = eval(e->bind.bind);
   if (!bound) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("for $%d = NULL\n", e->bind.ref->id);
	 dump("NULL\n");
      }
      return NULL;
   }

   if (g_dotrace) {
      dump_unindent();
      dump("for $%d = ", e->bind.ref->id);
      value_dump(bound);
      dump(" in ...\n");
      dump_indent();
   }

   value *ret = NULL;
   switch (bound->type) {
    case VT_INT:
    case VT_FLOAT:
    case VT_STRING:
    case VT_PNODE:
    case VT_INDEX:
    case VT_BUILTIN:
    case VT_LAMBDA:
     // singleton object
     ret = eval_bind(e, bound);
     break;

    case VT_TUPLE:
     ret = eval_for_tuple(e, bound);
     break;

    case VT_RANGE:
     ret = eval_for_range(e, bound);
     break;

    case VT_LIST:
     ret = eval_for_list(e, bound);
     break;

    case VT_ALL:
     ret = eval_for_all(e);
     break;
   }

   value_destroy(bound);
   if (g_dotrace) {
      dump_unindent();
      dump("for $%d = ... in ... -> ", e->bind.ref->id);
      value_dump(ret);
      dump("\n");
   }
   return ret;
}

static value *eval_let(expr *e) {
   if (g_dotrace) {
      dump("let $%d = ...\n", e->bind.ref->id);
      dump_indent();
   }
   value *bound = eval(e->bind.bind);
   if (!bound) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("let $%d = NULL\n", e->bind.ref->id);
	 dump("NULL\n");
      }
      return NULL;
   }

   if (g_dotrace) {
      dump_unindent();
      dump("let $%d = ", e->bind.ref->id);
      value_dump(bound);
      dump(" in ...\n");
      dump_indent();
   }

   value *ret = eval_bind(e, bound);
   value_destroy(bound);
   if (g_dotrace) {
      dump_unindent();
      dump("let $%d = ... in ... -> ", e->bind.ref->id);
      value_dump(ret);
      dump("\n");
   }
   return ret;
}

static value *eval_func(value *func, value *arg) {
   if (func->type == VT_LAMBDA) {
      expr *e = func->lambdaval.e;
      if (g_dotrace) {
	 dump_unindent();
	 dump("lambda $%d = ", e->bind.ref->id);
	 value_dump(arg);
	 dump(" in ...\n");
	 dump_indent();
      }

      value *ret = eval_bind(e, arg);

      if (g_dotrace) {
	 dump_unindent();
	 dump("lambda $%d = ... in ... -> ", e->bind.ref->id);
	 value_dump(ret);
	 dump("\n");
      }

      return ret;
   }
   if (func->type == VT_BUILTIN) {
      return builtin_exec(func->builtinval, arg);
   }
   whine(nowhere, "Call of non-function");
   return NULL;
}

static value *eval_cond(expr *e) {
   if (g_dotrace) {
      dump("if ...\n");
      dump_indent();
   }
   value *test = eval(e->cond.test);
   if (!test) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("if NULL then T else F\n");
	 dump("F\n");
      }
      return eval(e->cond.no);
   }
   if (g_dotrace) {
      dump_unindent();
      dump("if %s then T else F\n", value_istrue(test) ? "YES" : "NO");
      dump("%s\n", value_istrue(test) ? "T" : "F");
      dump_indent();
   }
   value *ret = eval(value_istrue(test) ? e->cond.yes : e->cond.no);
   value_destroy(test);
   if (g_dotrace) {
      dump_unindent();
      dump("if ... -> ");
      value_dump(ret);
      dump("\n");
   }
   return ret;
}

static value *eval_logand(expr *left, expr *right) {
   if (g_dotrace) {
      dump("L && R\n");
      dump_indent();
   }
   value *tmp = eval(left);
   if (tmp && value_istrue(tmp)) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("true && R\n");
	 dump_indent();
      }
      value_destroy(tmp);
      tmp = eval(right);
   }
   int result = tmp && value_istrue(tmp);
   value_destroy(tmp);
   if (g_dotrace) {
      dump_unindent();
      dump("L && R -> %d\n", result);
   }
   return value_int(result);
}

static value *eval_logor(expr *left, expr *right) {
   if (g_dotrace) {
      dump("L || R\n");
      dump_indent();
   }
   value *tmp = eval(left);
   if (!tmp || !value_istrue(tmp)) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("false || R\n");
	 dump_indent();
      }
      value_destroy(tmp);
      tmp = eval(right);
   }
   int result = tmp && value_istrue(tmp);
   value_destroy(tmp);
   if (g_dotrace) {
      dump_unindent();
      dump("L || R -> %d\n", result);
   }
   return value_int(result);
}

#if 0
static value *eval_logxor(expr *left, expr *right) {
   if (g_dotrace) {
      dump("L ^^ R\n");
      dump_indent();
   }

   value *lv = eval(left);
   value *rv = eval(right);
   int lt = lv && value_istrue(lv);
   int rt = rv && value_istrue(rv);
   value_destroy(lv);
   value_destroy(rv);

   // force to 0 or 1 to make sure comparison works
   int result = (!!lt != !!rt);

   if (g_dotrace) {
      dump_unindent();
      dump("L ^^ R -> %d\n", result);
   }

   return value_int(result);
}
#endif

static value *eval_union(expr *le, expr *re) {
   if (g_dotrace) {
      dump("union L R");
      dump_indent();
   }

   value *left = eval(le);
   value *right = eval(re);
   if (!left && !right) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("union NULL NULL -> NULL\n");
      }
      return NULL;
   }
   if (!left) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("union NULL R -> R\n");
      }
      return value_clone(right);
   }
   if (!right) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("union L NULL -> L\n");
      }
      return value_clone(left);
   }

   if (g_dotrace) {
      dump_unindent();
      dump("union L R -> ...");
   }

   valuelist *ll = left->listval;
   valuelist *rl = right->listval;

   // XXX this should probably be done on a clone
   valuelist_sort(ll);
   valuelist_sort(rl);
   valuelist_uniq(ll);
   valuelist_uniq(rl);

   value *result = value_list();

   int lp = 0, rp = 0;
   while (lp < ll->members.num() && rp < rl->members.num()) {
      value *lv = ll->members[lp];
      value *rv = rl->members[rp];
      int r = valuelist_compare(lv, rv);
      if (r==0) {
	 valuelist_add(result->listval, value_clone(lv));
	 lp++;
	 rp++;
      }
      else if (r < 0) {
	 valuelist_add(result->listval, value_clone(lv));
	 lp++;
      }
      else {
	 valuelist_add(result->listval, value_clone(rv));
	 rp++;
      }
   }
   while (lp < ll->members.num()) {
      value *lv = ll->members[lp];
      valuelist_add(result->listval, value_clone(lv));
      lp++;
   }
   while (rp < rl->members.num()) {
      value *rv = rl->members[rp];
      valuelist_add(result->listval, value_clone(rv));
      rp++;
   }

   return result;
}

static value *eval_intersection(value *left, value *right) {
   if (!left || !right) {
      return NULL;
   }

   if (g_dotrace) {
      dump("intersect L R\n");
   }

   valuelist *ll = left->listval;
   valuelist *rl = right->listval;

   // XXX this should probably be done on a clone
   valuelist_sort(ll);
   valuelist_sort(rl);
   valuelist_uniq(ll);
   valuelist_uniq(rl);

   value *result = value_list();

   int lp = 0, rp = 0;
   while (lp < ll->members.num() && rp < rl->members.num()) {
      value *lv = ll->members[lp];
      value *rv = rl->members[rp];
      int r = valuelist_compare(lv, rv);
      if (r==0) {
	 valuelist_add(result->listval, value_clone(lv));
	 lp++;
	 rp++;
      }
      else if (r < 0) {
	 lp++;
      }
      else {
	 rp++;
      }
   }

   return result;
}

static value *eval_setdifference(value *left, value *right) {
   if (!left) {
      return NULL;
   }
   if (!right) {
      return value_clone(left);
   }

   if (g_dotrace) {
      dump("setdifference L R\n");
   }

   valuelist *ll = left->listval;
   valuelist *rl = right->listval;

   // XXX this should probably be done on a clone
   valuelist_sort(ll);
   valuelist_sort(rl);
   valuelist_uniq(ll);
   valuelist_uniq(rl);

   value *result = value_list();

   int lp = 0, rp = 0;
   while (lp < ll->members.num() && rp < rl->members.num()) {
      value *lv = ll->members[lp];
      value *rv = rl->members[rp];
      int r = valuelist_compare(lv, rv);
      if (r==0) {
	 lp++;
	 rp++;
      }
      else if (r < 0) {
	 valuelist_add(result->listval, value_clone(lv));
	 lp++;
      }
      else {
	 rp++;
      }
   }
   while (lp < ll->members.num()) {
      value *lv = ll->members[lp];
      valuelist_add(result->listval, value_clone(lv));
      lp++;
   }

   return result;
}

static value *eval_eq(value *left, value *right) {
   if (g_dotrace) {
      dump("eq L R\n");
   }
   return value_int(value_eq(left, right));
}

static value *eval_match(value *left, value *right) {
   if (left->type != VT_STRING || right->type != VT_STRING) {
      whine(nowhere, "Type error: ~ requires string operands");
      return NULL;
   }

   if (g_dotrace) {
      dump("string.match %s %s\n", left->strval, right->strval);
   }

   int result = fnmatch(right->strval, left->strval, 0)==0;
   return value_int(result);
}

static value *eval_lt(value *left, value *right) {
   if (left->type == VT_INT && right->type == VT_INT) {
      if (g_dotrace) {
	 dump("int.lessthan\n");
      }
      return value_int(left->intval < right->intval);
   }
   if (left->type == VT_FLOAT && right->type == VT_FLOAT) {
      if (g_dotrace) {
	 dump("float.lessthan\n");
      }
      return value_int(left->floatval < right->floatval);
   }
   if (left->type == VT_STRING && right->type == VT_STRING) {
      if (g_dotrace) {
	 dump("string.lessthan\n");
      }
      return value_int(strcmp(left->strval, right->strval) < 0);
   }
   whine(nowhere, "Invalid type arguments for < > <= >=");
   return NULL;
}

static int listsubsequence(value *left, value *right, int proper) {
   // check if the sequence LEFT appears in RIGHT

   valuelist *leftlist = left->listval;
   valuelist *rightlist = right->listval;

   int lpos = 0;
   for (int rpos = 0; rpos < rightlist->members.num(); rpos++) {
      if (value_eq(leftlist->members[lpos], rightlist->members[rpos])) {
	 lpos++;
	 if (lpos == leftlist->members.num()) {
	    if (proper && leftlist->members.num()==rightlist->members.num()) {
	       return 0;
	    }
	    return 1;
	 }
      }
      else {
	 // start match over
	 lpos = 0;
      }
   }
   return 0;
}

static int tuplesubsequence(value *left, value *right, int proper) {
   // check if the sequence LEFT appears in RIGHT
   int lpos = 0;
   for (int rpos = 0; rpos < right->tupleval.nvals; rpos++) {
      if (value_eq(left->tupleval.vals[lpos], right->tupleval.vals[rpos])) {
	 lpos++;
	 if (lpos == left->tupleval.nvals) {
	    if (proper && left->tupleval.nvals == right->tupleval.nvals) {
	       return 0;
	    }
	    return 1;
	 }
      }
      else {
	 // start match over
	 lpos = 0;
      }
   }
   return 0;
}

static value *eval_contains(value *left, value *right) {
   if (left->type == VT_LIST && right->type == VT_LIST) {
      if (left->listval->membertype != right->listval->membertype) {
	 whine(nowhere,
	       "Type error: contains requires lists of same type");
	 return NULL;
      }
      if (g_dotrace) {
	 dump("list.contains\n");
      }
      return value_int(listsubsequence(left, right, 0));
   }
   if (left->type == VT_TUPLE && right->type == VT_TUPLE) {
      if (g_dotrace) {
	 dump("tuple.contains\n");
      }
      return value_int(tuplesubsequence(left, right, 0));
   }
   whine(nowhere, "Invalid type arguments for contains");
   return NULL;
}

#if 0
static int listsubset(valuelist *left, valuelist *right, int proper) {
   valuelist_sort(left);
   valuelist_sort(right);

   // check if every member of LEFT is in RIGHT.
   int lp = 0, rp = 0;
   while (lp < left->members.num() && rp < right->members.num()) {
      if (value_eq(left->members[lp], right->members[rp])) {
	 lp++;
	 rp++;
	 continue;
      }
      rp++;
   }

   if (lp != left->members.num()) {
      // ran out of RIGHT without accounting for everything in LEFT
      return 0;
   }

   // all members of LEFT accounted for
   if (proper && left->members.num() == right->members.num()) {
      return 0;
   }
   return 1;
}
#endif

#if 0
static value *eval_subset(value *left, value *right) {
   if (left->type == VT_LIST && right->type == VT_LIST) {
      if (left->listval->membertype != right->listval->membertype) {
	 whine(nowhere,
	       "Type error: subset/superset require sets of same type");
	 return NULL;
      }
      if (g_dotrace) {
	 dump("list.subset\n");
      }
      return value_int(listsubset(left->listval, right->listval, 0));
   }
   if (left->type == VT_TUPLE && right->type == VT_TUPLE) {
      if (g_dotrace) {
	 dump("tuple.subset\n");
      }
      return value_int(tuplesubset(left, right, 0));
   }
   whine(nowhere, "Invalid type arguments for subset operation");
   return NULL;
}
#endif

#if 0
static value *eval_propersubset(value *left, value *right) {
   if (left->type == VT_LIST && right->type == VT_LIST) {
      if (left->listval->membertype != right->listval->membertype) {
	 whine(nowhere,
	       "Type error: subset/superset require sets of same type");
	 return NULL;
      }
      if (g_dotrace) {
	 dump("list.propersubset\n");
      }
      return value_int(listsubset(left->listval, right->listval, 1));
   }
   if (left->type == VT_TUPLE && right->type == VT_TUPLE) {
      if (g_dotrace) {
	 dump("tuple.propersubset\n");
      }
      return value_int(tuplesubset(left, right, 1));
   }
   whine(nowhere, "Invalid type arguments for << >>");
   return NULL;
}
#endif

static long doaddi(long a, long b) { return a+b; }
static long dosubi(long a, long b) { return a-b; }
static long domuli(long a, long b) { return a*b; }
static long dodivi(long a, long b) { return a/b; }
static long domodi(long a, long b) { return a%b; }
static double doaddf(double a, double b) { return a+b; }
static double dosubf(double a, double b) { return a-b; }
static double domulf(double a, double b) { return a*b; }
static double dodivf(double a, double b) { return a/b; }
static double domodf(double a, double b) { return fmod(a,b); }

static value *arith(value *left, value *right, int notzero,
		    double (*f)(double, double), long (*i)(long, long)) {
   if (notzero) {
      if (right->type == VT_FLOAT && right->floatval == 0) {
	 whine(nowhere, "Division by zero");
	 return NULL;
      }
      if (right->type == VT_INT && right->intval == 0) {
	 whine(nowhere, "Division by zero");
	 return NULL;
      }
   }

   if (g_dotrace) {
      dump("arithmetic\n");
   }

   if (left->type == VT_FLOAT && right->type == VT_FLOAT) {
      return value_float(f(left->floatval, right->floatval));
   }
   if (left->type == VT_FLOAT && right->type == VT_INT) {
      return value_float(f(left->floatval, (double) right->intval));
   }
   if (left->type == VT_INT && right->type == VT_FLOAT) {
      return value_float(f((double) left->intval, (double) right->floatval));
   }
   if (left->type == VT_INT && right->type == VT_INT) {
      return value_int(i(left->intval, right->intval));
   }
   whine(nowhere, "Invalid type arguments for + - * / %%");
   return NULL;
}

static value *eval_add(value *left, value *right) {
   return arith(left, right, 0, doaddf, doaddi);
}

static value *eval_sub(value *left, value *right) {
   if (left->type == VT_LIST && right->type == VT_LIST) {
      return eval_setdifference(left, right);
   }
   return arith(left, right, 0, dosubf, dosubi);
}

static value *eval_mul(value *left, value *right) {
   return arith(left, right, 0, domulf, domuli);
}

static value *eval_div(value *left, value *right) {
   return arith(left, right, 1, dodivf, dodivi);
}

static value *eval_mod(value *left, value *right) {
   return arith(left, right, 1, domodf, domodi);
}

static value *eval_strcat(value *left, value *right) {
   if (left->type == VT_STRING && right->type == VT_STRING) {
      if (g_dotrace) {
	 dump("string.concat\n");
      }
      return value_str_two(left->strval, right->strval);
   }
   if (left->type == VT_LIST && right->type == VT_LIST) {
      if (g_dotrace) {
	 dump("list.concat\n");
      }
      if (left->listval->members.num()==0) {
	 return value_clone(right);
      }
      if (right->listval->members.num()==0) {
	 return value_clone(left);
      }
      if (left->listval->membertype != right->listval->membertype) {
	 whine(nowhere, "Type error: ++ requires lists of same type");
	 return NULL;
      }
      value *ret = value_list();
      for (int i=0; i<left->listval->members.num(); i++) {
	 valuelist_add(ret->listval, value_clone(left->listval->members[i]));
      }
      for (int i=0; i<right->listval->members.num(); i++) {
	 valuelist_add(ret->listval, value_clone(right->listval->members[i]));
      }
      return ret;
   }

   whine(nowhere, "Type error: ++ requires string or list operands");
   return NULL;
}

static value *eval_neg(value *v) {
   if (v->type == VT_INT) {
      if (g_dotrace) {
	 dump("int.neg\n");
      }
      return value_int(-v->intval);
   }
   if (v->type == VT_FLOAT) {
      if (g_dotrace) {
	 dump("float.neg\n");
      }
      return value_float(-v->floatval);
   }
   whine(nowhere, "Type error: unary minus requires a number");
   return NULL;
}

static value *eval_lognot(value *v) {
   if (g_dotrace) {
      dump("bool.not\n");
   }
   return value_int(!value_istrue(v));
}

// this is different from eval_lognot because it modifies the argument.
// which is fine when it's composed with something else, but not when it's
// standing alone, as the argument might have come out of a var.
static value *donot(value *v) {
   assert(v->type == VT_INT);
   v->intval = !v->intval;
   if (g_dotrace) {
      dump("bool.not\n");
   }
   return v;
}

static value *eval_op(expr *e) {
   switch (e->op.op) {

    // not supposed to be seen
    case OP_NOP:
    case OP_REVSORT:
     assert(0);
     return NULL;

    case OP_SORT:
     return eval_sort(e->op.left, e->op.right);

    case OP_PATH:
    case OP_LONGPATHZ:
    case OP_LONGPATHNZ:
     return eval_path(e);

    case OP_LOGAND:
     return eval_logand(e->op.left, e->op.right);
    case OP_LOGOR:
     return eval_logor(e->op.left, e->op.right);
#if 0
    case OP_LOGXOR:
     return eval_logxor(e->op.left, e->op.right);
#endif

    case OP_UNION:
     return eval_union(e->op.left, e->op.right);

    default:
     break;
   }

   if (g_dotrace) {
      dump("op L R\n");
      dump_indent();
   }

   value *lv = eval(e->op.left);
   if (lv == NULL) {
      if (g_dotrace) {
	 dump_unindent();
	 dump("op NULL R -> NULL\n");
      }
      return NULL;
   }
   value *rv = eval(e->op.right);
   if (rv == NULL && !isunaryop(e->op.op)) {
      value_destroy(lv);
      if (g_dotrace) {
	 dump_unindent();
	 dump("op L NULL -> NULL\n");
      }
      return NULL;
   }

   if (g_dotrace) {
      dump_unindent();
      dump("op ");
      value_dump(lv);
      dump(" ");
      value_dump(rv);
      dump("\n");
   }

   value *result;
   switch (e->op.op) {
    case OP_LOOKUP:         result = eval_lookup(lv, rv); break;
    case OP_FUNC:           result = eval_func(lv, rv); break;
    case OP_FIELD:          result = eval_field(lv, rv); break;
    case OP_INTERSECT:      result = eval_intersection(lv, rv); break;
    case OP_EQ:             result = eval_eq(lv, rv); break;
    case OP_NE:             result = donot(eval_eq(lv, rv)); break;
    case OP_MATCH:          result = eval_match(lv, rv); break;
    case OP_NOMATCH:        result = donot(eval_match(lv, rv)); break;
    case OP_LT:             result = eval_lt(lv, rv); break;
    case OP_GT:             result = eval_lt(rv, lv); break;
    case OP_LE:             result = donot(eval_lt(rv, lv)); break;
    case OP_GE:             result = donot(eval_lt(lv, rv)); break;
#if 0
    case OP_SUBSET:         result = eval_subset(lv, rv); break;
    case OP_SUPERSET:       result = eval_subset(rv, lv); break;
    case OP_PROPERSUBSET:   result = eval_propersubset(lv, rv); break;
    case OP_PROPERSUPERSET: result = eval_propersubset(rv, lv); break;
#endif
    case OP_CONTAINS:       result = eval_contains(rv, lv); break;
    case OP_ADD:            result = eval_add(lv, rv); break;
    case OP_SUB:            result = eval_sub(lv, rv); break;
    case OP_MUL:            result = eval_mul(lv, rv); break;
    case OP_DIV:            result = eval_div(lv, rv); break;
    case OP_MOD:            result = eval_mod(lv, rv); break;
    case OP_STRCAT:         result = eval_strcat(lv, rv); break;
    case OP_LOGNOT:         result = eval_lognot(lv); break;
    case OP_NEG:            result = eval_neg(lv); break;
    default:
     assert(0);
     result = NULL;
   }

   value_destroy(lv);
   value_destroy(rv);

   if (g_dotrace) {
      value_dump(result);
      dump("\n");
   }

   return result;
}

static value *eval_tuple(expr *e) {
   int num = e->tuple.nexprs;
   if (g_dotrace) {
      dump("eval_tuple (arity %d)\n", num);
      dump_indent();
   }
   value **vals = new value* [num];
   for (int i=0; i<num; i++) {
      vals[i] = eval(e->tuple.exprs[i]);
   }
   if (g_dotrace) {
      dump_unindent();
      dump("eval_tuple (arity %d)\n", num);
   }
   return value_tuple(vals, num);
}

value *eval(expr *e) {
   static int nextlam;

   value *v = NULL;
   if (e) {
      switch (e->type) {
       case ET_FOR:   v = eval_for(e); break;
       case ET_LET:   v = eval_let(e); break;
       case ET_LAMBDA: v = value_lambda(nextlam++, expr_clone(e)); break;
       case ET_COND:  v = eval_cond(e); break;
       case ET_OP:    v = eval_op(e); break;
       case ET_TUPLE: v = eval_tuple(e); break;
       case ET_REF:   v = value_clone(e->ref->val); break;
       case ET_VAL:   v = value_clone(e->val); break;
      }
   }
   return v;
}
