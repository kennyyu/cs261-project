/*
 * Copyright 2008, 2010
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
#include <assert.h>

#include "interact.h"
#include "user.h"
#include "query.h"

static int cmd_dump(char **words) {
   (void)words;
   query_dodumps(true);
   return 0;
}

static int cmd_nodump(char **words) {
   (void)words;
   query_dodumps(false);
   return 0;
}

static int cmd_trace(char **words) {
   (void)words;
   query_dotrace(true);
   return 0;
}

static int cmd_notrace(char **words) {
   (void)words;
   query_dotrace(false);
   return 0;
}

static int cmd_quit(char **words) {
   (void)words;
   return 1;
}

static const struct {
   unsigned nwords;
   const char *cmd;
   int (*func)(char *words[]);
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

static int builtincmd(char *cmd) {
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
	 return cmds[i].func(words);
      }
   }
   fprintf(stderr, "Invalid command :%s\n", words[0]);
   return 0;
}

void interact(bool forceprompt) {
   char *buf;
   size_t buflen, bufmax;
   bool useprompt;

   bufmax = 1024;
#ifdef __cplusplus
   buf = new char[bufmax];
#else
   buf = malloc(bufmax);
#endif
   if (buf == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(1);
   }

   useprompt = (forceprompt || ttyname(STDIN_FILENO));

   while (1) {
      buflen = 0;

      if (feof(stdin)) {
	 break;
      }

      if (useprompt) {
	 printf("sage>> ");
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

#ifdef __cplusplus
	    newbuf = new char[bufmax*2];
	    delete []buf;
#else
	    newbuf = realloc(buf, bufmax*2);
#endif
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
	 if (builtincmd(buf+1)) {
	    break;
	 }
      }
      else {
	 user_submit_string(buf);
      }
   }

#ifdef __cplusplus
   delete []buf;
#else
   free(buf);
#endif
}
