/*
 * Copyright (c) 2009 David A. Holland.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Author nor the names of any contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>

#define ARRAYINLINE
#include "array.h"

struct array *array_create(struct pqlcontext *pql) {
   struct array *a;
   a = domalloc(pql, sizeof(*a));
   if (a != NULL) {
      array_init(a);
   }
   return a;
}

void array_destroy(struct pqlcontext *pql, struct array *a) {
   array_cleanup(pql, a);
   dofree(pql, a, sizeof(*a));
}

void array_init(struct array *a) {
   a->num = a->max = 0;
   a->v = NULL;
}

void array_cleanup(struct pqlcontext *pql, struct array *a) {
   arrayassert(a->num == 0);
   dofree(pql, a->v, a->max * sizeof(a->v[0]));
#ifdef ARRAYS_CHECKED
   a->v = NULL;
#endif
}

int array_setsize(struct pqlcontext *pql, struct array *a, unsigned num) {
   unsigned newmax;
   void **newptr;

   if (num > a->max) {
      newmax = a->max;
      while (num > newmax) {
	 newmax = newmax ? newmax*2 : 4;
      }
      newptr = dorealloc(pql, a->v, a->max*sizeof(a->v[0]),
			 newmax*sizeof(a->v[0]));
      if (newptr == NULL) {
	 return -1;
      }
      a->v = newptr;
      a->max = newmax;
   }
   a->num = num;
   return 0;
}

int array_insert(struct pqlcontext *pql, struct array *a, unsigned index) {
   unsigned movers;

   arrayassert(a->num <= a->max);
   arrayassert(index < a->num);

   movers = a->num - index;

   if (array_setsize(pql, a, a->num + 1)) {
      return -1;
   }

   memmove(a->v + index+1, a->v + index, movers*sizeof(*a->v));
   return 0;
}

void array_remove(struct array *a, unsigned index) {
   unsigned movers;

   arrayassert(a->num <= a->max);
   arrayassert(index < a->num);

   movers = a->num - (index + 1);
   memmove(a->v + index, a->v + index+1, movers*sizeof(*a->v));
   a->num--;
}
