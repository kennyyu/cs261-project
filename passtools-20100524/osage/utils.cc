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
#include <stdlib.h>
#include <string.h>
#include "utils.h"

static int failed;

const location nowhere = { -1, -1 };

void unwhine(void) {
   failed = 0;
}

int whined(void) {
   return failed;
}

static void vwhine(location loc, const char *fmt, va_list ap) {
   fflush(stdout);
   fprintf(stderr, "%d:%d: ", loc.line, loc.col);
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
}

void whine(location loc, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   vwhine(loc, fmt, ap);
   va_end(ap);
}

void die(location loc, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   vwhine(loc, fmt, ap);
   va_end(ap);
   exit(1);
}

char *xstrdup(const char *s) {
   return strcpy(new char[strlen(s)+1], s);
}
