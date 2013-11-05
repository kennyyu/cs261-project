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

// Insert index lookups into generic query AST.
//
// For purposes of this module we have these indexes:
//
//   INDEX_ID	pnode -> pnode  (dummy)
//   INDEX_I2P	inode -> pnode
//   INDEX_NAME	name -> pnode
//   INDEX_ARGV	argument word -> pnode
//
// plus the dummy indexes pnode -> pnode and pnode+version -> pnode+version.
//
// We look for things we can handle by indexes as follows:
//    1. Find loops of the form FOR x IN ** SUCHTHAT p DO x
//    2. Make a model of the predicate in disjunctive normal form
//       (ands outside ors)
//    3. Inspect the ands for things where we can fetch subsets of **
//       via an index.
//    4. Choose one such thing, if any. (If not, give up.)
//    5. Convert the loop to
//         FOR x IN (lookup(index, v1) ++ lookup(index, v2) ++ ...)
//         SUCHTHAT p DO x
//       (do not reduce p - it's not worthwhile for various reasons,
//       and if some of the indexes are approximate it may not be
//       correct either.)

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ptrarray.h"
#include "primarray.h"
#include "dump.h"
#include "main.h"
#include "ast.h"

////////////////////////////////////////////////////////////

// info remembered when matching for indexes

struct planinfo {
   whichindex index;
   expr *target;
   ops op;
};

static expr *lookup_values(expr *target, whichindex ix, ops foldwith) {
   if (target->type == ET_TUPLE) {
      expr *result = NULL;
      for (int i=0; i<target->tuple.nexprs; i++) {
	 expr *val = expr_clone(target->tuple.exprs[i]);
	 expr *lookup = expr_op(OP_LOOKUP, expr_val(value_index(ix)), val);
	 result = result ? expr_op(foldwith, result, lookup) : lookup;
      }
      return result;
   }
   if (target->type == ET_VAL && target->val->type == VT_TUPLE) {
      expr *result = NULL;
      for (int i=0; i<target->val->tupleval.nvals; i++) {
	 expr *val = expr_val(value_clone(target->val->tupleval.vals[i]));
	 expr *lookup = expr_op(OP_LOOKUP, expr_val(value_index(ix)), val);
	 result = result ? expr_op(foldwith, result, lookup) : lookup;
      }
      return result;
   }
   return expr_op(OP_LOOKUP, expr_val(value_index(ix)), expr_clone(target));
}

static expr *compile_plan(const planinfo *p) {
   // not quite as simple as above, because the index might return
   // a set. so we'll need a FOR.

   ops op = p->op;

   expr *lookup;

   if (op == OP_EQ && p->index == INDEX_ID) {
      // very easy case (don't need the for, either)
      return expr_clone(p->target);
   }
   else if (op == OP_EQ) {
      // easy case
      expr *val = expr_clone(p->target);
      lookup = expr_op(OP_LOOKUP, expr_val(value_index(p->index)), val);
   }
#if 0
   else if (op == OP_SUBSET || op == OP_PROPERSUBSET) {
      assert(p->index == INDEX_ARGV);
      lookup = lookup_values(p->target, p->index, OP_UNION);
   }
   else if (op == OP_SUPERSET || op == OP_PROPERSUPERSET) {
      assert(p->index == INDEX_ARGV);
      lookup = lookup_values(p->target, p->index, OP_INTERSECT);
   }
#endif
   else if (op == OP_CONTAINS) {
      assert(p->index == INDEX_ARGV);
      lookup = lookup_values(p->target, p->index, OP_INTERSECT);
   }
   else {
      assert(0);
      lookup = NULL;
   }

   var *v = var_create();
   expr *forx = expr_for(v, lookup, expr_val(value_int(1)), expr_ref(v));
   return forx;
}

// do the conversion in step 5 above.
// if something breaks, just return the original expression to
// skip the optimization.

static expr *apply_plan(expr *e, primarray<planinfo> &plan) {
   expr *pe = NULL;

   for (int i=0; i<plan.num(); i++) {
      expr *tmp = compile_plan(&plan[i]);
      if (!tmp) {
	 if (pe) {
	    expr_destroy(pe);
	 }
	 return e;
      }
      if (pe) {
	 pe = expr_op(OP_STRCAT, pe, tmp);
      }
      else {
	 pe = tmp;
      }
   }

   expr_destroy(e->bind.bind);
   e->bind.bind = pe;
   return e;
}

////////////////////////////////////////////////////////////

enum letypes {
   L_AND,
   L_OR,
   //L_XOR,
   L_NOT,
   L_LEAF,
};

struct logexpr {
   letypes type;
   logexpr *left;
   logexpr *right;
   int leafnum;
};

////////////////////////////////////////////////////////////

static logexpr *logexpr_create(letypes t, logexpr *l, logexpr *r, int k) {
   logexpr *le = new logexpr;
   le->type = t;
   le->left = l;
   le->right = r;
   le->leafnum = k;
   return le;
}

static void logexpr_destroy(logexpr *le) {
   if (le) {
      logexpr_destroy(le->left);
      logexpr_destroy(le->right);
      delete le;
   }
}

static logexpr *le_clone(logexpr *le) {
   if (!le) {
      return NULL;
   }
   return logexpr_create(le->type, le_clone(le->left), le_clone(le->right),
			 le->leafnum);
}

static logexpr *le_leaf(int k) {
   return logexpr_create(L_LEAF, NULL, NULL, k);
}

static logexpr *le_not(logexpr *l) {
   return logexpr_create(L_NOT, l, NULL, 0);
}

static logexpr *le_bop(letypes t, logexpr *l, logexpr *r) {
   return logexpr_create(t, l, r, 0);
}

//static logexpr *le_and(logexpr *l, logexpr *r) { return le_bop(L_AND, l, r);}
static logexpr *le_or(logexpr *l, logexpr *r)  { return le_bop(L_OR,  l, r); }
//static logexpr *le_xor(logexpr *l, logexpr *r) { return le_bop(L_XOR, l, r);}

static void logexpr_dump_rec(logexpr *e) {
   switch (e->type) {
    case L_AND:
     dump("&&\n");
     dump_indent();
     logexpr_dump_rec(e->left);
     logexpr_dump_rec(e->right);
     dump_unindent();
     break;

    case L_OR:
     dump("||\n");
     dump_indent();
     logexpr_dump_rec(e->left);
     logexpr_dump_rec(e->right);
     dump_unindent();
     break;

#if 0
    case L_XOR:
     dump("^^\n");
     dump_indent();
     logexpr_dump_rec(e->left);
     logexpr_dump_rec(e->right);
     dump_unindent();
     break;
#endif

    case L_NOT:
     dump("! ");
     logexpr_dump_rec(e->left);
     assert(e->right==NULL);
     break;

    case L_LEAF:
     assert(e->left==NULL && e->right==NULL);
     dump("leaf %d\n", e->leafnum);
     break;

   }
}

static void logexpr_dump(logexpr *e) {
   dump_begin();
   logexpr_dump_rec(e);
   dump_end();
}

static logexpr *build(ptrarray<expr> &leaves, expr *e) {
   assert(e != NULL);
   if (e->type != ET_OP || !islogicalop(e->op.op)) {
      int k = leaves.add(e);
      return le_leaf(k);
   }
   if (e->op.op == OP_LOGNOT) {
      return le_not(build(leaves, e->op.left));
   }
   letypes t;
   switch (e->op.op) {
    case OP_LOGAND: t = L_AND; break;
    case OP_LOGOR:  t = L_OR; break;
#if 0
    case OP_LOGXOR: t = L_XOR; break;
#endif
    default:
     assert(0);
     return NULL;
   }
   return le_bop(t, build(leaves, e->op.left), build(leaves, e->op.right));
}

////////////////////////////////////////////////////////////

static logexpr *dnfify(logexpr *e) {
   if (!e) {
      return NULL;
   }
   while (1) {
      e->left = dnfify(e->left);
      e->right = dnfify(e->right);

      if (e->type == L_LEAF) {
	 break;
      }

#if 0
      // a XOR b  ==>  (a OR b) AND (!a OR !b)
      if (e->type == L_XOR) {
	 logexpr *a = e->left;
	 logexpr *b = e->right;

	 logexpr *lhs = le_or(le_clone(a), le_clone(b));
	 logexpr *rhs = le_or(le_not(a), le_not(b));

	 // make e the LHS
	 e->type = L_AND;
	 e->left = lhs;
	 e->right = rhs;
	 continue;
      }
#endif

      // !(a OR b)  ==>  !a AND !b
      if (e->type == L_NOT && e->left->type == L_OR) {
	 logexpr *a = e->left->left;
	 logexpr *b = e->left->right;
	 e->left->type = L_NOT;
	 e->left->left = a;
	 e->left->right = NULL;
	 e->right = le_not(b);
	 e->type = L_AND;
	 continue;
      }

      // !(a AND b)  ==>  !a OR !b
      if (e->type == L_NOT && e->left->type == L_AND) {
	 logexpr *a = e->left->left;
	 logexpr *b = e->left->right;
	 e->left->type = L_NOT;
	 e->left->left = a;
	 e->left->right = NULL;
	 e->right = le_not(b);
	 e->type = L_OR;
	 continue;
      }

      // !!a  ==>  a
      if (e->type == L_NOT && e->left->type == L_NOT) {
	 logexpr *a = e->left->left;
	 e->left->left = NULL;
	 logexpr_destroy(e);
	 e = a;
	 continue;
      }

      // (a AND b) OR c  ==> (a OR c) AND (b OR c)
      if (e->type == L_OR && e->left->type == L_AND) {
	 logexpr *a = e->left->left;
	 logexpr *b = e->left->right;
	 logexpr *c = e->right;
	 e->left->left = a;
	 e->left->right = le_clone(c);
	 e->right = le_or(b, c);
	 e->type = L_AND;
	 continue;
      }

      // a OR (b AND c)  ==> (a OR b) AND (a OR c)
      if (e->type == L_OR && e->right->type == L_AND) {
	 logexpr *a = e->left;
	 logexpr *b = e->right->left;
	 logexpr *c = e->right->right;
	 e->left = le_or(a, b);
	 e->right->left = le_clone(a);
	 e->right->right = c;
	 e->type = L_AND;
	 continue;
      }

      // er, this isn't true if the expr is just a || b
      //assert(e->type == L_AND);
      break;
   }
   return e;
}

static void extract_ands(logexpr *le, ptrarray<logexpr> &fill) {
   if (le->type != L_AND) {
      fill.add(le);
      return;
   }
   extract_ands(le->left, fill);
   extract_ands(le->right, fill);
}

static void extract_ors(logexpr *le, ptrarray<logexpr> &fill) {
   if (le->type != L_OR) {
      fill.add(le);
      return;
   }
   extract_ors(le->left, fill);
   extract_ors(le->right, fill);
}

// true if value can be evaluated at runtime
static int computable(expr *e) {
   return e != NULL;
}

//
// Check a leaf expression for use-of-index quality.
// If HASNOT, the expression is logically negated.
// REF is the loop induction variable, so we're interested
// mostly in fields referenced off REF.
//
// Things we can handle that are common enough to be worth coding in:
//    REF.pnode == x
//    REF.inode == x
//    REF.name == x
//    REF.argv == x
//    REF.argv >> x
//    REF.argv >>= x
//    REF.argv << x
//    REF.argv <<= x
//
// plus their logically flipped equivalents.
//

static expr *get_reffield(var *ref, expr *e) {
   if (e->type == ET_OP && e->op.op == OP_FIELD &&
       e->op.left->type == ET_REF && e->op.left->ref->id == ref->id) {
      return e->op.right;
   }
   return NULL;
}

static int score_leaf(var *ref, expr *e, int hasnot, planinfo *info_ret) {
   if (e->type != ET_OP) {
      // Hopeless.
      if (g_dodumps) {
	 printf("score_leaf: leaf is not an operator\n");
      }
      return 0;
   }

   ops op = e->op.op;
   if (hasnot) {
      op = op_logical_flip(op);
      if (op == OP_NOP) {
	 // no good
	 if (g_dodumps) {
	    printf("score_leaf: logical_flip of leaf failed\n");
	 }
	 return 0;
      }
   }

   expr *field;
   expr *target;
   int fieldonleft;
   field = get_reffield(ref, e->op.left);
   if (field) {
      target = e->op.right;
      fieldonleft = 1;
   }
   else {
      field = get_reffield(ref, e->op.right);
      target = e->op.left;
      fieldonleft = 0;
   }
   if (!field) {
      if (g_dodumps) {
	 printf("score_leaf: no object field found\n");
      }
      return 0;
   }
   if (!computable(target)) {
      if (g_dodumps) {
	 printf("score_leaf: target not computable\n");
      }
      return 0;
   }

   assert(field->type == ET_VAL && field->val->type == VT_STRING);

   if (fieldonleft) {
      op = op_leftright_flip(op);
      if (op == OP_NOP) {
	 if (g_dodumps) {
	    printf("score_leaf: leftright flip failed\n");
	 }
	 return 0;
      }
   }

   info_ret->op = op;
   info_ret->target = target;

   if (op == OP_EQ) {
      if (!strcmp(field->val->strval, "PNODE")) {
	 info_ret->index = INDEX_ID;
	 /*
	  * Target should be an integer. Promote it to a pnode number
	  * so we can use the identity index.
	  */
	 if (target->type != ET_VAL && target->val->type != VT_INT) {
	    if (g_dodumps) {
	       printf("score_leaf: pnode compared to non-integer\n");
	    }
	    return 0;
	 }

	 expr *nt = expr_val(value_pnode(target->val->intval));

	 // Can't do this - it belongs to the parse tree. Which means
	 // we're going to leak a value here. Suck. XXX
	 //expr_destroy(target);

	 info_ret->target = target = nt;
	 return 100;
      }
      if (!strcmp(field->val->strval, "INODE")) {
	 info_ret->index = INDEX_I2P;
	 return 80;
      }
      // disabled 6/1/07 because it doesn't work, for no clear reason
      //if (!strcmp(field->val->strval, "NAME")) {
      //   info_ret->index = INDEX_NAME;
      //   return 80;
      //}
      if (!strcmp(field->val->strval, "ARGV")) {
	 info_ret->index = INDEX_ARGV;
	 return 60;
      }
      if (g_dodumps) {
	 printf("score_leaf: field %s has no EQ index\n", field->val->strval);
      }
      return 0;
   }

   if (op == OP_CONTAINS
#if 0
       || op == OP_SUBSET
       || op == OP_PROPERSUBSET
       || op == OP_SUPERSET
       || op == OP_PROPERSUPERSET
#endif
       ) {
      if (!strcmp(field->val->strval, "ARGV")) {
	 info_ret->index = INDEX_ARGV;
	 return 40;
      }
   }

   if (g_dodumps) {
      printf("score_leaf: expression not matched\n");
   }

   return 0;
}


//
// Score a logical expression for use-of-index quality. It may be a
// conjunction (ORs) but will have no ANDs. NOTs will be at the bottom.
// The scale is (arbitrarily) 1-100. A score of 0 indicates scorn,
// that is, an expression that cannot be used.
//
static int score(var *ref, ptrarray<expr> &leaves, logexpr *le,
		 primarray<planinfo> &plan) {
   // Find all the or branches.
   ptrarray<logexpr> ors;
   extract_ors(le, ors);

   assert(ors.num() > 0);

   primarray<int> scores;
   scores.setsize(ors.num());
   plan.setsize(ors.num());

   if (g_dodumps) {
      printf("%d OR branches\n", ors.num());
   }

   for (int i=0; i<ors.num(); i++) {
      int hasnot = 0;
      logexpr *leaf = ors[i];
      if (leaf->type == L_NOT) {
	 hasnot = 1;
	 leaf = leaf->left;
      }
      assert(leaf->type == L_LEAF);
      expr *e = leaves[leaf->leafnum];

      if (g_dodumps) {
	 printf("OR branch %d:\n", i);
	 ast_dump(e);
      }

      scores[i] = score_leaf(ref, e, hasnot, &plan[i]);

      if (g_dodumps) {
	 printf("OR branch %d: score %d\n", i, scores[i]);
      }

      if (scores[i] == 0) {
	 // if anything in an OR requires a full scan, the works is no good
	 return 0;
      }
   }

   int overall_score = 0;
   for (int i=0; i<scores.num(); i++) {
      overall_score += scores[i];
   }
   overall_score /= scores.num();

   // voodoo
   overall_score += scores.num();

   return overall_score;
}

static expr *contemplate(expr *e) {
   // Given: e is a FOR whose SUCHTHAT clause is a logical operator,
   // or a leaf of a logical expression,
   // and whose BIND value is **.

   if (g_dodumps) {
      dump_begin();
      dump("indexify: contemplating this:\n");
      ast_dump(e);
   }

   // This is the induction variable.
   var *ref = e->bind.ref;

   // Build a lightweight copy of the logical expression, making a
   // table of the leaves and referring to them in the copy by number.
   ptrarray<expr> leaves;
   logexpr *le = build(leaves, e->bind.suchthat);

   if (g_dodumps) {
      dump("logical expression is:\n");
      logexpr_dump(le);
   }

   // convert le into disjunctive normal form (ands outside ors)
   le = dnfify(le);

   if (g_dodumps) {
      dump("normalized logical expression is:\n");
      logexpr_dump(le);
   }

   // extract all the AND alternatives
   ptrarray<logexpr> ands;
   extract_ands(le, ands);

   if (g_dodumps) {
      dump("%d AND branches\n", ands.num());
   }

   // score each and choose the best
   int bestnum = -1;
   int bestscore = 0;
   primarray<planinfo> bestplan;
   primarray<planinfo> plan;
   for (int i=0; i<ands.num(); i++) {
      plan.setsize(0);

      if (g_dodumps) {
	 dump("AND branch %d:\n", i);
	 logexpr_dump(ands[i]);
	 dump("Scoring AND branch %d:\n", i);
	 dump_indent();
      }

      int myscore = score(ref, leaves, ands[i], plan);

      if (g_dodumps) {
	 dump_unindent();
	 printf("AND branch %d: score %d\n", i, myscore);
      }

      if (myscore > bestscore) {
	 bestscore = myscore;
	 bestnum = i;
	 bestplan.setsize(plan.num());
	 for (int j=0; j<plan.num(); j++) {
	    bestplan[j] = plan[j];
	 }
      }
   }

   if (bestscore == 0) {
      // ...shyeah!
      logexpr_destroy(le);
      if (g_dodumps) {
	 dump("bestscore == 0. indexify punted\n");
	 dump_end();
      }
      return e;
   }

   if (g_dodumps) {
      dump("apply_plan:\n");
   }

   e = apply_plan(e, bestplan);

   if (g_dodumps) {
      dump("indexify: done contemplating\n");
      dump_end();
   }

   return e;
}

expr *indexify(expr *e) {
   if (!e) {
      return NULL;
   }
   switch (e->type) {
    case ET_FOR:
     e->bind.bind = indexify(e->bind.bind);
     e->bind.suchthat = indexify(e->bind.suchthat);
     e->bind.in = indexify(e->bind.in);
     if (e->bind.bind->type == ET_VAL && e->bind.bind->val->type == VT_ALL
	 && e->bind.suchthat->type == ET_OP) {
	e = contemplate(e);
     }
     return e;
    case ET_LET:
    case ET_LAMBDA:
     e->bind.bind = indexify(e->bind.bind);
     e->bind.suchthat = indexify(e->bind.suchthat);
     e->bind.in = indexify(e->bind.in);
     return e;
    case ET_COND:
     e->cond.test = indexify(e->cond.test);
     e->cond.yes = indexify(e->cond.yes);
     e->cond.no = indexify(e->cond.no);
     return e;
    case ET_OP:
     e->op.left = indexify(e->op.left);
     e->op.right = indexify(e->op.right);
     return e;
    case ET_TUPLE:
     for (int i=0; i<e->tuple.nexprs; i++) {
	e->tuple.exprs[i] = indexify(e->tuple.exprs[i]);
     }
     return e;
    case ET_REF:
    case ET_VAL:
     /* nothing doing */
     break;
   }
   return e;
}
