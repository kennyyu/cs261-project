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
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>

// Eventually we should be using this:
// But not everybody has this
//#include <sys/inotify.h>

// We don't use these either due to license issues
//#include "inotify.h"
//#include "inotify-syscalls.h"

#include "udev_sysdeps.h"

#include "log.h"

extern sig_atomic_t g_shutdown;

#define LOG_KERNEL      "k"
#define LOG_ACTIVE      "a"
#define LOG_BACKUP      "b"
#define LOG_PREFIX_LEN  1

#define LOG_DIGITS      11
#define LOG_SUFFIX      ".twig"

#define LOG_STRLEN	(LOG_PREFIX_LEN + LOG_DIGITS + strlen(LOG_SUFFIX))

/**
 * Valid characters in log digits.
 *
 * @note Changing this changes the base used for number conversion passed to
 * strtoull.
 *
 * @see log_is_valid_name
 */
#define LOG_DIGIT_CHARS "0123456789"
#define LOG_DIGIT_BASE  10

/* size of the event structure, not counting name */
#define EVENT_SIZE            (sizeof (struct inotify_event))

/* reasonable guess as to size of 1024 events */
#define BUF_LEN               (1024 * (EVENT_SIZE + 16))

struct log_entry {
    uint64_t        lognum;
    enum log_state  logstate;
};

static int log_inotify_dir(void);
static int log_is_valid_name(const char *name, enum log_state *log_state);
static const char *log_state_to_prefix(enum log_state log_state);

// Internal work queue implementation
struct log_q;
#define LOG_Q_CAPACITY_INITIAL   10
#define LOG_Q_CAPACITY_INCREMENT 10

static int uint64_cmp(const void *pleft, const void *pright);
static int log_q_create(struct log_q **q);
static int log_q_destroy(struct log_q **q);
static int log_q_reserve(struct log_q *q, size_t capacity);
static int log_q_push(struct log_q *q, const struct log_entry *entry);
static int log_q_pop(struct log_q *q, struct log_entry *entry);
static int log_q_tail(struct log_q *q, struct log_entry *entry);
static int log_q_is_empty(struct log_q *q);
static int log_q_size(struct log_q *q);
static int log_q_sort(struct log_q *q);

static struct log_q  *log_nums = NULL;

static int            log_inotify_fd = -1;
static int            log_inotify_wd = -1;

/**
 * Initialize the log searching code.
 *
 * @param[in]     logpath    directory to search
 *
 * @note: Sets the inotify watch. To avoid a race, this has to be
 * called before any new twig files are generated.
 *
 * @returns       0 on success, -1 on failure
 */
int log_startup(const char *logpath)
{
   int                       err;

   if ( logpath == NULL ) {
      fprintf( stderr, "log_startup: logpath is null\n" );
      return -1;
   }

   err = log_q_create(&log_nums);
   if ( err != 0 ) {
      fprintf( stderr, "log_startup: log_q_created failed %d\n", err );
      // TODO: error
      return -1;
   }

   if ( (log_inotify_fd >= 0) && (log_inotify_wd >= 0) ) {
      fprintf( stderr, "log_startup: bad state\n" );
      // TODO: error
      return -1;
   }

   log_inotify_fd = inotify_init();
   if ( log_inotify_fd < 0 ) {
      fprintf( stderr, "log_startup: inotify_init failed\n" );
      // TODO: error
      return -1;
   }

   fprintf( stderr, "log_startup: adding inotify watch on %s\n", logpath );
   log_inotify_wd = inotify_add_watch(log_inotify_fd, logpath, IN_CREATE);
   if ( log_inotify_wd < 0 ) {
      fprintf( stderr, "log_startup: inotify_add_watch failed %d %s\n",
               log_inotify_wd, strerror(errno) );
      // TODO: error
      close(log_inotify_fd);
      return -1;
   }

   return 0;
}

/**
 * Shutdown the log subsystem.
 *
 * @returns       whatever close returns
 */
int log_shutdown(void)
{
   int                       ret;

   log_q_destroy(&log_nums);

   fprintf( stderr, "log_shutdown: terminating inotify\n" );
   ret = close(log_inotify_fd), log_inotify_fd = -1;

   return ret;
}

/**
 * Return a sorted list of file numbers in the given directory.
 *
 * @param[in]     logpath    directory to search
 *
 * @returns       number of files found on success, -1 on failure
 */
int
log_find_files(const char *logpath)
{
   DIR                      *dfd;
   struct dirent            *dp;
   struct stat               sb;
   char                      filename[PATH_MAX];
   uint64_t                  number;
   enum log_state            logstate;
   struct log_entry          logentry;
   int                       serrno;
   int                       err;

   // Open the directory
   if ( (dfd = opendir(logpath)) == NULL ) {
      serrno = errno;
      fprintf( stderr, "filelist: %s: opendir: %s\n",
               logpath, strerror(serrno) );

      errno = serrno;
      return -1;
   }

   // Traverse the entries in the directory
   while ( (dp = readdir(dfd)) != NULL ) {
      snprintf(filename, sizeof(filename), "%s/%s", logpath, dp->d_name);

      err = stat(filename, &sb);
      if ( err != 0 ) {
         serrno = errno;
         fprintf( stderr, "WARNING filelist: stat %s failed %s\n",
                  filename, strerror(serrno) );

         // On failure skip this file and go onto the next file
         continue;
      }

      //
      // Is it a valid log file?
      //

      // Regular file?
      if (!S_ISREG(sb.st_mode)) {
         continue;
      }

      if ( log_is_valid_name(dp->d_name, &logstate) ) {
          switch ( logstate ) {
              // good cases
              case LOG_STATE_KERNEL:  // intentional FALL THROUGH
              case LOG_STATE_ACTIVE:
                  break;

              // ignore case
              case LOG_STATE_BACKUP:
                  continue;

              // should be impossible case
              default:
                  assert( "impossible log state" == 0 );
          }

         err = log_get_number(dp->d_name, &number);
         if ( err != 0 ) {
            fprintf( stderr, "log_find_files: "
                     "log_get_number failed returning %d\n",
                     err );
            // TODO: convert to syslog
            return -1;
         }

         fprintf( stderr, "log_find_files: adding log number %" PRIu64 "\n",
		  number );

         logentry.lognum = number;
         logentry.logstate = logstate;

         err = log_q_push(log_nums, &logentry);
         if ( err != 0 ) {
            fprintf( stderr, "log_find_files: "
                     "log_q_push failed with %d\n",
                     err );
            // TODO: convert to syslog
            return -1;
         }
      }
   }
   closedir(dfd);

   err = log_q_sort(log_nums);
   if ( err != 0 ) {
      // TODO: error
      return -1;
   }

   return log_q_size(log_nums);
}

/**
 * Get the next log file in the given location.
 *
 * @param[in]     logpath    directory to search
 * @param[in]     more       whether to search for more
 * @param[out]    lognum     log number
 * @param[out]    logstate   log state
 *
 * @returns       0 on success, -1 on failure
 *
 * @note if errno = ENOENT no file was found
 * @see log_state
 */
int log_next_filename(const char *logpath, int more,
                      uint64_t *lognum, enum log_state *logstate)
{
//   uint64_t                  num;
   struct log_entry          logentry;
   int                       serrno;
   int                       err;

   /*
    * Sanity checks
    */
   if ( logpath == NULL ) {
      fprintf( stderr, "log_next_filename: logpath is null\n" );
      // TODO: convert to syslog
      errno = EINVAL;
      return -1;
   }

   if ( lognum == NULL ) {
       fprintf( stderr, "log_next_filename(%s,%d,NULL,?): lognum is NULL\n",
                logpath, more );

       errno = EINVAL;
       return -1;
   }

   if ( logstate == NULL ) {
       fprintf( stderr, "log_next_filename(%s,%d,%p,NULL): logstate is NULL\n",
                logpath, more, lognum );

       errno = EINVAL;
       return -1;
   }

   if ( more ) {
      // Are there more files in our current list?
      while ( log_q_is_empty(log_nums) ) {
         err = log_inotify_dir();

         if ( err != 0 ) {
             serrno = errno;
             fprintf( stderr,
                      "log_next_filename: log_inotify dir failed %d %s\n",
                      err, strerror(serrno) );
             errno = serrno;
             return err;
         }
      }
   }

   if ( log_q_is_empty(log_nums) ) {
      fprintf( stderr, "log_next_filename: no entries in queue\n" );
      return 1;
   }

   // Allocate the filename
   err = log_q_pop(log_nums, &logentry);
   if ( err != 0 ) {
      // TODO: error
       serrno = errno;
       fprintf( stderr, "log_next_filename: log_q_pop failed %d %s\n",
                err, strerror(serrno) );
       errno = serrno;
       return -1;
   }

   *lognum = logentry.lognum;
   *logstate = logentry.logstate;

   return 0;
}

/**
 * Get the last log number.
 *
 * @param[out]    num        log number
 *
 * @returns       0 on success
 */
int log_last_lognum(uint64_t *num)
{
    struct log_entry        logentry;
    int                     ret;

    /*
     * Sanity checks
     */
    if ( num == NULL ) {
        fprintf( stderr, "log_last_lognum(NULL): "
                 "passed NULL pointer\n" );
        errno = EINVAL;

        return -1;
    }


    ret = log_q_tail(log_nums, &logentry);

    *num = logentry.lognum;

    return ret;
}

/**
 * Construct a log filename from the logpath and the log number.
 *
 * @param[in]     logpath    path to log files
 * @param[in]     num        log file number
 *
 * @returns       filename on success, NULL on failure
 */
char *
log_make_filename(const char *logpath,
                  uint64_t lognum, enum log_state log_state)
{
   char                     *filename;
   const char               *prefix;
   size_t                    len;
   int                       serrno;
   int                       wrote;

   /*
    * Sanity checks
    */
   if ( logpath == NULL ) {
      return NULL;
   }

   switch ( log_state ) {
       // valid
       case LOG_STATE_KERNEL:
       case LOG_STATE_ACTIVE:
       case LOG_STATE_BACKUP:
           break;

       // invalid
       default:
           assert( "log_make_filename: logstate invalid" == 0 );
   }

   /*
    * Start of real work
    */

   len = strlen(logpath) + strlen("/") + LOG_STRLEN + 1;
   filename = (char *)malloc(len);
   serrno = errno;
   if ( filename == NULL ) {
      // TODO: log
      errno = serrno;
      return NULL;
   }

   prefix = log_state_to_prefix(log_state);

   wrote = snprintf(filename, len, "%s/%s%0*" PRIu64 "%s",
                    logpath, prefix, LOG_DIGITS, lognum,
		    LOG_SUFFIX);

   if ( (size_t)wrote != (len - 1) ) {
      // TODO: log

      free(filename), filename = NULL;
   }

   return filename;
}

/**
 * Create a log filename with the kernel prefix in the given directory
 * and with the given log number.
 *
 * @param[in]     dir        directory portion of path
 * @param[in]     lognum     log number
 *
 * @returns       filename on success, NULL and sets errno on failure
 */
char *log_kernel_filename(const char *dir, const uint64_t lognum)
{
    return log_make_filename(dir, lognum, LOG_STATE_KERNEL);
}

/**
 * Create a log filename with the active prefix in the given directory
 * and with the given log number.
 *
 * @param[in]     dir        directory portion of path
 * @param[in]     lognum     log number
 *
 * @returns       filename on success, NULL and sets errno on failure
 */
char *log_active_filename(const char *dir, const uint64_t lognum)
{
    return log_make_filename(dir, lognum, LOG_STATE_ACTIVE);
}

/**
 * Create a log filename with the backup prefix in the given directory
 * and with the given log number.
 *
 * @param[in]     dir        directory portion of path
 * @param[in]     lognum     log number
 *
 * @returns       filename on success, NULL and sets errno on failure
 */

char *log_backup_filename(const char *dir, const uint64_t lognum)
{
    return log_make_filename(dir, lognum, LOG_STATE_BACKUP);
}

/**
 * Sleeps until it finds new log files to process.
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
static int log_inotify_dir(void)
{
   char                      inotify_buf[BUF_LEN];
   int                       len;
   int                       i;
   uint64_t                  number;
   enum log_state            logstate;
   struct log_entry          logentry;
   struct inotify_event     *event;
   int                       serrno;
   int                       err;


   do {

      // keep checking inotify until we get something
      do {
         len = read(log_inotify_fd, inotify_buf, BUF_LEN);
         serrno = errno;

         // shutdown is set -- time to go :)
         if ( g_shutdown ) {
            errno = EINTR;
            return -1;
         }
      } while ( (len < 0) && (errno == EINTR) );

      if ( len < 0 ) {
         fprintf( stderr, "log_inotify_dir: "
                  "number read is %d errno %s\n",
                  len, strerror(serrno) );
         errno = serrno;
         return -1;
      }

      if ( len == 0 ) {
         fprintf( stderr, "log_inotify_dir: number read is 0\n" );
         // TODO: error?
         return -1;
      }

      for ( i = 0; i < len; i += sizeof(*event) + event->len ) {
         event = (struct inotify_event *) &inotify_buf[i];

         // We only asked for one wd & CREATEs
         assert( event->wd == log_inotify_wd );
         assert( event->mask & IN_CREATE );

         // CREATE is supposed to pass the filename
         assert( event->len != 0 );

         // ignore directories
         if ( event->mask & IN_ISDIR ) {
            continue;
         }

         fprintf( stderr, "log_inotify_dir: "
                  "CONSIDERING %s\n",
                  event->name );

         if ( log_is_valid_name(event->name, &logstate) &&
              (logstate == LOG_STATE_KERNEL) )
         {
             err = log_get_number(event->name, &number);
             if ( err != 0 ) {
                 fprintf( stderr, "log_inotify_dir: "
                          "log_get_number failed %d %s\n",
                          err, strerror(errno) );
                 // TODO: error
                 return -1;
             }

             fprintf( stderr, "log_inotify_dir: "
                      "adding log number %" PRIu64 "\n",
                      number );
             logentry.lognum = number;
             logentry.logstate = logstate;
             log_q_push(log_nums, &logentry);
         } else {
            fprintf( stderr, "log_inotify_dir: "
                     "%s is not a valid log file\n",
                     event->name );
         }
      }

   } while ( log_q_is_empty(log_nums) );

   err = log_q_sort(log_nums);
   if ( err != 0 ) {
      // TODO: error
      return -1;
   }

   return 0;
}

/**
 * Is name a valid log file name?
 *
 * @param[in]     name       filename
 * @param[in]     logstate   log state
 *
 * @returns       0 on success, -1 on failure
 */
static int log_is_valid_name(const char *name, enum log_state *logstate)
{
    const char               *suffix;
    int                       ret;

    /*
     * Sanity checks
     */

    if ( name == NULL ) {
        // TODO: log
        return 0;
    }

    if ( logstate == NULL ) {
        return 0;
    }

    // strlen matches?
    if ( strlen(name) != LOG_STRLEN ) {
#ifdef DEBUG
        fprintf( stderr, "log_is_valid_name: "
                 "strlen(%s): %zu != expected: %zu\n",
                 name, strlen(name), LOG_STRLEN );
#endif /* DEBUG */
        return 0;
    }

    // Correct prefix?
    ret = log_filename_to_state(name, logstate);
    if ( ret != 0 ) {
        return 0;
    }

    switch ( *logstate ) {
        case LOG_STATE_KERNEL:
        case LOG_STATE_ACTIVE:
        case LOG_STATE_BACKUP:
            break;

        default:
            assert( "log_is_valid_name: invalid state" == 0 );
    }

   // Correct suffix?
   suffix = name + LOG_PREFIX_LEN + LOG_DIGITS;
   if ( 0 != strncmp(suffix , LOG_SUFFIX, strlen(LOG_SUFFIX)) ) {
      return 0;
   }

   // Are the characters in the middle digits?
   if ( LOG_DIGITS != strspn(name + LOG_PREFIX_LEN, LOG_DIGIT_CHARS) ) {
      return 0;
   }

   return 1;
}

/**
 * Get a log number from the log file name.
 *
 * @param[in]     name       filename
 * @param[out]    number     number read from filename
 *
 * @returns       0 on success, -1 on error
 */
int log_get_number(const char *name, uint64_t *number)
{
   if ( (name == NULL) || (number == NULL) ) {
      errno = EINVAL;
      return -1;
   }

   // @note per strtoull manual:
   //   set errno to 0 so we can tell if strtoull fails
   errno = 0;

   // Grab the number
   // uses strtoull -- atoi might overflow our max = 10^LOG_DIGITS
   *number = strtoull(name + LOG_PREFIX_LEN, NULL, LOG_DIGIT_BASE);

   if ( errno != 0 ) {
      return -1;
   }

   return 0;
}

/**
 * Convert log_state to log filename prefix.
 *
 * @param[in]     logstate   log state whose prefix we want
 *
 * @returns       prefix corresponding to given prefix
 */
static const char *log_state_to_prefix(enum log_state logstate)
{
   switch (logstate) {
       case LOG_STATE_KERNEL:
           return LOG_KERNEL;

       case LOG_STATE_ACTIVE:
           return LOG_ACTIVE;

       case LOG_STATE_BACKUP:
           return LOG_BACKUP;

       default:
           assert( "invalid log state" == 0 );
           return NULL;
   }

   assert( 0 );
   return NULL;
}

/**
 * Retrieve log state from filename.
 *
 * @param[in]     filename   filename to inspect
 * @param[out]    logstate   log state encoded in filename
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
int log_filename_to_state(const char *filename, enum log_state *logstate)
{
    char                    prefix[LOG_PREFIX_LEN+1];
    int                     ret = 1;

    /*
     * Sanity checks
     */
    if ( filename == NULL ) {
        fprintf( stderr, "log_filename_to_state: "
                 "called with NULL filename\n" );

        errno = EINVAL;
        return -1;
    }

    strncpy(prefix, filename, LOG_PREFIX_LEN);
    prefix[LOG_PREFIX_LEN] = '\0';

    if ( strcmp(prefix, LOG_KERNEL) == 0 ) {
        if ( logstate != NULL ) {
            *logstate = LOG_STATE_KERNEL;
        }
        ret = 0;
    } else if ( strcmp(prefix, LOG_ACTIVE) == 0 ) {
        if ( logstate != NULL ) {
            *logstate = LOG_STATE_ACTIVE;
        }
        ret = 0;
    } else if ( strcmp(prefix, LOG_BACKUP) == 0 ) {
        if ( logstate != NULL ) {
            *logstate = LOG_STATE_BACKUP;
        }
        ret = 0;
    } else {
#ifdef DEBUG
        fprintf( stderr, "log_filename_to_state(%s,%p): "
                 "unknown prefix\n",
                 filename, logstate );
#endif /* DEBUG */
        ret = 1;
    }

    return ret;
}

// **********************
// *                    *
// *   Log Work Queue   *
// *                    *
// **********************

struct log_q {
   struct log_entry        *array;
   size_t                   size;
   size_t                   capacity;
   size_t                   head;
   size_t                   tail;
};

/**
 * Comparison function for uint64s (for use by qsort).
 *
 * @param[in]     pleft      pointer to left value
 * @param[in]     pright     pointer to right value
 *
 * @returns       < 0 if pleft <  pright
 *                0   if pleft == pright
 *                > 0 if pleft >  pright
 */
static int uint64_cmp(const void *pleft, const void *pright)
{
   uint64_t                  left;
   uint64_t                  right;

   assert( pleft != NULL );
   assert( pright != NULL );

   left = *(uint64_t *)pleft;
   right = *(uint64_t *)pright;

   if ( left < right ) {
      return -1;
   }

   if ( left > right ) {
      return 1;
   }

   return 0;
}

/**
 * Create a log q.
 *
 * @param[out]    q          newly created log q
 *
 * @returns       0 on success, -1 on failure
 */
static int log_q_create(struct log_q **q)
{
   int                       err;

   assert( q != NULL );

   *q = malloc(sizeof(**q));
   if ( *q == NULL ) {
      return -1;
   }

   (*q)->array = NULL;
   (*q)->capacity = 0;
   (*q)->size  = 0;
   (*q)->head  = 0;
   (*q)->tail  = 0;

   err = log_q_reserve(*q, LOG_Q_CAPACITY_INITIAL);
   if ( err != 0 ) {
      return err;
   }

   return 0;
}

/**
 * Destroy a log q.
 *
 * @param[in,out] q          log q to destroy
 *
 * @return        0 on success, -1 on failure
 */
static int log_q_destroy(struct log_q **q)
{
   assert( q != NULL );

   if ( *q == NULL ) {
      return 0;
   }

   free( (*q)->array ), (*q)->array = NULL;
   free( *q ), *q = NULL;

   return 0;
}

/**
 * Reserve space.
 *
 * @param[in]     q          queue to operate on
 * @param[in]     capacity   minimum number of values to reserve space for
 *
 * @return        0 on success, -1 on failure
 */
static int log_q_reserve(struct log_q *q, size_t capacity)
{
   void                     *tmp_array;

   assert( q != NULL );

   if ( q->capacity >= capacity ) {
      return 0;
   }

   tmp_array = realloc(q->array, capacity * sizeof(q->array[0]));
   if ( tmp_array == NULL ) {
      return -1;
   }

   q->capacity = capacity;
   q->array = tmp_array;

   return 0;
}

/**
 * Push a value onto the end of the queue.
 *
 * @param[in]     q          queue to append to
 * @param[in]     val        value to append
 *
 * @return        0 on success, -1 on failure
 */
static int log_q_push(struct log_q *q, const struct log_entry *logentry)
{
   int                       err;

   assert( q != NULL );
   assert( logentry != NULL );

   if ( q->tail >= q->capacity ) {
      err = log_q_reserve(q, q->capacity + LOG_Q_CAPACITY_INCREMENT);
      if ( err != 0 ) {
         return err;
      }
   }

   q->array[q->tail++] = *logentry;
   q->size++;

   return 0;
}

/**
 * Pop a value from the front of the queue.
 *
 * @param[in]     q          queue to pop from
 * @param[out]    val        value popped
 *
 * @returns       0 on success, -1 on failure
 */
static int log_q_pop(struct log_q *q, struct log_entry *logentry)
{
   assert( q != NULL );
   assert( logentry != NULL );

   if ( q->tail <= q->head ) {
      return -1;
   }

   *logentry = q->array[q->head++];
   q->size--;

   if ( q->size == 0 ) {
      q->head = 0;
      q->tail = 0;
   }

   return 0;
}

static int log_q_tail(struct log_q *q, struct log_entry *logentry)
{
   assert( q != NULL );
   assert( logentry != NULL );

   if ( q->tail <= q->head ) {
      return -1;
   }

   *logentry = q->array[q->tail];

   return 0;
}

/**
 * Is the queue empty?
 *
 * @param[in]     q          queue to inspect
 *
 * @retval        1          empty
 * @retval        0          not empty
 */
static int log_q_is_empty(struct log_q *q)
{
   return (q->size == 0);
}

/**
 * How many elements are there in the queue?
 *
 * @param[in]     q          queue to inspect
 *
 * @returns       number of entries in the queue
 */
static int log_q_size(struct log_q *q)
{
    return q->size;
}

/**
 * Compare queue entries (for use by qsort).
 *
 * @param[in]     o1         entry one
 * @param[in]     o2         entry two
 *
 * @returns       what qsort expects
 */
static int log_entry_cmp(const void *o1, const void *o2)
{
    const struct log_entry *entry1;
    const struct log_entry *entry2;

    /*
     * Sanity checks
     */
    assert( o1 != NULL );
    assert( o2 != NULL );

    entry1 = o1;
    entry2 = o2;

    return uint64_cmp(&(entry1->lognum), &(entry2->lognum));
}

/**
 * Sort the values in the queue.
 *
 * @param[in]     q          queue to sort
 *
 * @returns       0 on success, -1 on failure
 */
static int log_q_sort(struct log_q *q)
{
   assert( q != NULL );
   assert( q->head + q->size == q->tail );

   qsort( &(q->array[q->head]), q->size, sizeof(q->array[0]), log_entry_cmp);

   return 0;
}

