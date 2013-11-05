#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#include <pass/provabi.h>
#include "libpasstest.h"

int __libpasstest_outputfd = -1;
int __libpasstest_initialized = 0;

int __libpasstest_initialize(void) {
   const char *file;

   file = getenv("PASSTEST");
   if (file == NULL) {
      file = "passtest.pt";
   }

   __libpasstest_outputfd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0664);
   if (__libpasstest_outputfd < 0) {
      warnx("libpasstest: %s: open", file);
      return -1;
   }

   __libpasstest_initialized = 1;
   return 0;
}
