/*
 * Copyright 2008
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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#ifndef MAP_COPY
#define MAP_COPY MAP_PRIVATE
#endif

#include "pql.h"
#include "pqlutil.h"

struct pqlquery *pql_compile_file(struct pqlcontext *pql, const char *path) {
   int fd;
   struct stat st;
   size_t size;
   void *ptr;
   struct pqlquery *ret;

   fd = open(path, O_RDONLY);
   if (fd < 0) {
      warn("%s", path);
      return NULL;
   }

   if (fstat(fd, &st)) {
      warn("%s: fstat", path);
      close(fd);
      return NULL;
   }

   if (st.st_size > 0x10000000) {
      warnx("%s: File is unreasonably large", path);
      return NULL;
   }
   /* note: assignment may truncate 64 to 32 */
   size = st.st_size;

   ptr = mmap(NULL, size, PROT_READ, MAP_FILE|MAP_COPY, fd, 0);
   if (ptr == MAP_FAILED) {
      warn("%s: mmap", path);
      close(fd);
      return NULL;
   }
   close(fd);

   ret = pqlquery_compile(pql, (const char *)ptr, size);

   munmap(ptr, size);
   return ret;
}

struct pqlquery *pql_compile_string(struct pqlcontext *pql, const char *text) {
   return pqlquery_compile(pql, text, strlen(text));
}
