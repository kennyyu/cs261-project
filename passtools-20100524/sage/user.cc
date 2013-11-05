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

#include <stdio.h>

#include "pql.h"
#include "result.h"
#include "query.h"
#include "user.h"

static void user_printresult(const struct result *res) {
   char buf[8192];
   int i;

   for (i=0; i<res->compile_messages.num(); i++) {
      fprintf(stderr, "%s\n", res->compile_messages[i]);
   }

   assert(res->compile_dumpnames.num() == res->compile_dumptexts.num());
   for (i=0; i<res->compile_dumpnames.num(); i++) {
      fprintf(stderr, "******** %s ********\n\n", res->compile_dumpnames[i]);
      fprintf(stderr, "%s", res->compile_dumptexts[i]);
      fprintf(stderr, "\n"); // add an extra blank line
   }

   if (res->compile_failed) {
      return;
   }

   if (res->run_tracelines.num() > 0) {
      fprintf(stderr, "******** eval trace ********\n\n");
      for (i=0; i<res->run_tracelines.num(); i++) {
	 fprintf(stderr, "%s\n", res->run_tracelines[i]);
      }
      /* add an extra blank line */
      fprintf(stderr, "\n");
   }

   if (res->run_failed) {
      printf("FAILED\n");
   }
   else {
      pqlvalue_print(buf, sizeof(buf), res->run_value);
      printf("%s\n", buf);
   }
}

int user_submit_file(const char *file) {
   struct result res;
   int bad = 0;

   result_init(&res);
   query_submit_file(file, &res);
   user_printresult(&res);
   if (res.compile_failed || res.run_failed) {
      bad = 1;
   }
   result_cleanup(&res);
   return bad;
}

int user_submit_string(const char *string) {
   struct result res;
   int bad = 0;

   result_init(&res);
   query_submit_string(string, &res);
   user_printresult(&res);
   if (res.compile_failed || res.run_failed) {
      bad = 1;
   }
   result_cleanup(&res);
   return bad;
}
