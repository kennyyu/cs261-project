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

#ifndef COLUMNS_H
#define COLUMNS_H

#include <stdbool.h>

#include "array.h"
struct pqlcontext;


struct columnmanager;   /* Opaque. */
struct colname;         /* Opaque. */
struct colset;          /* Opaque. */
struct coltree;         /* Opaque. */

DECLARRAY(colname);
DECLARRAY(coltree);


/* Manager. */
//struct columnmanager *columnmanager_create(struct pqlcontext *);
//void columnmanager_destroy(struct columnmanager *);


/*
 * colname: single column name.
 */

struct colname *mkcolname(struct pqlcontext *, const char *name);
struct colname *mkcolname_fresh(struct pqlcontext *);
void colname_incref(struct colname *);
void colname_decref(struct pqlcontext *, struct colname *);
const char *colname_getname(struct pqlcontext *pql, struct colname *tc);
struct layout *colname_layout(struct pqlcontext *pql, struct colname *col);


/*
 * colset: set of column names.
 */

struct colset *colset_empty(struct pqlcontext *pql);
struct colset *colset_singleton(struct pqlcontext *pql, struct colname *);
struct colset *colset_pair(struct pqlcontext *pql, struct colname *,
			   struct colname *);
struct colset *colset_triple(struct pqlcontext *pql, struct colname *,
			     struct colname *, struct colname *);
struct colset *colset_fromcoltree(struct pqlcontext *pql,
				  const struct coltree *ct);
struct colset *colset_clone(struct pqlcontext *pql, struct colset *cs);
void colset_destroy(struct pqlcontext *pql, struct colset *);

unsigned colset_num(const struct colset *cs);
struct colname *colset_get(const struct colset *cs, unsigned index);
void colset_set(struct colset *cs, unsigned pos, struct colname *col);
void colset_add(struct pqlcontext *pql, struct colset *cs, struct colname *col);
void colset_setsize(struct pqlcontext *pql, struct colset *cs, unsigned newarity);
bool colset_contains(struct colset *, struct colname *);
bool colset_eq(struct colset *, struct colset *);
int colset_find(struct colset *cs, struct colname *col, unsigned *ix_ret);
void colset_moveappend(struct pqlcontext *pql,
		       struct colset *to, struct colset *from);
void colset_replace(struct pqlcontext *pql,
		    struct colset *cs,
		    struct colname *oldcol, struct colname *newcol);
void colset_remove(struct colset *cs, struct colname *col);
void colset_removebyindex(struct colset *cs, unsigned which);
void colset_complement(struct pqlcontext *pql, struct colset *cs,
		       const struct coltree *context);
void colset_mark_tocomplement(struct colset *cs);
void colset_resolve_tocomplement(struct pqlcontext *pql, struct colset *cs,
				 const struct coltree *context);
struct layout *colset_layout(struct pqlcontext *pql, struct colset *cs);


/*
 * coltree: complete column information for a value or expression
 * (including nested tuples; hence "tree")
 */
struct coltree *coltree_create_scalar(struct pqlcontext *pql,
				      struct colname *wholecolumn);
struct coltree *coltree_create_scalar_fresh(struct pqlcontext *pql);
struct coltree *coltree_create_unit(struct pqlcontext *pql,
				    struct colname *wholecolumn);
struct coltree *coltree_create_triple(struct pqlcontext *pql,
				      struct colname *wholecolumn,
				      struct colname *member0,
				      struct colname *member1,
				      struct colname *member2);
struct coltree *coltree_create_tuple(struct pqlcontext *pql,
				     struct colname *wholecolumn,
				     struct colset *members);
struct coltree *coltree_clone(struct pqlcontext *pql, struct coltree *src);
void coltree_destroy(struct pqlcontext *pql, struct coltree *);


const char *coltree_getname(struct pqlcontext *pql, struct coltree *n);
unsigned coltree_arity(struct coltree *ct);
bool coltree_istuple(const struct coltree *ct);
struct colname *coltree_wholecolumn(const struct coltree *ct);
unsigned coltree_num(const struct coltree *ct);
struct colname *coltree_get(const struct coltree *ct, unsigned index);
struct coltree *coltree_getsubtree(const struct coltree *ct, unsigned index);
bool coltree_contains_toplevel(const struct coltree *name,struct colname *col);
int coltree_find(struct coltree *ct, struct colname *col, unsigned *ix_ret);
bool coltree_eq_col(struct coltree *ct1, struct colname *cn2);
bool coltree_eq(struct coltree *ct1, struct coltree *ct2);

void coltree_removebyindex(struct pqlcontext *pql, struct coltree *ct,
			   unsigned which);
void coltree_replace(struct pqlcontext *pql,
		     struct coltree *ct, struct colname *oldcol,
		     struct colname *newcol);

struct coltree *coltree_project(struct pqlcontext *pql, struct coltree *src,
				struct colset *keep);
struct coltree *coltree_strip(struct pqlcontext *pql, struct coltree *src,
			      struct colset *remove);
struct coltree *coltree_rename(struct pqlcontext *pql, struct coltree *src,
			       struct colname *oldcol, struct colname *newcol);
struct coltree *coltree_join(struct pqlcontext *pql,
			     struct coltree *left, struct coltree *right);
struct coltree *coltree_adjoin_coltree(struct pqlcontext *pql,
				       struct coltree *src,
				       struct coltree *newstuff);
struct coltree *coltree_adjoin(struct pqlcontext *pql, struct coltree *src,
			       struct colname *newcol);
struct coltree *coltree_nest(struct pqlcontext *pql, struct coltree *src,
			     struct colset *remove, struct colname *add);
struct coltree *coltree_unnest(struct pqlcontext *pql, struct coltree *src,
			       struct colname *expand);


#endif /* COLUMNS_H */
