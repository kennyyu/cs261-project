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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <err.h>

#include "pql.h"
#include "pqlutil.h"

#include "ptrarray.h"
#include "utils.h"
#include "socketpath.h"
#include "result.h"
#include "query.h"
#include "server.h"

//#define DEBUG

struct client {
   int fd;
   char buf[MAXIMUM_LINE_LENGTH];
   size_t bufpos;
   struct querycontext *ctx;

   bool collectinglines;
   bool toomanylines;
   ptrarray<char> lines;
};

static ptrarray<client> clients;
static bool server_done;
static int serversocket = -1;


////////////////////////////////////////////////////////////

static struct client *client_create(int fd) {
   struct client *cl;
   
   cl = new client;
   cl->fd = fd;
   cl->bufpos = 0;
   cl->ctx = NULL; /*querycontext_create();*/
   
   cl->collectinglines = false;
   cl->toomanylines = false;

   return cl;
}

static void client_destroy(struct client *cl) {
   int i;

   if (cl->lines.num() > 0) {
      /* this can happen if the user disconnects during a longquery */
      for (i=0; i<cl->lines.num(); i++) {
	 delete []cl->lines[i];
      }
      cl->lines.setsize(0);
   }

   /*querycontext_destroy(cl->ctx);*/
   delete cl;
}

////////////////////////////////////////////////////////////

static void client_say(struct client *cl, const char *fmt, ...) {
   char buf[4096];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   /* XXX ought to make sure it all gets written */
   write(cl->fd, buf, strlen(buf));
#ifdef DEBUG
   /* buf will already contain a \n */
   printf("%d -> %s", cl->fd, buf);
   fflush(stdout);
#endif
}

static void client_query(struct client *cl, const char *querytext) {
   struct pqlpickleblob blob;
   struct result res;
   int i;

   result_init(&res);
   query_submit_string(/*cl->ctx, */ querytext, &res);

   if (res.compile_messages.num() > 0) {
      client_say(cl, "300 Query compile warnings follow.\r\n");

      for (i=0; i<res.compile_messages.num(); i++) {
	 client_say(cl, "%s\r\n", res.compile_messages[i]);
      }
      client_say(cl, ".\r\n");
   }
   if (res.compile_failed) {
      client_say(cl, "550 Query not compilable.\r\n");
   }
   else {
      if (res.run_failed) {
	 client_say(cl, "501 Execution failed\r\n", blob.len);
      }
      else {
	 pqlpickle(res.run_value, &blob);
	 client_say(cl, "101 %zu byte result block\r\n", blob.len);
	 write(cl->fd, blob.data, blob.len);
	 pqlpickleblob_cleanup(&blob);
      }
   }
   result_cleanup(&res);
}

static void client_processlines(struct client *cl) {
   size_t len;
   int i;
   char *str;

   if (cl->toomanylines) {
      client_say(cl, "500 Block too long\r\n");
      return;
   }

   /*
    * It would be easier to feed a line at a time to the query parser,
    * but that isn't currently possible given the API.
    */
   len = 0;
   for (i=0; i<cl->lines.num(); i++) {
      len += strlen(cl->lines[i]);
   }
   str = new char[len+1];
   *str = 0;
   for (i=0; i<cl->lines.num(); i++) {
      strcat(str, cl->lines[i]);
   }
   client_query(cl, str);
   delete []str;

   for (i=0; i<cl->lines.num(); i++) {
      delete []cl->lines[i];
   }
   cl->lines.setsize(0);
}

static void client_processline(struct client *cl, char *buf) {
   size_t len;

#ifdef DEBUG
   printf("%d <- %s\n", cl->fd, buf);
   fflush(stdout);
#endif

   while (*buf == ' ' || *buf == '\t') {
      buf++;
   }
   len = strlen(buf);
   if (len > 0 && buf[len-1] == '\r') {
      len--;
   }
   while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\t')) {
      len--;
   }
   buf[len] = 0;

   if (!strncmp(buf, "toolong ", 8)) {
      client_say(cl, "500 Line too long\r\n");
   }
   else if (!strncmp(buf, "query ", 6)) {
      client_query(cl, buf+6);
   }
   else if (!strcmp(buf, "longquery")) {
      cl->collectinglines = true;
      cl->toomanylines = false;
   }
#if 0 /* no longer needed or desirable */
   else if (!strcmp(buf, "shutdown")) {
      /* shut down waldo (parent process) */
      kill(getppid(), SIGUSR1);
      server_done = true;
   }
#endif
   else {
      client_say(cl, "500 Unknown request\r\n");
   }
}

static int client_read(int fd) {
   struct client *cl;
   int r;
   char *s;
   
   cl = clients[fd];
   assert(cl != NULL);
   if (cl->bufpos >= sizeof(cl->buf)) {
      strcpy(cl->buf, "toolong ");
      cl->bufpos = 8;
   }
   r = read(fd, cl->buf+cl->bufpos, sizeof(cl->buf)-cl->bufpos);
   if (r < 0) {
      /* should syslog this? */
      warnx("client %d: read", fd);
      return -1;
   }
   if (r == 0) {
      /* eof */
      return -1;
   }
   cl->bufpos += r;

   while ((s = (char *)memchr(cl->buf, '\n', cl->bufpos)) != NULL) {
      *(s++) = 0;
      if (cl->collectinglines) {
	 if (!strcmp(cl->buf, ".\n") || !strcmp(cl->buf, ".\r\n")) {
	    client_processlines(cl);
	    cl->collectinglines = false;
	    cl->toomanylines = false;
	 }
	 else {
	    if (cl->lines.num() < MAXIMUM_LINE_COUNT) {
	       cl->lines.add(xstrdup(cl->buf));
	    }
	    else {
	       cl->toomanylines = true;
	    }
	 }
      }
      else {
	 client_processline(cl, cl->buf);
      }
      cl->bufpos -= (s - cl->buf);
      memmove(cl->buf, s, cl->bufpos);
   }

   return 0;
}

////////////////////////////////////////////////////////////

static void client_establish(int fd) {
   int i, num;

   num = clients.num();
   if (fd >= num) {
      clients.setsize(fd+1);
      for (i=num; i<fd+1; i++) {
	 clients[i] = NULL;
      }
   }
   assert(clients[fd] == NULL);
   clients[fd] = client_create(fd);
}

static void client_disestablish(int fd) {
   assert(clients[fd] != NULL);
   client_destroy(clients[fd]);
   clients[fd] = NULL;
}

static void server_bind(void) {
   struct sockaddr_un sun;
   mode_t oldumask;
   //char buf[PATH_MAX];

   // This is wrong - use "."
   //
   // We don't need the path to the cwd, just a file on the right
   // volume.  The directory whose path ends up in the socket name is
   // taken from the mount data.
   //if (getcwd(buf, sizeof(buf)) == NULL) {
   //   err(1, "getcwd");
   //}
   getsocketaddr(".", &sun);

   serversocket = socket(PF_UNIX, SOCK_STREAM, 0);
   if (serversocket < 0) {
      err(1, "socket");
   }

   oldumask = umask(0);
   unlink(sun.sun_path);
   if (bind(serversocket, (struct sockaddr *)&sun, SUN_LEN(&sun)) < 0) {
      err(1, "bind");
   }
   umask(oldumask);

   if (listen(serversocket, 16) < 0) {
      err(1, "listen");
   }
}

static void server_loop(void) {
   fd_set xset, aset;
   int newsocket;
   struct sockaddr_storage ss;
   socklen_t slen;
   int i;

   signal(SIGPIPE, SIG_IGN);

   clients.setsize(serversocket+1);
   for (i=0; i<serversocket+1; i++) {
      clients[i] = NULL;
   }

   server_done = false;
   FD_ZERO(&aset);
   FD_SET(serversocket, &aset);
   while (!server_done) {
      fflush(stdout);
      xset = aset;
      if (select(clients.num(), &xset, NULL, NULL, NULL) < 0) {
	 /* ? */
	 warn("select");
	 sleep(1);
	 continue;
      }
#if 0 /* we're no longer started by waldo */
      if (getppid() == 1) {
	 /* parent (waldo) exited, give up */
	 break;
      }
#endif
      if (FD_ISSET(serversocket, &xset)) {
	 slen = sizeof(ss);
	 newsocket = accept(serversocket, (struct sockaddr *)&ss, &slen);
#ifdef DEBUG
	 printf("%d ** new connection\n", newsocket);
	 fflush(stdout);
#endif
	 /*
	  * Don't care where it comes from really. (We should,
	  * however, get and examine the remote uid once we have real
	  * security controls.)
	  */
	 (void)ss;
	 (void)slen;
	 if (newsocket < 0) {
	    warn("accept");
	    continue;
	 }
	 FD_SET(newsocket, &aset);
	 client_establish(newsocket);
	 client_say(clients[newsocket], "100 %d sage is ready\r\n",
		    PROTOCOL_VERSION);
      }
      for (i=0; i<clients.num(); i++) {
	 if (i != serversocket && FD_ISSET(i, &xset)) {
	    if (client_read(i)) {
	       client_disestablish(i);
	       FD_CLR(i, &aset);
	       close(i);
#ifdef DEBUG
	       printf("%d ** connection closed\n", i);
	       fflush(stdout);
#endif
	    }
	 }
      }
   }
}

void serve(void) {
   server_bind();
   server_loop();
   close(serversocket);
   serversocket = -1;
}
