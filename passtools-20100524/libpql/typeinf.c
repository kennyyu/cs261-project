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
 * Figure out types (and column names) for tuple calculus.
 */

#include "datatype.h"
#include "columns.h"
#include "pqlvalue.h"
#include "tcalc.h"
#include "passes.h"

struct typeinf {
   struct pqlcontext *pql;
};

////////////////////////////////////////////////////////////
// context management

static void typeinf_init(struct typeinf *ctx, struct pqlcontext *pql) {
   ctx->pql = pql;
}

static void typeinf_cleanup(struct typeinf *ctx) {
   (void)ctx;
}

////////////////////////////////////////////////////////////
// tools

static struct datatype *get_member_type(struct typeinf *ctx, struct tcexpr *te,
					struct colname *col) {
   unsigned j;
   struct datatype *t;

   (void)ctx;

   /*
    * XXX shouldn't this check the whole-column *before* looking at
    * the members? And what about multiply-nested members?
    */

   if (coltree_istuple(te->colnames)) {
      if (coltree_find(te->colnames, col, &j) < 0) {
	 /* not there */
	 return NULL;
      }
      t = te->datatype;

      /* top must yield top regardless of indexing/arity */
      if (datatype_isabstop(t)) {
	 return t;
      }

      if (datatype_isset(t)) {
	 t = datatype_set_member(t);
      }
      else if (datatype_issequence(t)) {
	 t = datatype_sequence_member(t);
      }
      return datatype_getnth(t, j);
   }
   else {
      if (coltree_wholecolumn(te->colnames) == col) {
	 t = te->datatype;
	 if (datatype_isset(t)) {
	    t = datatype_set_member(t);
	 }
	 else if (datatype_issequence(t)) {
	    t = datatype_sequence_member(t);
	 }
	 return t;
      }
      /* not there */
      return NULL;
   }
}

static struct datatype *get_member_types(struct typeinf *ctx,
					 struct tcexpr *te,
					 const struct colset *cols) {
   unsigned i, num;
   struct datatypearray types;
   struct datatype *subtype, *ret;

   datatypearray_init(&types);
   num = colset_num(cols);
   for (i=0; i<num; i++) {
      subtype = get_member_type(ctx, te, colset_get(cols, i));
      if (subtype != NULL) {
	 datatypearray_add(ctx->pql, &types, subtype, NULL);
      }
   }
   ret = datatype_tuple_specific(ctx->pql, &types);

   datatypearray_setsize(ctx->pql, &types, 0);
   datatypearray_cleanup(ctx->pql, &types);

   return ret;
}

static struct datatype *get_member_types_coltree(struct typeinf *ctx,
						struct tcexpr *te,
						struct coltree *cols) {
   unsigned i, num;
   struct datatypearray types;
   struct datatype *subtype, *ret;
   struct colname *subcolumn;

   if (coltree_istuple(cols)) {
      datatypearray_init(&types);

      num = coltree_num(cols);
      for (i=0; i<num; i++) {
	 subcolumn = coltree_get(cols, i);
	 subtype = get_member_type(ctx, te, subcolumn);
	 if (subtype != NULL) {
	    datatypearray_add(ctx->pql, &types, subtype, NULL);
	 }
      }
      ret = datatype_tuple_specific(ctx->pql, &types);

      datatypearray_setsize(ctx->pql, &types, 0);
      datatypearray_cleanup(ctx->pql, &types);
   }
   else {
      ret = get_member_type(ctx, te, coltree_wholecolumn(cols));
      if (ret == NULL) {
	 ret = datatype_unit(ctx->pql);
      }
   }

   return ret;
}

static struct datatype *get_member_types_except(struct typeinf *ctx,
						struct tcexpr *te,
						struct colset *exclude) {
   unsigned i, num;
   struct datatypearray types;
   struct datatype *subtype, *ret;
   struct colname *subcolumn;

   if (!coltree_istuple(te->colnames)) {
      if (colset_contains(exclude, coltree_wholecolumn(te->colnames))) {
	 return datatype_unit(ctx->pql);
      }
      return te->datatype;
   }

   datatypearray_init(&types);

   num = coltree_num(te->colnames);
   for (i=0; i<num; i++) {
      subcolumn = coltree_get(te->colnames, i);
      if (colset_contains(exclude, subcolumn)) {
	 continue;
      }
      subtype = get_member_type(ctx, te, subcolumn);
      if (subtype != NULL) {
	 datatypearray_add(ctx->pql, &types, subtype, NULL);
      }
   }
   ret = datatype_tuple_specific(ctx->pql, &types);

   datatypearray_setsize(ctx->pql, &types, 0);
   datatypearray_cleanup(ctx->pql, &types);

   return ret;
}

static struct datatype *get_member_types_except_one(struct typeinf *ctx,
						    struct tcexpr *te,
						    struct colname *exclude) {
   struct colset *cs;
   struct datatype *ret;

   cs = colset_singleton(ctx->pql, exclude);

   ret = get_member_types_except(ctx, te, cs);

   colset_destroy(ctx->pql, cs);

   return ret;
}

/*
 * Return false if the datatype and colnames passed in don't have the
 * same number of columns.
 */
static bool datatype_matches_colnames(struct datatype *datatype,
				      struct coltree *colnames) {
   PQLASSERT(datatype != NULL);
   PQLASSERT(colnames != NULL);
   /*
    * Top may reasonably end up having multiple columns in some
    * contexts, e.g. "[error] union select A, B, C, from ...", or
    * unions of sets with mismatched arity, or other things.
    *
    * XXX: should we leave the columns or should this case force some
    * kind of error columnlist? For now I'm going with leaving them,
    * partly because this allows this function to exist sanely, but
    * this question requires more thought.
    */
   if (datatype_isabstop(datatype)) {
      return true;
   }

   /*
    * XXX this needs to check arbitrarily deep levels of nesting, or
    * better, be handled some other way.
    */
   if (datatype_isset(datatype) &&
       datatype_isabstop(datatype_set_member(datatype))) {
      return true;
   }
   if (datatype_issequence(datatype) &&
       datatype_isabstop(datatype_sequence_member(datatype))) {
      return true;
   }

   return datatype_nonset_arity(datatype) == coltree_arity(colnames);
}

/*
 * Wrapper for conciseness.
 */
static bool tcvar_datatype_matches_colnames(struct tcvar *var) {
   return datatype_matches_colnames(var->datatype, var->colnames);
}

////////////////////////////////////////////////////////////
// function types

#if 0 /* unused */
/*
 * Get the proper number of arguments for a function.
 */
static unsigned get_func_nargs(enum functions f) {
   switch (f) {
    case F_UNION:
    case F_INTERSECT:
    case F_SETDIFFERENCE:
    case F_IN:
     return 2;

    case F_NONEMPTY:
    case F_MAKESET:
    case F_GETELEMENT:
     return 1;

    case F_COUNT:
    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
    case F_ALLTRUE:
    case F_ANYTRUE:
     return 1;

    case F_AND:
    case F_OR:
     return 2;

    case F_NOT:
     return 1;

    case F_NEW:
     /* Multiple args in concrete syntax become one tuple. */
     return 1;

    case F_CTIME:
     return 0;

    case F_EQ:
    case F_NOTEQ:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
     return 2;

    case F_TOSTRING:
     return 1;

    case F_CONCAT:
     return 1;

    case F_CHOOSE:
     return 2;

    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     return 2;

    case F_NEG:
    case F_ABS:
     return 1;
   }

   PQLASSERT(0);
   return 0;
}
#endif /* 0: unused */

/*
 * Get the most general required type for one argument of a function.
 */
static struct datatype *get_func_argtype(struct typeinf *ctx, enum functions f,
					 unsigned argnum) {

   switch (f) {
    /* set(T1) x set(T2) */
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
     return datatype_set(ctx->pql, datatype_abstop(ctx->pql));

    /* T|set(T) x set(T) */
    case F_IN:
     if (argnum == 0) {
	return datatype_abstop(ctx->pql);
     }
     return datatype_set(ctx->pql, datatype_abstop(ctx->pql));
     break;

    /* set(T) */
    case F_NONEMPTY:
    case F_GETELEMENT:
    case F_COUNT:
     return datatype_set(ctx->pql, datatype_abstop(ctx->pql));

    /* set(number) */
    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
     return datatype_set(ctx->pql, datatype_absnumber(ctx->pql));

    /* set(bool) */
    case F_ALLTRUE:
    case F_ANYTRUE:
     return datatype_set(ctx->pql, datatype_bool(ctx->pql));

    /* T x T */
    case F_EQ:
    case F_NOTEQ:
    case F_CHOOSE:
     return datatype_abstop(ctx->pql);

    /* T */
    case F_MAKESET:
    case F_NEW:
    case F_TOSTRING:
     return datatype_abstop(ctx->pql);

    /* seq(T) x seq(T) | string x string */
    case F_CONCAT:
     return datatype_abstop(ctx->pql);

    /* string x string */
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
     return datatype_string(ctx->pql);

    /* number x number */
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     return datatype_absnumber(ctx->pql);

    /* number */
    case F_NEG:
    case F_ABS:
     return datatype_absnumber(ctx->pql);

    /* bool x bool */
    case F_AND:
    case F_OR:
     return datatype_bool(ctx->pql);
     break;

    /* bool */
    case F_NOT:
     return datatype_bool(ctx->pql);

    /* void */
    case F_CTIME:
     PQLASSERT(0);
     return NULL;
   }

   PQLASSERT(0);
   return datatype_absbottom(ctx->pql);
}

static struct datatype *get_func_result(struct typeinf *ctx, enum functions f,
					struct datatypearray *subtypes) {
   struct datatype *left, *right, *sub;

   /* note: this does something reasonable when things are mismatched */
   switch (datatypearray_num(subtypes)) {
    case 2:
     left = datatypearray_get(subtypes, 0);
     right = datatypearray_get(subtypes, 1);
     sub = left;
     break;
    case 1:
     left = right = sub = datatypearray_get(subtypes, 0);
     break;
    default:
     left = right = sub = datatype_absbottom(ctx->pql);
     break;
   }

   switch (f) {
    case F_CONCAT:
     /* XXX should this really be a special case? */
     if (datatype_ispathelement(left)) {
	left = datatype_sequence(ctx->pql, left);
     }
     if (datatype_ispathelement(right)) {
	right = datatype_sequence(ctx->pql, right);
     }
     /* FALLTHROUGH */
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_CHOOSE:
     return datatype_match_generalize(ctx->pql, left, right);

    case F_MAKESET:
     return datatype_set(ctx->pql, sub);

    case F_GETELEMENT:
    case F_SUM:
    case F_MIN:
    case F_MAX:
     if (datatype_isset(sub)) {
	return datatype_set_member(sub);
     }
     break;

    case F_COUNT:
     return datatype_int(ctx->pql);

    case F_AVG:
     if (datatype_isset(sub) &&
	 datatype_isanynumber(datatype_set_member(sub))) {
	return datatype_double(ctx->pql);
     }
     break;

    case F_IN:
    case F_NONEMPTY:
    case F_ALLTRUE:
    case F_ANYTRUE:
    case F_AND:
    case F_OR:
    case F_NOT:
    case F_EQ:
    case F_NOTEQ:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
     return datatype_bool(ctx->pql);

    case F_NEW:
     if (datatype_isset(sub) ||
	 datatype_issequence(sub) ||
	 datatype_ispathelement(sub) ||
	 datatype_istuple(sub)) {
	return datatype_struct(ctx->pql);
     }
     return sub;

    case F_CTIME:
    case F_TOSTRING:
     return datatype_string(ctx->pql);

    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     /* provided both are numbers, use the most general number */
     return datatype_match_generalize(ctx->pql, left, right);

    case F_NEG:
    case F_ABS:
     return sub;
   }

   return datatype_absbottom(ctx->pql);
}

static struct coltree *get_func_columns(struct typeinf *ctx, enum functions f,
					struct coltreearray *subcols) {
   switch (f) {
    case F_UNION:
    case F_INTERSECT:
    case F_EXCEPT:
    case F_UNIONALL:
    case F_INTERSECTALL:
    case F_EXCEPTALL:
    case F_CONCAT:
    case F_CHOOSE:
     if (coltreearray_num(subcols) == 2) {
	struct coltree *left, *right;

	left = coltreearray_get(subcols, 0);
	right = coltreearray_get(subcols, 1);
	if (coltree_arity(left) == coltree_arity(right)) {
	   return coltree_clone(ctx->pql, left);
	}
     }
     break;

    case F_MAKESET:
    case F_GETELEMENT:
    case F_NOT:
    case F_NEG:
    case F_ABS:
     if (coltreearray_num(subcols) == 1) {
	return coltree_clone(ctx->pql, coltreearray_get(subcols, 0));
     }
     break;

    case F_IN:
    case F_NONEMPTY:
    case F_COUNT:
    case F_SUM:
    case F_AVG:
    case F_MIN:
    case F_MAX:
    case F_ALLTRUE:
    case F_ANYTRUE:
    case F_AND:
    case F_OR:
    case F_NEW:
    case F_CTIME:
    case F_EQ:
    case F_NOTEQ:
    case F_LT:
    case F_GT:
    case F_LTEQ:
    case F_GTEQ:
    case F_LIKE:
    case F_GLOB:
    case F_GREP:
    case F_SOUNDEX:
    case F_TOSTRING:
    case F_ADD:
    case F_SUB:
    case F_MUL:
    case F_DIV:
    case F_MOD:
     break;
   }

   return coltree_create_scalar(ctx->pql, NULL);
}

////////////////////////////////////////////////////////////
// recursive traversal

/*
 * Determine a type for TE, which is used in a context such that
 * ENVIRONMENT is the expected type. The type we choose might be
 * ENVIRONMENT, or a subtype thereof, or something completely
 * different (in which case typecheck will later flag an error...)
 *
 * Also, determine TE's own column name (used if it's amalgamated into
 * a tuple) and, if it's itself a tuple or tuple set, the column names
 * it contains.
 *
 * This module does not issue errors, warnings, or other diagnostics;
 * when the types are wrong it should plug something in that will
 * cause typecheck to issue an error later, and choose that something
 * to hopefully minimize the number of type errors arising from any
 * one mistake.
 */

static void tcexpr_typeinf(struct typeinf *ctx, struct tcexpr *te,
			   struct datatype *environment,
			   struct coltree *envmembers);


/*
 * Shared code for functions and operators.
 */
static void func_typeinf(struct typeinf *ctx,
			 enum functions op, struct tcexprarray *args,
			 struct datatype **type_ret,
			 struct coltree **cols_ret) {
   struct datatype *subtype, *resulttype;
   struct tcexpr *subexpr;
   struct datatypearray types;
   struct coltreearray cols;
   unsigned i, num;

   datatypearray_init(&types);
   coltreearray_init(&cols);

   num = tcexprarray_num(args);

   datatypearray_setsize(ctx->pql, &types, num);
   coltreearray_setsize(ctx->pql, &cols, num);

   for (i=0; i<num; i++) {
      subtype = get_func_argtype(ctx, op, i);
      subexpr = tcexprarray_get(args, i);
      tcexpr_typeinf(ctx, subexpr, subtype, NULL);
      /* use the type actually found */
      datatypearray_set(&types, i, subexpr->datatype);
      coltreearray_set(&cols, i, subexpr->colnames);
   }

   /* note that constructing a tuple type out of types[] would be wrong */

   resulttype = get_func_result(ctx, op, &types);
   if (resulttype == NULL) {
      resulttype = datatype_absbottom(ctx->pql);
   }
   *type_ret = resulttype;
   *cols_ret = get_func_columns(ctx, op, &cols);

   datatypearray_setsize(ctx->pql, &types, 0);
   coltreearray_setsize(ctx->pql, &cols, 0);

   datatypearray_cleanup(ctx->pql, &types);
   coltreearray_cleanup(ctx->pql, &cols);
}

/*
 * Wrapper for operators (with fixed-size arglists)
 */
static void op_typeinf(struct typeinf *ctx, enum functions op,
		       struct tcexpr *a0, struct tcexpr *a1,
		       struct datatype **type_ret,
		       struct coltree **cols_ret) {
   struct tcexprarray args;

   tcexprarray_init(&args);
   tcexprarray_add(ctx->pql, &args, a0, NULL);
   if (a1 != NULL) {
      tcexprarray_add(ctx->pql, &args, a1, NULL);
   }
   func_typeinf(ctx, op, &args, type_ret, cols_ret);
   tcexprarray_setsize(ctx->pql, &args, 0);
   tcexprarray_cleanup(ctx->pql, &args);
}

/*
 * General case
 */
static void tcexpr_typeinf(struct typeinf *ctx, struct tcexpr *te,
			   struct datatype *environment,
			   struct coltree *envmembers) {
   struct datatype *subtype, *lefttype, *righttype;
   struct colname *subcol;
   unsigned i, num;

   switch (te->type) {
    case TCE_FILTER:
     /*
      * Filter: we are the same type as the subexpression, which
      * should be set(T), and the test expression should be T -> bool.
      */
     tcexpr_typeinf(ctx, te->filter.sub, environment, envmembers);
     te->datatype = te->filter.sub->datatype;
     te->colnames = coltree_clone(ctx->pql, te->filter.sub->colnames);
     if (datatype_isset(te->datatype)) {
	subtype = datatype_set_member(te->datatype);
     }
     else {
	subtype = te->datatype;
     }
     subtype = datatype_lambda(ctx->pql, subtype, datatype_bool(ctx->pql));
     tcexpr_typeinf(ctx, te->filter.predicate, subtype, te->colnames);
     break;

    case TCE_PROJECT:
     /*
      * Project: use the type of the subexpression but keep only the
      * selected columns.
      */
     tcexpr_typeinf(ctx, te->project.sub, environment, envmembers);
     te->colnames = coltree_project(ctx->pql, te->project.sub->colnames,
				   te->project.cols);
     te->datatype = get_member_types(ctx, te->project.sub, te->project.cols);
     if (datatype_isset(te->project.sub->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }
     else if (datatype_issequence(te->project.sub->datatype)) {
	te->datatype = datatype_sequence(ctx->pql, te->datatype);
     }

     /*
      * This test should ideally be ==. But for now, let's not crash if
      * we try to project out something that's not there.
      */
     PQLASSERT(coltree_arity(te->colnames) <= colset_num(te->project.cols));
     break;

    case TCE_STRIP:
     /*
      * Strip: use the type of the subexpression but remove the
      * selected columns.
      */
     tcexpr_typeinf(ctx, te->strip.sub, environment, envmembers);
     te->colnames = coltree_strip(ctx->pql, te->strip.sub->colnames,
				 te->strip.cols);
     te->datatype = get_member_types_coltree(ctx, te->strip.sub, te->colnames);
     if (datatype_isset(te->strip.sub->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }
     break;

    case TCE_RENAME:
     /*
      * Rename: use type of subexpression, adjust matching name.
      */
     tcexpr_typeinf(ctx, te->rename.sub, environment, envmembers);
     te->datatype = te->rename.sub->datatype;
     te->colnames = coltree_rename(ctx->pql, te->rename.sub->colnames,
				  te->rename.oldcol, te->rename.newcol);
     break;

    case TCE_JOIN:
     /* Check the halves of the cartesian product. */
     subtype = datatype_abstop(ctx->pql);
     subtype = datatype_set(ctx->pql, subtype);
     tcexpr_typeinf(ctx, te->join.left, subtype, NULL);
     tcexpr_typeinf(ctx, te->join.right, subtype, NULL);
     lefttype = te->join.left->datatype;
     righttype = te->join.right->datatype;

     if (datatype_isabstop(lefttype) || datatype_isabstop(righttype)) {
	te->datatype = datatype_abstop(ctx->pql);
     }
     else if (!datatype_isset(lefttype) && !datatype_isset(righttype)) {
	/* adapt if something failed underneath us */
	te->datatype = datatype_tuple_concat(ctx->pql, lefttype, righttype);
     }
     else {
	// XXX should really only have set x set
	if (datatype_isset(lefttype)) {
	   lefttype = datatype_set_member(lefttype);
	}
	if (datatype_isset(righttype)) {
	   righttype = datatype_set_member(righttype);
	}
	te->datatype = datatype_tuple_concat(ctx->pql, lefttype, righttype);
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }
     te->colnames = coltree_join(ctx->pql, te->join.left->colnames,
				te->join.right->colnames);
#if 0
     // leave name null
     if (datatype_arity(lefttype) > 1) {
	copy_memberlist(ctx, &te->members, &te->join.left->members);
     }
     else {
	PQLASSERT(te->join.left->name != NULL);
	colnamearray_add(&te->members, te->join.left->name, NULL);
     }
     if (datatype_arity(righttype) > 1) {
	copy_memberlist(ctx, &te->members, &te->join.right->members);
     }
     else {
	PQLASSERT(te->join.right->name != NULL);
	colnamearray_add(&te->members, te->join.right->name, NULL);
     }
#endif
     
     /* Check the condition. */
     if (te->join.predicate != NULL) {
	subtype = te->datatype;
	if (datatype_isset(subtype)) {
	   subtype = datatype_set_member(subtype);
	}
	subtype = datatype_lambda(ctx->pql, subtype, datatype_bool(ctx->pql));
	tcexpr_typeinf(ctx, te->join.predicate, subtype, te->colnames);
     }
     break;

    case TCE_ORDER:
     /* Order: columns remain, type changes from set to sequence */
     tcexpr_typeinf(ctx, te->order.sub, environment, envmembers);
     subtype = te->order.sub->datatype;
     if (datatype_isset(subtype)) {
	subtype = datatype_sequence(ctx->pql, datatype_set_member(subtype));
     }
     else {
	/* wrong, oh well */
     }
     te->datatype = subtype;
     te->colnames = coltree_clone(ctx->pql, te->order.sub->colnames);
#if 0
     copy_name(ctx, te, te->order.sub);
     copy_members(ctx, te, te->order.sub);
#endif
     break;

    case TCE_UNIQ:
     /* Uniq: type and columns carry over from subexpression. */
     tcexpr_typeinf(ctx, te->uniq.sub, environment, envmembers);
     te->datatype = te->uniq.sub->datatype;
     te->colnames = coltree_clone(ctx->pql, te->uniq.sub->colnames);
#if 0
     copy_name(ctx, te, te->uniq.sub);
     copy_members(ctx, te, te->uniq.sub);
#endif
     break;

    case TCE_NEST:
     /*
      * Nest: keep the columns not specified, strip the columns specified,
      * wrap them in a set, and add that back as a single column.
      */
     tcexpr_typeinf(ctx, te->nest.sub, environment, envmembers);

     colset_resolve_tocomplement(ctx->pql, te->nest.cols,
				 te->nest.sub->colnames);

     te->colnames = coltree_nest(ctx->pql, te->nest.sub->colnames,
				te->nest.cols, te->nest.newcol);
     te->datatype = get_member_types_except(ctx, te->nest.sub,
					    te->nest.cols);

     subtype = get_member_types(ctx, te->nest.sub, te->nest.cols);
     subtype = datatype_set(ctx->pql, subtype);
     te->datatype = datatype_tuple_append(ctx->pql, te->datatype, subtype);
     if (datatype_isset(te->nest.sub->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }
     break;
     
    case TCE_UNNEST:
     /* Unnest: keep all but specified column, unpack that one */
     tcexpr_typeinf(ctx, te->unnest.sub, environment, envmembers);

     te->colnames = coltree_unnest(ctx->pql, te->unnest.sub->colnames,
				  te->unnest.col);
     te->datatype = get_member_types_except_one(ctx, te->unnest.sub,
						te->unnest.col);

     subtype = get_member_type(ctx, te->unnest.sub, te->unnest.col);
     if (subtype == NULL) {
	/* column not there, use bottom */
	subtype = datatype_absbottom(ctx->pql);
     }

     if (datatype_isset(subtype)) {
	subtype = datatype_set_member(subtype);
     }
     else {
	/* Wrong... oh well. */
     }

     te->datatype = datatype_tuple_concat(ctx->pql, te->datatype, subtype);

     if (datatype_isset(te->unnest.sub->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }

     break;

    case TCE_DISTINGUISH:
     /* Distinguish: add distinguisher column */
     tcexpr_typeinf(ctx, te->distinguish.sub, environment, envmembers);

     subtype = te->distinguish.sub->datatype;
     if (datatype_isset(subtype)) {
	subtype = datatype_set_member(subtype);
     }
     te->datatype = datatype_tuple_append(ctx->pql,
					  subtype,
					  datatype_distinguisher(ctx->pql));
     if (datatype_isset(te->distinguish.sub->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }

     // nope, coltree_adjoin does this (XXX which is inconsistent)
     //colname_incref(te->distinguish.newcol);
     te->colnames = coltree_adjoin(ctx->pql, te->distinguish.sub->colnames,
				  te->distinguish.newcol);
     break;

    case TCE_ADJOIN:
     /* Adjoin: add column based on result type of func */
     tcexpr_typeinf(ctx, te->adjoin.left, environment, envmembers);
     lefttype = te->adjoin.left->datatype;
     if (datatype_isset(lefttype)) {
	lefttype = datatype_set_member(lefttype);
     }
     righttype = datatype_lambda(ctx->pql, lefttype,datatype_abstop(ctx->pql));
     tcexpr_typeinf(ctx, te->adjoin.func, righttype,te->adjoin.left->colnames);
     righttype = te->adjoin.func->datatype;
     if (datatype_islambda(righttype)) {
	righttype = datatype_lambda_result(righttype);
     }
     else {
	/* wrong, oh well */
     }

     /*
      * Adjoin is supposed to be for adjoining scalar values, not
      * pasting tuples together. However, if we get a tuple back from
      * the function, we don't want to create a type like (a, (b, c))
      * because that's not legal; that should either be (a, b, c) or
      * (a, {b,c}).
      */

     /* adjoining to top must yield top */
     if (datatype_isabstop(lefttype)) {
	te->datatype = lefttype;
	te->colnames = coltree_adjoin(ctx->pql, te->adjoin.left->colnames,
				     te->adjoin.newcol);
     }
     else if (datatype_arity(righttype) != 1) {
	struct coltree *rightcols;

	// XXX this is gross
	PQLASSERT(te->adjoin.func->type == TCE_LAMBDA);
	rightcols = te->adjoin.func->lambda.body->colnames;

	te->datatype = datatype_tuple_concat(ctx->pql, lefttype, righttype);
	te->colnames = coltree_join(ctx->pql, te->adjoin.left->colnames,
				   rightcols);
	/* this gets ignored */
	(void)te->adjoin.newcol;
     }
     else {
	te->datatype = datatype_tuple_append(ctx->pql, lefttype, righttype);
	te->colnames = coltree_adjoin(ctx->pql, te->adjoin.left->colnames,
				     te->adjoin.newcol);
     }
     if (datatype_isset(te->adjoin.left->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }
     break;

    case TCE_STEP:
     /* optimization result; not allowed here */
     PQLASSERT(0);
     break;

    case TCE_REPEAT:
     /* Repeat. Urgh. */

     /* start expression */
     tcexpr_typeinf(ctx, te->repeat.sub, environment, envmembers);

     /*
      * Hack. In some cases te->repeat.sub might be a scalar, e.g.
      *    select ... from ... as X where select ... from X(.foo)+ ....
      * and tuplify can't deal with this, at least for now, because it
      * doesn't know the types of columns. The loop variable always
      * needs to hold a set, because even if we start from a scalar
      * it'll be a set on the second iteration. So insert set() in
      * the start expression if needed.
      */
     if (!datatype_isset(te->repeat.sub->datatype)) {
	te->repeat.sub = mktcexpr_uop(ctx->pql, F_MAKESET, te->repeat.sub);
	te->repeat.sub->datatype = datatype_set(ctx->pql,
					    te->repeat.sub->uop.sub->datatype);
	te->repeat.sub->colnames = coltree_clone(ctx->pql,
					    te->repeat.sub->uop.sub->colnames);
     }

     /* loop variable */
     lefttype = get_member_type(ctx, te->repeat.sub, te->repeat.subendcolumn);
     if (lefttype == NULL) {
	/* column not there, use bottom */
	lefttype = datatype_absbottom(ctx->pql);
     }
     PQLASSERT(datatype_isset(te->repeat.sub->datatype));
     lefttype = datatype_set(ctx->pql, lefttype);
     te->repeat.loopvar->datatype = lefttype;
     te->repeat.loopvar->colnames = coltree_create_scalar(ctx->pql,
						te->repeat.bodystartcolumn);
     PQLASSERT(tcvar_datatype_matches_colnames(te->repeat.loopvar));

     /* body expression */
     righttype = datatype_set(ctx->pql, datatype_abstop(ctx->pql));
     tcexpr_typeinf(ctx, te->repeat.body, righttype, NULL);
     righttype = te->repeat.body->datatype;

     /*
      * Given
      *    sub :: S
      *    body :: set(B, O, P),
      * the result type is
      *    S x set(seq(B), O, P)
      */

     lefttype = te->repeat.sub->datatype;
     if (datatype_isset(lefttype)) {
	lefttype = datatype_set_member(lefttype);
     }

     {
	struct colset *cs;

	cs = colset_empty(ctx->pql);

	colset_add(ctx->pql, cs, te->repeat.bodystartcolumn);
	if (te->repeat.bodypathcolumn != NULL) {
	   colset_add(ctx->pql, cs, te->repeat.bodypathcolumn);
	}
	colset_add(ctx->pql, cs, te->repeat.bodyendcolumn);

	righttype = get_member_types_except(ctx, te->repeat.body, cs);

	if (datatype_arity(righttype) > 0) {
	   righttype = datatype_sequence(ctx->pql, righttype);
	   te->datatype = datatype_tuple_concat(ctx->pql, lefttype, righttype);

	   te->colnames = coltree_strip(ctx->pql, te->repeat.body->colnames,
				       cs);
	   te->colnames = coltree_adjoin_coltree(ctx->pql,
					       te->repeat.sub->colnames,
					       te->colnames);
	   PQLASSERT(coltree_istuple(te->colnames));
	}
	else {
	   /* loop doesn't bind anything; avoid adding in sequence(unit) */
	   te->datatype = lefttype;
	   te->colnames = coltree_clone(ctx->pql, te->repeat.sub->colnames);
	}

	colset_destroy(ctx->pql, cs);
     }

     {
	struct coltree *tmptree;

	if (te->repeat.bodypathcolumn != NULL) {
	   righttype = get_member_type(ctx, te->repeat.body,
				       te->repeat.bodypathcolumn);
	   if (datatype_ispathelement(righttype)) {
	      righttype = datatype_sequence(ctx->pql, righttype);
	   }
	   te->datatype = datatype_tuple_append(ctx->pql, te->datatype,
						righttype);
	   tmptree = te->colnames;
	   te->colnames = coltree_adjoin(ctx->pql, tmptree,
					 te->repeat.repeatpathcolumn);
	   coltree_destroy(ctx->pql, tmptree);
	}

	righttype = get_member_type(ctx, te->repeat.body,
				    te->repeat.bodyendcolumn);
	te->datatype = datatype_tuple_append(ctx->pql, te->datatype,righttype);
	tmptree = te->colnames;
	te->colnames = coltree_adjoin(ctx->pql, tmptree,
				      te->repeat.repeatendcolumn);
	coltree_destroy(ctx->pql, tmptree);
     }

     if (datatype_isset(te->repeat.body->datatype)) {
	te->datatype = datatype_set(ctx->pql, te->datatype);
     }

     break;

    case TCE_SCAN:
     subtype = datatype_tuple_triple(ctx->pql,
				     datatype_dbobj(ctx->pql),
				     datatype_dbedge(ctx->pql),
				     datatype_dbobj(ctx->pql));
     te->datatype = datatype_set(ctx->pql, subtype);
     te->colnames = coltree_create_triple(ctx->pql,
					 NULL,
					 te->scan.leftobjcolumn,
					 te->scan.edgecolumn,
					 te->scan.rightobjcolumn);
#if 0
     colname_incref(te->scan.leftobjcolumn);
     colname_incref(te->scan.edgecolumn);
     colname_incref(te->scan.rightobjcolumn);
     colnamearray_add(&te->members, te->scan.leftobjcolumn, NULL);
     colnamearray_add(&te->members, te->scan.edgecolumn, NULL);
     colnamearray_add(&te->members, te->scan.rightobjcolumn, NULL);
     // leave name null
#endif
     if (te->scan.predicate != NULL) {
	subtype = datatype_lambda(ctx->pql, subtype, datatype_bool(ctx->pql));
	tcexpr_typeinf(ctx, te->scan.predicate, subtype, te->colnames);
     }
     break;

    case TCE_BOP:
     op_typeinf(ctx, te->bop.op, te->bop.left, te->bop.right,
		&te->datatype, &te->colnames);
     break;

    case TCE_UOP:
     op_typeinf(ctx, te->uop.op, te->uop.sub, NULL,
		&te->datatype, &te->colnames);
     break;

    case TCE_FUNC:
     func_typeinf(ctx, te->func.op, &te->func.args,
		  &te->datatype, &te->colnames);
     break;

    case TCE_MAP:
     tcexpr_typeinf(ctx, te->map.set, environment, envmembers);
     subtype = te->map.set->datatype;
     if (datatype_isset(subtype)) {
	subtype = datatype_set_member(subtype);
     }
     else {
	/* wrong, oh well */
     }
     te->map.var->datatype = subtype;
     te->map.var->colnames = coltree_clone(ctx->pql, te->map.set->colnames);
#if 0
     copy_memberlist(ctx, &te->map.var->members, &te->map.set->members);
#endif
     PQLASSERT(tcvar_datatype_matches_colnames(te->map.var));
     tcexpr_typeinf(ctx, te->map.result, datatype_abstop(ctx->pql), NULL);
     te->datatype = datatype_set(ctx->pql, te->map.result->datatype);
     te->colnames = coltree_clone(ctx->pql, te->map.result->colnames);
#if 0
     copy_name(ctx, te, te->map.result);
#endif
     break;

    case TCE_LET:
     tcexpr_typeinf(ctx, te->let.value, environment, envmembers);
     te->let.var->datatype = te->let.value->datatype;
     te->let.var->colnames = coltree_clone(ctx->pql, te->let.value->colnames);
#if 0
     copy_memberlist(ctx, &te->let.var->members, &te->let.value->members);
#endif
     PQLASSERT(tcvar_datatype_matches_colnames(te->let.var));
     tcexpr_typeinf(ctx, te->let.body, datatype_abstop(ctx->pql), NULL);
     te->datatype = te->let.body->datatype;
     te->colnames = coltree_clone(ctx->pql, te->let.body->colnames);
#if 0
     copy_name(ctx, te, te->let.body);
#endif
     break;

    case TCE_LAMBDA:
     /*
      * This is kind of lame, but we should be able to get away with it:
      * the user isn't allowed to write lambda expressions and the ones
      * issued by tuplify should always appear in contexts where the
      * environment provides a type.
      */
     if (datatype_islambda(environment)) {
	/* the environment gives us a type for the var; use that */
	te->lambda.var->datatype = datatype_lambda_argument(environment);
	PQLASSERT(envmembers != NULL);
	PQLASSERT(te->lambda.var->colnames == NULL);
	te->lambda.var->colnames = coltree_clone(ctx->pql, envmembers);
#if 0
	copy_memberlist(ctx, &te->lambda.var->members, envmembers);
#endif
	PQLASSERT(tcvar_datatype_matches_colnames(te->lambda.var));
	tcexpr_typeinf(ctx, te->lambda.body,
		       datatype_lambda_result(environment), envmembers);
     }
     else {
	/* assume top, probably not optimal */
	te->lambda.var->datatype = datatype_abstop(ctx->pql);
	te->lambda.var->colnames = NULL;
#if 0
	colnamearray_setsize(&te->lambda.var->members, 0);
#endif
	PQLASSERT(tcvar_datatype_matches_colnames(te->lambda.var));
	tcexpr_typeinf(ctx, te->lambda.body, datatype_abstop(ctx->pql), NULL);
     }
     te->datatype = datatype_lambda(ctx->pql, te->lambda.var->datatype,
				    te->lambda.body->datatype);
     te->colnames = coltree_create_scalar(ctx->pql, NULL);
     break;

    case TCE_APPLY:
     tcexpr_typeinf(ctx, te->apply.arg, datatype_abstop(ctx->pql), NULL);
     subtype = datatype_lambda(ctx->pql, te->apply.arg->datatype,
			       environment);
     tcexpr_typeinf(ctx, te->apply.lambda, subtype, te->apply.arg->colnames);
     subtype = te->apply.lambda->datatype;
     if (datatype_islambda(subtype)) {
	subtype = datatype_lambda_result(subtype);
     }
     else {
	/* wrong, oh well */
     }
     te->datatype = subtype;
     te->colnames = coltree_clone(ctx->pql, te->apply.lambda->colnames);
     break;

    case TCE_READVAR:
     /* variable should have been bound already */
     PQLASSERT(te->readvar->datatype != NULL);
     PQLASSERT(te->readvar->colnames != NULL);
     PQLASSERT(tcvar_datatype_matches_colnames(te->readvar));
     te->datatype = te->readvar->datatype;
     te->colnames = coltree_clone(ctx->pql, te->readvar->colnames);
#if 0
     copy_memberlist(ctx, &te->members, &te->readvar->members);
     // leave name null
#endif
     break;

    case TCE_READGLOBAL:
     te->datatype = datatype_set(ctx->pql, datatype_dbobj(ctx->pql));
     te->colnames = coltree_create_scalar(ctx->pql, NULL);
     break;

    case TCE_CREATEPATHELEMENT:
     subtype = datatype_tuple_triple(ctx->pql,
				     datatype_dbobj(ctx->pql),
				     datatype_dbedge(ctx->pql),
				     datatype_dbobj(ctx->pql));
     tcexpr_typeinf(ctx, te->createpathelement, subtype, NULL);
     te->datatype = datatype_pathelement(ctx->pql);
     te->colnames = coltree_create_scalar(ctx->pql, NULL);
     break;

    case TCE_SPLATTER:
     tcexpr_typeinf(ctx, te->splatter.value, environment, envmembers);
     tcexpr_typeinf(ctx, te->splatter.name, datatype_absdbedge(ctx->pql),NULL);
     // XXX is splatter going to have to be its own data type?
     te->datatype = te->splatter.value->datatype;
     te->colnames = coltree_clone(ctx->pql, te->splatter.value->colnames);
     break;

    case TCE_TUPLE:
     {
	struct datatypearray types;
	struct colset *names;
	struct tcexpr *subexpr;

	num = tcexprarray_num(&te->tuple.exprs);
	PQLASSERT(num == colset_num(te->tuple.columns));

	datatypearray_init(&types);
	datatypearray_setsize(ctx->pql, &types, num);

	names = colset_empty(ctx->pql);
	colset_setsize(ctx->pql, names, num);

	subtype = datatype_abstop(ctx->pql);
	for (i=0; i<num; i++) {
	   subexpr = tcexprarray_get(&te->tuple.exprs, i);
	   tcexpr_typeinf(ctx, subexpr, subtype, NULL);
	   datatypearray_set(&types, i, subexpr->datatype);
	   subcol = colset_get(te->tuple.columns, i);
	   colset_set(names, i, subcol);
	}

	te->datatype = datatype_tuple_specific(ctx->pql, &types);
	te->colnames = coltree_create_tuple(ctx->pql, NULL, names);

	colset_destroy(ctx->pql, names);
	datatypearray_setsize(ctx->pql, &types, 0);
	datatypearray_cleanup(ctx->pql, &types);

#if 0
	colnamearray_setsize(&te->members, num);
	for (i=0; i<num; i++) {
	   subcol = colnamearray_get(&te->tuple.columns, i);
	   colname_incref(subcol);
	   colnamearray_set(&te->members, i, subcol);
	}
#endif
     }
     break;

    case TCE_VALUE:
     te->datatype = pqlvalue_datatype(te->value);
     if (datatype_arity(te->datatype) == 0) {
	/* unit */
	te->colnames = coltree_create_unit(ctx->pql, NULL);
     }
     else {
	te->colnames = coltree_create_scalar(ctx->pql, NULL);
     }
     break;
   }

   PQLASSERT(datatype_matches_colnames(te->datatype, te->colnames));
}

////////////////////////////////////////////////////////////
// entry point

void typeinf(struct pqlcontext *pql, struct tcexpr *te) {
   struct typeinf ctx;

   typeinf_init(&ctx, pql);
   tcexpr_typeinf(&ctx, te, datatype_abstop(pql), NULL);
   typeinf_cleanup(&ctx);
}
