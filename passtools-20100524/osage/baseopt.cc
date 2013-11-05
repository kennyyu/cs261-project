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

// basic optimizations.
//
// much of this would not be worthwhile if there weren't the question of
// the query optimizer downstream not wanting to be any smarter than
// necessary.

#include <stdio.h> // XXX temp
#include <string.h>
#include <assert.h>
#include "ast.h"
#include "main.h"

static expr *op_sel_left(expr *e) {
   expr *ret = e->op.left;
   e->op.left = NULL;
   expr_destroy(e);
   return ret;
}

static expr *op_sel_right(expr *e) {
   expr *ret = e->op.right;
   e->op.right = NULL;
   expr_destroy(e);
   return ret;
}

static int mentions(expr *e, var *ref) {
   if (!e) {
      return 0;
   }
   switch (e->type) {
    case ET_FOR:
    case ET_LET:
    case ET_LAMBDA:
     return mentions(e->bind.bind, ref) || mentions(e->bind.suchthat, ref)
	|| mentions(e->bind.in, ref);
    case ET_COND:
     return mentions(e->cond.test, ref) || mentions(e->cond.yes, ref)
	|| mentions(e->cond.no, ref);
    case ET_OP:
     return mentions(e->op.left, ref) || mentions(e->op.right, ref);
    case ET_TUPLE:
     for (int i=0; i<e->tuple.nexprs; i++) {
	if (mentions(e->tuple.exprs[i], ref)) {
	   return 1;
	}
     }
     return 0;
    case ET_REF:
     return e->ref->id == ref->id;
    case ET_VAL:
     return 0;
   }
   assert(0); // ?
   return 0;
}

static expr *squeeze_tuple(expr *e) {
   assert(e->type == ET_TUPLE);
   int n = 0;
   for (int i=0; i<e->tuple.nexprs; i++) {
      if (e->tuple.exprs[i] != NULL) {
	 n++;
      }
   }
   if (n == e->tuple.nexprs) {
      return e;
   }
   if (n==0) {
      return NULL;
   }
   if (n==1) {
      expr *ret = e->tuple.exprs[0];
      e->tuple.exprs[0] = NULL;
      expr_destroy(e);
      return ret;
   }
   expr **nx = new expr* [n];
   int j = 0;
   for (int i=0; i<e->tuple.nexprs; i++) {
      if (e->tuple.exprs[i] != NULL) {
	 nx[j++] = e->tuple.exprs[i];
      }
   }
   assert(j==n);
   delete []e->tuple.exprs;
   e->tuple.exprs = nx;
   e->tuple.nexprs = n;
   return e;
}

static void varsubst(expr *e, var *oldref, var *newref) {
   if (!e) {
      return;
   }
   switch (e->type) {
    case ET_FOR:
    case ET_LET:
    case ET_LAMBDA:
     varsubst(e->bind.bind, oldref, newref);
     varsubst(e->bind.suchthat, oldref, newref);
     varsubst(e->bind.in, oldref, newref);
     break;
    case ET_COND:
     varsubst(e->cond.test, oldref, newref);
     varsubst(e->cond.yes, oldref, newref);
     varsubst(e->cond.no, oldref, newref);
     break;
    case ET_OP:
     varsubst(e->op.left, oldref, newref);
     varsubst(e->op.right, oldref, newref);
     break;
    case ET_TUPLE:
     for (int i=0; i<e->tuple.nexprs; i++) {
	varsubst(e->tuple.exprs[i], oldref, newref);
     }
     break;
    case ET_REF:
     if (e->ref->id == oldref->id) {
	e->ref = newref;
     }
     break;
    case ET_VAL:
     break;
   }
}

static expr *varkill(expr *e, var *ref) {
   if (!e) {
      return NULL;
   }
   switch (e->type) {
    case ET_FOR:
    case ET_LET:
    case ET_LAMBDA:
     e->bind.bind = varkill(e->bind.bind, ref);
     e->bind.suchthat = varkill(e->bind.suchthat, ref);
     e->bind.in = varkill(e->bind.in, ref);
     e = baseopt(e);
     break;
    case ET_COND:
     e->cond.test = varkill(e->cond.test, ref);
     e->cond.yes = varkill(e->cond.yes, ref);
     e->cond.no = varkill(e->cond.no, ref);
     e = baseopt(e);
     break;
    case ET_OP:
     e->op.left = varkill(e->op.left, ref);
     e->op.right = varkill(e->op.right, ref);
     e = baseopt(e);
     break;
    case ET_TUPLE:
     for (int i=0; i<e->tuple.nexprs; i++) {
	e->tuple.exprs[i] = varkill(e->tuple.exprs[i], ref);
     }
     e = squeeze_tuple(e);
     break;
    case ET_REF:
     if (e->ref->id == ref->id) {
	expr_destroy(e);
	e = NULL;
     }
     break;
    case ET_VAL:
     break;
   }
   return e;
}

////////////////////////////////////////////////////////////

static expr *catch_identity_bind(expr *e) {
   // for x in foo suchthat 1 do x  ==>  foo
   // let x = foo in x  ==>  foo
   if (e->bind.in->type == ET_REF &&
       e->bind.in->ref->id == e->bind.ref->id &&
       e->bind.suchthat->type == ET_VAL && 
       value_istrue(e->bind.suchthat->val)) {
      expr *ret = e->bind.bind;
      e->bind.bind = NULL;
      expr_destroy(e);
      return ret;
   }
   return e;
}

static expr *baseopt_for(expr *e) {
   e->bind.bind = baseopt(e->bind.bind);
   e->bind.suchthat = baseopt(e->bind.suchthat);
   e->bind.in = baseopt(e->bind.in);

   if (e->bind.bind == NULL || e->bind.suchthat == NULL || e->bind.in == NULL){
      // for x in nil do ... = nil
      // for x in ... do nil = nil
      // for x in ... suchthat nil do ... = nil
      expr_destroy(e);
      return NULL;
   }

   if (e->bind.suchthat->type == ET_VAL && 
       !value_istrue(e->bind.suchthat->val)) {
      // for x in ... suchthat false do ... = nil
      expr_destroy(e);
      return NULL;
   }

   // could do hoisting here

   e = catch_identity_bind(e);
   if (e->type != ET_FOR) {
      return e;
   }

   // nothing else yet

   return e;
}

static expr *baseopt_let(expr *e) {
   e->bind.bind = baseopt(e->bind.bind);
   e->bind.in = baseopt(e->bind.in);

   if (e->bind.in == NULL) {
      // let x = ... in nil = nil
      expr_destroy(e);
      return NULL;
   }

   // let x = foo in expression-with-no-x  ==>  expression
   // could generalize this to hoist partial expressions
   if (!mentions(e->bind.in, e->bind.ref)) {
      expr *ret = e->bind.in;
      e->bind.in = NULL;
      expr_destroy(e);
      return ret;
   }

   if (e->bind.bind == NULL) {
      // let x = nil in expression  ==>  expression (killing x)
      expr *ret = varkill(e->bind.in, e->bind.ref);
      e->bind.in = NULL;
      expr_destroy(e);
      baseopt(ret);
      return ret;
   }

   // let x = y in expression  ==>  expression (substituting y for x)
   if (e->bind.bind->type == ET_REF) {
      varsubst(e->bind.in, e->bind.ref, e->bind.bind->ref);
      expr *ret = e->bind.in;
      e->bind.in = NULL;
      expr_destroy(e);
      baseopt(ret);
      return ret;
   }

   e = catch_identity_bind(e);
   if (e->type != ET_LET) {
      return e;
   }

   return e;
}

static expr *baseopt_lambda(expr *e) {
   assert(e->bind.bind == NULL);
   e->bind.in = baseopt(e->bind.in);

   if (e->bind.in == NULL) {
      // \ x in nil = nil
      expr_destroy(e);
      return NULL;
   }

   // \ x in expression-with-no-x  ==>  expression
   // could generalize this to hoist partial expressions
   if (!mentions(e->bind.in, e->bind.ref)) {
      expr *ret = e->bind.in;
      e->bind.in = NULL;
      expr_destroy(e);
      return ret;
   }

   return e;
}

static expr *intconstantfold(expr *e) {
   assert(e->type==ET_OP);
   if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT && 
       e->op.right->type == ET_VAL && e->op.right->val->type == VT_INT) {
      long lv = e->op.left->val->intval;
      long rv = e->op.right->val->intval;
      long val;
      switch (e->op.op) {
       case OP_LT:  val = lv < rv; break;
       case OP_GT:  val = lv > rv; break;
       case OP_LE:  val = lv <= rv; break;
       case OP_GE:  val = lv >= rv; break;
       case OP_ADD: val = lv + rv; break;
       case OP_SUB: val = lv - rv; break;
       case OP_MUL: val = lv * rv; break;
       case OP_DIV: if (rv == 0) return e; val = lv / rv; break;
       case OP_MOD: if (rv == 0) return e; val = lv % rv; break;
       default:
	assert(0);
	val = 0;
	break;
      }
      e->op.left->val->intval = val;
      return op_sel_left(e);
   }

   if (e->op.left->type==ET_VAL && e->op.left->val->type == VT_INT &&
       e->op.right->type==ET_OP && 
       (e->op.right->op.op == OP_ADD || e->op.right->op.op == OP_SUB) &&
       e->op.right->op.left->type == ET_VAL &&
       e->op.right->op.left->val->type == VT_INT) {
      // 1+(2+x) == 3+x
      e->op.right->op.left->val->intval += e->op.left->val->intval;
      return op_sel_right(e);
   }

   if (e->op.left->type==ET_VAL && e->op.left->val->type == VT_INT &&
       e->op.right->type==ET_OP && e->op.right->op.op == OP_SUB &&
       e->op.right->op.right->type == ET_VAL &&
       e->op.right->op.right->val->type == VT_INT) {
      // 1+(x-2) == x-1
      e->op.right->op.right->val->intval -= e->op.left->val->intval;
      return op_sel_right(e);
   }

   if (e->op.left->type==ET_VAL && e->op.left->val->type == VT_INT &&
       e->op.right->type==ET_OP && e->op.right->op.op == OP_MUL &&
       e->op.right->op.left->type == ET_VAL &&
       e->op.right->op.left->val->type == VT_INT) {
      // 2*(3*x) == 6*x
      e->op.right->op.left->val->intval *= e->op.left->val->intval;
      return op_sel_right(e);
   }

   return e;
}

static expr *prune_constant_tuple_fields(expr *e) {
   assert(e->type == ET_TUPLE);
   for (int i=0; i<e->tuple.nexprs; i++) {
      if (e->tuple.exprs[i] && e->tuple.exprs[i]->type == ET_VAL) {
	 expr_destroy(e->tuple.exprs[i]);
	 e->tuple.exprs[i] = NULL;
	 continue;
      }
   }
   return squeeze_tuple(e);
}

static expr *baseopt_cond(expr *e) {
   e->cond.test = baseopt(e->cond.test);
   e->cond.yes = baseopt(e->cond.yes);
   e->cond.no = baseopt(e->cond.no);

   if (e->cond.test == NULL) {
      expr *ret = e->cond.yes;
      e->cond.yes = NULL;
      expr_destroy(e);
      return ret;
   }
   if (e->cond.yes == NULL && e->cond.no == NULL) {
      expr_destroy(e);
      return NULL;
   }

   if (e->cond.test->type == ET_VAL) {
      expr *ret;
      if (value_istrue(e->cond.test->val)) {
	 ret = e->cond.yes;
	 e->cond.yes = NULL;
      }
      else {
	 ret = e->cond.no;
	 e->cond.no = NULL;
      }
      expr_destroy(e);
      return ret;
   }

   // could hoist common subexpressions

   return e;
}

static expr *baseopt_op(expr *e) {
   e->op.left = baseopt(e->op.left);
   e->op.right = baseopt(e->op.right);

   switch (e->op.op) {
    case OP_NOP:
     // not supposed to appear
     assert(0);
     break;

    case OP_SORT:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.right == NULL) {
	return op_sel_left(e);
     }

     // remove constant sort keys.

     // 1. if sort terms vanish, abolish sort operator
     // a tuple that's all constants is now a constant tuple and
     // doesn't need a separate test.
     if (e->op.right->type == ET_VAL) {
	return op_sel_left(e);
     }

     // 2. prune constant values out of the topmost tuple, if any.
     if (e->op.right->type == ET_TUPLE) {
	e->op.right = prune_constant_tuple_fields(e->op.right);
	if (e->op.right == NULL) {
	   return op_sel_left(e);
	}
     }

     break;

    case OP_REVSORT:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_LOOKUP:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_FUNC:
    case OP_FIELD:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_PATH:
    case OP_LONGPATHZ:
    case OP_LONGPATHNZ:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_UNION:
     if (e->op.left == NULL) {
	return op_sel_right(e);
     }
     if (e->op.right == NULL) {
	return op_sel_left(e);
     }
     break;

    case OP_INTERSECT:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_CONS:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_LOGAND:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return expr_val(value_int(0));
     }
     if (e->op.left->type == ET_VAL) {
	if (value_istrue(e->op.left->val)) {
	   e = op_sel_right(e);
	}
	else {
	   expr_destroy(e);
	   return expr_val(value_int(1));
	}
     }
     break;

    case OP_LOGOR:
     if (e->op.left == NULL) {
	return op_sel_right(e);
     }
     if (e->op.left->type == ET_VAL) {
	if (value_istrue(e->op.left->val)) {
	   expr_destroy(e);
	   return expr_val(value_int(1));
	}
	else {
	   e = op_sel_right(e);
	}
     }
     break;

#if 0
    case OP_LOGXOR:
     if (e->op.left == NULL && e->op.right == NULL) {
	expr_destroy(e);
	return expr_val(value_int(0));
     }
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return expr_val(value_int(1));
     }
     if (e->op.left->type == ET_VAL && e->op.right->type == ET_VAL) {
	int val = value_istrue(e->op.left->val) != 
	   value_istrue(e->op.right->val);
	expr_destroy(e);
	return expr_val(value_int(val));
     }
     break;
#endif

    case OP_EQ:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_REF && e->op.right->type == ET_REF &&
	 e->op.left->ref->id == e->op.right->ref->id) {
	// same variable
	expr_destroy(e);
	return expr_val(value_int(1));
     }
     if (e->op.left->type == ET_VAL && e->op.right->type == ET_VAL) {
	int val = value_eq(e->op.left->val, e->op.right->val);
	expr_destroy(e);
	return expr_val(value_int(val));
     }
     break;

    case OP_NE:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_VAL && e->op.right->type == ET_VAL) {
	int val = !value_eq(e->op.left->val, e->op.right->val);
	expr_destroy(e);
	return expr_val(value_int(val));
     }
     break;

    case OP_MATCH:
    case OP_NOMATCH:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     e = intconstantfold(e);
     break;

    case OP_ADD:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT &&
	 e->op.left->val->intval == 0) {
	// 0+x = x
	return op_sel_right(e);
     }
     if (e->op.right->type == ET_VAL && e->op.right->val->type == VT_INT &&
	 e->op.right->val->intval == 0) {
	// x+0 = x
	return op_sel_left(e);
     }
     if (e->op.left->type != ET_VAL && e->op.right->type == ET_VAL) {
	expr *tmp = e->op.left;
	e->op.left = e->op.right;
	e->op.right = tmp;
     }
     e = intconstantfold(e);
     break;

    case OP_SUB:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT &&
	 e->op.left->val->intval == 0) {
	// 0-x = -x
	expr_destroy(e->op.left);
	e->op.left = e->op.right;
	e->op.right = NULL;
	e->op.op = OP_NEG;
	goto neg;
     }
     if (e->op.right->type == ET_VAL && e->op.right->val->type == VT_INT &&
	 e->op.right->val->intval == 0) {
	// x-0 = x
	return op_sel_left(e);
     }
     e = intconstantfold(e);
     break;

    case OP_MUL:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT &&
	 e->op.left->val->intval == 0) {
	// 0*x = 0
	expr_destroy(e);
	return expr_val(value_int(0));
     }
     if (e->op.right->type == ET_VAL && e->op.right->val->type == VT_INT &&
	 e->op.right->val->intval == 0) {
	// x*0 = 0
	expr_destroy(e);
	return expr_val(value_int(0));
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT &&
	 e->op.left->val->intval == 1) {
	// 1*x = x
	return op_sel_right(e);
     }
     if (e->op.right->type == ET_VAL && e->op.right->val->type == VT_INT &&
	 e->op.right->val->intval == 1) {
	// x*1 = x
	return op_sel_left(e);
     }
     if (e->op.left->type != ET_VAL && e->op.right->type == ET_VAL) {
	expr *tmp = e->op.left;
	e->op.left = e->op.right;
	e->op.right = tmp;
     }
     e = intconstantfold(e);
     break;

    case OP_DIV:
    case OP_MOD:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT &&
	 e->op.left->val->intval == 0) {
	// 0/x = 0, 0%x = 0
	expr_destroy(e);
	return expr_val(value_int(0));
     }
     e = intconstantfold(e);
     break;

    case OP_STRCAT:
     if (e->op.left == NULL) {
	return op_sel_right(e);
     }
     if (e->op.right == NULL) {
	return op_sel_left(e);
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_STRING &&
	 *e->op.left->val->strval == 0) {
	// "" ++ x = x
	return op_sel_right(e);
     }
     if (e->op.right->type == ET_VAL && e->op.right->val->type == VT_STRING &&
	 *e->op.right->val->strval == 0) {
	// x ++ "" = x
	return op_sel_left(e);
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_STRING &&
	 e->op.right->type == ET_VAL && e->op.right->val->type == VT_STRING) {
	const char *l = e->op.left->val->strval;
	const char *r = e->op.right->val->strval;
	expr *ne = expr_val(value_str_two(l, r));
	expr_destroy(e);
	return ne;
     }
     break;

    case OP_LOGNOT:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_OP && e->op.left->op.op == OP_LOGNOT) {
	// !!x = x
	return op_sel_left(op_sel_left(e));
     }
     if (e->op.left->type == ET_VAL) {
	int val = !value_istrue(e->op.left->val);
	expr_destroy(e);
	return expr_val(value_int(val));
     }
     break;

    case OP_NEG:
    neg:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return NULL;
     }
     if (e->op.left->type == ET_OP && e->op.left->op.op == OP_NEG) {
	// -(-x) = x
	return op_sel_left(op_sel_left(e));
     }
     if (e->op.left->type == ET_OP && e->op.left->op.op == OP_SUB) {
	// -(a-b) = b-a
	e = op_sel_left(e);
	expr *tmp = e->op.left;
	e->op.left = e->op.right;
	e->op.right = tmp;
	return e;
     }
     if (e->op.left->type == ET_VAL && e->op.left->val->type == VT_INT) {
	e->op.left->val->intval = -e->op.left->val->intval;
	return op_sel_left(e);
     }
     break;

    case OP_OPTIONAL:
    case OP_REPEAT:
    case OP_EXTRACT:
     if (e->op.left == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;

#if 0
    case OP_SUBSET:
    case OP_SUPERSET:
    case OP_PROPERSUBSET:
    case OP_PROPERSUPERSET:
#endif
    case OP_CONTAINS:
     if (e->op.left == NULL || e->op.right == NULL) {
	expr_destroy(e);
	return NULL;
     }
     break;
   }

   return e;
}

static expr *baseopt_tuple(expr *e) {
   int isconst = 1;
   int num = e->tuple.nexprs;
   for (int i=0; i<num; i++) {
      e->tuple.exprs[i] = baseopt(e->tuple.exprs[i]);
      if (e->tuple.exprs[i] && e->tuple.exprs[i]->type != ET_VAL) {
	 isconst = 0;
      }
   }

   e = squeeze_tuple(e);
   if (!e || e->type != ET_TUPLE) {
      return e;
   }

   if (isconst) {
      // convert to a tuple value
      value **vals = new value* [num];
      for (int i=0; i<num; i++) {
	 vals[i] = e->tuple.exprs[i]->val;
	 e->tuple.exprs[i]->val = NULL;
      }
      expr_destroy(e);
      e = expr_val(value_tuple(vals, num));
   }

   return e;
}

expr *baseopt(expr *e) {
   if (!e) {
      return NULL;
   }
   switch (e->type) {
    case ET_FOR:
     return baseopt_for(e);
    case ET_LET:
     return baseopt_let(e);
    case ET_LAMBDA:
     return baseopt_lambda(e);
    case ET_COND:
     return baseopt_cond(e);
    case ET_OP:
     return baseopt_op(e);
    case ET_TUPLE:
     return baseopt_tuple(e);
    case ET_REF:
    case ET_VAL:
     // not much we can do with these
     return e;
   }
   assert(0); // ?
   return e;
}
