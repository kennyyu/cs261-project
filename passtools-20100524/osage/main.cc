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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "ptrarray.h"
#include "primarray.h"
#include "dump.h"
#include "ast.h"
#include "main.h"

#include "wdb.h"

int g_dodumps;
int g_dotrace;

static void process(expr *e) {
   if (g_dodumps || g_dotrace) {
      printf("------------------------------------------------------------\n");
   }

   if (g_dodumps) {
      printf("Starting AST:\n");
      ast_dump(e);
      printf("------------------------------------------------------------\n");
      printf("Baseopt:\n");
   }
   e = baseopt(e);
   if (g_dodumps) {
      ast_dump(e);
      printf("------------------------------------------------------------\n");
      printf("Indexify:\n");
   }

   e = indexify(e);
   if (g_dodumps) {
      ast_dump(e);
      printf("------------------------------------------------------------\n");
      printf("Baseopt after indexify:\n");
   }

   e = baseopt(e);
   if (g_dodumps) {
      ast_dump(e);
      printf("------------------------------------------------------------\n");
   }


   if (g_dotrace) {
      printf("Eval trace:\n");
      dump_begin();
   }

   value *v = eval(e);

   if (g_dotrace) {
      dump_end();
      printf("------------------------------------------------------------\n");
   }

   output_result(v);
}

////////////////////////////////////////////////////////////

static int cmd_quit(ptrarray<char> &) {
   return 1;
}

static const struct {
   int nwords;
   const char *cmd;
   int (*func)(ptrarray<char> &words);
} cmds[] = {
   { 1, "q",    cmd_quit },
   { 1, "quit", cmd_quit },
};
static const unsigned numcmds = sizeof(cmds) / sizeof(cmds[0]);

static int builtincmd(char *cmd) {
   ptrarray<char> words;

   for (char *s=strtok(cmd, " \t\r\n"); s; s=strtok(NULL, " \t\r\n")) {
      words.add(s);
   }
   if (words.num()==0) {
      return 0;
   }

   for (unsigned i=0; i<numcmds; i++) {
      if (words.num()==cmds[i].nwords && !strcmp(words[0], cmds[i].cmd)) {
	 return cmds[i].func(words);
      }
   }
   fprintf(stderr, "Invalid command :%s\n", words[0]);
   return 0;
}

static void interact(bool forceprompt) {
   primarray<char> buf;
   buf.setsize(1024);
   int buflen;
   bool useprompt = (forceprompt || ttyname(STDIN_FILENO));

   while (1) {
      buflen = 0;

      if (feof(stdin)) {
	 break;
      }

      if (useprompt) {
	 printf("sage: ");
	 fflush(stdout);
      }

      if (feof(stdout) || ferror(stdout)) {
	 break;
      }

      while (1) {
	 char *readplace = buf.getdata()+buflen;
	 size_t readmax = buf.num()-buflen;
	 if (!fgets(readplace, readmax, stdin)) {
	    break;
	 }
	 buflen += strlen(readplace);
	 assert(buflen <= buf.num());
	 if (buflen == buf.num()) {
	    buf.setsize(buf.num()*2);
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
	 if (builtincmd(buf.getdata()+1)) {
	    break;
	 }
      }
      else {
	 expr *e = compile_string(buf.getdata());
	 if (e) {
	    process(e);
	 }
      }
   }
}

////////////////////////////////////////////////////////////

static void usage(const char *av0) {
   fprintf(stderr, "Usage:\n");
   fprintf(stderr, "    %s [-p dbpath] [-dt] [-i]\n", av0);
   fprintf(stderr, "    %s [-p dbpath] [-dt] [-c cmd]\n", av0);
   fprintf(stderr, "    %s [-p dbpath] [-dt] script\n", av0);
   exit(1);
}

int main(int argc, char *argv[]) {
   const char *cmd = NULL;
   const char *cmdfile = NULL;
   const char *dbpath = ".";
   int interactive = 0;
   int forceprompt = 0;
   int bad = 0;

   int ch;
   while ((ch = getopt(argc, argv, "c:dip:t")) != -1) {
      switch (ch) {
       case 'c': cmd = optarg; break;
       case 'd': g_dodumps = 1; break;
       case 'i': interactive = 1; forceprompt = 1; break;
       case 'p': dbpath = optarg; break;
       case 't': g_dotrace = 1; break;
       default: usage(argv[0]); break;
      }
   }
   if (optind < argc) {
      cmdfile = argv[optind++];
   }
   if (optind < argc) {
      usage(argv[0]);
   }

   if ((cmdfile != NULL) + (cmd != NULL) + (interactive != 0) > 1) {
      usage(argv[0]);
   }

   wdb_startup(dbpath, WDB_O_RDONLY);

   if (cmdfile) {
      expr *e = compile_file(cmdfile);
      if (!e) {
	 bad = 1;
      }
      else {
	 process(e);
      }
   }
   else if (cmd) {
      expr *e = compile_string(cmd);
      if (!e) {
	 bad = 1;
      }
      else {
	 process(e);
      }
   }
   else {
      interact(forceprompt);
   }

   //wdb_close_all_dbs(); // apparently not
   wdb_shutdown();

   return bad;
}
