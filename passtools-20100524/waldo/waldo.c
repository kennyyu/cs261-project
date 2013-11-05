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
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <syslog.h>

#include <db.h>

#include "twig.h"
#include "debug.h"
#include "process.h"
#include "recover.h"
#include "log.h"
#include "wdb.h"

#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

#define DEFAULT_DB_PATH       "./db"
#define IDENT                 "PASS:Waldo"

sig_atomic_t g_shutdown = 0;
sig_atomic_t g_offline  = 0;
sig_atomic_t g_recovery = 0;
sig_atomic_t g_backup   = 0;

// Internal functions
static void usage(char *name);
static void catch_and_shutdown(int sig_num);

////////////////////////////////////////////////////////////////////////////////

/**
 * Display usage for this program (waldo).
 *
 * @param[in]     name       name of the program (waldo)
 */
static void usage(char *name)
{
   fprintf(stderr,
           "usage: %s "
           "[-h] [-u] [-b] [-r] [-o] [-p dbpath] logpath\n"
           "   -h             usage\n"
           "   -u             usage\n"
           "   -o             offline mode\n"
           "   -r             recovery mode\n"
           "   -b             backup the log files after processing\n"
           "   -p dbpath      path to database\n"
           "   logpath        where to find log file(s)\n"
           "\n"
           " If logpath is a file process just that file.\n"
           " Note that the file is *not* removed and -b has no effect.\n"
           " Otherwise, process all log files in that directory.\n"
           "\n"
           " In offline mode, exit when done processing twig files.\n"
           " Do _not_ wait for more twig files.\n"
           "\n"
           " Recovery mode is just like offline mode except that it will\n"
           " also run recovery.\n"
           "\n"
           "DEFAULT VALUES\n"
           "   dbpath         \"" DEFAULT_DB_PATH "\"\n"
           "\n"
           "NOTES\n"
           "   Sending waldo SIGUSR1 or SIGTERM causes it to shutdown at\n"
           "   the next EOF\n",
           name );
}

/**
 * Signal handler -- perform orderly shutdown.
 *
 * Stop at the next end-of-file.
 */
static void catch_and_shutdown(int sig_num UNUSED)
{
   g_shutdown = 1;
}

/**
 * The main method for the DBMS application.
 *
 * @see usage
 */
int
main(int argc, char *argv[])
{
   struct stat           sb;
   const char           *dbpath;
   const char           *logpath;
   int                   ch;

   // Set defaults
   dbpath = DEFAULT_DB_PATH;

   //
   // Parse options
   //
   while ( -1 != (ch = getopt(argc, argv, "orhubp:")) ) {
      switch ( ch ) {
         case 'o':
            g_offline = 1;
            break;

         case 'r':
            g_offline = 1;
            g_recovery = 1;
            break;

         case 'b':
            g_backup = 1;
            break;

         case 'p':
            dbpath = optarg;
            break;

         case 'h':
         case 'u':
         default:
            usage(argv[0]);
            return 0;
      }
   }

   if ( optind == argc - 1 ) {
      logpath = argv[optind];
   } else if ( optind != argc ) {
      // extra params -> print usage
      usage(argv[0]);
      return 0;
   } else {
      logpath = ".";  // ?
   }

   //
   // Initialize all 'databases' (database & log)
   //

   // BDB
   wdb_startup(dbpath, WDB_O_RDWR | WDB_O_CREAT);

   openlog(IDENT, 0, LOG_DAEMON);

   // TODO: First process any partially processed files
   // Then start processing files that exist

   signal(SIGUSR1, catch_and_shutdown);
   signal(SIGTERM, catch_and_shutdown);

   // Twig
   if ( stat(logpath, &sb) != 0 ) {
      fprintf( stderr, "No logs found at %s\n",
               logpath );
   }

   //
   // Process the entries
   //
   switch ( sb.st_mode & S_IFMT ) {
      case S_IFREG:
         g_shutdown = 1;

         waldo_process_file_norename(logpath, NULL);
         break;

      case S_IFDIR:
         waldo_recover(".", logpath, NULL);

         waldo_process_dir(logpath, NULL);
         break;

      default:
         fprintf( stderr, "%s is neither a file nor a directory\n",
                  logpath );
         break;
   }

   if ( g_recovery == 1 ) {
       waldo_recover(".", logpath, NULL);
   }

//   printf( "Waldo shutting down....\n" );

   //
   // Shut everything down (database & log)
   //
   log_shutdown();
   wdb_shutdown();

//   printf( "Waldo done.\n" );

   return 0;
}
