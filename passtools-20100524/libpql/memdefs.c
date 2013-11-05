/*
 * Copyright 2009, 2010
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <err.h>

#include "pqlcontext.h"
#include "utils.h"
#include "memdefs.h"

/*
 * Uncomment this to enable logging all malloc and free operations.
 * Uncomment the ALWAYSFLUSH definition to fflush the log file on each
 * operation, which is slower but relatively crash-proof.
 *
 * Caution: the logging code is not thread-safe.
 */
//#define LOG_MALLOC
//#define LOG_MALLOC_ALWAYSFLUSH

/*
 * Uncomment this to do cross-checking of malloc operations.
 */
#define PARANOID_MALLOC

/*
 * This is an absolute cap on the amount of memory used by the PQL engine.
 * (Roughly, anyway.) Go with 512M.
 *
 * If you have very large data sets you might need to increase this;
 * however, if there's this much memory in use there's more likely
 * something wrong.
 */
#define MEMORYUSE_DEADMAN_SIZE (512UL*1024UL*1024UL)

////////////////////////////////////////////////////////////
// configuration

#define PATH_PQLBRL_MEMLOG "pqlbrl.memlog"

#define SIG_ALLOCATED	0xfadecade
#define SIG_FREE	0xdeadbeef

#define OP_MALLOC	'A'
#define OP_FREE		'F'

////////////////////////////////////////////////////////////
// memory logging

#ifdef LOG_MALLOC

static FILE *malloclog;

static void malloclog_close(void) {
   FILE *f;

   if (malloclog != NULL) {
      f = malloclog;
      malloclog = NULL;
      fclose(f);
   }
}

static void malloclog_open(void) {
   FILE *f;

   if (malloclog == NULL) {
      f = fopen(PATH_PQLBRL_MEMLOG, "w");
      if (f != NULL) {
	 malloclog = f;
	 atexit(malloclog_close);
      }
   }
}

static void malloclog_dolog(int op, void *ptr, size_t size, void *caller) {
#define PRINTWIDTH ((CHAR_BIT * sizeof(void *) + 3) / 4)
   char callerstring[PRINTWIDTH + 4];
   const int width = PRINTWIDTH;

   malloclog_open();

   if (malloclog != NULL) {
      if (caller != NULL) {
	 snprintf(callerstring, sizeof(callerstring), "0x%0*jx", width,
		  (uintmax_t)(uintptr_t)caller);
      }
      else {
	 snprintf(callerstring, sizeof(callerstring), "%-*s", width+2, "?");
      }
      fprintf(malloclog, "%s : %c %p %8zu\n", callerstring, op, ptr, size);
#ifdef LOG_MALLOC_ALWAYS_FLUSH
      fflush(malloclog);
#endif
   }
}

#define MALLOCLOG(op, ptr, size, caller) malloclog_dolog(op, ptr, size, caller)

#else

#define MALLOCLOG(op, ptr, size, caller) ((void)(caller))

#endif /* LOG_MALLOC */

////////////////////////////////////////////////////////////
// paranoid cross-checking

#ifdef PARANOID_MALLOC

struct memheader {
   void *thisptr;
   size_t size;
   unsigned long signature;
};

#define ROUNDUP(a, b) ((((a) + (b) - 1)/(b))*(b))

static inline size_t getrealsize(size_t size) {
   return ROUNDUP(size, sizeof(void *));
}

static inline struct memheader *gethead(void *ptr) {
   struct memheader *head;

   head = ptr;
   head--;
   return head;
}

static inline struct memheader *gettail(void *ptr, size_t realsize) {
   return (struct memheader *)(((char *)ptr) + realsize);
}

static size_t paranoid_checkblock(void *ptr,
				  struct memheader **head_ret,
				  struct memheader **tail_ret) {
   struct memheader *head,  *tail;

   PQLASSERT(ptr != NULL);

   head = gethead(ptr);
   if (head->thisptr != ptr) {
      PQLASSERT(!"paranoid_free: block header has been garbaged (wrong ptr)");
   }
   if (head->signature != SIG_ALLOCATED) {
      PQLASSERT(!"paranoid_free: block header has been garbaged (wrong sig)");
   }

   tail = gettail(ptr, getrealsize(head->size));

   if (tail->thisptr != ptr) {
      PQLASSERT(!"paranoid_free: block trailer has been garbaged (wrong ptr)");
   }
   if (tail->signature != SIG_ALLOCATED) {
      PQLASSERT(!"paranoid_free: block trailer has been garbaged (wrong sig)");
   }
   if (tail->size != head->size) {
      PQLASSERT(!"paranoid_free: block trailer has been garbaged (bad size)");
   }

   if (head_ret != NULL) {
      *head_ret = head;
   }
   if (tail_ret != NULL) {
      *tail_ret = tail;
   }

   return head->size;
}

static void *paranoid_malloc(size_t reqsize, void *caller) {
   size_t realsize;
   struct memheader *head, *tail;
   void *ret;

   realsize = getrealsize(reqsize);
   head = malloc(realsize + 2 * sizeof(struct memheader));
   if (head == NULL) {
      return NULL;
   }
   ret = head+1;
   tail = gettail(ret, realsize);

   if (head->signature == SIG_ALLOCATED) {
      PQLASSERT(!"malloc returned a block that was already allocated!");
   }
   head->thisptr = ret;
   head->size = reqsize;
   head->signature = SIG_ALLOCATED;
   *tail = *head;
   memset(ret, 'A', realsize);

   MALLOCLOG(OP_MALLOC, ret, reqsize, caller);

   return ret;
}

static void paranoid_free(void *ptr, size_t usersize, void *caller) {
   size_t reqsize, realsize;
   struct memheader *head, *tail;

   if (ptr == NULL) {
      return;
   }

   reqsize = paranoid_checkblock(ptr, &head, &tail);
   PQLASSERT(usersize == reqsize);

   realsize = getrealsize(reqsize);
   memset(ptr, 'K', realsize);

   MALLOCLOG(OP_FREE, ptr, reqsize, caller);

   head->thisptr = NULL;
   head->size = 0;
   head->signature = SIG_FREE;
   *tail = *head;

   free(head);
}

static void *paranoid_realloc(void *oldptr, size_t useroldsize, size_t newsize,
			      void *caller) {
   void *newptr;
   size_t oldsize, copysize;

   newptr = paranoid_malloc(newsize, caller);
   if (newptr == NULL) {
      return NULL;
   }

   if (oldptr == NULL) {
      return newptr;
   }

   oldsize = paranoid_checkblock(oldptr, NULL, NULL);
   copysize = oldsize < newsize ? oldsize : newsize;
   memmove(newptr, oldptr, copysize);

   paranoid_free(oldptr, useroldsize, caller);

   return newptr;
}

#define MALLOC(size, f)         paranoid_malloc(size, f)
#define FREE(ptr, os, f)        paranoid_free(ptr, os, f)
#define REALLOC(ptr, os, ns, f) paranoid_realloc(ptr, os, ns, f)

#else

#define MALLOC(size, f)         malloc(size)
#define FREE(ptr, os, f)        free(ptr)
#define REALLOC(ptr, os, ns, f) realloc(ptr, ns)

#endif /* PARANOID_MALLOC */

////////////////////////////////////////////////////////////
// failure-checking entry points

static void addmem(struct pqlcontext *pql, size_t amt) {
   /* pql is (necessarily) null when allocating a pqlcontext */
   if (pql == NULL) {
      return;
   }
   pql->meminuse += amt;
   if (pql->meminuse > MEMORYUSE_DEADMAN_SIZE) {
      errx(1, "Exceeded deadman memory usage threshold");
   }
   if (pql->meminuse > pql->peakmem) {
      if (pql->peakmem <= MEMORYUSE_DEADMAN_SIZE/2 &&
	  pql->meminuse > MEMORYUSE_DEADMAN_SIZE/2) {
	 warnx("Exceeded half of deadman memory usage threshold");
      }
      pql->peakmem = pql->meminuse;
   }
}

static void submem(struct pqlcontext *pql, size_t amt) {
   /* pql is (necessarily) null when freeing a pqlcontext */
   if (pql == NULL) {
      return;
   }
   PQLASSERT(pql->meminuse >= amt);
   pql->meminuse -= amt;
}

/*
 * XXX these should not crash on error.
 */

void *domallocfrom(struct pqlcontext *pql, size_t len, void *caller) {
   void *ret;

   ret = MALLOC(len, caller);
   if (ret == NULL) {
      errx(1, "Out of memory");
   }

   addmem(pql, len);
   return ret;
}

void dofreefrom(struct pqlcontext *pql, void *ptr, size_t size, void *caller) {
   FREE(ptr, size, caller);
   submem(pql, size);
}

void *doreallocfrom(struct pqlcontext *pql, void *ptr,
		    size_t oldsize, size_t newsize, void *caller) {
   void *ret;

   submem(pql, oldsize);
   addmem(pql, newsize);

   ret = REALLOC(ptr, oldsize, newsize, caller);
   if (ret == NULL) {
      errx(1, "Out of memory");
   }
   return ret;
}

void *domalloc(struct pqlcontext *pql, size_t len) {
   return domallocfrom(pql, len, GETCALLER());
}

void dofree(struct pqlcontext *pql, void *ptr, size_t size) {
   dofreefrom(pql, ptr, size, GETCALLER());
}

void *dorealloc(struct pqlcontext *pql, void *ptr,
		size_t oldsize, size_t newsize) {
   return doreallocfrom(pql, ptr, oldsize, newsize, GETCALLER());
}
