/*
 * Copyright 2006, 2007
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
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "primarray.h"
#include "dump.h"

struct dumpstate {
   bool atbol;
   int indent;
};

static primarray<dumpstate> prevdumps;
static dumpstate curdump;
static bool dumping;

void dump_begin(void) {
   if (dumping) {
      if (!curdump.atbol) {
	 printf("\n");
      }
      curdump.atbol = true;
      prevdumps.add(curdump);
      curdump.indent++;
   }
   else {
      curdump.indent = 0;
      curdump.atbol = 1;
   }
   dumping = true;
}

void dump_end(void) {
   assert(dumping);
   if (!curdump.atbol) {
      printf("\n");
   }

   if (prevdumps.num() > 0) {
      int t = curdump.indent;
      curdump = prevdumps.pop();
      assert(curdump.indent == t-1);
   }
   else {
      assert(curdump.indent == 0);
      dumping = false;
   }
}

void dump_indent(void) {
   curdump.indent++;
}

void dump_unindent(void) {
   assert(curdump.indent > 0);
   curdump.indent--;
}

void dump(const char *fmt, ...) {
   char buf[4096];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   for (int i=0; buf[i]; i++) {
      if (buf[i] != '\n') {
	 if (curdump.atbol) {
	    printf("%-*s", curdump.indent*3, "");
	    curdump.atbol = false;
	 }
	 putchar(buf[i]);
      }
      else {
	 if (!curdump.atbol) {
	    putchar('\n');
	    curdump.atbol = 1;
	 }
      }
   }
}
