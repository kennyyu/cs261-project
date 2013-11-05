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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

#include "pql.h"
#include "pqlutil.h"

static int cmd_dump(struct pqlcontext *pql, char **words) {
   (void)words;
   pqlcontext_dodumps(pql, true);
   return 0;
}

static int cmd_nodump(struct pqlcontext *pql, char **words) {
   (void)words;
   pqlcontext_dodumps(pql, false);
   return 0;
}

static int cmd_trace(struct pqlcontext *pql, char **words) {
   (void)words;
   pqlcontext_dotrace(pql, true);
   return 0;
}

static int cmd_notrace(struct pqlcontext *pql, char **words) {
   (void)words;
   pqlcontext_dotrace(pql, false);
   return 0;
}

static int cmd_quit(struct pqlcontext *pql, char **words) {
   (void)pql;
   (void)words;
   return 1;
}

static const struct {
   unsigned nwords;
   const char *cmd;
   int (*func)(struct pqlcontext *, char *words[]);
} cmds[] = {
   { 1, "d",       cmd_dump },
   { 1, "dump",    cmd_dump },
   { 1, "nd",      cmd_nodump },
   { 1, "nodump",  cmd_nodump },
   { 1, "nt",      cmd_notrace },
   { 1, "notrace", cmd_notrace },
   { 1, "q",       cmd_quit },
   { 1, "quit",    cmd_quit },
   { 1, "t",       cmd_trace },
   { 1, "trace",   cmd_trace },
};
static const unsigned numcmds = sizeof(cmds) / sizeof(cmds[0]);

static int builtincmd(struct pqlcontext *pql, char *cmd) {
#define MAXWORDS 1024
   char *s, *words[MAXWORDS];
   unsigned i, numwords;

   numwords = 0;
   for (s=strtok(cmd, " \t\r\n"); s; s=strtok(NULL, " \t\r\n")) {
      if (numwords < MAXWORDS) {
	 words[numwords++] = s;
      }
      else {
	 fprintf(stderr, "Too many words in command (max is %u)\n",
		 MAXWORDS);
	 return 0;
      }
   }
   if (numwords == 0) {
      return 0;
   }

   for (i=0; i<numcmds; i++) {
      if (numwords == cmds[i].nwords && !strcmp(words[0], cmds[i].cmd)) {
	 return cmds[i].func(pql, words);
      }
   }
   fprintf(stderr, "Invalid command :%s\n", words[0]);
   return 0;
}

void pql_interact(struct pqlcontext *pql,
		  bool forceprompt,
		  void (*print_result)(struct pqlvalue *)) {
   char *buf;
   size_t buflen, bufmax;
   bool useprompt;

   bufmax = 1024;
   buf = malloc(bufmax);
   if (buf == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(1);
   }

   useprompt = (forceprompt || ttyname(STDIN_FILENO));

   pql_setprinterrorname(NULL);

   while (1) {
      buflen = 0;

      if (feof(stdin)) {
	 break;
      }

      if (useprompt) {
	 printf("PQL: ");
	 fflush(stdout);
      }

      if (feof(stdout) || ferror(stdout)) {
	 break;
      }

      while (1) {
	 char *readplace;
	 size_t readmax;

	 readplace = buf + buflen;
	 readmax = bufmax - buflen;
	 if (!fgets(readplace, readmax, stdin)) {
	    break;
	 }
	 buflen += strlen(readplace);
	 assert(buflen <= bufmax);
	 if (buflen == bufmax) {
	    char *newbuf;

	    newbuf = realloc(buf, bufmax*2);
	    if (!newbuf) {
	       fprintf(stderr, "Out of memory");
	       exit(1);
	    }
	    buf = newbuf;
	    bufmax *= 2;
	 }

	 if (buflen > 0 && buf[buflen-1]==';') {
	    break;
	 }
	 if (buflen > 1 && buf[buflen-2]==';' && buf[buflen-1]=='\n') {
	    break;
	 }
	 if (buflen > 0 && buf[0]==':' && buf[buflen-1]=='\n') {
	    break;
	 }
      }

      if (buf[0]==':') {
	 if (builtincmd(pql, buf+1)) {
	    break;
	 }
      }
      else {
	 struct pqlquery *pq;

	 pq = pql_compile_string(pql, buf);
	 /* always print errors, if any */
	 pql_printerrors(pql);
	 pqlcontext_clearerrors(pql);
	 /* always print dumps - there won't be any if we didn't want them */
	 pql_printdumps(pql);
	 if (pq != NULL) {
	    struct pqlvalue *pv;

	    pv = pqlquery_run(pql, pq);
	    /* always print the trace; it'll be empty if we didn't want any */
	    pql_printtrace(pql);
	    pqlquery_destroy(pq);
	    print_result(pv);
	    pqlvalue_destroy(pv);
	 }
      }
   }
   free(buf);
}
