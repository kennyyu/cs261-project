/*
 * Copyright 2008, 2009, 2010
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
#include <getopt.h>

#include "interact.h"
#include "user.h"
#include "server.h"
#include "query.h"


static void usage(const char *av0) {
   fprintf(stderr, "Usage:\n");
   fprintf(stderr, "    %s [-p dbpath | -f filesystem] [-dt] "
	   "[-i | -s | -c cmd | script]\n", av0);
   exit(1);
}

int main(int argc, char *argv[]) {
   const char *cmd = NULL;
   const char *cmdfile = NULL;
   const char *dbpath = ".";
   const char *socketpath = NULL;
   int dodumps = 0;
   int dotrace = 0;
   int dointeractive = 0;
   int forceprompt = 0;
   int servermode = 0;
   int bad = 0;

   int ch;
   while ((ch = getopt(argc, argv, "c:df:ip:st")) != -1) {
      switch (ch) {
       case 'c': cmd = optarg; break;
       case 'd': dodumps = 1; break;
       case 'f': socketpath = optarg; dbpath = NULL; break;
       case 'i': dointeractive = 1; forceprompt = 1; break;
       case 'p': dbpath = optarg; socketpath = NULL; break;
       case 's': servermode = 1; break;
       case 't': dotrace = 1; break;
       default: usage(argv[0]); break;
      }
   }
   if (optind < argc) {
      cmdfile = argv[optind++];
   }
   if (optind < argc) {
      usage(argv[0]);
   }

   if ((cmdfile != NULL) + (cmd != NULL) +
       (dointeractive != 0) + (servermode != 0) > 1) {
      usage(argv[0]);
   }

   query_init(dbpath, socketpath);
   query_dodumps(dodumps);
   query_dotrace(dotrace);

   if (servermode) {
      serve();
   }
   else if (cmdfile) {
      if (user_submit_file(cmdfile)) {
	 bad = 1;
      }
   }
   else if (cmd) {
      if (user_submit_string(cmd)) {
	 bad = 1;
      }
   }
   else {
      interact(forceprompt);
   }

   query_shutdown();

   return bad;
}
