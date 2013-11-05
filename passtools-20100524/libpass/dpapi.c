#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <pass/dpapi.h>
#include "libpass.h"

int dpapi_init(void) {
   if (LIBPASS_CHECKINIT()) {
      return -1;
   }
   return 0;
}

int dpapi_freeze(int fd) {
   if (LIBPASS_CHECKINIT()) {
      return -1;
   }
   return ioctl(__libpass_hook_fd, PASSIOCFREEZE, &fd);
}

int dpapi_mkphony(int reference_fd) {
   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   if (ioctl(__libpass_hook_fd, PASSIOCMKPHONY, &reference_fd)) {
      return -1;
   }
   return reference_fd;
}

int dpapi_revive_phony(int reference_fd, __pnode_t pnode, version_t version) {
   struct __pass_revive_phony_args a;   
   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   a.reference_fd = reference_fd;
   a.pnode = pnode;
   a.version = version;
   if (ioctl(__libpass_hook_fd, PASSIOCREVIVEPHONY, &a)) {
      return -1;
   }
   return a.ret_fd;
}

int dpapi_sync(int fd) {
   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   return ioctl(__libpass_hook_fd, PASSIOCSYNC, &fd);
}

ssize_t paread(int fd, void *data, size_t datalen,
	       __pnode_t *pnode_ret, version_t *version_ret) {
   struct __pass_paread_args a;

   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   a.fd = fd;
   a.data = data;
   a.datalen = datalen;
   if (ioctl(__libpass_hook_fd, PASSIOCREAD, &a) == -1) {
      return -1;
   }
   if (pnode_ret) {
       *pnode_ret = a.pnode_ret;
   }
   if (version_ret) {
       *version_ret = a.version_ret;
   }
   return a.datalen_ret;
}

ssize_t pawrite(int fd, const void *data, size_t datalen,
		const struct dpapi_addition *records, unsigned numrecords) {
   struct __pass_pawrite_args a;

   if (LIBPASS_CHECKINIT()) {
      return -1;
   }

   a.fd = fd;
   a.data = data;
   a.datalen = datalen;
   a.records = records;
   a.numrecords = numrecords;
   if (ioctl(__libpass_hook_fd, PASSIOCWRITE, &a) == -1) {
      return -1;
   }
   return a.datalen_ret;
}

