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
#include "dump.h"
#include "ast.h"

var *var_create(void) {
   static int nextvar;

   var *v = new var;
   v->id = nextvar++;
   v->val = NULL;
   return v;
}

// USE WITH CAUTION - most vars get shared around a lot
// (should probably be refcounted)
void var_destroy(var *ref) {
   assert(ref->val == NULL);
   delete ref;
}

expr *expr_for(var *ref, expr *bind, expr *suchthat, expr *in) {
   expr *e = new expr;
   e->type = ET_FOR;
   e->bind.ref = ref;
   e->bind.bind = bind;
   e->bind.suchthat = suchthat;
   e->bind.in = in;
   return e;
}

expr *expr_let(var *ref, expr *bind, expr *in) {
   expr *e = new expr;
   e->type = ET_LET;
   e->bind.ref = ref;
   e->bind.bind = bind;
   e->bind.suchthat = expr_val(value_int(1));
   e->bind.in = in;
   return e;
}

expr *expr_lambda(var *ref, expr *in) {
   expr *e = new expr;
   e->type = ET_LAMBDA;
   e->bind.ref = ref;
   e->bind.bind = NULL;
   e->bind.suchthat = expr_val(value_int(1));
   e->bind.in = in;
   return e;
}

expr *expr_cond(expr *test, expr *yes, expr *no) {
   expr *e = new expr;
   e->type = ET_COND;
   e->cond.test = test;
   e->cond.yes = yes;
   e->cond.no = no;
   return e;
}

expr *expr_op(ops o, expr *l, expr *r) {
   expr *e = new expr;
   e->type = ET_OP;
   e->op.op = o;
   e->op.left = l;
   e->op.right = r;
   return e;
}

expr *expr_tuple(expr **exprs, int nexprs) {
   expr *e = new expr;
   e->type = ET_TUPLE;
   e->tuple.exprs = exprs;
   e->tuple.nexprs = nexprs;
   return e;
}

expr *expr_ref(var *v) {
   expr *e = new expr;
   e->type = ET_REF;
   e->ref = v;
   return e;
}

expr *expr_val(value *v) {
   expr *e = new expr;
   e->type = ET_VAL;
   e->val = v;
   return e;
}

expr *expr_clone(expr *e) {
   if (!e) {
      return NULL;
   }
   switch (e->type) {
    case ET_FOR:
     return expr_for(e->bind.ref,      // XXX should refcount vars
		     expr_clone(e->bind.bind),
		     expr_clone(e->bind.suchthat),
		     expr_clone(e->bind.in));
    case ET_LET:
     return expr_let(e->bind.ref,      // XXX should refcount vars
		     expr_clone(e->bind.bind),
		     expr_clone(e->bind.in));
    case ET_LAMBDA:
     return expr_lambda(e->bind.ref,      // XXX should refcount vars
		     expr_clone(e->bind.in));
    case ET_COND:
     return expr_cond(expr_clone(e->cond.test),
		      expr_clone(e->cond.yes),
		      expr_clone(e->cond.no));
     break;
    case ET_OP:
     return expr_op(e->op.op, expr_clone(e->op.left), expr_clone(e->op.right));
    case ET_TUPLE:
     assert(e->tuple.exprs);
     {
	expr **exprs = new expr* [e->tuple.nexprs];
	for (int i=0; i<e->tuple.nexprs; i++) {
	   exprs[i] = expr_clone(e->tuple.exprs[i]);
	}
	return expr_tuple(exprs, e->tuple.nexprs);
     }
    case ET_REF:
     // XXX should refcount vars
     return expr_ref(e->ref);
     break;
    case ET_VAL:
     return expr_val(value_clone(e->val));
   }

   assert(0); // ?
   return NULL;
}

void expr_destroy(expr *e) {
   if (!e) {
      return;
   }
   switch (e->type) {
    case ET_FOR:
    case ET_LET:
    case ET_LAMBDA:
     // drop e->bind.ref on floor (XXX should refcount vars)
     expr_destroy(e->bind.bind);
     expr_destroy(e->bind.suchthat);
     expr_destroy(e->bind.in);
     break;
    case ET_COND:
     expr_destroy(e->cond.test);
     expr_destroy(e->cond.yes);
     expr_destroy(e->cond.no);
     break;
    case ET_OP:
     expr_destroy(e->op.left);
     expr_destroy(e->op.right);
     break;
    case ET_TUPLE:
     if (e->tuple.exprs) {
	for (int i=0; i<e->tuple.nexprs; i++) {
	   expr_destroy(e->tuple.exprs[i]);
	}
	delete []e->tuple.exprs;
     }
     break;
    case ET_REF:
     // drop e->ref on floor (XXX should refcount vars)
     break;
    case ET_VAL:
     if (e->val) {
	value_destroy(e->val);
     }
     break;
   }
   delete e;
}

////////////////////////////////////////////////////////////

static void dumprec(expr *e) {
   if (!e) {
      dump("<null>");
      return;
   }
   switch (e->type) {
    case ET_FOR:
    case ET_LET:
     dump("\n%s $%d = ", e->type==ET_FOR ? "for" : "let", e->bind.ref->id);
     dump_indent();
     dumprec(e->bind.bind);
     dump_unindent();
     if (e->bind.suchthat->type != ET_VAL || 
	 e->bind.suchthat->val->type != VT_INT ||
	 e->bind.suchthat->val->intval != 1) {
	dump("\nsuchthat ");
	dump_indent();
	dumprec(e->bind.suchthat);
	dump_unindent();
     }
     dump("\nin ");
     dump_indent();
     dumprec(e->bind.in);
     dump_unindent();
     break;

    case ET_LAMBDA:
     dump("\n\\ $%d ", e->bind.ref->id);
     assert(e->bind.bind == NULL);
     if (e->bind.suchthat->type != ET_VAL || 
	 e->bind.suchthat->val->type != VT_INT ||
	 e->bind.suchthat->val->intval != 1) {
	dump("\nsuchthat ");
	dump_indent();
	dumprec(e->bind.suchthat);
	dump_unindent();
     }
     dump("\nin ");
     dump_indent();
     dumprec(e->bind.in);
     dump_unindent();
     break;

    case ET_COND:
     dump("(");
     dump_indent();
     dumprec(e->cond.test);
     dump(" ? ");
     dumprec(e->cond.yes);
     dump(" : ");
     dumprec(e->cond.no);
     dump_unindent();
     dump(")");
     break;

    case ET_OP:
     dump("(%s ", opstr(e->op.op));
     dump_indent();
     dumprec(e->op.left);
     dump(" ");
     dumprec(e->op.right);
     dump_unindent();
     dump(")");
     break;

    case ET_TUPLE:
     dump("(");
     dump_indent();
     for (int i=0; i<e->tuple.nexprs; i++) {
	if (i>0) dump(", ");
	dumprec(e->tuple.exprs[i]);
     }
     dump_unindent();
     dump(")");
     break;

    case ET_REF:
     dump("$%d", e->ref->id);
     break;

    case ET_VAL:
     value_dump(e->val);
     break;
   }
}

void ast_dump(expr *e) {
   dump_begin();
   dumprec(e);
   dump_end();
}
