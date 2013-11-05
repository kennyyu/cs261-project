#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <assert.h>

#include <pass/dpapi.h>
#include "libpasstest.h"

#ifndef DPAPI_SELF // XXX this should get added already
#define DPAPI_SELF (-1)
#endif

static int dpapi_say(const char *fmt, ...) {
   char buf[4096];
   va_list ap;
   ssize_t ret;

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   ret = write(__libpasstest_outputfd, buf, strlen(buf));
   if (ret < 0 || (size_t)ret != strlen(buf)) {
      return -1;
   }
   return 0;
}

static const char *dpapi_name(int fd, int bufnum) {
   static struct {
      char b[32];
   } bufs[2];

   if (fd == DPAPI_SELF) {
      return "myself";
   }

   snprintf(bufs[bufnum].b, sizeof(bufs[bufnum].b), "fd%d", fd);
   return bufs[bufnum].b;
}

int dpapi_init(void) {
   if (LIBPASSTEST_CHECKINIT()) {
      return -1;
   }
   if (dpapi_say("format provtrace v3\n")) {
      return -1;
   }
   if (dpapi_say("create %d\n", dpapi_name(DPAPI_SELF, 0))) {
      return -1;
   }
   return 0;
}

int dpapi_freeze(int fd) {
   if (LIBPASSTEST_CHECKINIT()) {
      return -1;
   }
   return dpapi_say("freeze %s\n", dpapi_name(fd, 0));
}

int dpapi_mkphony(int reference_fd) {
   if (LIBPASSTEST_CHECKINIT()) {
      return -1;
   }

   int fd = open(_PATH_DEVNULL, O_RDWR);
   if (fd < 0) {
      return -1;
   }
   if (dpapi_say("phony %s %s\n",
		 dpapi_name(fd, 0), dpapi_name(reference_fd, 1))) {
      close(fd);
      return -1;
   }

   return fd;
}

ssize_t paread(int fd, void *data, size_t datalen, 
	       __pnode_t *pnode_ret, version_t *version_ret) {
   if (LIBPASSTEST_CHECKINIT()) {
      return -1;
   }
   *pnode_ret = 1;
   *version_ret = 1;
   return read(fd, data, datalen);
}

static int escape_string(char *buf, size_t maxlen, const char *str)
{
   size_t i, len;

   // insert an open quote
   assert(maxlen > 1);
   buf[0] = '"';
   buf++;
   maxlen--;

   // leave space for a close quote
   assert(maxlen > 1);
   maxlen--;

   for (i=0; str[i]; i++) {
      /* can't use <ctype.h> because of locale issues */
      unsigned char c = str[i];
      char tmp[8];

      switch (c) {
       case '\a': strcpy(tmp, "\\a"); break;
       case '\b': strcpy(tmp, "\\b"); break;
       case '\t': strcpy(tmp, "\\t"); break;
       case '\n': strcpy(tmp, "\\n"); break;
       case '\v': strcpy(tmp, "\\v"); break;
       case '\f': strcpy(tmp, "\\f"); break;
       case '\r': strcpy(tmp, "\\r"); break;
       default:
	if (c < 32 || c > 126) {
	   snprintf(tmp, sizeof(tmp), "\\%03o", c);
	}
	else {
	   tmp[0] = c;
	   tmp[1] = 0;
	}
	break;
      }

      len = strlen(tmp);
      if (len >= maxlen) {
	 /* don't issue part of an escape sequence */
	 errno = ERANGE;
	 break;
      }

      strcpy(buf, tmp);
      buf += len;
      maxlen -= len;
   }

   // we left room for a close quote above
   maxlen++;

   // insert close quote
   assert(maxlen > 0);
   buf[0] = '"';
   buf++;
   maxlen--;

   // null terminate
   assert(maxlen > 0);
   buf[0] = 0;

   return 0;
}

static int addition(const struct dpapi_addition *da, int srcobj, int dstobj) {
   char valbuf[4096];
   const char *val;
   int isxref = 0;

   if (da->da_conversion == PROV_CONVERT_NONE) {
      switch (da->da_precord.dp_value.dv_type) {
       case PROV_TYPE_NIL:
	val = "nil";
	break;
       case PROV_TYPE_STRING:
	if (escape_string(valbuf, sizeof(valbuf),
			  da->da_precord.dp_value.dv_string)) {
	   return -1;
	}
	val = valbuf;
	break;
       case PROV_TYPE_MULTISTRING:
	/* later - XXX */
	warnx("libpasstest: PROV_TYPE_MULTISTRING not yet implemented");
	errno = ENOSYS;
	return -1;
       case PROV_TYPE_INT:
	snprintf(valbuf, sizeof(valbuf), "%d",
		 da->da_precord.dp_value.dv_int);
	val = valbuf;
	break;
       case PROV_TYPE_REAL:
	/* XXX this is not a good thing to do, loses precision */ 
	snprintf(valbuf, sizeof(valbuf), "%g",
		 da->da_precord.dp_value.dv_real);
	val = valbuf;
	break;
       case PROV_TYPE_TIMESTAMP:
	snprintf(valbuf, sizeof(valbuf), "%lld.%09ld", 
		 (long long) da->da_precord.dp_value.dv_timestamp.pt_sec,
		 (long) da->da_precord.dp_value.dv_timestamp.pt_nsec);
	val = valbuf;
	break;
       case PROV_TYPE_OBJECT:
	snprintf(valbuf, sizeof(valbuf), "%s",
		 dpapi_name(da->da_precord.dp_value.dv_fd, 0));
	val = valbuf;
	isxref = 1;
	break;
       case PROV_TYPE_OBJECTVERSION:
	snprintf(valbuf, sizeof(valbuf), "%s %d",
		 dpapi_name(da->da_precord.dp_value.dv_fd, 0),
		 da->da_precord.dp_value.dv_version);
	val = valbuf;
	isxref = 1;
	break;
       case PROV_TYPE_INODE:
       case PROV_TYPE_PNODE:
       case PROV_TYPE_PNODEVERSION:
       default:
	errno = EINVAL;
	return -1;
      }
   }
   else switch (da->da_conversion) {
    case PROV_CONVERT_NONE:
     assert(0);
     val = "???";
     break;
    case PROV_CONVERT_REFER_SRC:
     snprintf(valbuf, sizeof(valbuf), "%s", dpapi_name(srcobj, 0));
     val = valbuf;
     isxref = 1;
     break;
    case PROV_CONVERT_REFER_DST:
     snprintf(valbuf, sizeof(valbuf), "%s", dpapi_name(dstobj, 0));
     val = valbuf;
     isxref = 1;
     break;
    default:
     errno = EINVAL;
     return -1;
   }

   return dpapi_say("add %s %s %s %s\n",
		    dpapi_name(da->da_target, 1),
		    da->da_precord.dp_attribute,
		    ((da->da_precord.dp_flags & PROV_IS_ANCESTRY) ?
		     (isxref ? "->" : "::") :
		     (isxref ? ">>" : ":")),
		    val);
}

ssize_t pawrite(int fd, const void *data, size_t datalen,
		const struct dpapi_addition *records, unsigned numrecords) {
   unsigned i;

   if (LIBPASSTEST_CHECKINIT()) {
      return -1;
   }

   for (i=0; i<numrecords; i++) {
      if (addition(&records[i], DPAPI_SELF, fd)) {
	 return -1;
      }
   }

   if (datalen == 0) {
      return 0;
   }

   return write(fd, data, datalen);
}
