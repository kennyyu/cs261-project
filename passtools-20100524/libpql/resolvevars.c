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

/*
 * Resolve variable references.
 */

#include <string.h>

#include "utils.h"
#include "pttree.h"
#include "passes.h"
#include "pqlcontext.h"

/*
 * Single scope.
 */
struct scope {
   struct scope *parent;
   struct ptcolumnvararray boundvars;
};

/*
 * Context structure for this operation.
 */
struct resolve {
   struct pqlcontext *pql;			/* Master context. */
   bool failed;					/* Error flag. */

   struct scope *current;			/* Current scope. */
   struct ptglobalvararray globals;		/* Global vars used. */
   struct ptcolumnvararray allcolumns;		/* Column vars used. */
};

////////////////////////////////////////////////////////////

/*
 * Create a scope.
 */
static struct scope *scope_create(struct pqlcontext *pql) {
   struct scope *ns;

   ns = domalloc(pql, sizeof(*ns));
   ns->parent = NULL;
   ptcolumnvararray_init(&ns->boundvars);

   return ns;
}

/*
 * Destroy a scope.
 *
 * The boundvars arrays do not hold references to the columnvars, so
 * just drop them.
 */
static void scope_destroy(struct pqlcontext *pql, struct scope *os) {
   PQLASSERT(os->parent == NULL);
   ptcolumnvararray_setsize(pql, &os->boundvars, 0);
   ptcolumnvararray_cleanup(pql, &os->boundvars);
   dofree(pql, os, sizeof(*os));
}

/*
 * Search a scope for a variable name.
 */
static struct ptcolumnvar *scope_find(struct scope *sc, const char *name) {
   unsigned i, num;
   struct ptcolumnvar *var;

   num = ptcolumnvararray_num(&sc->boundvars);
   for (i=0; i<num; i++) {
      var = ptcolumnvararray_get(&sc->boundvars, i);
      if (!strcmp(var->name, name)) {
	 return var;
      }
   }
   return NULL;
}

////////////////////////////////////////////////////////////

/*
 * Initialize the operation context.
 */
static void resolve_init(struct resolve *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   ctx->failed = false;
   ptglobalvararray_init(&ctx->globals);
   ptcolumnvararray_init(&ctx->allcolumns);
   ctx->current = scope_create(ctx->pql);
}

/*
 * Clean up the operation context.
 *
 * The globals array does not hold references to the vars, so just
 * drop them.
 */
static void resolve_cleanup(struct pqlcontext *pql, struct resolve *ctx) {
   ptglobalvararray_setsize(pql, &ctx->globals, 0);
   ptglobalvararray_cleanup(pql, &ctx->globals);
   ptcolumnvararray_setsize(pql, &ctx->allcolumns, 0);
   ptcolumnvararray_cleanup(pql, &ctx->allcolumns);
   PQLASSERT(ctx->current->parent == NULL);
   scope_destroy(ctx->pql, ctx->current);
}

/*
 * Enter a new scope.
 */
static void resolve_pushscope(struct resolve *ctx) {
   struct scope *ns;

   ns = scope_create(ctx->pql);
   ns->parent = ctx->current;
   ctx->current = ns;
}

/*
 * Leave a scope.
 */
static void resolve_popscope(struct resolve *ctx) {
   struct scope *os;

   os = ctx->current;
   ctx->current = os->parent;
   PQLASSERT(ctx->current != NULL);
   os->parent = NULL;
   scope_destroy(ctx->pql, os);
}

/*
 * Look for a (column) variable in the current scope or any parent.
 *
 * Note: this does not return a reference -- use ptcolumnvar_incref
 * afterwards if desired.
 */
static struct ptcolumnvar *resolve_look(struct resolve *ctx, const char *name){
   struct scope *sc;
   struct ptcolumnvar *var;

   for (sc = ctx->current; sc != NULL; sc = sc->parent) {
      var = scope_find(sc, name);
      if (var != NULL) {
	 return var;
      }
   }

   return NULL;
}

/*
 * Bind a (column) variable in the current scope.
 */
static void resolve_bind(struct resolve *ctx, struct ptcolumnvar *var) {
   struct ptcolumnvar *other;

   other = scope_find(ctx->current, var->name);
   if (other != NULL) {
      complain(ctx->pql, var->line, var->column, "Duplicate variable name %s",
	       var->name);
      complain(ctx->pql, other->line, other->column, "Previous binding for %s",
	       other->name);
      ctx->failed = true;
      return;
   }

   other = resolve_look(ctx, var->name);
   if (other != NULL) {
      complain(ctx->pql, var->line, var->column,
	       "Warning: variable %s shadows previous binding", var->name);
      complain(ctx->pql, other->line, other->column, "Previous binding for %s",
	       other->name);
   }

   ptcolumnvararray_add(ctx->pql, &ctx->current->boundvars, var, NULL);
   ptcolumnvararray_add(ctx->pql, &ctx->allcolumns, var, NULL);
}

/*
 * Look for a global variable, and create a reference to a new one if
 * it's not there. Unknown/unmatched variables are assumed to be global.
 * Nonexistent global variables are caught at eval time.
 *
 * Note: this DOES return a reference, because the var might be
 * freshly created.
 */
static struct ptglobalvar *resolve_getglobal(struct resolve *ctx,
					     unsigned line, unsigned column,
					     const char *name) {
   unsigned i, num;
   struct ptglobalvar *var;

   num = ptglobalvararray_num(&ctx->globals);
   for (i=0; i<num; i++) {
      var = ptglobalvararray_get(&ctx->globals, i);
      if (!strcmp(var->name, name)) {
	 ptglobalvar_incref(var);
	 return var;
      }
   }

   /* Wasn't found, make a new one */
   var = mkptglobalvar(ctx->pql, line, column, name);
   ptglobalvararray_add(ctx->pql, &ctx->globals, var, NULL);
   return var;
}

////////////////////////////////////////////////////////////

/*
 * Recursive traversal.
 */

static void ptpath_resolve(struct resolve *ctx, struct ptpath *pp);
static struct ptexpr *ptexpr_resolve(struct resolve *ctx, struct ptexpr *pe);

/*
 * Single columnvar, not yet resolved, comes from GROUP or UNGROUP.
 * (Attempts to group by globals must be rejected.)
 *
 * Return the replacement columnvar, the real and previously bound one
 * with the same name. Note that we must be careful not to introduce
 * any other references to the initially placed var before we get
 * here, or those references will become nonsensical.
 */
static struct ptcolumnvar *ptcolumnvar_resolve(struct resolve *ctx,
					       struct ptcolumnvar *var,
					       const char *opname) {
   struct ptcolumnvar *foundvar;

   foundvar = resolve_look(ctx, var->name);
   if (foundvar != NULL) {
      ptcolumnvar_incref(foundvar);
      /*ptcolumnvar_decref(var); -- done by region allocator */
      return foundvar;
   }
   else {
      /* must be global var */
      complain(ctx->pql, var->line, var->column,
	       "Cannot %s global %s", opname, var->name);
      ctx->failed = true;
      return var;
   }
}

/*
 * Array of columnvars, not yet resolved, comes from GROUP BY.
 * (Attempts to group by globals must be rejected.)
 *
 * Replace the columnvar in the array with the real (and previously
 * bound) one by the same name. Note that we must be careful not to
 * introduce any other references to the vars in the array before we
 * get here, or those references will become nonsensical.
 */
static void ptcolumnvararray_resolve(struct resolve *ctx,
				     struct ptcolumnvararray *vars,
				     const char *opname) {
   struct ptcolumnvar *var, *foundvar;
   unsigned i, num;
   
   num = ptcolumnvararray_num(vars);
   for (i=0; i<num; i++) {
      var = ptcolumnvararray_get(vars, i);
      foundvar = ptcolumnvar_resolve(ctx, var, opname);
      ptcolumnvararray_set(vars, i, foundvar);
   }
}

static void ptpatharray_resolve(struct resolve *ctx, struct ptpatharray *arr) {
   unsigned i, num;

   num = ptpatharray_num(arr);
   for (i=0; i<num; i++) {
      ptpath_resolve(ctx, ptpatharray_get(arr, i));
   }
}

static void ptexprarray_resolve(struct resolve *ctx, struct ptexprarray *arr) {
   unsigned i, num;
   struct ptexpr *pe;

   num = ptexprarray_num(arr);
   for (i=0; i<num; i++) {
      pe = ptexprarray_get(arr, i);
      pe = ptexpr_resolve(ctx, pe);
      ptexprarray_set(arr, i, pe);
   }
}

static void ptpath_resolve(struct resolve *ctx, struct ptpath *pp) {
   if (pp->bindobjbefore != NULL) {
      resolve_bind(ctx, pp->bindobjbefore);
   }

   switch (pp->type) {
    case PTP_SEQUENCE:
     ptpatharray_resolve(ctx, &pp->sequence.items);
     break;

    case PTP_ALTERNATES:
     ptpatharray_resolve(ctx, &pp->alternates.items);
     break;

    case PTP_OPTIONAL:
     ptpath_resolve(ctx, pp->optional.sub);
     break;

    case PTP_REPEATED:
     ptpath_resolve(ctx, pp->repeated.sub);
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	pp->edge.computedname = ptexpr_resolve(ctx, pp->edge.computedname);
     }
     break;
   }

   if (pp->bindobjafter != NULL) {
      resolve_bind(ctx, pp->bindobjafter);
   }
   if (pp->bindpath != NULL) {
      resolve_bind(ctx, pp->bindpath);
   }
}

static struct ptexpr *ptexpr_resolve(struct resolve *ctx, struct ptexpr *pe) {
   switch (pe->type) {

    case PTE_SELECT:
     resolve_pushscope(ctx);
     ptexpr_resolve(ctx, pe->select.sub);
     pe->select.result = ptexpr_resolve(ctx, pe->select.result);
     resolve_popscope(ctx);
     break;

    case PTE_FROM:
     ptexprarray_resolve(ctx, pe->from);
     break;

    case PTE_WHERE:
     pe->where.sub = ptexpr_resolve(ctx, pe->where.sub);
     pe->where.where = ptexpr_resolve(ctx, pe->where.where);
     break;

    case PTE_GROUP:
     pe->group.sub = ptexpr_resolve(ctx, pe->group.sub);
     ptcolumnvararray_resolve(ctx, pe->group.vars, "group by");
     if (pe->group.newvar != NULL) {
	/* add it into the current scope */
	resolve_bind(ctx, pe->group.newvar);
     }
     break;

    case PTE_UNGROUP:
     pe->ungroup.sub = ptexpr_resolve(ctx, pe->ungroup.sub);
     pe->ungroup.var = ptcolumnvar_resolve(ctx, pe->ungroup.var, "ungroup");
     break;

    case PTE_RENAME:
     pe->rename.sub = ptexpr_resolve(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	pe->rename.computedname = ptexpr_resolve(ctx, pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     pe->path.root = ptexpr_resolve(ctx, pe->path.root);
     ptpath_resolve(ctx, pe->path.body);
     /* these are only issued downstream of here */
     PQLASSERT(ptexprarray_num(&pe->path.morebindings) == 0);
     break;

    case PTE_TUPLE:
     ptexprarray_resolve(ctx, pe->tuple);
     break;

    case PTE_FORALL:
     pe->forall.set = ptexpr_resolve(ctx, pe->forall.set);
     resolve_pushscope(ctx);
     resolve_bind(ctx, pe->forall.var);
     pe->forall.predicate = ptexpr_resolve(ctx, pe->forall.predicate);
     resolve_popscope(ctx);
     break;

    case PTE_EXISTS:
     pe->exists.set = ptexpr_resolve(ctx, pe->exists.set);
     resolve_pushscope(ctx);
     resolve_bind(ctx, pe->exists.var);
     pe->exists.predicate = ptexpr_resolve(ctx, pe->exists.predicate);
     resolve_popscope(ctx);
     break;

    case PTE_MAP:
     pe->map.set = ptexpr_resolve(ctx, pe->map.set);
     resolve_pushscope(ctx);
     resolve_bind(ctx, pe->map.var);
     pe->map.result = ptexpr_resolve(ctx, pe->map.result);
     resolve_popscope(ctx);
     break;

    case PTE_ASSIGN:
     pe->assign.value = ptexpr_resolve(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	/* variable only exists within the body */
	resolve_pushscope(ctx);
	resolve_bind(ctx, pe->assign.var);
	pe->assign.body = ptexpr_resolve(ctx, pe->assign.body);
	resolve_popscope(ctx);
     }
     else {
	/* variable is entered into current scope */
	resolve_bind(ctx, pe->assign.var);
     }
     break;

    case PTE_BOP:
     pe->bop.l = ptexpr_resolve(ctx, pe->bop.l);
     pe->bop.r = ptexpr_resolve(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     pe->uop.sub = ptexpr_resolve(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	ptexprarray_resolve(ctx, pe->func.args);
     }
     break;

    case PTE_READANYVAR:
     {
	struct ptcolumnvar *cvar;
	struct ptglobalvar *gvar;
	
	cvar = resolve_look(ctx, pe->readanyvar.name);
	if (cvar != NULL) {
	   ptcolumnvar_incref(cvar);
	   //ptexpr_destroy(pe); -- nope! region allocator covers it
	   pe = mkptexpr_readcolumnvar(ctx->pql, cvar);
	}
	else {
	   /* must be global var */
	   gvar = resolve_getglobal(ctx,
				    pe->readanyvar.line, pe->readanyvar.column,
				    pe->readanyvar.name);
	   PQLASSERT(gvar != NULL);
	   //ptexpr_destroy(pe); -- nope! region allocator covers it
	   pe = mkptexpr_readglobalvar(ctx->pql, gvar);
	}
     }
     break;
     
    case PTE_READCOLUMNVAR:
    case PTE_READGLOBALVAR:
     /*
      * These do not exist in the input; we create them from
      * READANYVAR. However, parser-level expansion of syntactic sugar
      * can create them upstream of here. There's not much point in
      * creating such references unresolved and then resolving them
      * now. So if one comes along, just go by.
      */
     break;

    case PTE_VALUE:
     /* nothing to do */
     break;
   }

   return pe;
}

////////////////////////////////////////////////////////////

/*
 * Print warnings for variable names (column or global) that are used
 * more than once.
 */
static void check_duplicate_varnames(struct resolve *ctx) {
   unsigned cnum, gnum, i, j;
   struct ptglobalvar *gv1, *gv2;
   struct ptcolumnvar *cv1, *cv2;

   gnum = ptglobalvararray_num(&ctx->globals);
   cnum = ptcolumnvararray_num(&ctx->allcolumns);

   for (i=0; i<gnum; i++) {
      gv1 = ptglobalvararray_get(&ctx->globals, i);
      for (j=i+1; j<gnum; j++) {
	 gv2 = ptglobalvararray_get(&ctx->globals, j);

	 /* No global var should be listed twice. */
	 PQLASSERT(gv1 != gv2);
	 /* No two global vars should have the same name. */
	 PQLASSERT(strcmp(gv1->name, gv2->name) != 0);
      }
   }

   for (i=0; i<cnum; i++) {
      cv1 = ptcolumnvararray_get(&ctx->allcolumns, i);
      for (j=i+1; j<cnum; j++) {
	 cv2 = ptcolumnvararray_get(&ctx->allcolumns, j);

	 /* No column var should be listed twice. */
	 PQLASSERT(cv1 != cv2);
	 /* Warn if two column vars have the same name. */
	 if (!strcmp(cv1->name, cv2->name)) {
	    complain(ctx->pql, cv2->line, cv2->column,
		     "Warning: Variable name %s rebound in a later context",
		     cv2->name);
	    complain(ctx->pql, cv2->line, cv2->column,
		     "Warning: This is often a mistake "
		     "and can cause substantial confusion");
	    complain(ctx->pql, cv1->line, cv1->column,
		     "First binding was here");
	 }
      }
   }

   for (i=0; i<gnum; i++) {
      gv1 = ptglobalvararray_get(&ctx->globals, i);
      for (j=0; j<cnum; j++) {
	 cv2 = ptcolumnvararray_get(&ctx->allcolumns, j);

	 /* Warn if a global var and a column var have the same name. */
	 if (!strcmp(gv1->name, cv2->name)) {
	    complain(ctx->pql, cv2->line, cv2->column,
		     "Warning: Name of locally-bound variable %s also "
		     "used as a global variable",
		     cv2->name);
	    complain(ctx->pql, gv1->line, gv1->column,
		     "First global reference was here");
	 }
      }
   }
}

////////////////////////////////////////////////////////////

/*
 * Entry point.
 *
 * Note that the whole query (and, when we add statements, each
 * expression) gets its own scope, so that if we get an expression
 * like "foo.bar{B}" with no select-from-where the resulting variable
 * has a well defined scope. This is equivalent to the old code that
 * wrapped the query in a vacuous select-from-where if it wasn't
 * already one at the top level.
 */
struct ptexpr *resolvevars(struct pqlcontext *pql, struct ptexpr *pe) {
   struct resolve ctx;
   bool failed;

   resolve_init(&ctx, pql);
   pe = ptexpr_resolve(&ctx, pe);
   check_duplicate_varnames(&ctx);
   failed = ctx.failed;
   resolve_cleanup(pql, &ctx);

   if (failed) {
      //ptexpr_destroy(pe); -- not needed! region allocator handles it
      return NULL;
   }
   return pe;
}
