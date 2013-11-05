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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <err.h>

#include "pql.h"
#include "pqlutil.h"

#include "utils.h"
#include "socketpath.h"
#include "result.h"
#include "remote.h"

#ifndef ERPCMISMATCH
/* ah, linux... */
#define ERPCMISMATCH EPROTO
#endif


static struct pqlcontext *pql;

static int remotesocket = -1;

static char readbuf[4096];
static size_t readbufpos;

static int remote_getline(char *buf, size_t bufmax) {
   char *s;
   size_t amt, x;
   ssize_t result;
   int serrno;

   if (remotesocket == -1) {
      *buf = 0;
      return 0;
   }

   while (1) {
      if (readbufpos >= bufmax-1) {
	 amt = bufmax-1;
	 break;
      }
      s = (char *)memchr(readbuf, '\n', readbufpos);
      if (s != NULL) {
	 *s = 0;
	 amt = (s-readbuf)+1;
	 break;
      }
      result = read(remotesocket, readbuf+readbufpos,
		    sizeof(readbuf)-readbufpos);
      if (result < 0) {
	 serrno = errno;

	 close(remotesocket);
	 remotesocket = -1;

	 errno = serrno;
	 return -1;
      }
      if (result == 0) {
	 close(remotesocket);
	 remotesocket = -1;
	 amt = readbufpos;
	 break;
      }
      readbufpos += result;
   }

   assert(amt < bufmax);
   memcpy(buf, readbuf, amt);
   x = amt;
   if (x > 0 && buf[x-1] == 0) {
      x--;
   }
   if (x > 0 && buf[x-1] == '\n') {
      x--;
   }
   if (x > 0 && buf[x-1] == '\r') {
      x--;
   }
   buf[x] = 0;

   readbufpos -= amt;
   memmove(readbuf, readbuf+amt, readbufpos);
   return 0;
}

int remote_init(const char *socketpath) {
   struct sockaddr_un sun;
   char buf[256], *s;
   int serrno;

   pql = pqlcontext_create(NULL);
   if (pql == NULL) {
      err(1, "Error creating PQL context");
   }

   getsocketaddr(socketpath, &sun);
   remotesocket = socket(PF_UNIX, SOCK_STREAM, 0);
   if (remotesocket < 0) {
      return -1;
   }
   if (connect(remotesocket, (struct sockaddr *)&sun, SUN_LEN(&sun)) < 0) {
      serrno = errno;
      close(remotesocket);
      errno = serrno;
      return -1;
   }

   if (remote_getline(buf, sizeof(buf)) < 0) {
      serrno = errno;
      close(remotesocket);
      errno = serrno;
      return -1;
   }
   if (*buf == '\0') {
      warnx("Server hung up without saying anything (?)");
      errno = EPROTO;
      return -1;
   }
   if (atoi(buf) != 100) {
      warnx("Unexpected server banner message %s", buf);
      errno = EPROTO;
      return -1;
   }
   s = strchr(buf, ' ');
   if (!s) {
      warnx("Corrupt server banner message %s", buf);
      errno = EPROTO;
      return -1;
   }
   if (atoi(s) != PROTOCOL_VERSION) {
      warnx("Wrong server protocol version %d", atoi(s));
      errno = ERPCMISMATCH;
      return -1;
   }

   return 0;
}

int remote_shutdown(void) {
   close(remotesocket);
   return 0;
}

static void remote_say(const char *fmt, ...) {
   va_list ap;
   char buf[4096];

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   if (remotesocket >= 0) {
      write(remotesocket, buf, strlen(buf));
   }
   // XXX if remotesocket < 0 should fail
}

static int do_remote_getline(char *buf, size_t bufmax, struct result *res) {
   int result;

   (void)res;

   result = remote_getline(buf, bufmax);
   if (result < 0) {
      // XXX should add message to something in res instead
      warn("remote_getline");
      res->compile_failed = true;
      return -1;
   }
   else if (*buf == 0) {
      // XXX should add message to something in res instead
      warnx("remote_getline: unexpected disconnect");
      res->compile_failed = true;
      return -1;
   }
   return 0;
}

static void remote_get_results(struct result *res) {
   char buf[4096];
   ssize_t result;
   int code;

   if (do_remote_getline(buf, sizeof(buf), res) < 0) {
      return;
   }

   /*
    * responses to query:
    *   101 nnnnn success, then a blob
    *   300 nnnnn warning messages (then the messages, then another response)
    *   500 protocol syntax error
    *   550 compile error
    *
    * XXX shouldn't really use atoi()
    */

   code = atoi(buf);
   if (code == 300) {
      while (1) {
	 if (do_remote_getline(buf, sizeof(buf), res) < 0) {
	    return;
	 }
	 if (!strcmp(buf, ".")) {
	    break;
	 }
	 res->compile_messages.add(xstrdup(buf));
      }
      if (do_remote_getline(buf, sizeof(buf), res) < 0) {
	 return;
      }
      code = atoi(buf);
   }

   if (code == 550) {
      res->compile_failed = true;
      return;
   }

   if (code == 500) {
      // XXX should go into res somewhere
      warnx("Protocol syntax error");
      res->compile_failed = true;
      return;
   }

   if (code == 101) {
      char *s;
      unsigned char *blob;
      size_t blobsize, blobpos;

      s = strchr(buf, ' ');
      if (s == NULL) {
	 // XXX should go into res somewhere
	 warnx("Garbage success result from query server");
	 res->compile_failed = true;
	 return;
      }
      blobsize = atoi(s);
      blob = new unsigned char[blobsize];
      blobpos = 0;
      while (blobpos < blobsize) {
	if (readbufpos > 0) {
	  result = blobsize - blobpos;
	  if ((size_t)result > readbufpos) {
	    result = readbufpos;
	  }
	  memcpy(blob, readbuf, result);
	  readbufpos -= result;
	  memmove(readbuf, readbuf+result, readbufpos);
	}
	else {
	  result = read(remotesocket, blob, blobsize);
	}
	if (result < 0) {
	  // XXX should go into res somewhere
	  warn("read result");
	  res->compile_failed = true;
	  delete []blob;
	  return;
	}
	if (result == 0) {
	  // XXX should go into res somewhere
	  warnx("read result: unexpected EOF");
	  res->compile_failed = true;
	  delete []blob;
	  return;
	}
	blobpos += result;
      }
      res->run_value = pqlunpickle(pql, blob, blobsize);
      if (res->run_value == NULL) {
	 warnx("unpack result");
	 res->compile_failed = true;
	 delete []blob;
	 return;
      }
      /* succeed */
      delete []blob;
      return;
   }

   warnx("Unexpected query server response %s", buf);
   res->compile_failed = true;
}

////////////////////////////////////////////////////////////

void remote_dodumps(bool val) {
   // XXX not currently supported
   (void)val;
}

void remote_dotrace(bool val) {
   // XXX not currently supported
   (void)val;
}

static void send_string(const char *str) {
   char *s;

   for (s = strchr(str, '\n'); s != NULL; s = strchr(str = s+1, '\n')) {
      write(remotesocket, str, s-str);
      write(remotesocket, "\r\n", 2);
   }
   if (s && *s) {
      write(remotesocket, str, strlen(str));
      write(remotesocket, "\r\n", 2);
   }
}

void remote_submit_file(const char *file, struct result *res) {
   FILE *f;
   char buf[4096];

   // XXX if remotesocket < 0 should fail

   f = fopen(file, "r");
   if (!f) {
      err(1, "%s", file);
   }

   remote_say("longquery\r\n");

   while (fgets(buf, sizeof(buf), f) != NULL) {
      send_string(buf);
   }
   remote_say(".\r\n");
   remote_get_results(res);
}

void remote_submit_string(const char *cmd, struct result *res) {
   // XXX if remotesocket < 0 should fail

   if (!strchr(cmd, '\n')) {
      remote_say("query %s\r\n", cmd);
   }
   else {
      remote_say("longquery\r\n");
      send_string(cmd);
      remote_say(".\r\n");
   }
   remote_get_results(res);
}
