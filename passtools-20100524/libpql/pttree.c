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

#include <string.h>

#include "pqlvalue.h"
#include "layout.h"
#include "passes.h"	// where ptmemory_create currently lives
#include "pqlcontext.h"

#define PTTREE_INLINE
#include "pttree.h"

////////////////////////////////////////////////////////////

/*
 * Destroy ONLY the pathnode, not the things it points to that are
 * registered separately. All columnvars, strings, path, and expr
 * nodes are registered separately. We only need to clean up embedded
 * arrays that are not separately allocated.
 */
static void ptpath_destroy(struct pqlcontext *pql, struct ptpath *pp) {
   switch (pp->type) {
    case PTP_SEQUENCE:
     // items[] contents are registered separately
     ptpatharray_setsize(pql, &pp->sequence.items, 0);
     ptpatharray_cleanup(pql, &pp->sequence.items);
     break;
    case PTP_ALTERNATES:
     // items[] contents are registered separately
     ptpatharray_setsize(pql, &pp->alternates.items, 0);
     ptpatharray_cleanup(pql, &pp->alternates.items);
     // tailvar is registered separately
     break;
    case PTP_OPTIONAL:
     // sub is registered separately
     // nilcolumns[] contents are registered separately
     ptcolumnvararray_setsize(pql, &pp->optional.nilcolumns, 0);
     ptcolumnvararray_cleanup(pql, &pp->optional.nilcolumns);
     break;
    case PTP_REPEATED:
     // sub is registered separately
     // pathfrominside and pathonoutside are registered separately
     break;
    case PTP_NILBIND:
     // columnsbefore[] contents are registered separately
     ptcolumnvararray_setsize(pql, &pp->nilbind.columnsbefore, 0);
     ptcolumnvararray_cleanup(pql, &pp->nilbind.columnsbefore);
     // sub is registered separately
     // columnsafter[] contents are registered separately
     ptcolumnvararray_setsize(pql, &pp->nilbind.columnsafter, 0);
     ptcolumnvararray_cleanup(pql, &pp->nilbind.columnsafter);
     break;
    case PTP_EDGE:
     // staticname/computedname is registered separately
     break;
   }
   // bindobjbefore/bindobjafter/bindpath are registered separately
   dofree(pql, pp, sizeof(*pp));
}

/*
 * Likewise.
 */
static void ptexpr_destroy(struct pqlcontext *pql, struct ptexpr *pe) {
   switch (pe->type) {
    case PTE_SELECT:
     // sub, result are registered separately
     break;
    case PTE_FROM:
     // from is registered separately
     break;
    case PTE_WHERE:
     // sub, where are registered separately
     break;
    case PTE_GROUP:
     // sub, vars, newvar are registered separately
     break;
    case PTE_UNGROUP:
     // sub, var are registered separately
     break;
    case PTE_RENAME:
     // staticname/computedname and sub are registered separately
     break;
    case PTE_PATH:
     // root, body are registered separately
     // morebindings[] contents are registered separately
     ptexprarray_setsize(pql, &pe->path.morebindings, 0);
     ptexprarray_cleanup(pql, &pe->path.morebindings);
     break;
    case PTE_TUPLE:
     // tuple is registered separately
     break;
    case PTE_FORALL:
     // var, set, predicate are registered separately
     break;
    case PTE_EXISTS:
     // var, set, predicate are registered separately
     break;
    case PTE_MAP:
     // var, set, result are registered separately
     break;
    case PTE_ASSIGN:
     // var, value, body are registered separately
     break;
    case PTE_BOP:
     // l,r are registered separately
     break;
    case PTE_UOP:
     // sub is registered separately
     break;
    case PTE_FUNC:
     // args is registered separately
     break;
    case PTE_READANYVAR:
     // name is registered separately
     break;
    case PTE_READCOLUMNVAR:
     // readcolumnvar is registered separately
     break;
    case PTE_READGLOBALVAR:
     // readglobalvar is registered separately
     break;
    case PTE_VALUE:
     // value is registered separately
     break;
   }
   dofree(pql, pe, sizeof(*pe));
}

////////////////////////////////////////////////////////////

/* array of ptexprarrays */
DECLARRAY(ptexprarray);
DEFARRAY(ptexprarray, /*noinline*/);

/* array of ptcolumnvararrays */
DECLARRAY(ptcolumnvararray);
DEFARRAY(ptcolumnvararray, /*noinline*/);

struct ptmanager {
   struct pqlcontext *pql;

   /* loose pointers */
   struct array pointers;
   size_t *sizes;

   /* node types */
   struct pqlvaluearray values;
   struct ptpatharray ptpaths;
   struct ptexprarray ptexprs;

   /* array types */
   struct ptexprarrayarray arrays;
   struct ptcolumnvararrayarray cvarrays;
};

void ptmanager_destroyall(struct ptmanager *ptm) {
   unsigned i, num;
   struct ptexprarray *arr;
   struct ptcolumnvararray *cvarr;

   /* pointers allocated with domalloc -> dofree */
   num = array_num(&ptm->pointers);
   for (i=0; i<num; i++) {
      dofree(ptm->pql, array_get(&ptm->pointers, i), ptm->sizes[i]);
   }
   array_setsize(ptm->pql, &ptm->pointers, 0);
   dofree(ptm->pql, ptm->sizes, num * sizeof(ptm->sizes[0]));
   ptm->sizes = 0;

   /* values allocated with pqlvalue_* -> pqlvalue_destroy */
   num = pqlvaluearray_num(&ptm->values);
   for (i=0; i<num; i++) {
      pqlvalue_destroy(pqlvaluearray_get(&ptm->values, i));
   }
   pqlvaluearray_setsize(ptm->pql, &ptm->values, 0);

   /* ptpath nodes -> ptpath_destroy */
   num = ptpatharray_num(&ptm->ptpaths);
   for (i=0; i<num; i++) {
      ptpath_destroy(ptm->pql, ptpatharray_get(&ptm->ptpaths, i));
   }
   ptpatharray_setsize(ptm->pql, &ptm->ptpaths, 0);

   /* ptexpr nodes -> ptexpr_destroy */
   num = ptexprarray_num(&ptm->ptexprs);
   for (i=0; i<num; i++) {
      ptexpr_destroy(ptm->pql, ptexprarray_get(&ptm->ptexprs, i));
   }
   ptexprarray_setsize(ptm->pql, &ptm->ptexprs, 0);

   /* arrays allocated with ptexprarray_create -> _destroy (contents void) */
   num = ptexprarrayarray_num(&ptm->arrays);
   for (i=0; i<num; i++) {
      arr = ptexprarrayarray_get(&ptm->arrays, i);
      ptexprarray_setsize(ptm->pql, arr, 0);
      ptexprarray_destroy(ptm->pql, arr);
   }
   ptexprarrayarray_setsize(ptm->pql, &ptm->arrays, 0);

   /* arrays allocated with ptcolumnvararray_create -> _destroy (ditto) */
   num = ptcolumnvararrayarray_num(&ptm->cvarrays);
   for (i=0; i<num; i++) {
      cvarr = ptcolumnvararrayarray_get(&ptm->cvarrays, i);
      ptcolumnvararray_setsize(ptm->pql, cvarr, 0);
      ptcolumnvararray_destroy(ptm->pql, cvarr);
   }
   ptcolumnvararrayarray_setsize(ptm->pql, &ptm->cvarrays, 0);
}

struct ptmanager *ptmanager_create(struct pqlcontext *pql) {
   struct ptmanager *ptm;

   ptm = domalloc(pql, sizeof(*ptm));
   ptm->pql = pql;

   array_init(&ptm->pointers);
   ptm->sizes = NULL;

   pqlvaluearray_init(&ptm->values);
   ptpatharray_init(&ptm->ptpaths);
   ptexprarray_init(&ptm->ptexprs);

   ptexprarrayarray_init(&ptm->arrays);
   ptcolumnvararrayarray_init(&ptm->cvarrays);

   return ptm;
}

void ptmanager_destroy(struct ptmanager *ptm) {
   ptmanager_destroyall(ptm);

   array_cleanup(ptm->pql, &ptm->pointers);
   PQLASSERT(ptm->sizes == NULL);

   pqlvaluearray_cleanup(ptm->pql, &ptm->values);
   ptpatharray_cleanup(ptm->pql, &ptm->ptpaths);
   ptexprarray_cleanup(ptm->pql, &ptm->ptexprs);

   ptexprarrayarray_cleanup(ptm->pql, &ptm->arrays);
   ptcolumnvararrayarray_cleanup(ptm->pql, &ptm->cvarrays);

   dofree(ptm->pql, ptm, sizeof(*ptm));
}

static void ptmanager_add_pointer(struct ptmanager *ptm, void *ptr,
				  size_t size) {
   unsigned ix;

   array_add(ptm->pql, &ptm->pointers, ptr, &ix);
   ptm->sizes = dorealloc(ptm->pql, ptm->sizes, ix*sizeof(ptm->sizes[0]),
			  (ix+1)*sizeof(ptm->sizes[0]));
   ptm->sizes[ix] = size;
}

static void ptmanager_add_value(struct ptmanager *ptm, struct pqlvalue *val) {
   pqlvaluearray_add(ptm->pql, &ptm->values, val, NULL);
}

static void ptmanager_add_ptpath(struct ptmanager *ptm, struct ptpath *val) {
   ptpatharray_add(ptm->pql, &ptm->ptpaths, val, NULL);
}

static void ptmanager_add_ptexpr(struct ptmanager *ptm, struct ptexpr *val) {
   ptexprarray_add(ptm->pql, &ptm->ptexprs, val, NULL);
}

void ptmanager_add_exprarray(struct ptmanager *ptm, struct ptexprarray *arr) {
   ptexprarrayarray_add(ptm->pql, &ptm->arrays, arr, NULL);
}

void ptmanager_add_columnvararray(struct ptmanager *ptm,
				  struct ptcolumnvararray *arr) {
   ptcolumnvararrayarray_add(ptm->pql, &ptm->cvarrays, arr, NULL);
}

////////////////////////////////////////////////////////////

static void *pt_malloc(struct pqlcontext *pql, size_t len) {
   void *ptr;

   ptr = domalloc(pql, len);
   ptmanager_add_pointer(pql->ptm, ptr, len);
   return ptr;
}

static char *pt_strdup(struct pqlcontext *pql, const char *str) {
   char *ret;

   ret = dostrdup(pql, str);
   ptmanager_add_pointer(pql->ptm, ret, strlen(ret)+1);
   return ret;
}

static char *pt_strndup(struct pqlcontext *pql, const char *str, size_t len) {
   char *ret;

   ret = dostrndup(pql, str, len);
   ptmanager_add_pointer(pql->ptm, ret, len+1);
   return ret;
}

////////////////////////////////////////////////////////////

struct ptglobalvar *mkptglobalvar(struct pqlcontext *pql,
				  unsigned line, unsigned column,
				  const char *name) {
   struct ptglobalvar *ret;

   ret = pt_malloc(pql, sizeof(*ret));
   ret->line = line;
   ret->column = column;
   ret->refcount = 1;
   ret->name = pt_strdup(pql, name);
   return ret;
}

void ptglobalvar_incref(struct ptglobalvar *var) {
   var->refcount++;
}

//////////////////////////////

struct ptcolumnvar *mkptcolumnvar(struct pqlcontext *pql,
				  unsigned line, unsigned column,
				  const char *name, size_t namelen) {
   struct ptcolumnvar *ret;

   ret = pt_malloc(pql, sizeof(*ret));
   ret->line = line;
   ret->column = column;
   ret->refcount = 1;
   ret->name = pt_strndup(pql, name, namelen);
   ret->id = pql->nextcolumnid++;
   return ret;
}

struct ptcolumnvar *mkptcolumnvar_fresh(struct pqlcontext *pql) {
   char name[128];
   pqlcontext_getfreshname(pql, name, sizeof(name));
   return mkptcolumnvar(pql, 0, 0, name, strlen(name));
}

void ptcolumnvar_incref(struct ptcolumnvar *var) {
   var->refcount++;
}

//////////////////////////////

static struct ptpath *mkptpath(struct pqlcontext *pql, enum ptpathtypes type) {
   struct ptpath *ret;

   ret = domalloc(pql, sizeof(*ret));
   ptmanager_add_ptpath(pql->ptm, ret);
   ret->type = type;
   ret->bindobjbefore = NULL;
   ret->bindobjafter = NULL;
   ret->bindpath = NULL;
   ret->dontmerge = false;
   ret->parens = false;
   return ret;
}

struct ptpath *mkptpath_emptysequence(struct pqlcontext *pql) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_SEQUENCE);
   ptpatharray_init(&ret->sequence.items);
   return ret;
}

struct ptpath *mkptpath_emptyalternates(struct pqlcontext *pql) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_ALTERNATES);
   ptpatharray_init(&ret->alternates.items);
   ret->alternates.tailvar = NULL;
   return ret;
}

struct ptpath *mkptpath_optional(struct pqlcontext *pql, struct ptpath *sub) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_OPTIONAL);
   ret->optional.sub = sub;
   ptcolumnvararray_init(&ret->optional.nilcolumns);
   return ret;
}

struct ptpath *mkptpath_repeated(struct pqlcontext *pql, struct ptpath *sub) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_REPEATED);
   ret->repeated.sub = sub;
   ret->repeated.pathfrominside = NULL;
   // XXX: should pathonoutside exist or should we just use ->bindpath?
   ret->repeated.pathonoutside = NULL;
   return ret;
}

struct ptpath *mkptpath_nilbind(struct pqlcontext *pql, struct ptpath *sub) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_NILBIND);
   ret->nilbind.sub = sub;
   ptcolumnvararray_init(&ret->nilbind.columnsbefore);
   ptcolumnvararray_init(&ret->nilbind.columnsafter);
   return ret;
}

struct ptpath *mkptpath_staticedge(struct pqlcontext *pql,
				   const char *name, size_t namelen,
				   bool reversed) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_EDGE);
   ret->edge.iscomputed = false;
   ret->edge.staticname = pt_strndup(pql, name, namelen);
   ret->edge.reversed = reversed;
   return ret;
}

struct ptpath *mkptpath_computededge(struct pqlcontext *pql,
				     struct ptexpr *computedname,
				     bool reversed) {
   struct ptpath *ret;

   ret = mkptpath(pql, PTP_EDGE);
   ret->edge.iscomputed = true;
   ret->edge.computedname = computedname;
   ret->edge.reversed = reversed;
   return ret;
}

//////////////////////////////

static struct ptexpr *mkptexpr(struct pqlcontext *pql, enum ptexprtypes type) {
   struct ptexpr *ret;

   ret = domalloc(pql, sizeof(*ret));
   ptmanager_add_ptexpr(pql->ptm, ret);
   ret->type = type;
   return ret;
}

struct ptexpr *mkptexpr_select(struct pqlcontext *pql,
			       struct ptexpr *sub,
			       struct ptexpr *result,
			       bool distinct) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_SELECT);
   ret->select.sub = sub;
   ret->select.result = result;
   ret->select.distinct = distinct;
   return ret;
}

struct ptexpr *mkptexpr_from(struct pqlcontext *pql,
			     struct ptexprarray *from) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_FROM);
   ret->from = from;
   return ret;
}

struct ptexpr *mkptexpr_where(struct pqlcontext *pql,
			      struct ptexpr *sub,
			      struct ptexpr *where) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_WHERE);
   ret->where.sub = sub;
   ret->where.where = where;
   return ret;
}

struct ptexpr *mkptexpr_group(struct pqlcontext *pql,
			      struct ptexpr *sub,
			      struct ptcolumnvararray *vars,
			      struct ptcolumnvar *newvar) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_GROUP);
   ret->group.sub = sub;
   ret->group.vars = vars;
   ret->group.newvar = newvar;
   return ret;
}

struct ptexpr *mkptexpr_ungroup(struct pqlcontext *pql,
				struct ptexpr *sub,
				struct ptcolumnvar *var) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_UNGROUP);
   ret->ungroup.sub = sub;
   ret->ungroup.var = var;
   return ret;
}

struct ptexpr *mkptexpr_rename_static(struct pqlcontext *pql,
				      const char *name, size_t namelen,
				      struct ptexpr *sub) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_RENAME);
   ret->rename.iscomputed = false;
   ret->rename.staticname = pt_strndup(pql, name, namelen);
   ret->rename.sub = sub;
   return ret;
}

struct ptexpr *mkptexpr_rename_computed(struct pqlcontext *pql,
					struct ptexpr *name,
					struct ptexpr *sub) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_RENAME);
   ret->rename.iscomputed = true;
   ret->rename.computedname = name;
   ret->rename.sub = sub;
   return ret;
}

struct ptexpr *mkptexpr_path(struct pqlcontext *pql,
			     struct ptexpr *root,
			     struct ptpath *body) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_PATH);
   ret->path.root = root;
   ret->path.body = body;
   ptexprarray_init(&ret->path.morebindings);
   return ret;
}

struct ptexpr *mkptexpr_tuple(struct pqlcontext *pql,
			      struct ptexprarray *exprs) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_TUPLE);
   ret->tuple = exprs;
   return ret;
}

struct ptexpr *mkptexpr_forall(struct pqlcontext *pql,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *predicate) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_FORALL);
   ret->forall.var = var;
   ret->forall.set = set;
   ret->forall.predicate = predicate;
   return ret;
}

struct ptexpr *mkptexpr_exists(struct pqlcontext *pql,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *predicate) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_EXISTS);
   ret->exists.var = var;
   ret->exists.set = set;
   ret->exists.predicate = predicate;
   return ret;
}

struct ptexpr *mkptexpr_map(struct pqlcontext *pql,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *result) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_MAP);
   ret->map.var = var;
   ret->map.set = set;
   ret->map.result = result;
   return ret;
}

struct ptexpr *mkptexpr_assign(struct pqlcontext *pql,
			       struct ptcolumnvar *var,
			       struct ptexpr *value,
			       struct ptexpr *body) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_ASSIGN);
   ret->assign.var = var;
   ret->assign.value = value;
   ret->assign.body = body;
   return ret;
}

struct ptexpr *mkptexpr_bop(struct pqlcontext *pql,
			    struct ptexpr *l,
			    enum functions op,
			    struct ptexpr *r) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_BOP);
   ret->bop.l = l;
   ret->bop.op = op;
   ret->bop.r = r;
   return ret;
}

struct ptexpr *mkptexpr_uop(struct pqlcontext *pql,
			    enum functions op,
			    struct ptexpr *sub) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_UOP);
   ret->uop.op = op;
   ret->uop.sub = sub;
   return ret;
}

struct ptexpr *mkptexpr_func(struct pqlcontext *pql,
			     enum functions op,
			     struct ptexprarray *args) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_FUNC);
   ret->func.op = op;
   ret->func.args = args;
   return ret;
}

struct ptexpr *mkptexpr_readanyvar(struct pqlcontext *pql,
				   unsigned line, unsigned column,
				   const char *name, size_t namelen) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_READANYVAR);
   ret->readanyvar.line = line;
   ret->readanyvar.column = column;
   ret->readanyvar.name = pt_strndup(pql, name, namelen);
   return ret;
}

struct ptexpr *mkptexpr_readcolumnvar(struct pqlcontext *pql,
				      struct ptcolumnvar *var) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_READCOLUMNVAR);
   ret->readcolumnvar = var;
   return ret;
}

struct ptexpr *mkptexpr_readglobalvar(struct pqlcontext *pql,
				      struct ptglobalvar *var) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_READGLOBALVAR);
   ret->readglobalvar = var;
   return ret;
}

struct ptexpr *mkptexpr_value(struct pqlcontext *pql,
			      struct pqlvalue *val) {
   struct ptexpr *ret;

   ret = mkptexpr(pql, PTE_VALUE);
   ret->value = val;
   ptmanager_add_value(pql->ptm, val);
   return ret;
}

////////////////////////////////////////////////////////////

static struct layout *ptexpr_layout(struct pqlcontext *pql, struct ptexpr *pe);

static struct layout *ptglobalvar_layout(struct pqlcontext *pql,
					 struct ptglobalvar *v) {
   return mklayout_wrap(pql, "<<", mklayout_text(pql, v->name), ">>");
}

static struct layout *ptcolumnvar_layout(struct pqlcontext *pql,
					 struct ptcolumnvar *v) {
   return mklayout_text(pql, v->name);
}

static struct layout *ptcolumnvararray_layout(struct pqlcontext *pql,
					      struct ptcolumnvararray *arr) {
   unsigned i, num;
   struct layout *l2;
   struct layout *ret;

   ret = mklayout_sequence_empty(pql);
   num = ptcolumnvararray_num(arr);
   for (i=0; i<num; i++) {
      if (i > 0) {
	 layoutarray_add(pql, &ret->sequence, mklayout_text(pql, ","), NULL);
      }
      l2 = ptcolumnvar_layout(pql, ptcolumnvararray_get(arr, i));
      layoutarray_add(pql, &ret->sequence, l2, NULL);
   }
   return ret;
}

static struct layout *ptpath_layout(struct pqlcontext *pql, struct ptpath *pp){
   unsigned i, num;
   struct layout *ret;
   struct layout *l2;

   switch (pp->type) {
    case PTP_SEQUENCE:
     ret = mklayout_sequence_empty(pql);
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	l2 = ptpath_layout(pql, ptpatharray_get(&pp->sequence.items, i));
	layoutarray_add(pql, &ret->sequence, l2, NULL);
     }
     break;

    case PTP_ALTERNATES:
     ret = mklayout_sequence_empty(pql);
     num = ptpatharray_num(&pp->sequence.items);
     for (i=0; i<num; i++) {
	if (i > 0) {
	   layoutarray_add(pql, &ret->sequence, mklayout_text(pql, "|"), NULL);
	}
	l2 = ptpath_layout(pql, ptpatharray_get(&pp->sequence.items, i));
	layoutarray_add(pql, &ret->sequence, l2, NULL);
     }
     break;

    case PTP_OPTIONAL:
     if (pp->optional.sub->type == PTP_REPEATED &&
	 pp->optional.sub->repeated.sub->type == PTP_EDGE &&
	 pp->optional.sub->repeated.sub->bindobjbefore == NULL && 
	 pp->optional.sub->repeated.sub->bindobjafter == NULL && 
	 pp->optional.sub->repeated.sub->bindpath == NULL && 
	 !pp->optional.sub->repeated.sub->edge.reversed &&
	 !pp->optional.sub->repeated.sub->edge.iscomputed &&
	 !strcmp(pp->optional.sub->repeated.sub->edge.staticname, "%")) {
	ret = mklayout_text(pql, ".#");
	break;
     }

     if (pp->optional.sub->type == PTP_REPEATED) {
	/* check we got what we expected */
	ret = ptpath_layout(pql, pp->optional.sub);
	PQLASSERT(ret->type == L_SEQUENCE);
	PQLASSERT(layoutarray_num(&ret->sequence) == 2);
	l2 = layoutarray_get(&ret->sequence, 1);
	if (l2->type == L_SEQUENCE) {
	   /* the pathfrominside != NULL case below */
	   PQLASSERT(layoutarray_num(&l2->sequence) == 2);
	   l2 = layoutarray_get(&l2->sequence, 0);
	}
	PQLASSERT(l2->type == L_TEXT);
	PQLASSERT(l2->text.width == 1);
	PQLASSERT(!strcmp(l2->text.string, "+"));
	/* kind of gross: convert + to * */
	l2->text.string[0] = '*';
	break;
     }

     ret = ptpath_layout(pql, pp->optional.sub);
     ret = mklayout_pair(pql, ret, mklayout_text(pql, "?"));
     /* skip nilcolumns[] for now */
     break;

    case PTP_REPEATED:
     l2 = mklayout_text(pql, "+");

     if (pp->repeated.pathfrominside != NULL) {
	struct layout *inl, *outl, *l3;

	inl = ptcolumnvar_layout(pql, pp->repeated.pathfrominside);
	outl = ptcolumnvar_layout(pql, pp->repeated.pathonoutside);
	l3 = mklayout_triple(pql, inl, mklayout_text(pql, "->"), outl);
	l3 = mklayout_wrap(pql, "[", l3, "]");
	l2 = mklayout_pair(pql, l2, l3);
     }

     ret = ptpath_layout(pql, pp->repeated.sub);
     ret = mklayout_pair(pql, ret, l2);
     break;

    case PTP_NILBIND:
     ret = NULL;
     num = ptcolumnvararray_num(&pp->nilbind.columnsbefore);
     for (i=0; i<num; i++) {
	l2 = ptcolumnvar_layout(pql,
				ptcolumnvararray_get(&pp->nilbind.columnsbefore, i));
	l2 = mklayout_wrap(pql, "{!", l2, "!}");
	ret = ret ? mklayout_pair(pql, ret, l2) : l2;
     }

     l2 = ptpath_layout(pql, pp->nilbind.sub);
     ret = ret ? mklayout_pair(pql, ret, l2) : l2;

     num = ptcolumnvararray_num(&pp->nilbind.columnsafter);
     for (i=0; i<num; i++) {
	l2 = ptcolumnvar_layout(pql,
				ptcolumnvararray_get(&pp->nilbind.columnsafter, i));
	l2 = mklayout_wrap(pql, "{!", l2, "!}");
	ret = mklayout_pair(pql, ret, l2);
     }
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	ret = ptexpr_layout(pql, pp->edge.computedname);
	ret = mklayout_wrap(pql, "[[", ret, "]]");
     }
     else {
	ret = mklayout_text(pql, pp->edge.staticname);
     }
     ret = mklayout_pair(pql, mklayout_text(pql, "."), ret);
     if (pp->edge.reversed) {
	ret = mklayout_pair(pql, ret, mklayout_text(pql, "-of"));
     }
     break;
   }

   if (pp->parens) {
      ret = mklayout_wrap(pql, "(", ret, ")");
   }

   // XXX don't know how to print this
   PQLASSERT(pp->bindobjbefore == NULL);

   if (pp->bindpath != NULL) {
      l2 = ptcolumnvar_layout(pql, pp->bindpath);
      ret = mklayout_triple(pql, ret, mklayout_text(pql, "@"), l2);
   }
   if (pp->bindobjafter != NULL) {
      l2 = ptcolumnvar_layout(pql, pp->bindobjafter);
      ret = mklayout_pair(pql, ret, mklayout_wrap(pql, "{", l2, "}"));
   }

   return ret;
}

static struct layout *ptexprarray_layout(struct pqlcontext *pql,
					 struct ptexprarray *arr) {
   unsigned i, num;
   struct layout *l2;
   struct layout *ret;

   ret = mklayout_sequence_empty(pql);
   num = ptexprarray_num(arr);
   for (i=0; i<num; i++) {
      if (i > 0) {
	 layoutarray_add(pql, &ret->sequence, mklayout_text(pql, ","), NULL);
      }
      l2 = ptexpr_layout(pql, ptexprarray_get(arr, i));
      layoutarray_add(pql, &ret->sequence, l2, NULL);
   }
   return ret;
}

static bool ptexpr_select_layout(struct pqlcontext *pql, struct layout *ret,
				 struct ptexpr *pe) {
   struct layout *text, *expr;
   bool seenwhere;

   if (pe == NULL) {
      PQLASSERT(0);
      seenwhere = false;
      text = mklayout_text(pql, "from");
      expr = mklayout_text(pql, "nothing");
   }
   else {
      switch (pe->type) {
       case PTE_FROM:
	seenwhere = false;
	text = mklayout_text(pql, "from");
	if (ptexprarray_num(pe->from) > 0) {
	   expr = ptexprarray_layout(pql, pe->from);
	}
	else {
	   expr = mklayout_text(pql, "nothing");
	}
	break;
       case PTE_WHERE:
	ptexpr_select_layout(pql, ret, pe->where.sub);
	text = mklayout_text(pql, "where");
	expr = ptexpr_layout(pql, pe->where.where);
	seenwhere = true;
	break;
       case PTE_GROUP:
	seenwhere = ptexpr_select_layout(pql, ret, pe->group.sub);
	text = mklayout_text(pql, "group by");
	expr = ptcolumnvararray_layout(pql, pe->group.vars);
	if (pe->group.newvar != NULL) {
	   expr = mklayout_triple(pql, expr,
				  mklayout_text(pql, "with"),
				  ptcolumnvar_layout(pql, pe->group.newvar));
	}
	break;
       case PTE_UNGROUP:
	seenwhere = ptexpr_select_layout(pql, ret, pe->ungroup.sub);
	text = mklayout_text(pql, "ungroup");
	expr = ptcolumnvar_layout(pql, pe->ungroup.var);
	break;
       default:
	PQLASSERT(0);
	break;
      }
   }

   PQLASSERT(ret->type == L_LEFTALIGN);
   layoutarray_add(pql, &ret->leftalign, mklayout_pair(pql, text, expr), NULL);
   return seenwhere;
}

static struct layout *ptexpr_layout(struct pqlcontext *pql, struct ptexpr *pe){
   struct layout *ret;

   ret = NULL; // gcc 4.1

   switch (pe->type) {
    case PTE_SELECT:
     {
	struct layout *text, *expr;
	bool seenwhere;

	ret = mklayout_leftalign_empty(pql);

	text = mklayout_text(pql, "select");
	if (pe->select.distinct) {
	   text = mklayout_pair(pql, text, mklayout_text(pql, "distinct"));
	}
	expr = ptexpr_layout(pql, pe->select.result);
	layoutarray_add(pql, &ret->leftalign, mklayout_pair(pql, text, expr), NULL);

	seenwhere = ptexpr_select_layout(pql, ret, pe->select.sub);
	if (!seenwhere) {
	   text = mklayout_text(pql, "where");
	   expr = mklayout_text(pql, "true");
	   layoutarray_add(pql, &ret->leftalign, mklayout_pair(pql, text, expr),
			   NULL);
	}
     }
     break;

    case PTE_FROM:
    case PTE_WHERE:
    case PTE_GROUP:
    case PTE_UNGROUP:
     /* handled inside PTE_SELECT */
     PQLASSERT(0);
     break;

    case PTE_RENAME:
     {
	struct layout *lsub, *lname;

	lsub = ptexpr_layout(pql, pe->rename.sub);
	if (pe->rename.iscomputed) {
	   lname = ptexpr_layout(pql, pe->rename.computedname);
	   lname = mklayout_wrap(pql, "[[", lname, "]]");
	}
	else {
	   lname = mklayout_text(pql, pe->rename.staticname);
	}
	ret = mklayout_triple(pql, lsub, mklayout_text(pql, "as"), lname);
     }
     break;

    case PTE_PATH:
     {
	struct layout *lroot, *lbody;
	struct ptexpr *sublet;
	unsigned i, num;

	lroot = ptexpr_layout(pql, pe->path.root);
	lbody = ptpath_layout(pql, pe->path.body);
	ret = mklayout_pair(pql, lroot, lbody);
	num = ptexprarray_num(&pe->path.morebindings);
	for (i=0; i<num; i++) {
	   sublet = ptexprarray_get(&pe->path.morebindings, i);
	   PQLASSERT(sublet->type == PTE_ASSIGN);
	   PQLASSERT(sublet->assign.body == NULL);
	   ret = mklayout_pair(pql, ret,
			       mklayout_wrap(pql, "{{", 
					     ptexpr_layout(pql, sublet),
					     "}}"));
	}
     }
     break;

    case PTE_TUPLE:
     ret = ptexprarray_layout(pql, pe->tuple);
     break;

    case PTE_FORALL:
     {
	struct layout *lvar, *lset, *lpred, *lhead;

	lvar = ptcolumnvar_layout(pql, pe->forall.var);
	lset = ptexpr_layout(pql, pe->forall.set);
	lpred = ptexpr_layout(pql, pe->forall.predicate);

	lhead = mklayout_quint(pql, mklayout_text(pql, "for all"), lvar,
			       mklayout_text(pql, "in"), lset,
			       mklayout_text(pql, ":"));
	ret = mklayout_indent(pql, lhead, lpred, NULL);
     }
     break;

    case PTE_EXISTS:
     {
	struct layout *lvar, *lset, *lpred, *lhead;

	lvar = ptcolumnvar_layout(pql, pe->exists.var);
	lset = ptexpr_layout(pql, pe->exists.set);
	lpred = ptexpr_layout(pql, pe->exists.predicate);

	lhead = mklayout_quint(pql, mklayout_text(pql, "exists"), lvar,
			       mklayout_text(pql, "in"), lset,
			       mklayout_text(pql, ":"));
	ret = mklayout_indent(pql, lhead, lpred, NULL);
     }
     break;

    case PTE_MAP:
     {
	struct layout *lvar, *lset, *lres, *lhead;

	lvar = ptcolumnvar_layout(pql, pe->map.var);
	lset = ptexpr_layout(pql, pe->map.set);
	lres = ptexpr_layout(pql, pe->map.result);

	lhead = mklayout_quad(pql, mklayout_text(pql, "map"), lvar,
			      mklayout_text(pql, "in"), lset);
	ret = mklayout_indent(pql, lhead, lres, NULL);
     }
     break;

    case PTE_ASSIGN:
     {
	struct layout *lvar, *lval, *lbody, *lhead;

	lvar = ptcolumnvar_layout(pql, pe->assign.var);
	lval = ptexpr_layout(pql, pe->assign.value);

	if (pe->assign.body != NULL) {
	   lhead = mklayout_quad(pql, mklayout_text(pql, "let"), lvar,
				 mklayout_text(pql, ":="), lval);
	   lbody = ptexpr_layout(pql, pe->assign.body);
	   lbody = mklayout_pair(pql, mklayout_text(pql, "in"), lbody);
	   ret = mklayout_leftalign_pair(pql, lhead, lbody);
	}
	else {
	   ret = mklayout_quad(pql, mklayout_text(pql, "set"), lvar,
			       mklayout_text(pql, ":="), lval);
	}
     }
     break;

    case PTE_BOP:
     {
	struct layout *ll, *opl, *rl;

	ll = ptexpr_layout(pql, pe->bop.l);
	opl = mklayout_text(pql, function_getname(pe->bop.op));
	rl = ptexpr_layout(pql, pe->bop.r);

	ret = mklayout_leftalign_triple(pql, ll, opl, rl);
	ret = mklayout_wrap(pql, "(", ret, ")");
     }
     break;

    case PTE_UOP:
     {
	struct layout *opl, *subl;

	opl = mklayout_text(pql, function_getname(pe->uop.op));
	subl = ptexpr_layout(pql, pe->uop.sub);

	ret = mklayout_pair(pql, opl, mklayout_wrap(pql, "(", subl, ")"));
     }
     break;
	
    case PTE_FUNC:
     {
	struct layout *opl, *subl;

	opl = mklayout_text(pql, function_getname(pe->func.op));
	if (pe->func.args != NULL) {
	   subl = ptexprarray_layout(pql, pe->func.args);
	   subl = mklayout_wrap(pql, "(", subl, ")");
	}
	else {
	   subl = mklayout_text(pql, "( )");
	}

	ret = mklayout_pair(pql, opl, subl);
     }
     break;
	
    case PTE_READANYVAR:
     ret = mklayout_text(pql, pe->readanyvar.name);
     break;

    case PTE_READCOLUMNVAR:
     ret = ptcolumnvar_layout(pql, pe->readcolumnvar);
     break;

    case PTE_READGLOBALVAR:
     ret = ptglobalvar_layout(pql, pe->readglobalvar);
     break;

    case PTE_VALUE:
     ret = pqlvalue_layout(pql, pe->value);
     break;
   }

   return ret;
}


char *ptdump(struct pqlcontext *pql, struct ptexpr *pe) {
   struct layout *l;
   char *ret;

   l = ptexpr_layout(pql, pe);
   l = layout_format(pql, l, pql->dumpwidth);
   ret = layout_tostring(pql, l);
   layout_destroy(pql, l);

   return ret;
}

////////////////////////////////////////////////////////////

/*
 * Return a reference to the var bound by the end of PP.
 *
 * Note: this only works after normalize() has run. Otherwise it will
 * likely assert.
 */
struct ptcolumnvar *ptpath_get_tailvar(struct pqlcontext *pql,
				       struct ptpath *pp) {
   unsigned num;
   struct ptpath *sub;

   switch (pp->type) {
    case PTP_SEQUENCE:
     PQLASSERT(pp->bindobjafter == NULL);
     num = ptpatharray_num(&pp->sequence.items);
     PQLASSERT(num > 0);
     sub = ptpatharray_get(&pp->sequence.items, num-1);
     PQLASSERT(sub->type != PTP_SEQUENCE);
     return ptpath_get_tailvar(pql, sub);

    case PTP_ALTERNATES:
     PQLASSERT(pp->bindobjafter == NULL);
     ptcolumnvar_incref(pp->alternates.tailvar);
     return pp->alternates.tailvar;

    case PTP_OPTIONAL:
     break;

    case PTP_REPEATED:
     break;

    case PTP_NILBIND:
     /* not allowed */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     break;
   }

   if (pp->bindobjafter == NULL) {
      pp->bindobjafter = mkptcolumnvar_fresh(pql);
   }
   ptcolumnvar_incref(pp->bindobjafter);
   return pp->bindobjafter;
}
