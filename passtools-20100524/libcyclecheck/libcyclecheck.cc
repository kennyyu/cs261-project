/*
 * Copyright 2007
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
 * Common code for cycle-checking.
 *
 * This is not an analyzer - it is a cross-checking tool, or rather,
 * the guts of several cross-checking tools that operate on comparable
 * but different data formats.
 */

#include <string.h>
#include "ptrarray.h"
#include "primarray.h"
#include "libcyclecheck.h"

struct cyclecheck_edge {
   cyclecheck_node *anc;			/* ancestor side */
   cyclecheck_node *dec;			/* descendent side */
   int ancindex;			/* index in anc->edges_dec[] */
   int decindex;			/* index in dec->edges_anc[] */
   void *clientdata;
};

struct cyclecheck_node {
   int index;				/* index in allnodes[] */
   ptrarray<cyclecheck_edge> edges_anc;	/* edges leading to my ancestors */
   ptrarray<cyclecheck_edge> edges_dec;	/* edges leading to my descendents */
   void *clientdata;
};

struct cyclecheck_cycle {
   primarray<int> nodes;
   cyclecheck_edge *cause;
};

static ptrarray<cyclecheck_node> allnodes;
static ptrarray<cyclecheck_cycle> allcycles;
static int dup_count;

////////////////////////////////////////////////////////////

void cyclecheck_addedge(cyclecheck_node *anc, cyclecheck_node *dec, void *cd) {
   // first check if this edge already exists
   for (int i=0; i<anc->edges_dec.num(); i++) {
      if (anc->edges_dec[i]->dec == dec) {
	 dup_count++;
	 return;
      }
   }

   // now add it
   cyclecheck_edge *e = new cyclecheck_edge;
   e->anc = anc;
   e->dec = dec;
   e->ancindex = anc->edges_dec.add(e);
   e->decindex = dec->edges_anc.add(e);
   e->clientdata = cd;
}

cyclecheck_node *cyclecheck_addnode(void *cd) {
   cyclecheck_node *n = new cyclecheck_node;
   n->clientdata = cd;
   n->index = allnodes.add(n);
   return n;
}

////////////////////////////////////////////////////////////

int cyclecheck_getdups(void) {
   return dup_count;
}

int cyclecheck_getnumcycles(void) {
   return allcycles.num();
}

void *cyclecheck_getonecycle(int n, ptrarray<void> &nodedata) {
   cyclecheck_cycle *cc = allcycles[n];
   nodedata.setsize(cc->nodes.num());
   for (int i=0; i<nodedata.num(); i++) {
      nodedata[i] = allnodes[cc->nodes[i]]->clientdata;
   }
   return cc->cause->clientdata;
}

////////////////////////////////////////////////////////////

enum seenstate {
   S_NONE=0,
   S_EXAMINED,
   S_UNDERNEATH,
};

static primarray<seenstate> seen;
static primarray<int> where;

static void cyclecheck_gotcycle(int repeated_ix, cyclecheck_edge *cause) {
   int first = 0;
   for (int i=1; i<where.num()-1; i++) {
      if (where[i] == repeated_ix) {
	 first = i;
      }
   }

   cyclecheck_cycle *cc = new cyclecheck_cycle;
   cc->nodes.setsize(where.num()-first);
   for (int i=first; i<where.num(); i++) {
      cc->nodes[i-first] = where[i];
      cc->cause = cause;
   }
   allcycles.add(cc);
}

#include <stdio.h>
static void cyclecheck_docheck(int ix, cyclecheck_edge *ce) {
   where.push(ix);

   //fprintf(stderr, "\r");
   //for (int i=0; i<where.num(); i++) {
   //   fprintf(stderr, "%d.", where[i]);
   //}

   if (seen[ix]==S_UNDERNEATH) {
      cyclecheck_gotcycle(ix, ce);
   }
   else if (seen[ix]==S_EXAMINED) {
      // already been here
   }
   else {
      seen[ix] = S_UNDERNEATH;
      cyclecheck_node *cn = allnodes[ix];
      assert(cn);
      for (int i=0; i<cn->edges_dec.num(); i++) {
	 cyclecheck_docheck(cn->edges_dec[i]->dec->index, cn->edges_dec[i]);
      }
      seen[ix] = S_EXAMINED;
   }
   where.pop();
}

void cyclecheck_check(void) {
   seen.setsize(allnodes.num());
   for (int i=0; i<seen.num(); i++) {
      seen[i] = S_NONE;
   }

   for (int i=0; i<seen.num(); i++) {
      assert(seen[i] != S_UNDERNEATH);
      cyclecheck_docheck(i, NULL);
      assert(seen[i] == S_EXAMINED);
   }   
}

