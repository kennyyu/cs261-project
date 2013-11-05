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

#include <stdio.h> // for snprintf
#include <stdarg.h>
#include <string.h>
#include <err.h> // XXX

#include "utils.h"
#include "memdefs.h"
#include "pql.h"
#include "datatype.h"
#include "pttree.h"
#include "pqlcontext.h"

#define DEFAULT_DUMPWIDTH 80

struct pqlcontext *pqlcontext_create(const struct pqlbackend_ops *ops) {
   struct pqlcontext *pql;

   pql = domalloc(NULL, sizeof(*pql));
   pql->ops = ops;
   pql->dodumps = false;
   pql->dumpwidth = DEFAULT_DUMPWIDTH;
   pql->dotrace = false;

   /*
    * Null everything first, for safety.
    * (It is in particular important that the memory stats get zeroed
    * before we call anything else that might call domalloc.)
    */
   pql->dtm = NULL;
   pql->ptm = NULL;
   pql->meminuse = 0;
   pql->peakmem = 0;

   pql->nextnameid = 0;
   pql->nextcolumnid = 0;
   pql->nextvarid = 0;
   stringarray_init(&pql->errors);
   stringarray_init(&pql->dumps);
   stringarray_init(&pql->trace);

   pql->pcb = NULL;

   /*
    * Initialize things that are supposed to always exist.
    */

   pql->dtm = datatypemanager_create(pql);
   pql->ptm = ptmanager_create(pql);

   return pql;
}

size_t pqlcontext_destroy(struct pqlcontext *pql) {
   size_t ret;

   PQLASSERT(pql->pcb == NULL);

   pqlcontext_clearerrors(pql);
   stringarray_cleanup(pql, &pql->errors);

   pqlcontext_cleardumps(pql);
   stringarray_cleanup(pql, &pql->dumps);

   pqlcontext_cleartrace(pql);
   stringarray_cleanup(pql, &pql->trace);

   ptmanager_destroy(pql->ptm);
   pql->ptm = NULL;

   datatypemanager_destroy(pql->dtm);
   pql->dtm = NULL;

   ret = pql->meminuse;
   dofree(NULL, pql, sizeof(*pql));
   return ret;
}

size_t pqlcontext_getmemorypeak(struct pqlcontext *pql) {
   return pql->peakmem;
}

void pqlcontext_getfreshname(struct pqlcontext *pql, char *buf, size_t maxlen){
   snprintf(buf, maxlen, ".T%u", pql->nextnameid++);
}

////////////////////////////////////////////////////////////

void complain(struct pqlcontext *pql, unsigned line, unsigned col,
	      const char *msgfmt, ...) {
   char buf[1024];
   char buf2[1024 + 32];
   char *msg;
   va_list ap;

   va_start(ap, msgfmt);
   vsnprintf(buf, sizeof(buf), msgfmt, ap);
   va_end(ap);

   snprintf(buf2, sizeof(buf2), "%u:%u: %s", line, col, buf);

   msg = dostrdup(pql, buf2);
   stringarray_add(pql, &pql->errors, msg, NULL);
}

unsigned pqlcontext_getnumerrors(struct pqlcontext *pql) {
   return stringarray_num(&pql->errors);
}

const char *pqlcontext_geterror(struct pqlcontext *pql, unsigned which) {
   PQLASSERT(which < stringarray_num(&pql->errors));
   return stringarray_get(&pql->errors, which);
}

void pqlcontext_clearerrors(struct pqlcontext *pql) {
   unsigned i, num;

   num = stringarray_num(&pql->errors);
   for (i=0; i<num; i++) {
      dostrfree(pql, stringarray_get(&pql->errors, i));
   }
   stringarray_setsize(pql, &pql->errors, 0);
}

////////////////////////////////////////////////////////////

void pqlcontext_dodumps(struct pqlcontext *pql, bool setting) {
   pql->dodumps = setting;
}

unsigned pqlcontext_getnumdumps(struct pqlcontext *pql) {
   unsigned num;

   num = stringarray_num(&pql->dumps);
   PQLASSERT(num == 0 || num == NUM_DUMPSTAGES);
   return num;
}

const char *pqlcontext_getdumpname(struct pqlcontext *pql, unsigned num) {
   const char *ret;
   enum dumpstages stage;

   (void)pql; /* unused */

   ret = NULL; // for gcc 4.1

   PQLASSERT(num < NUM_DUMPSTAGES);
   stage = num;

   switch (stage) {
    case DUMP_PARSER: ret = "parser"; break;
    case DUMP_RESOLVE: ret = "resolve"; break;
    case DUMP_NORMALIZE: ret = "normalize"; break;
    case DUMP_UNIFY: ret = "unify"; break;
    case DUMP_MOVEPATHS: ret = "movepaths"; break;
    case DUMP_BINDNIL: ret = "bindnil"; break;
    case DUMP_DEQUANTIFY: ret = "dequantify"; break;
    case DUMP_TUPLIFY: ret = "tuplify"; break;
    case DUMP_TYPEINF: ret = "typeinf"; break;
    case DUMP_NORENAMES: ret = "norenames"; break;
    case DUMP_BASEOPT: ret = "baseopt"; break;
    case DUMP_STEPJOINS: ret = "stepjoins"; break;
   }

   return ret;
}

const char *pqlcontext_getdumptext(struct pqlcontext *pql, unsigned num) {
   PQLASSERT(num < NUM_DUMPSTAGES);
   return stringarray_get(&pql->dumps, num);
}

const char *pqlcontext_getdumpbyname(struct pqlcontext *pql, const char *name){
   unsigned i;

   for (i = 0; i < NUM_DUMPSTAGES; i++) {
      if (!strcmp(name, pqlcontext_getdumpname(pql, i))) {
	 return pqlcontext_getdumptext(pql, i);
      }
   }

   return NULL;
}

void pqlcontext_cleardumps(struct pqlcontext *pql) {
   unsigned i, num;

   num = stringarray_num(&pql->dumps);
   for (i=0; i<num; i++) {
      dostrfree(pql, stringarray_get(&pql->dumps, i));
   }
   stringarray_setsize(pql, &pql->dumps, 0);
}

void pqlcontext_adddump(struct pqlcontext *pql, enum dumpstages stage,
			char *txt) {
   unsigned num, i;

   num = stringarray_num(&pql->dumps);
   if (num == 0) {
      stringarray_setsize(pql, &pql->dumps, NUM_DUMPSTAGES);
      for (i=0; i<NUM_DUMPSTAGES; i++) {
	 stringarray_set(&pql->dumps, i, NULL);
      }
      num = stringarray_num(&pql->dumps);
   }
   PQLASSERT(num == NUM_DUMPSTAGES);

   stringarray_set(&pql->dumps, stage, txt);
}

////////////////////////////////////////////////////////////

void pqlcontext_dotrace(struct pqlcontext *pql, bool setting) {
   pql->dotrace = setting;
}

unsigned pqlcontext_getnumtracelines(struct pqlcontext *pql) {
   return stringarray_num(&pql->trace);
}

const char *pqlcontext_gettraceline(struct pqlcontext *pql, unsigned num) {
   PQLASSERT(num < pqlcontext_getnumtracelines(pql));
   return stringarray_get(&pql->trace, num);
}

void pqlcontext_cleartrace(struct pqlcontext *pql) {
   unsigned i, num;

   num = stringarray_num(&pql->trace);
   for (i=0; i<num; i++) {
      dostrfree(pql, stringarray_get(&pql->trace, i));
   }
   stringarray_setsize(pql, &pql->trace, 0);
}

void pqlcontext_addtrace(struct pqlcontext *pql, char *txt) {
   stringarray_add(pql, &pql->trace, txt, NULL);
}

////////////////////////////////////////////////////////////

