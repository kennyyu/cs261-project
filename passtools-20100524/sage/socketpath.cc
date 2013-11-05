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
#include <sys/statvfs.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <err.h>

#include "pathnames.h"
#include "socketpath.h"

/*
 * Turn on TEST if testing/running as non-root.
 *
 * Note: don't use /tmp directly on a machine where there might be
 * hostile users or processes that can screw around in /tmp. Make a
 * subdir instead that isn't world-writable.
 *
 * Note that while the sage server can do getuid() to figure out if it
 * should use /var/run or not, the client has no way of telling what
 * you mean. So the simplest way of supporting this case, for now at
 * least, is to compile something in.
 */

//#define TEST

#ifdef TEST
#define SOCKETDIR _PATH_TMP
//#define SOCKETDIR _PATH_TMP "sagetest/"
#else
#define SOCKETDIR _PATH_VARRUN
#endif

static void adjust(char *path) {
   size_t i;

   for (i=0; path[i]; i++) {
      if (path[i] == '/') {
	 path[i] = '|';
      }
   }
}

#ifdef __linux__
static void linux_match_mountpoint(char *buf, size_t max, unsigned long fsid) {
   struct statvfs st;
   FILE *f;
   char line[256], *s, *t;

   f = fopen(PATH_LINUX_PROC_MOUNTS, "r");
   if (!f) {
      err(1, "%s", PATH_LINUX_PROC_MOUNTS);
   }
   while (fgets(line, sizeof(line), f)) {
      s = strchr(line, ' ');
      if (!s) {
	 continue;
      }
      s++;
      t = strchr(s, ' ');
      if (!t) {
	 continue;
      }
      *t = 0;
      if (statvfs(s, &st) < 0) {
	 warn("%s: statvfs", s);
	 continue;
      }
      if (st.f_fsid == fsid) {
	 fclose(f);
	 snprintf(buf, max, "%s", s);
	 return;
      }
   }
   fclose(f);
   warnx("Cannot find mount point, using \"???\"");
   strcpy(buf, "???");
}
#endif /* loonix */

void getsocketaddr(const char *socketpath, struct sockaddr_un *sun) {
   struct statvfs st;
   char mountpoint[PATH_MAX];

   if (statvfs(socketpath, &st) < 0) {
      err(1, "%s: statvfs", socketpath);
   }

#if defined(__NetBSD__)
   strlcpy(mountpoint, st.f_mntonname, sizeof(mountpoint));
#elif defined(__linux__)
   /*
    * On Linux we get to go play hunt the spoon.
    */
   linux_match_mountpoint(mountpoint, sizeof(mountpoint), st.f_fsid);
#else
   strcpy(mountpoint, "???");
#endif

   adjust(mountpoint);
   sun->sun_family = AF_UNIX;
   snprintf(sun->sun_path, sizeof(sun->sun_path), "%ssage.%s", SOCKETDIR,
	    mountpoint);
#ifndef __linux__
   sun->sun_len = SUN_LEN(sun);
#endif
}

