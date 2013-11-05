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

#include <stdio.h> // XXX temporary
#include <string.h>
#include "utils.h"
#include "dump.h"
#include "ast.h"
#include "dbops.h"
#include "main.h"

/*
 * Eval a path expression.
 *
 * (1) Collect the whole path.
 * (2) Pick which end to start from.
 * (3) Compile it into regexp states.
 * (4) Eval at one end to get starting hypotheses.
 * (5) For every active hypothesis, transit and compare what we have
 *     to the expression at that point.
 * (6) If we're at the result state, add a copy to the result list.
 * (7) If we run out of hypotheses, stop.
 */

enum pathdirs { PATH_L_TO_R, PATH_R_TO_L };
enum matchmodes { MATCH_ONE, MATCH_OPT, MATCH_STAR, MATCH_PLUS };

struct pathentry {
   matchmodes how;
   expr *e;
   int isresult;
};

struct state {
   int id;
   pathentry *match;
   ptrarray<state> nextstates;
   int isaccepting;
};

struct hypothesis {
   unsigned refcount;
   hypothesis *prev;
   value *v;
   state *s;
};

static pathdirs dir;
static value *results;
static ptrarray<hypothesis> hypotheses;
static ptrarray<hypothesis> newhypotheses;
static ptrarray<state> allstates;

////////////////////////////////////////////////////////////

static void dumpstateshort(state *s) {
   assert(s);
   dump(" %d", s->id);
}

static void dumpstate(state *s) {
   assert(s);
   dump("state %d", s->id);
   if (s->match->isresult) {
      dump(" [result-value]");
   }
   switch (s->match->how) {
    case MATCH_ONE: dump(" one"); break;
    case MATCH_OPT: dump(" zero/one"); break;
    case MATCH_STAR: dump(" zero-or-more"); break;
    case MATCH_PLUS: dump(" one-or-more"); break;
   }
   ast_dump(s->match->e);
   dump("nextstates:");
   for (int j=0; j<s->nextstates.num(); j++) {
      dumpstateshort(s->nextstates[j]);
   }
   if (s->isaccepting) {
      dump(" ACCEPT");
   }
}

////////////////////////////////////////////////////////////

// return true if PV (which is a pnode) matches the expression E.
// E may be "**" or "for x in ** where pred x" - otherwise we 
// just eval it.
static bool matches_expr(value *pv, expr *e) {
   if (e->type == ET_VAL && e->val->type == VT_ALL) {
      return true;
   }
   if (e->type == ET_FOR 
       && e->bind.bind->type == ET_VAL && e->bind.bind->val->type == VT_ALL
       && e->bind.in->type == ET_REF
       && e->bind.in->ref->id == e->bind.ref->id) {
      var *ref = e->bind.ref;
      ref->val = pv;
      value *ok = eval(e->bind.suchthat);
      ref->val = NULL;
      if (ok && value_istrue(ok)) {
	 value_destroy(ok);
	 return true;
      }
      if (ok) {
	 value_destroy(ok);
      }
      return false;
   }

   // not one of the special forms, so eval e and see if we can find pv in it.
   value *stuff = eval(e);
   if (!stuff) {
      return false;
   }
   if (stuff->type == VT_ALL || value_eq(pv, stuff)) {
      value_destroy(stuff);
      return true;
   }
   if (stuff->type == VT_LIST) {
      for (int i=0; i<stuff->listval->members.num(); i++) {
	 value *candidate = stuff->listval->members[i];
	 if (value_eq(pv, candidate)) {
	    value_destroy(stuff);
	    return true;
	 }
      }
   }
   value_destroy(stuff);

   return false;
}

////////////////////////////////////////////////////////////

static bool is_all_val(value *v) {
   return v->type == VT_ALL;
}

static bool is_all_expr(expr *e) {
   if (e->type == ET_VAL && is_all_val(e->val)) {
      return true;
   }
   if (e->type == ET_REF && e->ref->val && is_all_val(e->ref->val)) {
      return true;
   }
   if (e->type == ET_FOR
       && e->bind.bind->type == ET_VAL && e->bind.bind->val->type == VT_ALL
       && e->bind.suchthat->type == ET_VAL 
       && value_istrue(e->bind.suchthat->val)) {
      return true;
   }
   if (e->type == ET_OP
       && e->op.op == OP_EXTRACT && is_all_expr(e->op.left)) {
      return true;
   }
   return false;
}

////////////////////////////////////////////////////////////

static hypothesis *hypothesis_create(hypothesis *prev, value *v, state *s) {
   hypothesis *h = new hypothesis;
   h->refcount = 1;
   h->prev = prev;
   if (prev) {
      prev->refcount++;
   }
   h->v = v;
   h->s = s;
   return h;
}

static void decref(hypothesis *h) {
   assert(h->refcount>0);
   h->refcount--;
   if (h->refcount == 0) {
      value_destroy(h->v);
      delete h;
   }
}

static void extract_result(hypothesis *h, value *last) {
   ptrarray<value> hvalues;
   if (h->s->match->isresult) {
      assert(last);
      hvalues.add(last);
   }
   while (h->prev) {
      if (h->prev->s->match->isresult) {
	 assert(h->v);
	 hvalues.add(h->v);
      }
      h = h->prev;
   }

   int n = hvalues.num();
   if (n > 1) {
      value **vals = new value* [n];
      for (int i=0; i<n; i++) {
	 vals[i] = value_clone(hvalues[i]);
      }
      value *tuple = value_tuple(vals, n);
      if (g_dotrace) {
	 dump("accept; result is ");
	 value_dump(tuple);
	 dump("\n");
      }
      valuelist_add(results->listval, tuple);
   }
   else if (n==1) {
      if (g_dotrace) {
	 dump("accept; result is ");
	 value_dump(hvalues[0]);
	 dump("\n");
      }
      valuelist_add(results->listval, value_clone(hvalues[0]));
   }
   else {
      // nothing (?)
      if (g_dotrace) {
	 dump("accept; empty result discarded\n");
      }
   }
}

////////////////////////////////////////////////////////////

static void eval_to_pnodes(expr *e, ptrarray<value> &fill) {
   value *base = eval(e);
   if (!base) {
      return;
   }
 again:
   if (base->type == VT_LIST) {
      for (int i=0; i<base->listval->members.num(); i++) {
	 value *vv = base->listval->members[i];
	 if (vv->type != VT_PNODE) {
	    whine(nowhere, "Invalid object type in path");
	 }
	 fill.add(vv);
      }
      base->listval->members.setsize(0);
      value_destroy(base);
   }
   else if (base->type == VT_PNODE) {
      fill.add(base);
   }
   else if (base->type == VT_ALL) {
      // Eesh.
      var *ref = var_create();
      expr *where = expr_val(value_int(1));
      expr *in = expr_ref(ref);
      value_destroy(base);
      base = db_evalallprov(ref, where, in);
      assert(base);
      var_destroy(ref);
      expr_destroy(where);
      expr_destroy(in);
      goto again;
   }
   else if (base->type == VT_LAMBDA) {
      var *ref = var_create();
      expr *in = expr_ref(ref);
      value *newbase = db_evalallprov(ref, base->lambdaval.e, in);
      assert(newbase);
      value_destroy(base);
      base = newbase;
      var_destroy(ref);
      expr_destroy(in);
      goto again;
   }
   else {
      whine(nowhere, "Invalid object type in path");
   }
}

////////////////////////////////////////////////////////////

static void run_one(hypothesis *h) {
   assert(h->v);
   assert(h->v->type == VT_PNODE);

   value *next;
   if (dir == PATH_R_TO_L) {
      next = db_get_parents(h->v->pnodeval);
   }
   else {
      next = db_get_children(h->v->pnodeval);
   }

   assert(next->type == VT_LIST);
   if (next->listval->members.num() == 0) {
      value_destroy(next);
      if (g_dotrace) {
	 dump("no successors; pruned\n");
      }
      return;
   }

   if (g_dotrace) {
      dump("%d successors\n", next->listval->members.num());
   }

   assert(next->listval->membertype == VT_PNODE);
   ptrarray<value> nextvals;
   for (int i=0; i<next->listval->members.num(); i++) {
      expr *e = h->s->match->e;
      value *pv = next->listval->members[i];
      if (g_dotrace) {
	 dump("checking: ");
	 value_dump(pv);
	 dump("\nagainst: ");
	 ast_dump(e);
	 dump("\n(state %d)\n", h->s->id);
      }
      if (matches_expr(pv, e)) {
	 if (g_dotrace) {
	    dump(" yes\n");
	 }
	 nextvals.add(pv);
	 next->listval->members[i] = NULL;
      }
      else {
	 if (g_dotrace) {
	    dump(" no\n");
	 }
      }
   }
   value_destroy(next);

   if (nextvals.num()==0) {
      if (g_dotrace) {
	 dump("no matching successors - pruned\n");
      }
      return;
   }

   for (int i=0; i<nextvals.num(); i++) {
      if (h->s->isaccepting) {
	 extract_result(h, nextvals[i]);
      }

      for (int j=0; j<h->s->nextstates.num(); j++) {
	 hypothesis *nh = hypothesis_create(h, value_clone(nextvals[i]), 
					       h->s->nextstates[j]);
	 int nx = newhypotheses.add(nh);
	 if (g_dotrace) {
	    dump("new hypothesis %d (state %d)\n", nx, nh->s->id);
	 }
      }
      value_destroy(nextvals[i]);
   }
}

static void run(void) {
   while (hypotheses.num() > 0) {
      if (g_dotrace) {
	 dump("run: new step (%d live hypotheses)\n", hypotheses.num());
      }
      for (int i=0; i<hypotheses.num(); i++) {
	 if (g_dotrace) {
	    dump("run: hypothesis %d (state %d) value ", i, 
		 hypotheses[i]->s->id);
	    value_dump(hypotheses[i]->v);
	    dump("\n");
	    dump_indent();
	 }
	 run_one(hypotheses[i]);
	 decref(hypotheses[i]);
	 if (g_dotrace) {
	    dump_unindent();
	 }
      }
      hypotheses.setsize(newhypotheses.num());
      for (int i=0; i<hypotheses.num(); i++) {
	 hypotheses[i] = newhypotheses[i];
      }
      newhypotheses.setsize(0);
   }
}

////////////////////////////////////////////////////////////

static void startup(ptrarray<state> &startstates) {
   for (int i=0; i<startstates.num(); i++) {
      if (g_dotrace) {
	 dump("initializing start state %d:\n", i);
	 dump_indent();
      }
      ptrarray<value> vals;
      eval_to_pnodes(startstates[i]->match->e, vals);
      if (g_dotrace) {
	 dump_unindent();
	 dump("seeding %d hypotheses\n", vals.num());
      }
      for (int j=0; j<vals.num(); j++) {
	 hypothesis *seed = hypothesis_create(NULL, NULL, startstates[i]);
	 for (int k=0; k<startstates[i]->nextstates.num(); k++) {
	    hypotheses.add(hypothesis_create(seed, value_clone(vals[j]),
					     startstates[i]->nextstates[k]));
	 }
	 value_destroy(vals[j]);
      }
   }
}

////////////////////////////////////////////////////////////

static state *state_create(pathentry *p) {
   state *s = new state;
   s->match = p;
   s->id = allstates.add(s);
   s->isaccepting = 0;
   return s;
}

static void state_destroy(state *s) {
   delete s;
}

static void compilepath(ptrarray<pathentry> &path, ptrarray<state> &starts) {
   // we have N states S_i.
   // each means we're expecting an object that matches path[i].
   // and we can go to several next states.

   ptrarray<state> states;
   states.setsize(path.num());
   for (int i=0; i<path.num(); i++) {
      states[i] = state_create(path[i]);
   }

   for (int i=0; i<path.num(); i++) {
      // in states[i] we always go to states[i+1].
      if (i<path.num()-1) {
	 states[i]->nextstates.add(states[i+1]);
      }
      else {
	 states[i]->isaccepting = true;
      }

      // in states[i], if path[i] is repeatable, we also loop back to
      // ourselves.
      if (path[i]->how == MATCH_PLUS || path[i]->how == MATCH_STAR) {
	 states[i]->nextstates.add(states[i]);
      }

      // in states[i], for all immediately succeeding optional path[j],
      // we also jump past states[j].
      for (int j=i+1; j<path.num(); j++) {
	 if (path[j]->how == MATCH_STAR || path[j]->how == MATCH_OPT) {
	    if (j<path.num()-1) {
	       states[i]->nextstates.add(states[j+1]);
	    }
	    else {
	       states[i]->isaccepting = true;
	    }
	 }
	 else {
	    break;
	 }
      }
   }

   starts.add(states[0]);
   for (int j=0; j<path.num(); j++) {
      if (path[j]->how == MATCH_STAR || path[j]->how == MATCH_OPT) {
	 if (j<path.num()-1) {
	    starts.add(states[j+1]);
	 }
	 else {
	    // nothing - immediate accept is vacuous
	 }
      }
      else {
	 break;
      }
   }
}

////////////////////////////////////////////////////////////

static bool ispathop(expr *e) {
   return e->type == ET_OP &&
      (e->op.op == OP_PATH ||
       e->op.op == OP_LONGPATHZ ||
       e->op.op == OP_LONGPATHNZ);
}

static pathentry *makepathentry(expr *e) {
   pathentry *pe = new pathentry;
   pe->how = MATCH_ONE;
   pe->isresult = 0;

   if (e->type == ET_OP && e->op.op == OP_OPTIONAL
       && e->op.left->type == ET_OP && e->op.left->op.op == OP_REPEAT) {
      pe->how = MATCH_STAR;
      e = e->op.left->op.left;
   }
   else if (e->type == ET_OP && e->op.op == OP_OPTIONAL) {
      pe->how = MATCH_OPT;
      e = e ->op.left;
   }
   else if (e->type == ET_OP && e->op.op == OP_REPEAT) {
      pe->how = MATCH_PLUS;
      e = e->op.left;
   }
   else if (e->type == ET_OP && e->op.op == OP_EXTRACT) {
      pe->isresult = 1;
      e = e->op.left;
   }

   pe->e = e;

   return pe;
}

static void collectpath(expr *e, ptrarray<pathentry> &path) {
   assert(ispathop(e));

   // left first
   if (ispathop(e->op.left)) {
      collectpath(e->op.left, path);
   }
   else {
      path.add(makepathentry(e->op.left));
   }

   // middle
   if (e->op.op == OP_LONGPATHZ) {
      pathentry *pe = new pathentry;
      pe->how = MATCH_STAR;
      pe->e = expr_val(value_all());
      pe->isresult = 0;
      path.add(pe);
   }
   else if (e->op.op == OP_LONGPATHNZ) {
      pathentry *pe = new pathentry;
      pe->how = MATCH_PLUS;
      pe->e = expr_val(value_all());
      pe->isresult = 0;
      path.add(pe);
   }

   // right
   if (ispathop(e->op.right)) {
      collectpath(e->op.right, path);
   }
   else {
      path.add(makepathentry(e->op.right));
   }

   // don't do this! we're evaling, not compiling, and we might need to 
   // be able to rerun this path later.
   //e->op.left = NULL;
   //e->op.right = NULL;
   //expr_destroy(e);
}

static void reversepath(ptrarray<pathentry> &path) {
   int n = path.num();
   for (int i=0; i<n/2; i++) {
      pathentry *t = path[i];
      path[i] = path[n-i-1];
      path[n-i-1] = t;
   }
}

static void reverseoneresult(value *val) {
   if (val->type == VT_PNODE) {
      return;
   }
   assert(val->type == VT_TUPLE);

   int n = val->tupleval.nvals;
   for (int i=0; i<n/2; i++) {
      value *t = val->tupleval.vals[i];
      val->tupleval.vals[i] = val->tupleval.vals[n-i-1];
      val->tupleval.vals[n-i-1] = t;
   }
}

static void reverseresults(value *val) {
   assert(val->type == VT_LIST);
   for (int i=0; i<val->listval->members.num(); i++) {
      reverseoneresult(val->listval->members[i]);
   }
}

////////////////////////////////////////////////////////////

value *eval_path(expr *e) {
   ptrarray<pathentry> path;
   collectpath(e, path);
   assert(path.num() > 1);

   if (g_dotrace) {
      dump("eval_path: %d components\n", path.num());
      dump_indent();
   }

   // Choose which end to start from. For now, just pick the end
   // that isn't ALL.
   pathentry *leftend = path[0];
   pathentry *rightend = path[path.num()-1];
   if (is_all_expr(rightend->e)) {
      dir = PATH_L_TO_R;
   }
   else if (is_all_expr(leftend->e)) {
      dir = PATH_R_TO_L;
   }
   else {
      // should estimate selectivity of each side or something, but...
      dir = PATH_L_TO_R;
   }

   //dir = PATH_R_TO_L;

   if (dir == PATH_R_TO_L) {
      if (g_dotrace) {
	 dump("eval_path: flipping path\n", path.num());
      }
      reversepath(path);
   }

   ptrarray<state> startstates;
   compilepath(path, startstates);

   if (g_dotrace) {
      dump_unindent();
      dump("eval_path: state machine:\n");
      dump_indent();
      for (int i=0; i<allstates.num(); i++) {
	 dumpstate(allstates[i]);
	 dump("\n");
      }
      dump_unindent();
      dump("eval_path: end of state machine\n");
      dump("eval_path: startstates are:");
      for (int i=0; i<startstates.num(); i++) {
	 dumpstateshort(startstates[i]);
      }
      dump("\n");
      dump("eval_path: startup\n");
      dump_indent();
   }

   results = value_list();
   startup(startstates);

   if (g_dotrace) {
      dump_unindent();
      dump("eval_path: run\n");
      dump_indent();
   }

   run();
   value *ret = results;

   results = NULL;
   assert(hypotheses.num()==0);
   assert(newhypotheses.num()==0);
   startstates.setsize(0);
   for (int i=0; i<allstates.num(); i++) {
      state_destroy(allstates[i]);
   }
   allstates.setsize(0);
   for (int i=0; i<path.num(); i++) {
      delete path[i];
   }
   path.setsize(0);

   if (dir == PATH_L_TO_R) {
      reverseresults(ret);
   }

   if (g_dotrace) {
      dump_unindent();
      dump("eval_path: done\n");
      dump("result: ");
      value_dump(ret);
      dump("\n");
   }

   return ret;
}
