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

#ifndef TCALC_H
#define TCALC_H

#include <stdbool.h>

#include "array.h"
#include "functions.h"

struct pqlcontext;


/*
 * (Nested) tuple calculus.
 */

#ifndef TCALC_INLINE
#define TCALC_INLINE INLINE
#endif
struct tcglobal;
struct tcvar;
struct tcexpr;
DECLARRAY(tcglobal);
DECLARRAY(tcvar);
DECLARRAY(tcexpr);
DEFARRAY(tcglobal, TCALC_INLINE);
DEFARRAY(tcvar, TCALC_INLINE);
DEFARRAY(tcexpr, TCALC_INLINE);


/*
 * Standard relational operations:
 *
 *    FILTER(sub, expr) - take subset of SUB where EXPR is true.
 *                        ("select" in relational calculus terms.)
 *
 *    PROJECT_cols(sub) - extract columns
 *
 *    STRIP_cols(sub) - complement of PROJECT
 *
 *    RENAME(sub, col => col) - change column name
 *
 *    |x|_cols - join on columns COLS
 *
 *    ORDER_cols - sort by values in COLS
 *
 *    UNIQ_cols - discard duplicates going by values in COLS
 *
 *
 * Nested relational operations:
 *
 *    NEST_cols_newcol - combine rows whose values are equal outside
 *                of COLS, and collect the values found in COLS into a
 *                single tuple set as the column NEWCOL.
 *
 *    UNNEST_col - replicate row values outside of COL, generating one
 *                for each member of the set found in COL.
 *
 *
 * Nonstandard or nonconvential operations:
 *
 *
 * DISTINGUISH_col(sub)
 *
 *    Pastes a distinguisher onto SUB with the column name COL.
 *
 *
 * ADJOIN_col ( left, func )
 *
 *    Computes func(left) and pastes it on.
 *    Definition:
 *
 *    let t = DISTINGUISH_d(LEFT)
 *        u = map t' over t: (FUNC(t'), PROJECT_d(t'))
 *    in t |x|_d u
 *
 * STEP_col(sub, edge)
 *
 *    Arises from "SUB.EDGE". Definition:
 *
 *    let t = SUB x ALLEDGES
 *    in filter t by project_COL(t) = project_ALLEDGES.LEFT(t)
 *                   and project_ALLEDGES.NAME(t) = EDGE
 *
 *    (where ALLEDGES with schema (LEFT, NAME, RIGHT) is the table
 *    of all object linkages.)
 *
 * REPEAT_d0_seq ( input, body )
 *
 *    Arises from ".EDGE+". Definition:
 *
 *    \input ->				       :: d0, start
 *    result := emptyset()                     :: d0, seq
 *    s := ADJOIN_seq(input, emptysequence())  :: d0, seq, start
 *    repeat
 *       let b0 = DISTINGUISH_d(s)             :: d0, seq, start, d
 *           b1 = PROJECT_start_d(b0)          :: start, d
 *           b2 = map b1' over b1: body(b1')   :: d, q*, start
 *           b3 = PROJECT_d0_seq_d(b0)         :: d0, seq, d
 *           b4 = b3 |x|_d b2                  :: d0, seq, d, q*, start
 *           b5 = ADJOIN_newseq(b4, \k -> PROJECT_seq(k) ++ PROJECT_q*(k))
 *                                             :: d0, seq, d, q*, newseq, start
 *           b6 = PROJECT_newseq_start(b5)     :: d0, newseq, start
 *           b7 = RENAME(b6, newseq -> seq)    :: d0, seq, start
 *       in s := b7
 *       result := result ++ PROJECT_d0_seq(s)
 *    while nonempty(s)
 *    return result                            :: d0, seq
 *
 *    This is huge. Bleh.
 */


/* Global (database) variable */
struct tcglobal {
   char *name;
   unsigned refcount;
};

/* Ordinary tuple variable */
struct tcvar {
   unsigned id;
   unsigned refcount;
   struct datatype *datatype;
   struct coltree *colnames;
};

/* Expression */
enum tcexprtypes {
   TCE_FILTER,				/* keep tuples where column = true */
   TCE_PROJECT,				/* extract column(s) */
   TCE_STRIP,				/* remove column(s) */
   TCE_RENAME,				/* change column name */
   TCE_JOIN,				/* join */
   TCE_ORDER,				/* sort by column(s) */
   TCE_UNIQ,				/* uniq by column(s) */

   TCE_NEST,				/* collect column(s) */
   TCE_UNNEST,				/* expand column(s) */

   TCE_DISTINGUISH,			/* add column containing unique id */
   TCE_ADJOIN,				/* add column containing f(x) */

   TCE_STEP,				/* follow graph edge */
   TCE_REPEAT,				/* repeat until convergence */
   TCE_SCAN,				/* inspect whole objects table */

   TCE_BOP,				/* binary operator */
   TCE_UOP,				/* unary operator */
   TCE_FUNC,				/* user-called function */

   TCE_MAP,				/* map over a tuple set */
   TCE_LET,				/* let-bind a tuple set */
   TCE_LAMBDA,				/* lambda-bind a tuple variable */
   TCE_APPLY,				/* substitute into a lambda */
   TCE_READVAR,				/* read a tuple set var */
   TCE_READGLOBAL,			/* read from a global variable */

   TCE_CREATEPATHELEMENT,		/* construct a pathelement */
   TCE_SPLATTER,			/* splatter (computed edge name) */
   TCE_TUPLE,				/* construct a tuple */

   TCE_VALUE,				/* constant */
};
struct tcexpr {
   struct coltree *colnames;		/* column info */
   struct datatype *datatype;

   enum tcexprtypes type;
   union {
      /*
       * Standard relational algebra ops on tuple sets.
       */
      struct {
	 struct tcexpr *sub;
	 struct tcexpr *predicate;
      } filter;
      struct {
	 struct tcexpr *sub;
	 struct colset *cols;
      } project;
      struct {
	 struct tcexpr *sub;
	 struct colset *cols;
      } strip;
      struct {
	 // XXX how do we do computed names? what if they're not uniform?
	 struct tcexpr *sub;
	 // XXX change this to "inside" and "outside" col for clarity
	 struct colname *oldcol;
	 struct colname *newcol;
      } rename;
      struct {
	 struct tcexpr *left;
	 struct tcexpr *right;
	 struct tcexpr *predicate;
      } join;
      struct {
	 struct tcexpr *sub;
	 struct colset *cols;
      } order;
      struct {
	 struct tcexpr *sub;
	 struct colset *cols;
      } uniq;

      /*
       * For nest/unnest the columns listed are the ones that are
       * transformed between sets and members. Thus nest is the
       * complement of the language-level group-by: given (A, B, C),
       * "group by C" means "nest on A, B".
       */
      struct {
	 struct tcexpr *sub;
	 struct colset *cols;
	 struct colname *newcol;
      } nest;
      struct {
	 struct tcexpr *sub;
	 struct colname *col;
      } unnest;

      /*
       * Variant relational algebra ops on tuple sets, as defined above.
       */
      struct {
	 struct tcexpr *sub;
	 struct colname *newcol;
      } distinguish;
      struct {
	 struct tcexpr *left;
	 struct tcexpr *func;
	 struct colname *newcol;
      } adjoin;

      /*
       * Graph-related ops, as defined above.
       */
      struct {
	 struct tcexpr *sub;
	 struct colname *subcolumn;
	 struct pqlvalue *edgename;
	 bool reversed;
	 struct colname *leftobjcolumn;	/* Column IDs to generate */
	 struct colname *edgecolumn;
	 struct colname *rightobjcolumn;
	 struct tcexpr *predicate;
      } step;
      struct {
	 struct tcexpr *sub;		/* expr for stepping-off point */
	 struct colname *subendcolumn;	/* trailing column from subexpr */
	 struct tcvar *loopvar;		/* var used by body */
	 struct colname *bodystartcolumn;/* leading column used by body */
	 struct tcexpr *body;		/* repeated expression */
	 struct colname *bodypathcolumn;/* path column generated by body */
	 struct colname *bodyendcolumn;/* trailing column generated by body */
	 struct colname *repeatpathcolumn;/* path column for repeat */
	 struct colname *repeatendcolumn;/* trailing column for whole repeat*/
      } repeat;

      struct {
	 struct colname *leftobjcolumn;	/* Column IDs to generate */
	 struct colname *edgecolumn;
	 struct colname *rightobjcolumn;
	 struct tcexpr *predicate;
      } scan;

      /*
       * Computational operators.
       */
      struct {
	 struct tcexpr *left;
	 enum functions op;
	 struct tcexpr *right;
      } bop;
      struct {
	 enum functions op;
	 struct tcexpr *sub;
      } uop;
      struct {
	 enum functions op;
	 struct tcexprarray args;
      } func;

      /*
       * Tuple variables.
       */
      struct {
	 struct tcvar *var;
	 struct tcexpr *set;
	 struct tcexpr *result;
      } map;
      struct {
	 struct tcvar *var;
	 struct tcexpr *value;
	 struct tcexpr *body;
      } let;
      struct {
	 struct tcvar *var;
	 struct tcexpr *body;
      } lambda;
      struct {
	 struct tcexpr *lambda;
	 struct tcexpr *arg;
      } apply;
      struct tcvar *readvar;
      struct tcglobal *readglobal;

      /*
       * Type constructors
       */
      struct tcexpr *createpathelement;
      struct {
	 struct tcexpr *value;
	 struct tcexpr *name;
      } splatter;
      struct {
	 struct tcexprarray exprs;
	 struct colset *columns;
      } tuple;

      /*
       * And finally, values.
       */
      struct pqlvalue *value;
   };
};

////////////////////////////////////////////////////////////
// constructors

struct tcglobal *mktcglobal(struct pqlcontext *, const char *name);
void tcglobal_incref(struct tcglobal *);

struct tcvar *mktcvar_fresh(struct pqlcontext *);
void tcvar_incref(struct tcvar *);

struct tcexpr *tcexpr_clone(struct pqlcontext *, struct tcexpr *te);

struct tcexpr *mktcexpr_filter(struct pqlcontext *,
			       struct tcexpr *sub, struct tcexpr *textexpr);
struct tcexpr *mktcexpr_project_none(struct pqlcontext *,
				     struct tcexpr *sub);
struct tcexpr *mktcexpr_project_one(struct pqlcontext *,
				    struct tcexpr *sub, struct colname *col);
struct tcexpr *mktcexpr_project_two(struct pqlcontext *,
				    struct tcexpr *sub, struct colname *col1,
				    struct colname *col2);
struct tcexpr *mktcexpr_project_three(struct pqlcontext *,
				      struct tcexpr *sub,
				      struct colname *col1,
				      struct colname *col2,
				      struct colname *col3);
struct tcexpr *mktcexpr_strip_none(struct pqlcontext *,
				   struct tcexpr *sub);
struct tcexpr *mktcexpr_strip_one(struct pqlcontext *,
				  struct tcexpr *sub, struct colname *col);
struct tcexpr *mktcexpr_rename(struct pqlcontext *,
			       struct tcexpr *sub, struct colname *oldcol,
			       struct colname *newcol);

struct tcexpr *mktcexpr_join(struct pqlcontext *,
			     struct tcexpr *left, struct tcexpr *right,
			     struct tcexpr *predicate);
struct tcexpr *mktcexpr_order(struct pqlcontext *, struct tcexpr *sub);
struct tcexpr *mktcexpr_uniq(struct pqlcontext *, struct tcexpr *sub);


struct tcexpr *mktcexpr_nest_none(struct pqlcontext *,
				  struct tcexpr *sub,
				  struct colname *newcol);
struct tcexpr *mktcexpr_nest_one(struct pqlcontext *,
				 struct tcexpr *sub, struct colname *col,
				 struct colname *newcol);
struct tcexpr *mktcexpr_nest_set(struct pqlcontext *,
				 struct tcexpr *sub, struct colset *cols,
				 struct colname *newcol);
struct tcexpr *mktcexpr_unnest(struct pqlcontext *,
			       struct tcexpr *sub, struct colname *col);


struct tcexpr *mktcexpr_distinguish(struct pqlcontext *,
			       struct tcexpr *sub, struct colname *newcol);
struct tcexpr *mktcexpr_adjoin(struct pqlcontext *,
			       struct tcexpr *left, struct tcexpr *func,
			       struct colname *newcol);


struct tcexpr *mktcexpr_step(struct pqlcontext *,
			     struct tcexpr *sub,
			     struct colname *subcolumn,
			     struct pqlvalue *edgename,
			     bool reversed,
			     struct colname *leftobjcolumn,
			     struct colname *edgecolumn,
			     struct colname *rightobjcolumn,
			     struct tcexpr *predicate);
struct tcexpr *mktcexpr_repeat(struct pqlcontext *,
			       struct tcexpr *sub,
			       struct colname *subendcolumn,
			       struct tcvar *loopvar,
			       struct colname *bodystartcolumn,
			       struct tcexpr *body,
			       struct colname *bodypathcolumn,
			       struct colname *bodyendcolumn,
			       struct colname *repeatpathcolumn,
			       struct colname *repeatendcolumn);

struct tcexpr *mktcexpr_scan(struct pqlcontext *,
			     struct colname *leftobjcolumn,
			     struct colname *edgecolumn,
			     struct colname *rightobjcolumn,
			     struct tcexpr *predicate);


struct tcexpr *mktcexpr_bop(struct pqlcontext *, struct tcexpr *left,
			    enum functions op, struct tcexpr *right);
struct tcexpr *mktcexpr_uop(struct pqlcontext *,
			    enum functions op, struct tcexpr *sub);
struct tcexpr *mktcexpr_func(struct pqlcontext *pql, enum functions op);

struct tcexpr *mktcexpr_map(struct pqlcontext *, struct tcvar *var,
			    struct tcexpr *set, struct tcexpr *result);
struct tcexpr *mktcexpr_let(struct pqlcontext *, struct tcvar *var,
			    struct tcexpr *value, struct tcexpr *body);
struct tcexpr *mktcexpr_lambda(struct pqlcontext *, struct tcvar *var,
			       struct tcexpr *body);
struct tcexpr *mktcexpr_apply(struct pqlcontext *,
			      struct tcexpr *lambda, struct tcexpr *arg);

struct tcexpr *mktcexpr_readvar(struct pqlcontext *, struct tcvar *var);
struct tcexpr *mktcexpr_readglobal(struct pqlcontext *, struct tcglobal *var);

struct tcexpr *mktcexpr_createpathelement(struct pqlcontext *,
					  struct tcexpr *sub);
struct tcexpr *mktcexpr_splatter(struct pqlcontext *pql,
				 struct tcexpr *value, struct tcexpr *name);
struct tcexpr *mktcexpr_tuple(struct pqlcontext *pql, unsigned arity);

struct tcexpr *mktcexpr_value(struct pqlcontext *, struct pqlvalue *val);


////////////////////////////////////////////////////////////
// destructors

void tcglobal_decref(struct pqlcontext *, struct tcglobal *);
void tcvar_decref(struct pqlcontext *, struct tcvar *);

void tcexpr_destroy(struct pqlcontext *pql, struct tcexpr *);

////////////////////////////////////////////////////////////
// dump

char *tcdump(struct pqlcontext *pql, struct tcexpr *te, bool showtypes);


#endif /* TCALC_H */
