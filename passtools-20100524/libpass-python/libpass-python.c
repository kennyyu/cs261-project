/*
 * Copyright 2006-2008
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
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <pass/dpapi.h>
#include "libpass.h"

int pydpapi_init(void);
int mkphony(int fd);
int addxref(int fd, char *key, int xref_fd);
int addstr(int fd, char *key, char *value);

static int pyfd = -1;

int pydpapi_init(void)
{
   pyfd = open(".pyfd", O_RDWR | O_CREAT, 0644);
   if ( pyfd < 0 ) {
      return -1;
   }

   return dpapi_init();
}

int mkphony(int fd)
{
   if ( fd < 0 ) {
      fd = pyfd;
   }

   return dpapi_mkphony(fd);
}

int addxref(int fd, char *key, int xref_fd) {
//   struct __pass_pawrite_args a;
   struct dpapi_addition rec;
   uint32_t version;
   __pnode_t  pnode;
   int err;

//   printf( "addxref: about to check if lp is init\n" );

   if (LIBPASS_CHECKINIT()) {
//      printf( "addxref: checkinit failed\n" );
      return -1;
   }


//   printf( "addxref: about to call paread\n" );
   err = paread(xref_fd, NULL, 0, &pnode, &version);
   if ( err < 0 ) {
      version = 0;
   }

   (void)pnode; // XXX should do something with the pnode number
   rec.da_target = fd;
   rec.da_precord.dp_flags = PROV_IS_ANCESTRY;
   rec.da_precord.dp_attribute = key;
   rec.da_precord.dp_value.dv_type = PROV_TYPE_OBJECTVERSION;
   rec.da_precord.dp_value.dv_fd = xref_fd;
   rec.da_precord.dp_value.dv_version = version;
   rec.da_conversion = PROV_CONVERT_NONE;

//   printf( "addxref: calling pawrite w/key %s\n", key );
   return pawrite(fd, NULL, 0, &rec, 1);

/*
   a.fd = fd;
   a.data = NULL;
   a.datalen = 0;
   a.records = &rec;
   a.numrecords = 1;

   if (ioctl(__libpass_hook_fd, PASSIOCWRITE, &a) == -1) {
      return -1;
   }

   return a.datalen_ret;
*/
}

int addstr(int fd, char *key, char *value) {
   struct __pass_pawrite_args a;
   struct dpapi_addition rec;

   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   rec.da_target = fd;
   rec.da_precord.dp_flags = PROV_IS_ANCESTRY;
   rec.da_precord.dp_attribute = key;
   rec.da_precord.dp_value.dv_type = PROV_TYPE_STRING;
   rec.da_precord.dp_value.dv_string = value;
   rec.da_conversion = PROV_CONVERT_NONE;

   a.fd = fd;
   a.data = NULL;
   a.datalen = 0;
   a.records = &rec;
   a.numrecords = 1;

   if (ioctl(__libpass_hook_fd, PASSIOCWRITE, &a) == -1) {
      return -1;
   }

   return a.datalen_ret;
}
