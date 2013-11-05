#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include <pass/provabi.h>
#include "libpass.h"

int __libpass_hook_fd = -1;
int __libpass_initialized = 0;

int __libpass_initialize(void) {
   int ver;

   __libpass_hook_fd = open(_PATH_DEV_PROVENANCE, O_RDWR);
   if (__libpass_hook_fd < 0) {
      return -1;
   }
   if (ioctl(__libpass_hook_fd, PASSIOCGETABI, &ver) < 0) {
      return -1;
   }
   if (ver != PROV_ABI_VERSION) {
      warnx("Wrong provenance ABI version");
      errno = ENXIO;
      return -1;
   }
   __libpass_initialized = 1;
   return 0;
}
