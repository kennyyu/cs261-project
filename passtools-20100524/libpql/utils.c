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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pql.h"
#include "memdefs.h"
#include "utils.h"

static unsigned evil = 0;
static pqlassertionhandler handler;

void pql_set_assertion_handler(pqlassertionhandler newhandler) {
   handler = newhandler;
}

void badassert(const char *x, const char *file, int line, const char *func) {
   char buf[4096];

   if (evil == 0) {
      evil++;
      if (handler != NULL) {
	 handler(x, file, line, func);
      }
   }
   if (evil == 1) {
      evil++;
      snprintf(buf, sizeof(buf), "PQL assertion failed: %s, at %s:%d in %s\n",
	       x, file, line, func);
      write(STDERR_FILENO, buf, strlen(buf));
   }
   abort();
}

char *dostrdup(struct pqlcontext *pql, const char *str) {
   char *ret;
   size_t len;

   len = strlen(str);
   ret = domallocfrom(pql, len+1, GETCALLER());
   strcpy(ret, str);
   return ret;
}

char *dostrndup(struct pqlcontext *pql, const char *str, size_t len) {
   char *ret;

   ret = domallocfrom(pql, len+1, GETCALLER());
   memcpy(ret, str, len);
   ret[len] = 0;
   return ret;
}

void dostrfree(struct pqlcontext *pql, char *s) {
   if (s != NULL) {
      dofreefrom(pql, s, strlen(s)+1, GETCALLER());
   }
}
