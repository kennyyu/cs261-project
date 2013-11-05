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

#include "utils.h"
#include "pql.h"
#include "pttree.h"  // for ptmanager
#include "tcalc.h"  // for tcexpr_destroy
#include "passes.h"
#include "pqlcontext.h"

struct pqlquery {
   struct pqlcontext *pql;
   struct tcexpr *te;
};

struct pqlquery *pqlquery_compile(struct pqlcontext *pql,
				  const char *textbuf, size_t len) {
   struct pqlquery *q;
   struct ptexpr *pe;
   struct tcexpr *te;

   /*
    * Parse
    */

   /* Should not be a parse in progress... */
   PQLASSERT(pql->pcb == NULL);

   pe = parse(pql, textbuf, len);

   /* ...and the parser is supposed to clean up after itself. */
   PQLASSERT(pql->pcb == NULL);

   if (pe == NULL) {
      return NULL;
   }
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_PARSER, ptdump(pql, pe));
   }

   /*
    * Resolve variable references
    */

   pe = resolvevars(pql, pe);
   if (pe == NULL) {
      ptmanager_destroyall(pql->ptm);
      return NULL;
   }
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_RESOLVE, ptdump(pql, pe));
   }

   /*
    * Normalize everything, especially paths.
    */
   pe = normalize(pql, pe);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_NORMALIZE, ptdump(pql, pe));
   }

   /*
    * Unify common path segments.
    */
   pe = unify(pql, pe);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_UNIFY, ptdump(pql, pe));
   }

   /*
    * Move all paths to the from-clause. (Note: not for the
    * set-canonicalization world, if that ever reappears.)
    */
   pe = movepaths(pql, pe);
   if (pe == NULL) {
      ptmanager_destroyall(pql->ptm);
      return NULL;
   }
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_MOVEPATHS, ptdump(pql, pe));
   }

   /*
    * Arrange to deal with variables whose bindings are skippable.
    */
   pe = bindnil(pql, pe);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_BINDNIL, ptdump(pql, pe));
   }

   /*
    * Turn quantifier expressions into map expressions.
    */
   pe = dequantify(pql, pe);

   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_DEQUANTIFY, ptdump(pql, pe));
   }

   /*
    * Convert to tuple calculus
    */

   te = tuplify(pql, pe);
   //ptexpr_destroy(pe); -- handled by region allocator
   ptmanager_destroyall(pql->ptm);
   pe = NULL;

   if (te == NULL) {
      return NULL;
   }

   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_TUPLIFY, tcdump(pql, te, false));
   }

   /*
    * Infer types
    */
   typeinf(pql, te);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_TYPEINF, tcdump(pql, te, true));
   }

   /*
    * Check the resulting types
    */
   if (typecheck(pql, te) < 0) {
      tcexpr_destroy(pql, te);
      return NULL;
   }

   /*
    * First, scratch out all the rename operations.
    */
   te = norenames(pql, te);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_NORENAMES, tcdump(pql, te, false));
   }

   te = baseopt(pql, te);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_BASEOPT, tcdump(pql, te, false));
   }

   te = stepjoins(pql, te);
   if (pql->dodumps) {
      pqlcontext_adddump(pql, DUMP_STEPJOINS, tcdump(pql, te, false));
   }

   q = domalloc(pql, sizeof(*q));
   q->pql = pql;
   q->te = te;
   return q;
}

void pqlquery_destroy(struct pqlquery *q) {
   tcexpr_destroy(q->pql, q->te);
   q->te = NULL;
   dofree(q->pql, q, sizeof(*q));
}

struct pqlvalue *pqlquery_run(struct pqlcontext *pql,
			      const struct pqlquery *q) {
   PQLASSERT(pql == q->pql);

   return eval(pql, q->te);
}
