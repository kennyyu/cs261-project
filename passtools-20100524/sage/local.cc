/*
 * Copyright 2010
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

#include <stddef.h>
#include <err.h>
#include <assert.h>

#include "pql.h"
#include "pqlutil.h"

#include "utils.h"
#include "result.h"
#include "backend.h"
#include "local.h"


static struct pqlcontext *pql;

void local_init(const char *dbpath) {
   if (backend_init(dbpath)) {
      err(1, "Error initializing database");
   }

   pql = pqlcontext_create(&myops);
   if (pql == NULL) {
      backend_shutdown();
      err(1, "Error creating PQL context");
   }
}

void local_shutdown(void) {
   size_t leaks;

   leaks = pqlcontext_destroy(pql);
   pql = NULL;
   if (backend_shutdown()) {
      warn("Error closing database");
   }
   if (leaks > 0) {
      warn("%zu bytes leaked", leaks);
   }
}

void local_dodumps(bool val) {
   pqlcontext_dodumps(pql, val);
}

void local_dotrace(bool val) {
   pqlcontext_dotrace(pql, val);
}

static void local_process(struct pqlcontext *pql, struct pqlquery *pq,
			  struct result *res) {
   int i, num;
   const char *msg;

   /* always get any errors */
   num = pqlcontext_getnumerrors(pql);
   for (i=0; i<num; i++) {
      msg = pqlcontext_geterror(pql, i);
      res->compile_messages.add(xstrdup(msg));
   }
   pqlcontext_clearerrors(pql);

   /* always get any dumps - there won't be any if we didn't want them */
   num = pqlcontext_getnumdumps(pql);
   for (i=0; i<num; i++) {
      const char *passname = pqlcontext_getdumpname(pql, i);
      const char *dumptext = pqlcontext_getdumptext(pql, i);

      if (dumptext == NULL) {
	 continue;
      }
      res->compile_dumpnames.add(xstrdup(passname));
      res->compile_dumptexts.add(xstrdup(dumptext));
   }
   if (pq == NULL) {
      res->compile_failed = true;
   }
   else {
      res->compile_failed = false;

      pqlvalue *pv = pqlquery_run(pql, pq);
      assert(pv != NULL);

      res->run_failed = false;

      /* always get the trace; it'll be empty if we didn't want any */
      num = pqlcontext_getnumtracelines(pql);
      for (i=0; i<num; i++) {
	 msg = pqlcontext_gettraceline(pql, i);
	 res->run_tracelines.add(xstrdup(msg));
      }

      pqlquery_destroy(pq);

      res->run_value = pv;
   }
}

void local_submit_file(const char *file, struct result *res) {
   struct pqlquery *pq;

   pq = pql_compile_file(pql, file);
   local_process(pql, pq, res);
}

void local_submit_string(const char *cmd, struct result *res) {
   struct pqlquery *pq;

   pq = pql_compile_string(pql, cmd);
   local_process(pql, pq, res);
}
