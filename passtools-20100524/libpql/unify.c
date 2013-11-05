/*
 * Copyright 2008, 2009
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
 * Path unification.
 * Mostly copied from the old engine.
 */

#include <string.h>

#include "utils.h"
#include "pttree.h"
#include "passes.h"

/*
 * Operation context.
 */
struct unify {
   struct pqlcontext *pql;			/* Global context */
   bool infrom;					/* True while in from clause */
   bool sawfrom;				/* True if from seen in sfw */
   struct ptexprarray savepaths;		/* Paths for matching */
   struct ptexprarray insertexprs;		/* Expressions to insert */
};

////////////////////////////////////////////////////////////
// Context handling

static void unify_init(struct unify *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
   ctx->infrom = false;
   ctx->sawfrom = false;
   ptexprarray_init(&ctx->savepaths);
   ptexprarray_init(&ctx->insertexprs);
}

static void unify_cleanup(struct unify *ctx) {
   /* Some paths may be left in paths[] if a path appears outside a select */
   ptexprarray_setsize(ctx->pql, &ctx->savepaths, 0);
   ptexprarray_cleanup(ctx->pql, &ctx->savepaths);

   PQLASSERT(ptexprarray_num(&ctx->insertexprs) == 0);
   ptexprarray_cleanup(ctx->pql, &ctx->insertexprs);
}

static unsigned unify_save(struct unify *ctx) {
   return ptexprarray_num(&ctx->savepaths);
}

static void unify_restore(struct unify *ctx, unsigned mark) {
   PQLASSERT(mark <= ptexprarray_num(&ctx->savepaths));
   ptexprarray_setsize(ctx->pql, &ctx->savepaths, mark);
}

/*
 * These and the infrom flag handling are the same as in normalize,
 * and it would be nice to share them somehow.
 */
static void unify_mkletbind(struct unify *ctx, struct ptcolumnvar *var,
			    struct ptexpr *pe) {
   struct ptexpr *let;

   PQLASSERT(var != NULL);

   let = mkptexpr_assign(ctx->pql, var, pe, NULL);
   ptexprarray_add(ctx->pql, &ctx->insertexprs, let, NULL);
}

static void unify_mkletbindvar(struct unify *ctx, struct ptcolumnvar *var,
			       struct ptcolumnvar *othervar) {
   unify_mkletbind(ctx, var, mkptexpr_readcolumnvar(ctx->pql, othervar));
}

static void unify_getexprs_forward(struct ptexprarray *fill, struct unify *ctx) {
   unsigned i, num;

   num = ptexprarray_num(&ctx->insertexprs);
   for (i=0; i<num; i++) {
      ptexprarray_add(ctx->pql, fill, ptexprarray_get(&ctx->insertexprs, i), NULL);
   }
   ptexprarray_setsize(ctx->pql, &ctx->insertexprs, 0);
}

static void unify_getexprs_back(struct ptexprarray *fill, struct unify *ctx) {
   unsigned i, num;

   num = ptexprarray_num(&ctx->insertexprs);
   for (i=num; i-- > 0; ) {
      ptexprarray_add(ctx->pql, fill, ptexprarray_get(&ctx->insertexprs, i), NULL);
   }
   ptexprarray_setsize(ctx->pql, &ctx->insertexprs, 0);
}

////////////////////////////////////////////////////////////
// path utils

/*
 * Check if path is empty.
 */
static inline bool is_runt_path(const struct ptexpr *pe) {
   PQLASSERT(pe->type == PTE_PATH);
   return pe->path.body == NULL;
}

/*
 * Empty a fullpath, leaving only the start variable behind.
 */
static inline void truncate_path(struct ptexpr *pe) {
   PQLASSERT(pe->type == PTE_PATH);
   //ptpath_destroy(pe->path.body); -- handled by region allocator
   pe->path.body = NULL;
}

/*
 * Move any supplementary bindings from BLUE to RED.
 */
static void move_supplementary(struct unify *ctx,
			       struct ptexpr *red, struct ptexpr *blue) {
   unsigned i, num;

   PQLASSERT(red->type == PTE_PATH);
   PQLASSERT(blue->type == PTE_PATH);

   num = ptexprarray_num(&blue->path.morebindings);
   for (i=0; i<num; i++) {
      ptexprarray_add(ctx->pql, &red->path.morebindings,
		      ptexprarray_get(&blue->path.morebindings, i), NULL);
   }
   ptexprarray_setsize(ctx->pql, &blue->path.morebindings, 0);
}

/*
 * Return the object variable bound after PP, creating it if needed.
 * Returns a (counted) reference.
 */
static struct ptcolumnvar *get_objvar(struct unify *ctx, struct ptpath *pp) {
   if (pp->bindobjafter == NULL) {
      pp->bindobjafter = mkptcolumnvar_fresh(ctx->pql);
   }
   ptcolumnvar_incref(pp->bindobjafter);
   return pp->bindobjafter;
}

/*
 * Replace existing root of F (whether global or column) with column
 * var V. Consumes a reference to V.
 */
static void ptpath_replace_root(struct ptexpr *pe, struct ptcolumnvar *var) {
   struct ptexpr *root;

   PQLASSERT(pe->type == PTE_PATH);
   root = pe->path.root;

   if (root->type == PTE_READGLOBALVAR) {
      //ptglobalvar_decref(root->readglobalvar); -- handled by region allocator
      root->type = PTE_READCOLUMNVAR;
   }
   else {
      PQLASSERT(root->type == PTE_READCOLUMNVAR);
      //ptcolumnvar_decref(root->readcolumnvar); -- handled by region allocator
   }
   root->readcolumnvar = var;
}

////////////////////////////////////////////////////////////
// path matching/merging stuff

/*
 * Check if two pathnodes are equal (same type, same substructure).
 * If marked dontmerge, they are not equal.
 */
static bool ptpath_equal(struct ptpath *redpp, struct ptpath *bluepp) {
   unsigned rednum, bluenum, i;

   if (redpp->type != bluepp->type) {
      return false;
   }

   /*
    * XXX: because we don't track dontmerge separately for binding paths
    * and binding objects, two paths A.b{B1}.c and A.b@B2.c will not be
    * merged, whereas they would have been in the old engine. Which is
    * the correct behavior?
    */
   if (redpp->dontmerge && bluepp->dontmerge) {
      return false;
   }

   switch (redpp->type) {

    case PTP_SEQUENCE:
     rednum = ptpatharray_num(&redpp->sequence.items);
     bluenum = ptpatharray_num(&bluepp->sequence.items);
     if (rednum != bluenum) {
	return false;
     }
     for (i=0; i<rednum; i++) {
	if (!ptpath_equal(ptpatharray_get(&redpp->sequence.items, i),
			  ptpatharray_get(&bluepp->sequence.items, i))) {
	   return false;
	}
     }
     break;

    case PTP_ALTERNATES:
     rednum = ptpatharray_num(&redpp->alternates.items);
     bluenum = ptpatharray_num(&bluepp->alternates.items);
     if (rednum != bluenum) {
	return false;
     }
     for (i=0; i<rednum; i++) {
	if (!ptpath_equal(ptpatharray_get(&redpp->alternates.items, i),
			  ptpatharray_get(&bluepp->alternates.items, i))) {
	   return false;
	}
     }
     break;

    case PTP_OPTIONAL:
     PQLASSERT(ptcolumnvararray_num(&redpp->optional.nilcolumns) == 0);
     PQLASSERT(ptcolumnvararray_num(&bluepp->optional.nilcolumns) == 0);
     if (!ptpath_equal(redpp->optional.sub,
		       bluepp->optional.sub)) {
	return false;
     }
     break;

    case PTP_REPEATED:
     if (!ptpath_equal(redpp->repeated.sub,
		       bluepp->repeated.sub)) {
	return false;
     }
     // XXX shouldn't this check pathfrominside/pathonoutside?
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (redpp->edge.reversed != bluepp->edge.reversed) {
	return false;
     }
     if (redpp->edge.iscomputed != bluepp->edge.iscomputed) {
	return false;
     }
     if (redpp->edge.iscomputed) {
	// XXX what to do here? textual equality?
	struct ptexpr *redexpr, *blueexpr;

	redexpr = redpp->edge.computedname;
	blueexpr = bluepp->edge.computedname;

	// XXX this is just to mimic the previous code that was for
	// vars only.
	if (redexpr->type == PTE_READGLOBALVAR &&
	    blueexpr->type == PTE_READGLOBALVAR &&
	    redexpr->readglobalvar == blueexpr->readglobalvar) {
	   return true;
	}
	if (redexpr->type == PTE_READCOLUMNVAR &&
	    blueexpr->type == PTE_READCOLUMNVAR &&
	    redexpr->readcolumnvar == blueexpr->readcolumnvar) {
	   return true;
	}
	return false;
     }
     else {
	return !strcmp(redpp->edge.staticname, bluepp->edge.staticname);
     }
     break;

   }
   return true;
}

/*
 * find out the matching (== equal, as discussed below) length
 * of red and blue, both of which must be sequences.
 */
static unsigned ptpath_match_length(struct ptpath *redpp,
				    struct ptpath *bluepp) {
   unsigned rednum, bluenum, k;
   struct ptpath *redsub, *bluesub;

   PQLASSERT(redpp->type == PTP_SEQUENCE);
   PQLASSERT(bluepp->type == PTP_SEQUENCE);

   rednum = ptpatharray_num(&redpp->sequence.items);
   bluenum = ptpatharray_num(&bluepp->sequence.items);

   for (k = 0; k < rednum && k < bluenum; k++) {
      redsub = ptpatharray_get(&redpp->sequence.items, k);
      bluesub = ptpatharray_get(&bluepp->sequence.items, k);
      if (!ptpath_equal(redsub, bluesub)) {
	 break;
      }
   }
   return k;
}

/*
 * move all variables from blue to red.
 * if ct is nonzero, take only that many (if a sequence)
 */
static void ptpath_move_vars_into(struct unify *ctx,
				  struct ptpath *redpp,
				  struct ptpath *bluepp, unsigned ct) {
   unsigned rednum, bluenum, i;
   struct ptpath *redsub, *bluesub;
   struct ptcolumnvar *bluevar;
   bool inhibit_motion_here = false;

   PQLASSERT(redpp->type == bluepp->type);

   /* bleh, should just get rid of this XXX */
   PQLASSERT(redpp->bindobjbefore == NULL);
   PQLASSERT(bluepp->bindobjbefore == NULL);

   /* recurse... */
   switch (redpp->type) {
    case PTP_SEQUENCE:

     /* guaranteed by normalize */
     PQLASSERT(redpp->bindobjafter == NULL);
     PQLASSERT(bluepp->bindobjafter == NULL);
     PQLASSERT(redpp->bindpath == NULL);
     PQLASSERT(bluepp->bindpath == NULL);

     rednum = ptpatharray_num(&redpp->sequence.items);
     bluenum = ptpatharray_num(&bluepp->sequence.items);

     if (ct == 0) {
	PQLASSERT(rednum == bluenum);
	ct = bluenum;
     }
     for (i=0; i<ct; i++) {
	redsub = ptpatharray_get(&redpp->sequence.items, i);
	bluesub = ptpatharray_get(&bluepp->sequence.items, i);
	ptpath_move_vars_into(ctx, redsub, bluesub, 0);
     }

     /*
      * The bindobj and bindpath *this* node come at the end, so if we
      * aren't going all the way to the end, we shouldn't move them.
      */
     if (ct < bluenum) {
	inhibit_motion_here = true;
     }
     break;

    case PTP_ALTERNATES:
     rednum = ptpatharray_num(&redpp->alternates.items);
     bluenum = ptpatharray_num(&bluepp->alternates.items);
     PQLASSERT(rednum == bluenum);

     for (i=0; i<rednum; i++) {
	redsub = ptpatharray_get(&redpp->alternates.items, i);
	bluesub = ptpatharray_get(&bluepp->alternates.items, i);
	ptpath_move_vars_into(ctx, redsub, bluesub, 0);
     }
     break;

    case PTP_OPTIONAL:
     redsub = redpp->optional.sub;
     bluesub = redpp->optional.sub;
     ptpath_move_vars_into(ctx, redsub, bluesub, 0);
     break;

    case PTP_REPEATED:
     redsub = redpp->repeated.sub;
     bluesub = redpp->repeated.sub;
     ptpath_move_vars_into(ctx, redsub, bluesub, 0);
     // XXX what about pathfrominside/pathonoutside?
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     break;
   }

   /* ...now do the real work */

   if (inhibit_motion_here) {
      // only sequences, and sequences don't have bindings.
      PQLASSERT(bluepp->type == PTP_SEQUENCE);
      PQLASSERT(bluepp->bindobjafter == NULL);
      PQLASSERT(bluepp->bindpath == NULL);
      return;
   }

   bluevar = bluepp->bindpath;
   bluepp->bindpath = NULL;
   if (bluevar != NULL) {
      if (redpp->bindpath == NULL) {
	 redpp->bindpath = bluevar;
      }
      else {
	 ptcolumnvar_incref(redpp->bindpath);
	 unify_mkletbindvar(ctx, bluevar, redpp->bindpath);
      }
   }

   bluevar = bluepp->bindobjafter;
   bluepp->bindobjafter = NULL;
   if (bluevar != NULL) {
      if (redpp->bindobjafter == NULL) {
	 redpp->bindobjafter = bluevar;
      }
      else {
	 ptcolumnvar_incref(redpp->bindobjafter);
	 unify_mkletbindvar(ctx, bluevar, redpp->bindobjafter);
      }
   }
}

/*
 * merge blue into red, maybe.
 *
 * redp may be pd->red->p or may be a subnode thereof.
 *
 * returns true if we did anything.
 */
static bool path_merge(struct unify *ctx,
		       struct ptexpr *red, struct ptpath *redpp,
		       struct ptexpr *blue) {
   unsigned rednum, bluenum, i;
   unsigned prefixnum;
   struct ptpath *bluepp;
   struct ptpath *redsub, *bluesub;
   struct ptcolumnvar *joinvar;

   PQLASSERT(red->type == PTE_PATH);
   PQLASSERT(blue->type == PTE_PATH);

   bluepp = blue->path.body;

   /* If tagged dontmerge, we can't do anything. */
   if (redpp->dontmerge && bluepp->dontmerge) {
      return false;
   }

   PQLASSERT(redpp->type == PTP_SEQUENCE);
   PQLASSERT(bluepp->type == PTP_SEQUENCE);

   rednum = ptpatharray_num(&redpp->sequence.items);
   bluenum = ptpatharray_num(&bluepp->sequence.items);

   /* do they match? */
   prefixnum = ptpath_match_length(redpp, bluepp);
   if (prefixnum == 0) {
      return false;
   }

   /* move any vars in the prefix of blue into red, so we don't lose them */
   ptpath_move_vars_into(ctx, redpp, bluepp, prefixnum);

   /* populate prefix from red; destroy prefix of blue */
   for (i=0; i<prefixnum; i++) {
      //bluesub = ptpatharray_get(&bluepp->sequence.items, i);
      //ptpath_destroy(bluesub); -- handled by region allocator
      ptpatharray_set(&bluepp->sequence.items, i, NULL);
   }

   /* update blue to be its tail */
   for (i=prefixnum; i<bluenum; i++) {
      bluesub = ptpatharray_get(&bluepp->sequence.items, i);
      ptpatharray_set(&bluepp->sequence.items, i - prefixnum, bluesub);
   }
   bluenum -= prefixnum;
   ptpatharray_setsize(ctx->pql, &bluepp->sequence.items, bluenum);

   if (bluenum == 0) {
      /* total prefix; zap blue's path completely */
      move_supplementary(ctx, red, blue);
      truncate_path(blue);
   }

   /* get the var to use as the head of the new blue */
   redsub = ptpatharray_get(&redpp->sequence.items, prefixnum - 1);
   joinvar = get_objvar(ctx, redsub);

   /* update blue's start variable */
   ptpath_replace_root(blue, joinvar);

   return true;
}

/*
 * Try matching two paths.
 * Updates RED and BLUE as needed.
 *
 * Note that "blue" and "red" have no particular semantic significance;
 * they're just a lot easier to keep straight than "p1" and "p2".
 */
static void path_match(struct unify *ctx,
		       struct ptexpr *red, struct ptexpr *blue) {
   struct ptexpr *redroot, *blueroot;
   struct ptpath *redbody, *bluebody;

   PQLASSERT(red->type == PTE_PATH);
   PQLASSERT(blue->type == PTE_PATH);

   redroot = red->path.root;
   blueroot = blue->path.root;
   redbody = red->path.body;
   bluebody = blue->path.body;

   if (redroot->type != blueroot->type) {
      /* Path roots are different, so paths aren't the same. */
      return;
   }
   if (redroot->type == PTE_READGLOBALVAR) {
      if (redroot->readglobalvar != blueroot->readglobalvar) {
	 return;
      }
   }
   else {
      PQLASSERT(redroot->type == PTE_READCOLUMNVAR);
      if (redroot->readcolumnvar != blueroot->readcolumnvar) {
	 return;
      }
   }

   /*
    * Two paths match if:
    *    (1) both are sequences and the members match up to some point;
    *    (1a) one is a sequence and the other is the first step of the first;
    *    (2) both are not sequences but are equal;
    *
    * Two paths are equal if they are the same type and their subnodes
    * are all equal, *and* they don't both have nonempty bindobj[]
    * arrays in the same places.
    *
    * Since sequences are not supposed to have sequences as their
    * elements, the members of a sequence match only if equal.
    */

   if (redbody->type == PTP_SEQUENCE && bluebody->type == PTP_SEQUENCE) {
      path_merge(ctx, red, redbody, blue);
   }
   else if (redbody->type == PTP_SEQUENCE &&
	    ptpath_equal(ptpatharray_get(&redbody->sequence.items, 0),
			 bluebody)) {
      /* blue is the first element of red */
      struct ptpath *red0;

      red0 = ptpatharray_get(&redbody->sequence.items, 0);
      ptpath_move_vars_into(ctx, red0, bluebody, 0);

      move_supplementary(ctx, red, blue);

      ptpath_replace_root(blue, get_objvar(ctx, red0));
      truncate_path(blue);
   }
   else if (bluebody->type == PTP_SEQUENCE &&
	    ptpath_equal(ptpatharray_get(&bluebody->sequence.items, 0),
			 redbody)) {
      /* red is the first element of blue */

      /*
       * XXX I don't think this logic is going to DTRT
       */
      struct ptpath *dummy;

      /* wrap it in a sequence and call path_merge to do the work */
      dummy = mkptpath_emptysequence(ctx->pql);
      ptpatharray_add(ctx->pql, &dummy->sequence.items, redbody, NULL);

      path_merge(ctx, red, dummy, blue);

      /* now unwrap it again; make sure it's still what we expected */
      PQLASSERT(dummy->type == PTP_SEQUENCE);
      PQLASSERT(dummy->bindpath == NULL);
      PQLASSERT(dummy->bindobjafter == NULL);
      PQLASSERT(ptpatharray_num(&dummy->sequence.items) == 1);
      PQLASSERT(ptpatharray_get(&dummy->sequence.items, 0) == redbody);
      ptpatharray_setsize(ctx->pql, &dummy->sequence.items, 0);
      //ptpath_destroy(dummy); -- handled by region allocator
   }
   else if (redbody->type == bluebody->type) {
      if (ptpath_equal(redbody, bluebody)) {
	 ptpath_move_vars_into(ctx, redbody, bluebody, 0);
	 move_supplementary(ctx, red, blue);
	 truncate_path(blue);
	 /* update blue's start var to be the end of red */

	 /* redbody isn't a sequence, so can use it directly */
	 ptpath_replace_root(blue, get_objvar(ctx, redbody));
      }
   }
}

/*
 * Merge paths that are the same or share suitable common components.
 *
 * Don't stop after merging unless there's nothing left, because
 * we might match something else; e.g. in
 *
 *   select ... from A.b as B, B.c as C where A.b.c == 3
 *
 * we want to translate the A.b.c to first B.c and then C.
 */
static void match_paths(struct unify *ctx, struct ptexpr *blue) {
   unsigned i, num;
   struct ptexpr *red;

   PQLASSERT(blue->type == PTE_PATH);

   num = ptexprarray_num(&ctx->savepaths);
   for (i=0; i<num; i++) {
      red = ptexprarray_get(&ctx->savepaths, i);
      path_match(ctx, red, blue);
      if (is_runt_path(blue)) {
	 return;
      }
   }
}

////////////////////////////////////////////////////////////

/*
 * Recursive traversal.
 */

static void ptpath_unify(struct unify *ctx, struct ptpath *pp);
static struct ptexpr *ptexpr_unify(struct unify *ctx, struct ptexpr *pe);

static void ptpatharray_unify(struct unify *ctx, struct ptpatharray *arr) {
   unsigned i, num;

   num = ptpatharray_num(arr);
   for (i=0; i<num; i++) {
      ptpath_unify(ctx, ptpatharray_get(arr, i));
   }
}

static void ptexprarray_unify(struct unify *ctx, struct ptexprarray *arr) {
   unsigned i, num;
   struct ptexpr *pe;

   num = ptexprarray_num(arr);
   for (i=0; i<num; i++) {
      pe = ptexprarray_get(arr, i);
      pe = ptexpr_unify(ctx, pe);
      ptexprarray_set(arr, i, pe);
   }
}

static void ptpath_unify(struct unify *ctx, struct ptpath *pp) {
   switch (pp->type) {
    case PTP_SEQUENCE:
     ptpatharray_unify(ctx, &pp->sequence.items);
     break;

    case PTP_ALTERNATES:
     ptpatharray_unify(ctx, &pp->alternates.items);
     break;

    case PTP_OPTIONAL:
     ptpath_unify(ctx, pp->optional.sub);
     break;

    case PTP_REPEATED:
     ptpath_unify(ctx, pp->repeated.sub);
     break;

    case PTP_NILBIND:
     /* may not exist here */
     PQLASSERT(0);
     break;

    case PTP_EDGE:
     if (pp->edge.iscomputed) {
	pp->edge.computedname = ptexpr_unify(ctx, pp->edge.computedname);
     }
     break;
   }
}

static struct ptexpr *ptexpr_unify(struct unify *ctx, struct ptexpr *pe) {
   switch (pe->type) {
     /*
      * Scoping:
      *
      * As per Lorel, paths in each of the where and select clauses
      * are unified with the paths in the from clause, but not with
      * each other.
      *
      * Also, paths in a given select do not leak out of it.
      *
      * 20090823: if there is no from-clause, do unification between
      * the where and select clauses. Otherwise one ends up with huge
      * unwanted cartesian products. It is possible that we should
      * always do this unification regardless. XXX decide.
      * 
      * (Note that while we technically create illegal variable
      * scoping if a select is partially unified with a where path,
      * movepaths will fix this downstream.)
      */

    case PTE_SELECT:
     {
	unsigned mark1;
	bool saveinfrom;
	bool savesawfrom;

	mark1 = unify_save(ctx);
	saveinfrom = ctx->infrom;
	savesawfrom = ctx->sawfrom;
	ctx->infrom = false;
	ctx->sawfrom = false;

	pe->select.sub = ptexpr_unify(ctx, pe->select.sub);
	pe->select.result = ptexpr_unify(ctx, pe->select.result);

	ctx->infrom = saveinfrom;
	ctx->sawfrom = savesawfrom;
	unify_restore(ctx, mark1);
     }
     break;

    case PTE_FROM:
     if (ptexprarray_num(pe->from) > 0) {
	ctx->sawfrom = true;
     }
     ctx->infrom = true;
     ptexprarray_unify(ctx, pe->from);
     unify_getexprs_back(pe->from, ctx);
     ctx->infrom = false;
     break;

    case PTE_WHERE:
     {
	unsigned mark2;

	pe->where.sub = ptexpr_unify(ctx, pe->where.sub);

	if (ctx->sawfrom) {
	   /* Save where we are before looking at the where-clause */
	   mark2 = unify_save(ctx);
	}

	pe->where.where = ptexpr_unify(ctx, pe->where.where);

	if (ctx->sawfrom) {
	   /* Drop results from the where clause as per 20090823 note above */
	   unify_restore(ctx, mark2);
	}
     }
     break;

    case PTE_GROUP:
     pe->group.sub = ptexpr_unify(ctx, pe->group.sub);
     break;

    case PTE_UNGROUP:
     pe->ungroup.sub = ptexpr_unify(ctx, pe->ungroup.sub);
     break;

    case PTE_RENAME:
     pe->rename.sub = ptexpr_unify(ctx, pe->rename.sub);
     if (pe->rename.iscomputed) {
	pe->rename.computedname = ptexpr_unify(ctx, pe->rename.computedname);
     }
     break;

    case PTE_PATH:
     pe->path.root = ptexpr_unify(ctx, pe->path.root);
     match_paths(ctx, pe);
     if (is_runt_path(pe)) {
	struct ptexpr *sub;

	PQLASSERT(ptexprarray_num(&pe->path.morebindings) == 0);

	/* convert to just reading the path root */
	sub = pe->path.root;
	pe->path.root = NULL;
	//ptexpr_destroy(pe); -- handled by region allocator
	pe = sub;
     }
     else {
	ptexprarray_add(ctx->pql, &ctx->savepaths, pe, NULL);
	/* Recurse on the path afterwards, to handle computednames. */
	ptpath_unify(ctx, pe->path.body);
     }
     /*
      * Properly, we ought to run unify across PE's supplementary
      * variable bindings. There's a problem, though, which is that we
      * don't want to do them *before* the main path as they're
      * supposed to happen after it, but afterwards by the time we get
      * back up here PE may no longer be a path expr... in which case
      * the bindings will have been shuffled off elsewhere and they
      * won't get handled unless we insert the recursive call to
      * handle them deep in the unify logic. I don't want to do the
      * latter, so we'll take advantage of the fact that we know the
      * value expressions in the supplementary bindings are generated
      * by upstream passes for doing simple things and do not contain
      * paths. If the supplementary bindings start getting used for
      * more stuff this needs to be revisited.
      */
     /* ptexprarray_unify(ctx, &pe->path.morebindings); */
     if (!ctx->infrom) {
	unify_getexprs_forward(&pe->path.morebindings, ctx);
     }
     break;

    case PTE_TUPLE:
     ptexprarray_unify(ctx, pe->tuple);
     break;

    case PTE_FORALL:
     pe->forall.set = ptexpr_unify(ctx, pe->forall.set);
     pe->forall.predicate = ptexpr_unify(ctx, pe->forall.predicate);
     break;

    case PTE_EXISTS:
     pe->exists.set = ptexpr_unify(ctx, pe->exists.set);
     pe->exists.predicate = ptexpr_unify(ctx, pe->exists.predicate);
     break;

    case PTE_MAP:
     pe->map.set = ptexpr_unify(ctx, pe->map.set);
     pe->map.result = ptexpr_unify(ctx, pe->map.result);
     break;

    case PTE_ASSIGN:
     pe->assign.value = ptexpr_unify(ctx, pe->assign.value);
     if (pe->assign.body != NULL) {
	pe->assign.body = ptexpr_unify(ctx, pe->assign.body);
     }
     break;

    case PTE_BOP:
     pe->bop.l = ptexpr_unify(ctx, pe->bop.l);
     pe->bop.r = ptexpr_unify(ctx, pe->bop.r);
     break;

    case PTE_UOP:
     pe->uop.sub = ptexpr_unify(ctx, pe->uop.sub);
     break;

    case PTE_FUNC:
     if (pe->func.args != NULL) {
	ptexprarray_unify(ctx, pe->func.args);
     }
     break;

    case PTE_READANYVAR:
     /* not allowed here */
     PQLASSERT(0);
     break;

    case PTE_READCOLUMNVAR:
    case PTE_READGLOBALVAR:
    case PTE_VALUE:
     break;
   }

   return pe;
}

////////////////////////////////////////////////////////////

/*
 * Entry point.
 */
struct ptexpr *unify(struct pqlcontext *pql, struct ptexpr *pe) {
   struct unify ctx;

   unify_init(&ctx, pql);
   pe = ptexpr_unify(&ctx, pe);
   unify_cleanup(&ctx);

   return pe;
}
