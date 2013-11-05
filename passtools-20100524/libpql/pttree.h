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

#ifndef PTTREE_H
#define PTTREE_H

#include <stdbool.h>

#include "array.h"
#include "functions.h"

struct pqlcontext;


#ifndef PTTREE_INLINE
#define PTTREE_INLINE INLINE
#endif
struct ptglobalvar;
struct ptcolumnvar;
struct ptpath;
struct ptexpr;
DECLARRAY(ptglobalvar);
DECLARRAY(ptcolumnvar);
DECLARRAY(ptpath);
DECLARRAY(ptexpr);
DEFARRAY(ptglobalvar, PTTREE_INLINE);
DEFARRAY(ptcolumnvar, PTTREE_INLINE);
DEFARRAY(ptpath, PTTREE_INLINE);
DEFARRAY(ptexpr, PTTREE_INLINE);

struct ptglobalvar {
   unsigned line, column;
   unsigned refcount;
   char *name;
};

struct ptcolumnvar {
   unsigned line, column;
   unsigned refcount;
   char *name;
   unsigned id;
};

enum ptpathtypes {
   PTP_SEQUENCE,
   PTP_ALTERNATES,
   PTP_OPTIONAL,
   PTP_REPEATED,
   PTP_NILBIND,
   PTP_EDGE,
};
struct ptpath {
   enum ptpathtypes type;
   union {
      struct {
	 struct ptpatharray items;
      } sequence;
      struct {
	 struct ptpatharray items;
	 struct ptcolumnvar *tailvar;
      } alternates;
      struct {
	 struct ptpath *sub;
	 struct ptcolumnvararray nilcolumns;	/* bind these if SUB skipped */
      } optional;
      struct {
	 struct ptpath *sub;
	 struct ptcolumnvar *pathfrominside;	/* path to read out of SUB */
	 struct ptcolumnvar *pathonoutside;	/* path to emit */
      } repeated;
      struct {
	 struct ptcolumnvararray columnsbefore;
	 struct ptpath *sub;
	 struct ptcolumnvararray columnsafter;
      } nilbind;
      struct {
	 bool iscomputed;
	 union {
	    char *staticname;
	    struct ptexpr *computedname;
	 };
	 bool reversed;
      } edge;
   };
   struct ptcolumnvar *bindobjbefore;
   struct ptcolumnvar *bindobjafter;
   struct ptcolumnvar *bindpath;
   bool dontmerge;
   bool parens;
};

enum ptexprtypes {
   PTE_SELECT,
   PTE_FROM,
   PTE_WHERE,
   PTE_GROUP,
   PTE_UNGROUP,
   PTE_RENAME,
   PTE_PATH,
   PTE_TUPLE,
   PTE_FORALL,
   PTE_EXISTS,
   PTE_MAP,
   PTE_ASSIGN,
   PTE_BOP,
   PTE_UOP,
   PTE_FUNC,
   PTE_READANYVAR,
   PTE_READCOLUMNVAR,
   PTE_READGLOBALVAR,
   PTE_VALUE,
};
struct ptexpr {
   enum ptexprtypes type;
   union {
      struct {
	 struct ptexpr *sub;
	 struct ptexpr *result;
	 bool distinct;
      } select;
      struct ptexprarray *from;
      struct {
	 struct ptexpr *sub;
	 struct ptexpr *where;
      } where;
      struct {
	 struct ptexpr *sub;
	 struct ptcolumnvararray *vars;
	 struct ptcolumnvar *newvar;
      } group;
      struct {
	 struct ptexpr *sub;
	 struct ptcolumnvar *var;
      } ungroup;
      struct {
	 bool iscomputed;
	 union {
	    char *staticname;
	    struct ptexpr *computedname;
	 };
	 struct ptexpr *sub;
      } rename;
      struct {
	 struct ptexpr *root;
	 struct ptpath *body;
	 /* additional variable bindings computed from the path */
	 struct ptexprarray morebindings;
      } path;
      struct ptexprarray *tuple;
      struct {
	 struct ptcolumnvar *var;
	 struct ptexpr *set;
	 struct ptexpr *predicate;
      } forall;
      struct {
	 struct ptcolumnvar *var;
	 struct ptexpr *set;
	 struct ptexpr *predicate;
      } exists;
      struct {
	 struct ptcolumnvar *var;
	 struct ptexpr *set;
	 struct ptexpr *result;
      } map;
      struct {
	 struct ptcolumnvar *var;
	 struct ptexpr *value;
	 struct ptexpr *body;
      } assign;
      struct {
	 struct ptexpr *l;
	 enum functions op;
	 struct ptexpr *r;
      } bop;
      struct {
	 enum functions op;
	 struct ptexpr *sub;
      } uop;
      struct {
	 enum functions op;
	 struct ptexprarray *args; /* may be null */
      } func;
      struct {
	 unsigned line, column;
	 char *name; 
      } readanyvar;
      struct ptcolumnvar *readcolumnvar;
      struct ptglobalvar *readglobalvar;
      struct pqlvalue *value;
   };
};

////////////////////////////////////////////////////////////

struct ptglobalvar *mkptglobalvar(struct pqlcontext *pqlcontext,
				  unsigned line, unsigned column,
				  const char *name);
void ptglobalvar_incref(struct ptglobalvar *);

struct ptcolumnvar *mkptcolumnvar(struct pqlcontext *pqlcontext,
				  unsigned line, unsigned column,
				  const char *name, size_t namelen);
struct ptcolumnvar *mkptcolumnvar_fresh(struct pqlcontext *pqlcontext);
void ptcolumnvar_incref(struct ptcolumnvar *);

struct ptpath *mkptpath_emptysequence(struct pqlcontext *pqlcontext);
struct ptpath *mkptpath_emptyalternates(struct pqlcontext *pqlcontext);
struct ptpath *mkptpath_optional(struct pqlcontext *pqlcontext,
				 struct ptpath *sub);
struct ptpath *mkptpath_repeated(struct pqlcontext *pqlcontext,
				 struct ptpath *sub);
struct ptpath *mkptpath_nilbind(struct pqlcontext *pqlcontext,
				struct ptpath *sub);
struct ptpath *mkptpath_staticedge(struct pqlcontext *pqlcontext,
				   const char *name, size_t namelen,
				   bool reversed);
struct ptpath *mkptpath_computededge(struct pqlcontext *pqlcontext,
				     struct ptexpr *computedname,
				     bool reversed);

struct ptexpr *mkptexpr_select(struct pqlcontext *pqlcontext,
			       struct ptexpr *sub,
			       struct ptexpr *result,
			       bool distinct);
struct ptexpr *mkptexpr_from(struct pqlcontext *pqlcontext,
			     struct ptexprarray *from);
struct ptexpr *mkptexpr_where(struct pqlcontext *pqlcontext,
			      struct ptexpr *sub,
			      struct ptexpr *where);
struct ptexpr *mkptexpr_group(struct pqlcontext *pqlcontext,
			      struct ptexpr *sub,
			      struct ptcolumnvararray *vars,
			      struct ptcolumnvar *newvar);
struct ptexpr *mkptexpr_ungroup(struct pqlcontext *pqlcontext,
				struct ptexpr *sub,
				struct ptcolumnvar *var);
struct ptexpr *mkptexpr_rename_static(struct pqlcontext *pqlcontext,
				      const char *name, size_t namelen,
				      struct ptexpr *sub);
struct ptexpr *mkptexpr_rename_computed(struct pqlcontext *pqlcontext,
					struct ptexpr *name,
					struct ptexpr *sub);
struct ptexpr *mkptexpr_path(struct pqlcontext *pqlcontext,
			     struct ptexpr *root,
			     struct ptpath *body);
struct ptexpr *mkptexpr_tuple(struct pqlcontext *pqlcontext,
			      struct ptexprarray *exprs);
struct ptexpr *mkptexpr_forall(struct pqlcontext *pqlcontext,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *predicate);
struct ptexpr *mkptexpr_exists(struct pqlcontext *pqlcontext,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *predicate);
struct ptexpr *mkptexpr_map(struct pqlcontext *pqlcontext,
			       struct ptcolumnvar *var,
			       struct ptexpr *set,
			       struct ptexpr *result);
struct ptexpr *mkptexpr_assign(struct pqlcontext *pqlcontext,
			       struct ptcolumnvar *var,
			       struct ptexpr *value,
			       struct ptexpr *body);
struct ptexpr *mkptexpr_bop(struct pqlcontext *pqlcontext,
			    struct ptexpr *l,
			    enum functions op,
			    struct ptexpr *r);
struct ptexpr *mkptexpr_uop(struct pqlcontext *pqlcontext,
			    enum functions op,
			    struct ptexpr *sub);
struct ptexpr *mkptexpr_func(struct pqlcontext *pql,
			     enum functions op,
			     struct ptexprarray *args);
struct ptexpr *mkptexpr_readanyvar(struct pqlcontext *pqlcontext,
				   unsigned line, unsigned column,
				   const char *name, size_t namelen);
struct ptexpr *mkptexpr_readcolumnvar(struct pqlcontext *pqlcontext,
				      struct ptcolumnvar *var);
struct ptexpr *mkptexpr_readglobalvar(struct pqlcontext *pqlcontext,
				      struct ptglobalvar *var);
struct ptexpr *mkptexpr_value(struct pqlcontext *pqlcontext,
			      struct pqlvalue *val);

////////////////////////////////////////////////////////////

struct ptmanager;
struct ptmanager *ptmanager_create(struct pqlcontext *);
void ptmanager_destroy(struct ptmanager *);

void ptmanager_add_exprarray(struct ptmanager *ptm, struct ptexprarray *arr);
void ptmanager_add_columnvararray(struct ptmanager *ptm,
				  struct ptcolumnvararray *arr);
void ptmanager_destroyall(struct ptmanager *ptm);

////////////////////////////////////////////////////////////

char *ptdump(struct pqlcontext *pql, struct ptexpr *pe);

////////////////////////////////////////////////////////////

struct ptcolumnvar *ptpath_get_tailvar(struct pqlcontext *pql,
				       struct ptpath *pp);


#endif /* PTTREE_H */
