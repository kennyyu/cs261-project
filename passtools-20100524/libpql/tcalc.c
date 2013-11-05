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

#include <stdio.h> // for snprintf
#include "utils.h"
#include "datatype.h"
#include "columns.h"
#include "layout.h"
#include "pqlvalue.h"
#include "pqlcontext.h"

#define TCALC_INLINE
#include "tcalc.h"

////////////////////////////////////////////////////////////
// constructors

struct tcglobal *mktcglobal(struct pqlcontext *pql, const char *name) {
   struct tcglobal *tcg;

   (void)pql; // not currently needed

   tcg = domalloc(pql, sizeof(*tcg));
   tcg->name = dostrdup(pql, name);
   tcg->refcount = 1;

   return tcg;
}

void tcglobal_incref(struct tcglobal *tcg) {
   PQLASSERT(tcg != NULL);
   tcg->refcount++;
}

//////////////////////////////

struct tcvar *mktcvar_fresh(struct pqlcontext *pql) {
   struct tcvar *var;
   
   var = domalloc(pql, sizeof(*var));
   var->id = pql->nextvarid++;
   var->refcount = 1;
   var->datatype = NULL;
   var->colnames = NULL;

   return var;
}

void tcvar_incref(struct tcvar *var) {
   PQLASSERT(var != NULL);
   var->refcount++;
}

//////////////////////////////

static struct tcexpr *mktcexpr(struct pqlcontext *pql, enum tcexprtypes type) {
   struct tcexpr *te;

   (void)pql; // not currently required

   te = domalloc(pql, sizeof(*te));
   te->colnames = NULL;
   te->datatype = NULL;
   te->type = type;

   return te;
}

//////////

struct tcexpr *mktcexpr_filter(struct pqlcontext *pql,
			       struct tcexpr *sub, struct tcexpr *predicate) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_FILTER);
   te->filter.sub = sub;
   te->filter.predicate = predicate;

   return te;
}

static struct tcexpr *mktcexpr_project_base(struct pqlcontext *pql,
					    struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_PROJECT);
   te->project.sub = sub;
   te->project.cols = NULL;

   return te;
}

struct tcexpr *mktcexpr_project_none(struct pqlcontext *pql,
				     struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr_project_base(pql, sub);
   te->project.cols = colset_empty(pql);

   return te;
}

struct tcexpr *mktcexpr_project_one(struct pqlcontext *pql,
				    struct tcexpr *sub, struct colname *col) {
   struct tcexpr *te;

   te = mktcexpr_project_base(pql, sub);
   te->project.cols = colset_singleton(pql, col);

   return te;
}

struct tcexpr *mktcexpr_project_two(struct pqlcontext *pql,
				    struct tcexpr *sub, struct colname *col1,
				    struct colname *col2) {
   struct tcexpr *te;

   te = mktcexpr_project_base(pql, sub);
   te->project.cols = colset_pair(pql, col1, col2);

   return te;
}

struct tcexpr *mktcexpr_project_three(struct pqlcontext *pql,
				      struct tcexpr *sub,
				      struct colname *col1,
				      struct colname *col2,
				      struct colname *col3) {
   struct tcexpr *te;

   te = mktcexpr_project_base(pql, sub);
   te->project.cols = colset_triple(pql, col1, col2, col3);

   return te;
}

static struct tcexpr *mktcexpr_strip_base(struct pqlcontext *pql,
					  struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_STRIP);
   te->strip.sub = sub;
   te->strip.cols = NULL;

   return te;
}

struct tcexpr *mktcexpr_strip_none(struct pqlcontext *pql,
				   struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr_strip_base(pql, sub);
   te->strip.cols = colset_empty(pql);

   return te;
}

struct tcexpr *mktcexpr_strip_one(struct pqlcontext *pql,
				  struct tcexpr *sub, struct colname *col) {
   struct tcexpr *te;

   te = mktcexpr_strip_base(pql, sub);
   te->strip.cols = colset_singleton(pql, col);

   return te;
}

struct tcexpr *mktcexpr_rename(struct pqlcontext *pql,
			       struct tcexpr *sub, struct colname *oldcol,
			       struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_RENAME);
   te->rename.sub = sub;
   te->rename.oldcol = oldcol;
   te->rename.newcol = newcol;

   return te;
}

struct tcexpr *mktcexpr_join(struct pqlcontext *pql,
			     struct tcexpr *left, struct tcexpr *right,
			     struct tcexpr *predicate) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_JOIN);
   te->join.left = left;
   te->join.right = right;
   te->join.predicate = predicate;

   return te;
}

struct tcexpr *mktcexpr_order(struct pqlcontext *pql, struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_ORDER);
   te->order.sub = sub;
   te->order.cols = colset_empty(pql);

   return te;
}

struct tcexpr *mktcexpr_uniq(struct pqlcontext *pql, struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_UNIQ);
   te->uniq.sub = sub;
   te->uniq.cols = colset_empty(pql);

   return te;
}

//////////

struct tcexpr *mktcexpr_nest_none(struct pqlcontext *pql,
				  struct tcexpr *sub,
				  struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_NEST);
   te->nest.sub = sub;
   te->nest.cols = colset_empty(pql);
   te->nest.newcol = newcol;

   return te;
}

struct tcexpr *mktcexpr_nest_one(struct pqlcontext *pql,
				 struct tcexpr *sub, struct colname *col,
				 struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr_nest_none(pql, sub, newcol);
   colset_add(pql, te->nest.cols, col);

   return te;
}

struct tcexpr *mktcexpr_nest_set(struct pqlcontext *pql,
				 struct tcexpr *sub,
				 struct colset *cols,
				 struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_NEST);
   te->nest.sub = sub;
   te->nest.cols = cols;
   te->nest.newcol = newcol;

   return te;
}

struct tcexpr *mktcexpr_unnest(struct pqlcontext *pql,
			       struct tcexpr *sub, struct colname *col) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_UNNEST);
   te->unnest.sub = sub;
   te->unnest.col = col;

   return te;
}

//////////

struct tcexpr *mktcexpr_distinguish(struct pqlcontext *pql,
			       struct tcexpr *sub, struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_DISTINGUISH);
   te->distinguish.sub = sub;
   te->distinguish.newcol = newcol;

   return te;
}

struct tcexpr *mktcexpr_adjoin(struct pqlcontext *pql,
			       struct tcexpr *left, struct tcexpr *func,
			       struct colname *newcol) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_ADJOIN);
   te->adjoin.left = left;
   te->adjoin.func = func;
   te->adjoin.newcol = newcol;

   return te;
}

//////////

struct tcexpr *mktcexpr_step(struct pqlcontext *pql,
			     struct tcexpr *sub,
			     struct colname *subcolumn,
			     struct pqlvalue *edgename,
			     bool reversed,
			     struct colname *leftobjcolumn,
			     struct colname *edgecolumn,
			     struct colname *rightobjcolumn,
			     struct tcexpr *predicate) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_STEP);
   te->step.sub = sub;
   te->step.subcolumn = subcolumn;
   te->step.edgename = edgename;
   te->step.reversed = reversed;
   te->step.leftobjcolumn = leftobjcolumn;
   te->step.edgecolumn = edgecolumn;
   te->step.rightobjcolumn = rightobjcolumn;
   te->step.predicate = predicate;

   return te;
}

struct tcexpr *mktcexpr_repeat(struct pqlcontext *pql,
			       struct tcexpr *sub,
			       struct colname *subendcolumn,
			       struct tcvar *loopvar,
			       struct colname *bodystartcolumn,
			       struct tcexpr *body,
			       struct colname *bodypathcolumn,
			       struct colname *bodyendcolumn,
			       struct colname *repeatpathcolumn,
			       struct colname *repeatendcolumn) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_REPEAT);
   te->repeat.sub = sub;
   te->repeat.subendcolumn = subendcolumn;
   te->repeat.loopvar = loopvar;
   te->repeat.bodystartcolumn = bodystartcolumn;
   te->repeat.body = body;
   te->repeat.bodypathcolumn = bodypathcolumn;
   te->repeat.bodyendcolumn = bodyendcolumn;
   te->repeat.repeatpathcolumn = repeatpathcolumn;
   te->repeat.repeatendcolumn = repeatendcolumn;

   return te;
}

//////////

struct tcexpr *mktcexpr_scan(struct pqlcontext *pql,
			     struct colname *leftobjcolumn,
			     struct colname *edgecolumn,
			     struct colname *rightobjcolumn,
			     struct tcexpr *predicate) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_SCAN);
   te->scan.leftobjcolumn = leftobjcolumn;
   te->scan.edgecolumn = edgecolumn;
   te->scan.rightobjcolumn = rightobjcolumn;
   te->scan.predicate = predicate;

   return te;
}

//////////

struct tcexpr *mktcexpr_bop(struct pqlcontext *pql, struct tcexpr *left,
			    enum functions op, struct tcexpr *right) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_BOP);
   te->bop.left = left;
   te->bop.op = op;
   te->bop.right = right;

   return te;
}

struct tcexpr *mktcexpr_uop(struct pqlcontext *pql,
			    enum functions op, struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_UOP);
   te->uop.op = op;
   te->uop.sub = sub;

   return te;
}

struct tcexpr *mktcexpr_func(struct pqlcontext *pql, enum functions op) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_FUNC);
   te->func.op = op;
   tcexprarray_init(&te->func.args);

   return te;
}

//////////

struct tcexpr *mktcexpr_map(struct pqlcontext *pql, struct tcvar *var,
			    struct tcexpr *set, struct tcexpr *result) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_MAP);
   te->map.var = var;
   te->map.set = set;
   te->map.result = result;

   return te;
}

struct tcexpr *mktcexpr_let(struct pqlcontext *pql, struct tcvar *var,
			    struct tcexpr *value, struct tcexpr *body) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_LET);
   te->let.var = var;
   te->let.value = value;
   te->let.body = body;

   return te;
}

struct tcexpr *mktcexpr_lambda(struct pqlcontext *pql, struct tcvar *var,
			       struct tcexpr *body) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_LAMBDA);
   te->lambda.var = var;
   te->lambda.body = body;

   return te;
}

struct tcexpr *mktcexpr_apply(struct pqlcontext *pql,
			      struct tcexpr *lambda, struct tcexpr *arg) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_APPLY);
   te->apply.lambda = lambda;
   te->apply.arg = arg;

   return te;
}

//////////

struct tcexpr *mktcexpr_readvar(struct pqlcontext *pql, struct tcvar *var) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_READVAR);
   te->readvar = var;

   return te;
}

struct tcexpr *mktcexpr_readglobal(struct pqlcontext *pql,
				   struct tcglobal *var) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_READGLOBAL);
   te->readglobal = var;

   return te;
}

//////////

struct tcexpr *mktcexpr_createpathelement(struct pqlcontext *pql,
					  struct tcexpr *sub) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_CREATEPATHELEMENT);
   te->createpathelement = sub;

   return te;
}

struct tcexpr *mktcexpr_splatter(struct pqlcontext *pql,
				 struct tcexpr *value, struct tcexpr *name) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_SPLATTER);
   te->splatter.value = value;
   te->splatter.name = name;

   return te;
}

struct tcexpr *mktcexpr_tuple(struct pqlcontext *pql, unsigned arity) {
   struct tcexpr *te;
   unsigned i;

   te = mktcexpr(pql, TCE_TUPLE);
   tcexprarray_init(&te->tuple.exprs);
   te->tuple.columns = colset_empty(pql);
   colset_setsize(pql, te->tuple.columns, arity);
   tcexprarray_setsize(pql, &te->tuple.exprs, arity);
   for (i=0; i<arity; i++) {
      tcexprarray_set(&te->tuple.exprs, i, NULL);
   }

   return te;
}

//////////

struct tcexpr *mktcexpr_value(struct pqlcontext *pql, struct pqlvalue *val) {
   struct tcexpr *te;

   te = mktcexpr(pql, TCE_VALUE);
   te->value = val;

   return te;
}

//////////////////////////////

struct tcexpr *tcexpr_clone(struct pqlcontext *pql, struct tcexpr *te) {
   struct tcexpr *ret;
   unsigned i, num;

   ret = NULL; // gcc 4.1

   if (te == NULL) {
      return NULL;
   }

   switch (te->type) {
    case TCE_FILTER:
     ret = mktcexpr_filter(pql,
			   tcexpr_clone(pql, te->filter.sub),
			   tcexpr_clone(pql, te->filter.predicate));
     break;

    case TCE_PROJECT:
     ret = mktcexpr_project_none(pql, tcexpr_clone(pql, te->project.sub));
     ret->project.cols = colset_clone(pql, te->project.cols);
     break;

    case TCE_STRIP:
     ret = mktcexpr_strip_none(pql, tcexpr_clone(pql, te->strip.sub));
     ret->strip.cols = colset_clone(pql, te->strip.cols);
     break;

    case TCE_RENAME:
     colname_incref(te->rename.oldcol);
     colname_incref(te->rename.newcol);
     ret = mktcexpr_rename(pql,
			   tcexpr_clone(pql, te->rename.sub),
			   te->rename.oldcol,
			   te->rename.newcol);
     break;

    case TCE_JOIN:
     ret = mktcexpr_join(pql,
			   tcexpr_clone(pql, te->join.left),
			   tcexpr_clone(pql, te->join.right),
			   tcexpr_clone(pql, te->join.predicate));
     break;

    case TCE_ORDER:
     ret = mktcexpr_order(pql, tcexpr_clone(pql, te->order.sub));
     ret->order.cols = colset_clone(pql, te->order.cols);
     break;

    case TCE_UNIQ:
     ret = mktcexpr_uniq(pql, tcexpr_clone(pql, te->uniq.sub));
     ret->uniq.cols = colset_clone(pql, te->uniq.cols);
     break;

    case TCE_NEST:
     colname_incref(te->nest.newcol);
     ret = mktcexpr_nest_none(pql,
			      tcexpr_clone(pql, te->nest.sub),
			      te->nest.newcol);
     ret->nest.cols = colset_clone(pql, te->nest.cols);
     break;

    case TCE_UNNEST:
     colname_incref(te->unnest.col);
     ret = mktcexpr_unnest(pql,
			   tcexpr_clone(pql, te->unnest.sub),
			   te->unnest.col);
     break;

    case TCE_DISTINGUISH:
     colname_incref(te->distinguish.newcol);
     ret = mktcexpr_distinguish(pql,
				tcexpr_clone(pql, te->distinguish.sub),
				te->distinguish.newcol);
     break;

    case TCE_ADJOIN:
     colname_incref(te->adjoin.newcol);
     ret = mktcexpr_adjoin(pql,
			   tcexpr_clone(pql, te->adjoin.left),
			   tcexpr_clone(pql, te->adjoin.func),
			   te->adjoin.newcol);
     break;

    case TCE_STEP:
     colname_incref(te->step.subcolumn);
     colname_incref(te->step.leftobjcolumn);
     colname_incref(te->step.edgecolumn);
     colname_incref(te->step.rightobjcolumn);
     ret = mktcexpr_step(pql,
			 tcexpr_clone(pql, te->step.sub),
			 te->step.subcolumn,
			 pqlvalue_clone(pql, te->step.edgename),
			 te->step.reversed,
			 te->step.leftobjcolumn,
			 te->step.edgecolumn,
			 te->step.rightobjcolumn,
			 tcexpr_clone(pql, te->step.predicate));
     break;

    case TCE_REPEAT:
     tcvar_incref(te->repeat.loopvar);
     colname_incref(te->repeat.subendcolumn);
     colname_incref(te->repeat.bodystartcolumn);
     colname_incref(te->repeat.bodypathcolumn);
     colname_incref(te->repeat.bodyendcolumn);
     colname_incref(te->repeat.repeatpathcolumn);
     colname_incref(te->repeat.repeatendcolumn);
     ret = mktcexpr_repeat(pql,
			   tcexpr_clone(pql, te->repeat.sub),
			   te->repeat.subendcolumn,
			   te->repeat.loopvar,
			   te->repeat.bodystartcolumn,
			   tcexpr_clone(pql, te->repeat.body),
			   te->repeat.bodypathcolumn,
			   te->repeat.bodyendcolumn,
			   te->repeat.repeatpathcolumn,
			   te->repeat.repeatendcolumn);
     break;

    case TCE_SCAN:
     colname_incref(te->scan.leftobjcolumn);
     colname_incref(te->scan.edgecolumn);
     colname_incref(te->scan.rightobjcolumn);
     ret = mktcexpr_scan(pql,
			 te->scan.leftobjcolumn,
			 te->scan.edgecolumn,
			 te->scan.rightobjcolumn,
			 tcexpr_clone(pql, te->scan.predicate));
     break;

    case TCE_BOP:
     ret = mktcexpr_bop(pql,
			tcexpr_clone(pql, te->bop.left),
			te->bop.op,
			tcexpr_clone(pql, te->bop.right));
     break;

    case TCE_UOP:
     ret = mktcexpr_uop(pql,
			te->uop.op,
			tcexpr_clone(pql, te->uop.sub));
     break;

    case TCE_FUNC:
     ret = mktcexpr_func(pql, te->func.op);
     num = tcexprarray_num(&te->func.args);
     for (i=0; i<num; i++) {
	tcexprarray_add(pql, &ret->func.args,
			tcexpr_clone(pql, tcexprarray_get(&te->func.args, i)),
			NULL);
     }
     break;

    case TCE_MAP:
     tcvar_incref(te->map.var);
     ret = mktcexpr_map(pql,
			te->map.var,
			tcexpr_clone(pql, te->map.set),
			tcexpr_clone(pql, te->map.result));
     break;

    case TCE_LET:
     tcvar_incref(te->let.var);
     ret = mktcexpr_let(pql,
			te->let.var,
			tcexpr_clone(pql, te->let.value),
			tcexpr_clone(pql, te->let.body));
     break;

    case TCE_LAMBDA:
     tcvar_incref(te->lambda.var);
     ret = mktcexpr_lambda(pql,
			   te->lambda.var,
			   tcexpr_clone(pql, te->lambda.body));
     break;

    case TCE_APPLY:
     ret = mktcexpr_apply(pql,
			  tcexpr_clone(pql, te->apply.lambda),
			  tcexpr_clone(pql, te->apply.arg));
     break;

    case TCE_READVAR:
     tcvar_incref(te->readvar);
     ret = mktcexpr_readvar(pql, te->readvar);
     break;

    case TCE_READGLOBAL:
     tcglobal_incref(te->readglobal);
     ret = mktcexpr_readglobal(pql, te->readglobal);
     break;

    case TCE_CREATEPATHELEMENT:
     ret = mktcexpr_createpathelement(pql,
				      tcexpr_clone(pql,
						   te->createpathelement));
     break;

    case TCE_SPLATTER:
     ret = mktcexpr_splatter(pql,
			     tcexpr_clone(pql, te->splatter.value),
			     tcexpr_clone(pql, te->splatter.name));
     break;

    case TCE_TUPLE:
     num = tcexprarray_num(&te->tuple.exprs);
     ret = mktcexpr_tuple(pql, num);
     for (i=0; i<num; i++) {
	tcexprarray_set(&ret->tuple.exprs, i,
			tcexprarray_get(&te->tuple.exprs, i));
     }
     ret->tuple.columns = colset_clone(pql, te->tuple.columns);
     break;

    case TCE_VALUE:
     ret = mktcexpr_value(pql, pqlvalue_clone(pql, te->value));
     break;
   }
   if (te->datatype != NULL) {
      ret->datatype = te->datatype;
   }
   if (te->colnames != NULL) {
      ret->colnames = coltree_clone(pql, te->colnames);
   }

   return ret;
}

////////////////////////////////////////////////////////////
// destructors

void tcglobal_decref(struct pqlcontext *pql, struct tcglobal *tcg) {
   PQLASSERT(tcg != NULL);
   PQLASSERT(tcg->refcount > 0);

   (void)pql;

   tcg->refcount--;
   if (tcg->refcount == 0) {
      dostrfree(pql, tcg->name);
      dofree(pql, tcg, sizeof(*tcg));
   }
}

void tcvar_decref(struct pqlcontext *pql, struct tcvar *var) {
   PQLASSERT(var != NULL);
   PQLASSERT(var->refcount > 0);

   var->refcount--;
   if (var->refcount == 0) {
      coltree_destroy(pql, var->colnames);
      dofree(pql, var, sizeof(*var));
   }
}

static void tcexprarray_destroyall(struct pqlcontext *pql,
				   struct tcexprarray *arr) {
   unsigned i, num;

   num = tcexprarray_num(arr);
   for (i=0; i<num; i++) {
      tcexpr_destroy(pql, tcexprarray_get(arr, i));
   }
   tcexprarray_setsize(pql, arr, 0);
}

void tcexpr_destroy(struct pqlcontext *pql, struct tcexpr *tc) {
   if (tc == NULL) {
      return;
   }
   switch (tc->type) {
    case TCE_FILTER:
     tcexpr_destroy(pql, tc->filter.sub);
     tcexpr_destroy(pql, tc->filter.predicate);
     break;
    case TCE_PROJECT:
     tcexpr_destroy(pql, tc->project.sub);
     colset_destroy(pql, tc->project.cols);
     break;
    case TCE_STRIP:
     tcexpr_destroy(pql, tc->strip.sub);
     colset_destroy(pql, tc->strip.cols);
     break;
    case TCE_RENAME:
     tcexpr_destroy(pql, tc->rename.sub);
     if (tc->rename.oldcol != NULL) {
	colname_decref(pql, tc->rename.oldcol);
     }
     if (tc->rename.newcol != NULL) {
	colname_decref(pql, tc->rename.newcol);
     }
     break;
    case TCE_JOIN:
     tcexpr_destroy(pql, tc->join.left);
     tcexpr_destroy(pql, tc->join.right);
     tcexpr_destroy(pql, tc->join.predicate);
     break;
    case TCE_ORDER:
     tcexpr_destroy(pql, tc->order.sub);
     colset_destroy(pql, tc->order.cols);
     break;
    case TCE_UNIQ:
     tcexpr_destroy(pql, tc->uniq.sub);
     colset_destroy(pql, tc->uniq.cols);
     break;
    case TCE_NEST:
     tcexpr_destroy(pql, tc->nest.sub);
     colset_destroy(pql, tc->nest.cols);
     if (tc->nest.newcol != NULL) {
	colname_decref(pql, tc->nest.newcol);
     }
     break;
    case TCE_UNNEST:
     tcexpr_destroy(pql, tc->unnest.sub);
     if (tc->unnest.col != NULL) {
	colname_decref(pql, tc->unnest.col);
     }
     break;
    case TCE_DISTINGUISH:
     tcexpr_destroy(pql, tc->distinguish.sub);
     if (tc->distinguish.newcol != NULL) {
	colname_decref(pql, tc->distinguish.newcol);
     }
     break;
    case TCE_ADJOIN:
     tcexpr_destroy(pql, tc->adjoin.left);
     tcexpr_destroy(pql, tc->adjoin.func);
     if (tc->adjoin.newcol != NULL) {
	colname_decref(pql, tc->adjoin.newcol);
     }
     break;
    case TCE_STEP:
     tcexpr_destroy(pql, tc->step.sub);
     if (tc->step.subcolumn != NULL) {
	colname_decref(pql, tc->step.subcolumn);
     }
     if (tc->step.edgename != NULL) {
	pqlvalue_destroy(tc->step.edgename);
     }
     if (tc->step.leftobjcolumn != NULL) {
	colname_decref(pql, tc->step.leftobjcolumn);
     }
     if (tc->step.edgecolumn != NULL) {
	colname_decref(pql, tc->step.edgecolumn);
     }
     if (tc->step.rightobjcolumn != NULL) {
	colname_decref(pql, tc->step.rightobjcolumn);
     }
     tcexpr_destroy(pql, tc->step.predicate);
     break;
    case TCE_REPEAT:
     tcexpr_destroy(pql, tc->repeat.sub);
     if (tc->repeat.subendcolumn != NULL) {
	colname_decref(pql, tc->repeat.subendcolumn);
     }
     tcvar_decref(pql, tc->repeat.loopvar);
     if (tc->repeat.bodystartcolumn != NULL) {
	colname_decref(pql, tc->repeat.bodystartcolumn);
     }
     tcexpr_destroy(pql, tc->repeat.body);
     if (tc->repeat.bodypathcolumn != NULL) {
	colname_decref(pql, tc->repeat.bodypathcolumn);
     }
     if (tc->repeat.bodyendcolumn != NULL) {
	colname_decref(pql, tc->repeat.bodyendcolumn);
     }
     if (tc->repeat.repeatpathcolumn != NULL) {
	colname_decref(pql, tc->repeat.repeatpathcolumn);
     }
     if (tc->repeat.repeatendcolumn != NULL) {
	colname_decref(pql, tc->repeat.repeatendcolumn);
     }
     break;
    case TCE_SCAN:
     if (tc->scan.leftobjcolumn != NULL) {
	colname_decref(pql, tc->scan.leftobjcolumn);
     }
     if (tc->scan.edgecolumn != NULL) {
	colname_decref(pql, tc->scan.edgecolumn);
     }
     if (tc->scan.rightobjcolumn != NULL) {
	colname_decref(pql, tc->scan.rightobjcolumn);
     }
     tcexpr_destroy(pql, tc->scan.predicate);
     break;
    case TCE_BOP:
     tcexpr_destroy(pql, tc->bop.left);
     tcexpr_destroy(pql, tc->bop.right);
     break;
    case TCE_UOP:
     tcexpr_destroy(pql, tc->uop.sub);
     break;
    case TCE_FUNC:
     tcexprarray_destroyall(pql, &tc->func.args);
     tcexprarray_cleanup(pql, &tc->func.args);
     break;
    case TCE_MAP:
     tcvar_decref(pql, tc->map.var);
     tcexpr_destroy(pql, tc->map.set);
     tcexpr_destroy(pql, tc->map.result);
     break;
    case TCE_LET:
     tcvar_decref(pql, tc->let.var);
     tcexpr_destroy(pql, tc->let.value);
     tcexpr_destroy(pql, tc->let.body);
     break;
    case TCE_LAMBDA:
     tcvar_decref(pql, tc->lambda.var);
     tcexpr_destroy(pql, tc->lambda.body);
     break;
    case TCE_APPLY:
     tcexpr_destroy(pql, tc->apply.lambda);
     tcexpr_destroy(pql, tc->apply.arg);
     break;
    case TCE_READVAR:
     tcvar_decref(pql, tc->readvar);
     break;
    case TCE_READGLOBAL:
     tcglobal_decref(pql, tc->readglobal);
     break;
    case TCE_CREATEPATHELEMENT:
     tcexpr_destroy(pql, tc->createpathelement);
     break;
    case TCE_SPLATTER:
     tcexpr_destroy(pql, tc->splatter.value);
     tcexpr_destroy(pql, tc->splatter.name);
     break;
    case TCE_TUPLE:
     tcexprarray_destroyall(pql, &tc->tuple.exprs);
     tcexprarray_cleanup(pql, &tc->tuple.exprs);
     colset_destroy(pql, tc->tuple.columns);
     break;
    case TCE_VALUE:
     if (tc->value != NULL) {
	pqlvalue_destroy(tc->value);
     }
     break;
   }
   if (tc->colnames != NULL) {
      coltree_destroy(pql, tc->colnames);
   }
   dofree(pql, tc, sizeof(*tc));
}

////////////////////////////////////////////////////////////
// dump

struct tclayout {
   struct pqlcontext *pql;
   bool showtypes;
};

static struct layout *tctype_layout(struct tclayout *ctx, struct layout *l,
				    struct datatype *t) {
   if (ctx->showtypes) {
      l = mklayout_triple(ctx->pql, l,
			  mklayout_text(ctx->pql, "::"),
			  mklayout_text(ctx->pql, datatype_getname(t)));
   }
   return l;
}

static struct layout *tcvar_layout(struct tclayout *ctx, struct tcvar *var) {
   char buf[32];
   struct layout *ret;

   snprintf(buf, sizeof(buf), ".K%u", var->id);
   ret = mklayout_text(ctx->pql, buf);
   return tctype_layout(ctx, ret, var->datatype);
}

static struct layout *tcexpr_layout(struct tclayout *ctx, struct tcexpr *te) {
   struct layout *ret;

   ret = NULL; // for gcc 4.1

   switch (te->type) {

    case TCE_FILTER:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->filter.sub),
			   mklayout_text(ctx->pql, "where"),
			   tcexpr_layout(ctx, te->filter.predicate));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_PROJECT:
     ret = mklayout_triple(ctx->pql,
			   mklayout_text(ctx->pql, "project"),
			   colset_layout(ctx->pql, te->project.cols),
			   mklayout_wrap(ctx->pql, "(",
					 tcexpr_layout(ctx, te->project.sub),
					 ")"));
     break;

    case TCE_STRIP:
     ret = mklayout_triple(ctx->pql,
			   mklayout_text(ctx->pql, "strip"),
			   colset_layout(ctx->pql, te->strip.cols),
			   mklayout_wrap(ctx->pql, "(",
					 tcexpr_layout(ctx, te->strip.sub),
					 ")"));
     break;

    case TCE_RENAME:
     ret = mklayout_quad(ctx->pql,
			 tcexpr_layout(ctx, te->rename.sub),
			 colname_layout(ctx->pql, te->rename.oldcol),
			 mklayout_text(ctx->pql, "=>"),
			 colname_layout(ctx->pql, te->rename.newcol));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_JOIN:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->join.left),
			   mklayout_text(ctx->pql, "x"),
			   tcexpr_layout(ctx, te->join.right));
     if (te->join.predicate != NULL) {
	ret = mklayout_triple(ctx->pql, ret,
			      mklayout_text(ctx->pql, "where"),
			      tcexpr_layout(ctx, te->join.predicate));
     }
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_ORDER:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->order.sub),
			   mklayout_text(ctx->pql, "order-by"),
			   colset_layout(ctx->pql, te->order.cols));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_UNIQ:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->uniq.sub),
			   mklayout_text(ctx->pql, "uniq-on"),
			   colset_layout(ctx->pql, te->uniq.cols));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_NEST:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->nest.sub),
			   mklayout_text(ctx->pql, "nest"),
			   colset_layout(ctx->pql, te->nest.cols));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_UNNEST:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->unnest.sub),
			   mklayout_text(ctx->pql, "unnest"),
			   colname_layout(ctx->pql, te->unnest.col));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_DISTINGUISH:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->distinguish.sub),
			   mklayout_text(ctx->pql, "|+| DISTINGUISH as"),
			   colname_layout(ctx->pql, te->distinguish.newcol));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_ADJOIN:
     ret = mklayout_quint(ctx->pql,
			  tcexpr_layout(ctx, te->adjoin.left),
			  mklayout_text(ctx->pql, "|+|"),
			  tcexpr_layout(ctx, te->adjoin.func),
			  mklayout_text(ctx->pql, "as"),
			  colname_layout(ctx->pql, te->adjoin.newcol));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_STEP:
     ret = mklayout_quint(ctx->pql,
			  colname_layout(ctx->pql, te->step.leftobjcolumn),
			  mklayout_text(ctx->pql, ","),
			  colname_layout(ctx->pql, te->step.edgecolumn),
			  mklayout_text(ctx->pql, ","),
			  colname_layout(ctx->pql, te->step.rightobjcolumn));
     ret = mklayout_pair(ctx->pql,
			 mklayout_text(ctx->pql, "as"),
			 ret);
     ret = mklayout_quint(ctx->pql,
			  mklayout_text(ctx->pql, "step"),
			  colname_layout(ctx->pql, te->step.subcolumn),
			  mklayout_text(ctx->pql, "."),
			  (te->step.edgename ?
			   pqlvalue_layout(ctx->pql, te->step.edgename) :
			   mklayout_text(ctx->pql, "%")),
			  ret);
     ret = mklayout_leftalign_triple(ctx->pql,
				     tcexpr_layout(ctx, te->step.sub),
				     mklayout_text(ctx->pql, "x"),
				     ret);

     if (te->step.predicate != NULL) {
	ret = mklayout_triple(ctx->pql, ret,
			      mklayout_text(ctx->pql, "where"),
			      tcexpr_layout(ctx, te->step.predicate));
     }
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_REPEAT:
     {
	struct layout *locals, *inputs, *path;
	struct layout *head, *body, *tail;

	inputs = mklayout_triple(ctx->pql,
				 tcexpr_layout(ctx, te->repeat.sub),
				 mklayout_text(ctx->pql, "."),
				 colname_layout(ctx->pql,
						 te->repeat.subendcolumn));

	locals = mklayout_triple(ctx->pql,
				 tcvar_layout(ctx, te->repeat.loopvar),
				 mklayout_text(ctx->pql, "."),
				 colname_layout(ctx->pql,
						 te->repeat.bodystartcolumn));

	head = mklayout_triple(ctx->pql, locals,
				  mklayout_text(ctx->pql, "<-"),
				  inputs);
	head = mklayout_triple(ctx->pql,
			       mklayout_text(ctx->pql, "repeat"),
			       head,
			       mklayout_text(ctx->pql, "{"));

	body = tcexpr_layout(ctx, te->repeat.body);

	tail = mklayout_triple(ctx->pql,
			       colname_layout(ctx->pql,
					       te->repeat.bodyendcolumn),
			       mklayout_text(ctx->pql, "->"),
			       colname_layout(ctx->pql,
					       te->repeat.repeatendcolumn));

	if (te->repeat.bodypathcolumn != NULL) {
	   path = mklayout_triple(ctx->pql, 
				  colname_layout(ctx->pql,
						  te->repeat.bodypathcolumn),
				  mklayout_text(ctx->pql, "->"),
				  colname_layout(ctx->pql,
						 te->repeat.repeatpathcolumn));

	   tail = mklayout_triple(ctx->pql, path,
				  mklayout_text(ctx->pql, ","),
				  tail);
	}

	tail = mklayout_pair(ctx->pql,
			     mklayout_text(ctx->pql, "}"), tail);
			      
	ret = mklayout_indent(ctx->pql, head, body, tail);
     }
     break;

    case TCE_SCAN:
     ret = mklayout_quint(ctx->pql,
			  colname_layout(ctx->pql, te->scan.leftobjcolumn),
			  mklayout_text(ctx->pql, ","),
			  colname_layout(ctx->pql, te->scan.edgecolumn),
			  mklayout_text(ctx->pql, ","),
			  colname_layout(ctx->pql, te->scan.rightobjcolumn));
     ret = mklayout_pair(ctx->pql,
			 mklayout_text(ctx->pql, "scan as"),
			 ret);
     if (te->scan.predicate != NULL) {
	ret = mklayout_triple(ctx->pql, ret,
			      mklayout_text(ctx->pql, "where"),
			      tcexpr_layout(ctx, te->scan.predicate));
     }
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_BOP:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->bop.left),
			   mklayout_text(ctx->pql, function_getname(te->bop.op)),
			   tcexpr_layout(ctx, te->bop.right));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_UOP:
     ret = mklayout_pair(ctx->pql,
			 mklayout_text(ctx->pql, function_getname(te->uop.op)),
			 tcexpr_layout(ctx, te->uop.sub));
     ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     break;

    case TCE_FUNC:
     {
	struct layout *name, *sub;
	unsigned i, num;

	name = mklayout_text(ctx->pql, function_getname(te->func.op));

	num = tcexprarray_num(&te->func.args);
	ret = mklayout_leftalign_empty(ctx->pql);
	layoutarray_setsize(ctx->pql, &ret->leftalign, num);
	for (i=0; i<num; i++) {
	   sub = tcexpr_layout(ctx, tcexprarray_get(&te->func.args, i));
	   if (i < num-1) {
	      sub = mklayout_pair(ctx->pql, sub,
				  mklayout_text(ctx->pql, ","));
	   }
	   layoutarray_set(&ret->leftalign, i, sub);
	}
	ret = mklayout_wrap(ctx->pql, "(", ret, ")");
	ret = mklayout_pair(ctx->pql, name, ret);
     }
     break;

    case TCE_MAP:
     ret = mklayout_quint(ctx->pql,
			  mklayout_text(ctx->pql, "map"),
			  tcvar_layout(ctx, te->map.var),
			  mklayout_text(ctx->pql, "in"),
			  tcexpr_layout(ctx, te->map.set),
			  mklayout_text(ctx->pql, ":"));
     ret = mklayout_indent(ctx->pql,
			   ret,
			   tcexpr_layout(ctx, te->map.result),
			   NULL);
     break;

    case TCE_LET:
     ret = mklayout_quint(ctx->pql,
			  mklayout_text(ctx->pql, "let"),
			  tcvar_layout(ctx, te->let.var),
			  mklayout_text(ctx->pql, "="),
			  tcexpr_layout(ctx, te->let.value),
			  mklayout_text(ctx->pql, ":"));
     ret = mklayout_indent(ctx->pql,
			   ret,
			   tcexpr_layout(ctx, te->let.body),
			   NULL);
     break;

     case TCE_LAMBDA:
     ret = mklayout_triple(ctx->pql, 
			   mklayout_text(ctx->pql, "lambda"),
			   tcvar_layout(ctx, te->lambda.var),
			   mklayout_text(ctx->pql, ":"));
     ret = mklayout_indent(ctx->pql,
			   ret,
			   tcexpr_layout(ctx, te->lambda.body),
			   NULL);
     break;

    case TCE_APPLY:
     ret = mklayout_pair(ctx->pql,
			 tcexpr_layout(ctx, te->apply.lambda),
			 mklayout_wrap(ctx->pql, "(",
				       tcexpr_layout(ctx, te->apply.arg),
				       ")"));
     break;

    case TCE_READVAR:
     ret = tcvar_layout(ctx, te->readvar);
     break;

    case TCE_READGLOBAL:
     ret = mklayout_text(ctx->pql, te->readglobal->name);
     break;

    case TCE_CREATEPATHELEMENT:
     ret = mklayout_wrap(ctx->pql, "PATHELEMENT(",
			 tcexpr_layout(ctx, te->createpathelement), ")");
     break;

    case TCE_SPLATTER:
     ret = mklayout_triple(ctx->pql,
			   tcexpr_layout(ctx, te->splatter.value),
			   mklayout_text(ctx->pql, ","),
			   tcexpr_layout(ctx, te->splatter.name));
     ret = mklayout_wrap(ctx->pql, "SPLATTER(", ret, ")");
     break;

    case TCE_TUPLE:
     {
	struct layout *sub;
	unsigned i, num;

	num = tcexprarray_num(&te->tuple.exprs);
	PQLASSERT(num > 0);
	ret = mklayout_leftalign_empty(ctx->pql);
	layoutarray_setsize(ctx->pql, &ret->leftalign, num);
	for (i=0; i<num; i++) {
	   sub = tcexpr_layout(ctx, tcexprarray_get(&te->tuple.exprs, i));
	   if (i < num-1) {
	      sub = mklayout_pair(ctx->pql, sub,
				  mklayout_text(ctx->pql, ","));
	   }
	   layoutarray_set(&ret->leftalign, i, sub);
	}
	ret = mklayout_wrap(ctx->pql, "(", ret, ")");
     }
     break;

    case TCE_VALUE:
     ret = pqlvalue_layout(ctx->pql, te->value);
     break;

   }

   ret = tctype_layout(ctx, ret, te->datatype);
   return ret;
}

char *tcdump(struct pqlcontext *pql, struct tcexpr *te, bool showtypes) {
   struct tclayout ctx;
   struct layout *l;
   char *ret;

   ctx.pql = pql;
   ctx.showtypes = showtypes;
   l = tcexpr_layout(&ctx, te);

   l = layout_format(pql, l, pql->dumpwidth);
   ret = layout_tostring(pql, l);
   layout_destroy(pql, l);

   return ret;
}

////////////////////////////////////////////////////////////

#if 0 /* debugging code */

struct colname *bogon;

static unsigned colname_colrefs(struct colname *cn, struct colname *col) {
   return cn == col ? 1 : 0;
}

static unsigned colset_colrefs(struct colset *cs, struct colname *col) {
   unsigned i, num, ret;

   ret = 0;
   num = colset_num(cs);
   for (i=0; i<num; i++) {
      if (colset_get(cs, i) == col) {
	 ret++;
      }
   }
   return ret;
}

static unsigned coltree_colrefs(struct coltree *ct, struct colname *col) {
   unsigned i, num, ret;

   if (ct == NULL) {
      return 0;
   }

   ret = 0;
   if (coltree_wholecolumn(ct) == col) {
      ret++;
   }
   if (coltree_istuple(ct)) {
      num = coltree_num(ct);
      for (i=0; i<num; i++) {
	 ret += coltree_colrefs(coltree_getsubtree(ct, i), col);
      }
   }
   return ret;
}

static unsigned tcvar_colrefs(struct tcvar *v, struct colname *col) {
   return coltree_colrefs(v->colnames, col);
}

static unsigned tcexpr_colrefs(struct tcexpr *te, struct colname *col);

static unsigned tcexprarray_colrefs(struct tcexprarray *arr,
				     struct colname *col) {
   unsigned i, num, ret;

   ret = 0;
   num = tcexprarray_num(arr);
   for (i=0; i<num; i++) {
      ret += tcexpr_colrefs(tcexprarray_get(arr, i), col);
   }
   return ret;
}

static unsigned tcexpr_colrefs(struct tcexpr *te, struct colname *col) {
   unsigned ret;

   if (te == NULL) {
      return 0;
   }

   switch (te->type) {
    case TCE_FILTER:
     ret = tcexpr_colrefs(te->filter.sub, col)
	+ tcexpr_colrefs(te->filter.predicate, col);
     break;
    case TCE_PROJECT:
     ret = tcexpr_colrefs(te->project.sub, col)
	+ colset_colrefs(te->project.cols, col);
     break;
    case TCE_STRIP:
     ret = tcexpr_colrefs(te->strip.sub, col)
	+ colset_colrefs(te->strip.cols, col);
     break;
    case TCE_RENAME:
     ret = tcexpr_colrefs(te->rename.sub, col)
	+ colname_colrefs(te->rename.oldcol, col)
	+ colname_colrefs(te->rename.newcol, col);
     break;
    case TCE_JOIN:
     ret = tcexpr_colrefs(te->join.left, col)
	+ tcexpr_colrefs(te->join.right, col)
	+ tcexpr_colrefs(te->join.predicate, col);
     break;
    case TCE_ORDER:
     ret = tcexpr_colrefs(te->order.sub, col)
	+ colset_colrefs(te->order.cols, col);
     break;
    case TCE_UNIQ:
     ret = tcexpr_colrefs(te->uniq.sub, col)
	+ colset_colrefs(te->uniq.cols, col);
     break;
    case TCE_NEST:
     ret = tcexpr_colrefs(te->nest.sub, col)
	+ colset_colrefs(te->nest.cols, col)
	+ colname_colrefs(te->nest.newcol, col);
     break;
    case TCE_UNNEST:
     ret = tcexpr_colrefs(te->unnest.sub, col)
	+ colname_colrefs(te->unnest.col, col);
     break;
    case TCE_DISTINGUISH:
     ret = tcexpr_colrefs(te->distinguish.sub, col)
	+ colname_colrefs(te->distinguish.newcol, col);
     break;
    case TCE_ADJOIN:
     ret = tcexpr_colrefs(te->adjoin.left, col)
	+ tcexpr_colrefs(te->adjoin.func, col)
	+ colname_colrefs(te->adjoin.newcol, col);
     break;
    case TCE_STEP:
     ret = tcexpr_colrefs(te->step.sub, col)
	+ colname_colrefs(te->step.subcolumn, col)
	+ colname_colrefs(te->step.leftobjcolumn, col)
	+ colname_colrefs(te->step.edgecolumn, col)
	+ colname_colrefs(te->step.rightobjcolumn, col)
	+ tcexpr_colrefs(te->step.predicate, col);
     break;
    case TCE_REPEAT:
     ret = tcexpr_colrefs(te->repeat.sub, col)
	+ colname_colrefs(te->repeat.subendcolumn, col)
	+ tcvar_colrefs(te->repeat.loopvar, col)
	+ colname_colrefs(te->repeat.bodystartcolumn, col)
	+ tcexpr_colrefs(te->repeat.body, col)
	+ colname_colrefs(te->repeat.bodypathcolumn, col)
	+ colname_colrefs(te->repeat.bodyendcolumn, col)
	+ colname_colrefs(te->repeat.repeatpathcolumn, col)
	+ colname_colrefs(te->repeat.repeatendcolumn, col);
     break;
    case TCE_SCAN:
     ret = colname_colrefs(te->scan.leftobjcolumn, col)
	+ colname_colrefs(te->scan.edgecolumn, col)
	+ colname_colrefs(te->scan.rightobjcolumn, col)
	+ tcexpr_colrefs(te->scan.predicate, col);
     break;
    case TCE_BOP:
     ret = tcexpr_colrefs(te->bop.left, col)
	+ tcexpr_colrefs(te->bop.right, col);
     break;
    case TCE_UOP:
     ret = tcexpr_colrefs(te->uop.sub, col);
     break;
    case TCE_FUNC:
     ret = tcexprarray_colrefs(&te->func.args, col);
     break;
    case TCE_MAP:
     ret = tcvar_colrefs(te->map.var, col)
	+ tcexpr_colrefs(te->map.set, col)
	+ tcexpr_colrefs(te->map.result, col);
     break;
    case TCE_LET:
     ret = tcvar_colrefs(te->let.var, col)
	+ tcexpr_colrefs(te->let.value, col)
	+ tcexpr_colrefs(te->let.body, col);
     break;
    case TCE_LAMBDA:
     ret = tcvar_colrefs(te->lambda.var, col)
	+ tcexpr_colrefs(te->lambda.body, col);
     break;
    case TCE_APPLY:
     ret = tcexpr_colrefs(te->apply.lambda, col)
	+ tcexpr_colrefs(te->apply.arg, col);
     break;
    case TCE_READVAR:
     //ret = tcvar_colrefs(te->readvar, col);
     ret = 0;
     break;
    case TCE_READGLOBAL:
     ret = 0;
     break;
    case TCE_CREATEPATHELEMENT:
     ret = tcexpr_colrefs(te->createpathelement, col);
     break;
    case TCE_SPLATTER:
     ret = tcexpr_colrefs(te->splatter.value, col)
	+ tcexpr_colrefs(te->splatter.name, col);
     break;
    case TCE_TUPLE:
     ret = tcexprarray_colrefs(&te->tuple.exprs, col)
	+ colset_colrefs(te->tuple.columns, col);
     break;
    case TCE_VALUE:
     ret = 0;
     break;
   }

   ret += coltree_colrefs(te->colnames, col);

   return ret;
}

bool colname_checkrefcount(struct tcexpr *te, struct colname *col,
			   unsigned extrarefs) {
   unsigned a, b;

   if (col == NULL) {
      return true;
   }

   a = tcexpr_colrefs(te, col) + extrarefs;
   b = colname_getrefcount(col);
   if (a != b) {
      printf("!!! found %u count %u\n", a, b);
   }
   return a == b;
}

#endif

#if 0 /* more debugging code */

struct tcvar *bogon;

#define colname_varrefs(a, b) (0)
#define colset_varrefs(a, b) (0)
#define coltree_varrefs(a, b) (0)

static unsigned tcvar_varrefs(struct tcvar *v, struct tcvar *var) {
   return v == var;
}

static unsigned tcexpr_varrefs(struct tcexpr *te, struct tcvar *var);

static unsigned tcexprarray_varrefs(struct tcexprarray *arr,
				    struct tcvar *var) {
   unsigned i, num, ret;

   ret = 0;
   num = tcexprarray_num(arr);
   for (i=0; i<num; i++) {
      ret += tcexpr_varrefs(tcexprarray_get(arr, i), var);
   }
   return ret;
}

static unsigned tcexpr_varrefs(struct tcexpr *te, struct tcvar *var) {
   unsigned ret;

   if (te == NULL) {
      return 0;
   }

   switch (te->type) {
    case TCE_FILTER:
     ret = tcexpr_varrefs(te->filter.sub, var)
	+ tcexpr_varrefs(te->filter.predicate, var);
     break;
    case TCE_PROJECT:
     ret = tcexpr_varrefs(te->project.sub, var)
	+ colset_varrefs(te->project.cols, var);
     break;
    case TCE_STRIP:
     ret = tcexpr_varrefs(te->strip.sub, var)
	+ colset_varrefs(te->strip.cols, var);
     break;
    case TCE_RENAME:
     ret = tcexpr_varrefs(te->rename.sub, var)
	+ colname_varrefs(te->rename.oldcol, var)
	+ colname_varrefs(te->rename.newcol, var);
     break;
    case TCE_JOIN:
     ret = tcexpr_varrefs(te->join.left, var)
	+ tcexpr_varrefs(te->join.right, var)
	+ tcexpr_varrefs(te->join.predicate, var);
     break;
    case TCE_ORDER:
     ret = tcexpr_varrefs(te->order.sub, var)
	+ colset_varrefs(te->order.cols, var);
     break;
    case TCE_UNIQ:
     ret = tcexpr_varrefs(te->uniq.sub, var)
	+ colset_varrefs(te->uniq.cols, var);
     break;
    case TCE_NEST:
     ret = tcexpr_varrefs(te->nest.sub, var)
	+ colset_varrefs(te->nest.cols, var)
	+ colname_varrefs(te->nest.newcol, var);
     break;
    case TCE_UNNEST:
     ret = tcexpr_varrefs(te->unnest.sub, var)
	+ colname_varrefs(te->unnest.col, var);
     break;
    case TCE_DISTINGUISH:
     ret = tcexpr_varrefs(te->distinguish.sub, var)
	+ colname_varrefs(te->distinguish.newcol, var);
     break;
    case TCE_ADJOIN:
     ret = tcexpr_varrefs(te->adjoin.left, var)
	+ tcexpr_varrefs(te->adjoin.func, var)
	+ colname_varrefs(te->adjoin.newcol, var);
     break;
    case TCE_STEP:
     ret = tcexpr_varrefs(te->step.sub, var)
	+ colname_varrefs(te->step.subcolumn, var)
	+ colname_varrefs(te->step.leftobjcolumn, var)
	+ colname_varrefs(te->step.edgecolumn, var)
	+ colname_varrefs(te->step.rightobjcolumn, var)
	+ tcexpr_varrefs(te->step.predicate, var);
     break;
    case TCE_REPEAT:
     ret = tcexpr_varrefs(te->repeat.sub, var)
	+ colname_varrefs(te->repeat.subendcolumn, var)
	+ tcvar_varrefs(te->repeat.loopvar, var)
	+ colname_varrefs(te->repeat.bodystartcolumn, var)
	+ tcexpr_varrefs(te->repeat.body, var)
	+ colname_varrefs(te->repeat.bodypathcolumn, var)
	+ colname_varrefs(te->repeat.bodyendcolumn, var)
	+ colname_varrefs(te->repeat.repeatpathcolumn, var)
	+ colname_varrefs(te->repeat.repeatendcolumn, var);
     break;
    case TCE_SCAN:
     ret = colname_varrefs(te->scan.leftobjcolumn, var)
	+ colname_varrefs(te->scan.edgecolumn, var)
	+ colname_varrefs(te->scan.rightobjcolumn, var)
	+ tcexpr_varrefs(te->scan.predicate, var);
     break;
    case TCE_BOP:
     ret = tcexpr_varrefs(te->bop.left, var)
	+ tcexpr_varrefs(te->bop.right, var);
     break;
    case TCE_UOP:
     ret = tcexpr_varrefs(te->uop.sub, var);
     break;
    case TCE_FUNC:
     ret = tcexprarray_varrefs(&te->func.args, var);
     break;
    case TCE_MAP:
     ret = tcvar_varrefs(te->map.var, var)
	+ tcexpr_varrefs(te->map.set, var)
	+ tcexpr_varrefs(te->map.result, var);
     break;
    case TCE_LET:
     ret = tcvar_varrefs(te->let.var, var)
	+ tcexpr_varrefs(te->let.value, var)
	+ tcexpr_varrefs(te->let.body, var);
     break;
    case TCE_LAMBDA:
     ret = tcvar_varrefs(te->lambda.var, var)
	+ tcexpr_varrefs(te->lambda.body, var);
     break;
    case TCE_APPLY:
     ret = tcexpr_varrefs(te->apply.lambda, var)
	+ tcexpr_varrefs(te->apply.arg, var);
     break;
    case TCE_READVAR:
     ret = tcvar_varrefs(te->readvar, var);
     break;
    case TCE_READGLOBAL:
     ret = 0;
     break;
    case TCE_CREATEPATHELEMENT:
     ret = tcexpr_varrefs(te->createpathelement, var);
     break;
    case TCE_SPLATTER:
     ret = tcexpr_varrefs(te->splatter.value, var)
	+ tcexpr_varrefs(te->splatter.name, var);
     break;
    case TCE_TUPLE:
     ret = tcexprarray_varrefs(&te->tuple.exprs, var)
	+ colset_varrefs(te->tuple.columns, var);
     break;
    case TCE_VALUE:
     ret = 0;
     break;
   }

   ret += coltree_varrefs(te->colnames, var);

   return ret;
}

bool tcvar_checkrefcount(struct tcexpr *te, struct tcvar *var,
			 unsigned extrarefs) {
   unsigned a, b;

   if (var == NULL) {
      return true;
   }

   a = tcexpr_varrefs(te, var) + extrarefs;
   b = var->refcount;
   if (a != b) {
      printf("!!! found %u count %u\n", a, b);
   }
   return a == b;
}

#endif
