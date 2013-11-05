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

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "debug.h"

#include "wdb.h"
#include "dbs.h"

//
// File and name constants
//
#define WDB_FILE_MODE        S_IRWXU
#define WDB_FILENAME_EXT     ".db"

int        g_aborting = FALSE;
DB_ENV    *g_dbenv    = NULL;

/* Store the path and open flags so we can use them in recovery if necessary */
char      *g_path     = NULL;
uint32_t   g_openflags;

struct waldo_db *g_provdb;
struct waldo_db *g_tnum2tokdb;
struct waldo_db *g_tok2tnumdb;
struct waldo_db *g_env2pdb;
struct waldo_db *g_arg2pdb;
struct waldo_db *g_i2pdb;
struct waldo_db *g_p2idb;
struct waldo_db *g_namedb;
struct waldo_db *g_childdb;
struct waldo_db *g_parentdb;

static int   wdb_startup_internal(const int fatal);

static int   wdb_open_all_dbs(uint32_t openflags);
static int   wdb_close_all_dbs(void);

static void  wdb_abort(void);
static int   wdb_recover(void);
static int   wdb_recover_fatal(void);

static char *wdb_filename(struct waldo_db *wdb);

static void  wdb_log(const char *fmt, ...);

/**
 * Startup wdb subsystem -- initialize a BDB environment.
 *
 * @param[in]     path       where to put the db directory
 * @param[in]     openflags  additional flags to open
 *
 * Use openflags to specify whether to create the underlying databases,
 * open the databases read-only etc.
 */
int wdb_startup(const char *path, const uint32_t openflags)
{
   char       *filename;
   char       *provdb_path;
   struct stat sb;
   int         serrno;
   int         ret;

   g_path = malloc( strlen(path) + 1 );
   strcpy(g_path, path);
   g_openflags = openflags;

   /* Check if the path specified is valid.
    * It is if:
    *   we are about to create the databases
    *   the databases exist at the given location
    */
   if ( (openflags & WDB_O_CREAT) == 0 ) {
      if (stat(g_path, &sb) != 0) {
         serrno = errno;
         wdb_log( "FATAL wdb_startup: stat db directory %s failed with %s\n",
                  g_path, strerror(serrno) );

         errno = serrno;
         return -1;
      }

      filename = wdb_filename( &(g_databases[0]) );

      if ( filename == NULL ) {
         serrno = errno;
         wdb_log( "FATAL wdb_startup: wdb_filename for provdb failed %s\n",
                  strerror(serrno) );

         errno = serrno;
         return -1;
      }

      provdb_path = malloc( strlen(g_path) + 1 + strlen(filename) + 1 );
      if ( provdb_path == NULL ) {
         serrno = errno;
         wdb_log( "FATAL wdb_startup: malloc provdb_path failed %s\n",
                  strerror(serrno) );

         free(filename), filename = NULL;
         errno = serrno;
         return -1;
      }

      strcpy(provdb_path, g_path);
      strcat(provdb_path, "/");
      strcat(provdb_path, filename);

      free(filename), filename = NULL;
      if ( stat(provdb_path, &sb) != 0 ) {
         serrno = errno;
         wdb_log( "FATAL wdb_startup: stat provdb %s failed with %s\n",
                  filename, strerror(serrno) );

         free(provdb_path), provdb_path = NULL;
         errno = serrno;
         return -1;
      }

      free(provdb_path), provdb_path = NULL;
   }

   ret = wdb_startup_internal(0 /* no fatal recovery */);
   if ( ret != 0 ) {
      return wdb_recover();
   }

   return 0;
}

/**
 * Startup and open the databases.
 *
 * @param[in]     fatal      perform fatal recovery?
 *
 * @returns       0 on success, an error code on failure
 */
static int wdb_startup_internal(const int fatal)
{
   struct stat sb;
   uint32_t    dbenv_open_flags;
   int         serrno;
   int         ret;

   g_aborting = FALSE;

   /* Initialize db environment */
   if (stat(g_path, &sb) != 0) {
      ret = mkdir(g_path, S_IRWXU);
      if ( ret != 0 ) {
         serrno = errno;
         wdb_log("Create DB directory %s failed %s\n",
                 g_path, strerror(serrno));

         errno = serrno;
         exit(ret);
      }
   }

   ret = db_env_create(&g_dbenv, 0);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "db_env_create failed %s\n", db_strerror(ret) );

      errno = serrno;

      // I see nothing we could do here so abort!
      wdb_abort();
   }

   ret = g_dbenv->set_cachesize(g_dbenv,
				0 /*GB*/, 16*1024*1024 /*bytes*/,
				1 /*contiguosity*/);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "set_cachesize failed %s\n", db_strerror(ret) );

      errno = serrno;

      // I see nothing we could do here so abort!
      wdb_abort();
   }

   /* We do not use PRIVATE yet because
    *   waldo & sage access the database separately
    *   eventually sage should probably access the database via waldo
    *
    * We do not use locking because we are single threaded
    */
   dbenv_open_flags =
      DB_CREATE             /* Create the databases if they do not exist */
      /* | DB_PRIVATE */    /* Single process mode */
      | DB_INIT_MPOOL       /* Initialize the shared memory buffer pool */
      | DB_INIT_LOG         /* Initialize logging */
      | DB_INIT_TXN;        /* Initialize transactional environment */

   /* Normal recovery or fatal recovery? */
   if ( ! fatal ) {
      dbenv_open_flags |= DB_RECOVER;
   } else {
      dbenv_open_flags |= DB_RECOVER_FATAL;
   }

   ret = g_dbenv->open(g_dbenv, g_path, dbenv_open_flags, WDB_FILE_MODE);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "dbenv open failed %s\n", db_strerror(ret) );

      errno = serrno;
      return -1;
   }

   g_dbenv->set_errpfx(g_dbenv, g_path);
   g_dbenv->set_errfile(g_dbenv, stderr);

   return wdb_open_all_dbs(g_openflags);
}

/**
 * Shutdown the wdb subsystem -- close the BDB environment.
 *
 */
int wdb_shutdown(void)
{
   int serrno;
   int ret;

   // This should go to whatever our log mechanism becomes.
   // wdb_log( "Shutting down....\n" );

   wdb_close_all_dbs();

   /* Close the environment */
   ret = g_dbenv->close(g_dbenv, 0);

   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "dbenv close failed %s", db_strerror(ret) );

      errno = serrno;
      // Nothing to do besides reporting the error to our caller
   }

   // This should go to whatever our log mechanism becomes.
   // wdb_log( "Shut down complete\n" );

   return ret;
}

/**
 * Aborts the DBMS in the face of an unexpected or fatal exception.
 */
static void wdb_abort(void)
{
    g_aborting = TRUE;
    wdb_shutdown();
    abort();
}

/**
 * Attempt to recover. Will invoke fatal recovery if normal recovery fails.
 */
static int wdb_recover(void)
{
   int ret;

   wdb_log( "Running RECOVERY\n" );

   wdb_shutdown();
   ret = wdb_startup_internal(0 /* Normal recovery */ );

   if ( ret != 0 ) {
      wdb_recover_fatal();
   } else {
      wdb_log( "RECOVERY succeeded\n" );
   }

   // XXX How do we start application recovery?

   return ret;
}

/**
 * Last ditch attempt to recover.
 *
 * @returns nothing on success, terminates the application on failure
 */
static int wdb_recover_fatal(void)
{
   int ret;

   wdb_log( "Running FATAL RECOVERY\n" );

   wdb_shutdown();
   ret = wdb_startup_internal(1 /* FATAL recovery */ );

   if ( ret != 0 ) {
      /* This is really bad! But it will never happen ;) */

      wdb_log( "FATAL RECOVERY FAILED!!!\n" );

      exit(1);
   }

   wdb_log( "FATAL RECOVERY succeeded\n" );

   // XXX How do we start application recovery?
   return ret;
}

/**
 * Open a database using the parameters in the array.
 *
 * @param[in]     wdb        database to open
 * @param[in]     openflags  additional flags to bdb open
 * @param[in]     extflags   extra flags for atypical functionality
 *
 * @returns       0 on success, an error code on failure
 */
int wdb_open(struct waldo_db *wdb, uint32_t openflags, uint32_t extflags)
{
   char            *filename;
   int              serrno;
   int              ret;

   /* Set g_<databasename>db to point to this wdb */
   *(wdb->wdb) = wdb;

   ret = db_create(&wdb->db, g_dbenv, 0);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "db_create %s failed %s\n", wdb->name, db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   ret = wdb->db->set_flags(wdb->db, wdb->db_flags);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "set flags on %s failed %s\n", wdb->name, db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   if ( NULL != wdb->bt_compare_fcn ) {
      ret = wdb->db->set_bt_compare(wdb->db, wdb->bt_compare_fcn);
      if ( ret != 0 ) {
         serrno = errno;
         wdb_log( "set_bt_compare on %s failed %s\n",
                  wdb->name, db_strerror(ret) );

         errno = serrno;
         return ret;
      }
   }

   if ( NULL != wdb->dup_compare_fcn ) {
      ret = wdb->db->set_dup_compare(wdb->db, wdb->dup_compare_fcn);
      if ( ret != 0 ) {
         serrno = errno;
         wdb_log( "set_dup_compare on %s failed %s\n",
                  wdb->name, db_strerror(ret) );

         errno = serrno;
         return ret;
      }
   }

   if ( (extflags & WDB_E_MEMONLY) == 0 ) {

      filename = wdb_filename(wdb);
      if ( filename == NULL ) {
         serrno = errno;
         wdb_log( "wdb_open: wdb_filename failed %s\n",
                  strerror(serrno) );

         errno = serrno;
         return -1;
      }
   } else {
      filename = NULL;
   }

   ret = wdb->db->open(wdb->db, NULL, filename, NULL,
                       wdb->type, openflags, WDB_FILE_MODE);
   free(filename), filename = NULL;

   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "db->open on %s failed %s\n",
               wdb->name, db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   return 0;
}

/**
 * Close a database.
 *
 * @param[in]     wdb        database to close
 *
 * @returns       0 on success, an error code on failure
 */
int wdb_close(struct waldo_db *wdb)
{
   int serrno;
   int ret;

   /*
    * Sanity checks
    */

   if ( wdb == NULL ) {
       wdb_log( "close: wdb == NULL\n" );

       errno = EINVAL;
       return -1;
   }

   // Is it already closed?
   if ( wdb->db == NULL ) {
       return 0;
   }

   ret = wdb->db->close(wdb->db, 0), wdb->db = NULL;

   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "db->close on %s failed %s\n",
               wdb->name, db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   return 0;
}

/**
 * Open all databases listed in the array.
 *
 * @param[in]     openflags  flags to pass the open calls
 *
 * @returns       0 on success, an error code on failure
 */
static int wdb_open_all_dbs(uint32_t openflags)
{
   int      ret;
   uint32_t i;

   for ( i = 0; NUM_DATABASES > i; ++i ) {
      ret = wdb_open( &g_databases[i], openflags, 0);

      if ( ret != 0 ) {
         return ret;
      }
   }

   return 0;
}

/**
 * Close all databases listed in the array.
 *
 * @returns       0 on success, an error code on failure
 */
static int wdb_close_all_dbs(void)
{
   int      ret;
   uint32_t i;

   for ( i = 0; NUM_DATABASES > i; ++i ) {
      ret = wdb_close( *(g_databases[i].wdb) );

      if ( ret != 0 ) {
         return ret;
      }
   }

   return 0;
}

/**
 * Construct the filename for the given waldo database.
 *
 * @param[in]     wdb        database whose filename we want
 *
 * @returns       non NULL on success, sets errno on failure
 */
static char *wdb_filename(struct waldo_db *wdb)
{
   char            *filename;
   size_t           filename_len;
   int              serrno;

   assert(wdb != NULL);

   filename_len = strlen(wdb->name) + strlen(WDB_FILENAME_EXT) + 1;
   filename = malloc( filename_len );
   if ( filename == NULL ) {
      serrno = errno;
      wdb_log( "failed to malloc database filename %s\n",
               strerror(serrno) );

      errno = serrno;
      return NULL;
   }

   strcpy(filename, wdb->name);
   strcat(filename, WDB_FILENAME_EXT);

   return filename;
}

/**
 * Inspect err to determine whether an error occurred and print a message if
 * an error did occur.
 *
 * @param[in]     file       source file where error (if any) occurred
 * @param[in]     line       line in source code where error (if any) occurred
 * @param[in]     err        return code to check for error 
 */
void wdb_check_err_(const char *file, const int line, int err)
{
    if (err != 0) {
        fprintf(stderr, "%s:%d %s\n", file, line, db_strerror(err));

        if (!g_aborting)
            wdb_abort();
    }
}

/**
 * Log a message to the appropriate place. For now we just print it to stderr.
 *
 * @param[in]     fmt        format string
 */
static void wdb_log(const char *fmt, ...) {
#ifndef DEBUG
    (void) fmt;
#else
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
#endif /* ! DEBUG */
}

////////////////////////////////////////////////////////////////////////////////

//
// tnum <-> token
//

/**
 * Lookup a token matching the given tnum.
 *
 * @param[in]     tnum       tnum to lookup
 * @param[in]     txn        transaction this operation is part of
 * @param[out]    token      token matching the given tnum
 *
 * @returns       0 on success, an error code on failure
 */
int wdb_lookup_token(const tnum_t *tnum, DB_TXN *txn, char **token)
{
   DBT                       dbt_token;
   DBT                       dbt_tnum;
   int                       serrno;
   int                       ret;

   assert( token != NULL );

   memset(&dbt_token, 0, sizeof(dbt_token));
   memset(&dbt_tnum, 0, sizeof(dbt_tnum));

   dbt_tnum.data = (char *)tnum;
   dbt_tnum.size = sizeof(*tnum);

   // Lookup the token
   ret = g_tnum2tokdb->db->get(g_tnum2tokdb->db, txn,
                               &dbt_tnum, &dbt_token,
                               0);

   *token = malloc( dbt_token.size+1 );
   if ( *token == NULL ) {
      serrno = errno;
      wdb_log( "malloc failed\n" );

      return serrno;
   }

   memcpy( *token, dbt_token.data, dbt_token.size );
   (*token)[dbt_token.size] = 0;

   return ret;
}

/**
 * Lookup a token's tnum.
 *
 * @param[in]  token     token to lookup (or add)
 * @param[in]  txn       transaction these operations are part of
 * @param[out] tnum      return tnum here
 *
 * @returns    0 on success, an error code on failure
 */
int wdb_lookup_tnum(const char *token, DB_TXN *txn, tnum_t *tnum)
{
   DBT                        dbt_token;
   DBT                        dbt_tnum;
   int                        ret;

   memset(&dbt_token, 0, sizeof(dbt_token));
   memset(&dbt_tnum, 0, sizeof(dbt_tnum));

   dbt_token.data = (char *)token;
   dbt_token.size = strlen(token);

   // Lookup the token
   ret = g_tok2tnumdb->db->get(g_tok2tnumdb->db, txn,
                               &dbt_token, &dbt_tnum,
                               0);

   memcpy(&tnum, dbt_tnum.data, sizeof(tnum));

   return ret;
}

/**
 * Lookup a token's tnum or add it if the token is not there.
 *
 * @param[in]  token     token to lookup (or add)
 * @param[in]  txn       transaction these operations are part of
 * @param[out] tnum      return tnum here
 *
 * @returns    0 on success, an error code on failure
 */
int wdb_lookup_or_add_tnum(const char *token, DB_TXN *txn, tnum_t *tnum)
{
   DBT                        dbt_token;
   DBT                        dbt_tnum;
   int                        serrno;
   int                        ret;

   memset(&dbt_token, 0, sizeof(dbt_token));
   memset(&dbt_tnum, 0, sizeof(dbt_tnum));

   dbt_token.data = (char *)token;
   dbt_token.size = strlen(token);

   // Lookup the token
   ret = g_tok2tnumdb->db->get(g_tok2tnumdb->db, txn,
                               &dbt_token, &dbt_tnum,
                               0);

   // If we did not find the token we need to add it

   if ( DB_NOTFOUND == ret ) {
      // NEW token
      // Need to add this to both tok2tnum and tnum2tok

      ret = g_tnum2tokdb->db->put(g_tnum2tokdb->db, txn,
                                  &dbt_tnum, &dbt_token,
                                  g_tnum2tokdb->put_flags);
      if ( ret != 0 ) {
         serrno = errno;
         wdb_log( "tnum2tok->put failed %s\n", db_strerror(ret) );

         errno = serrno;
         return ret;
      }

      dprintf( "Adding to tok2tnum %u: %u [%.*s]\n",
               *(db_recno_t *)(dbt_tnum.data),
               dbt_token.size,
               dbt_token.size, (char *)(dbt_token.data) );

      ret = g_tok2tnumdb->db->put(g_tok2tnumdb->db, txn,
                                  &dbt_token, &dbt_tnum,
                                  g_tok2tnumdb->put_flags);
   }

   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "tok2tnum get/put failed %s\n", db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   memcpy(tnum, dbt_tnum.data, sizeof(*tnum));

   return ret;
}

//
// inode <-> pnode
//

/**
 * Lookup the inode for a given pnode.
 *
 * @param[in]     pnode      pnode who's inode we want
 * @param[out]    inode      inode for the given pnode
 *
 * @returns       0 on success, an error code on failure
 */
int wdb_lookup_inode(const pnode_t *pnode, DB_TXN *txn, lasagna_ino_t *inode)
{
   DBT                        dbt_inode;
   DBT                        dbt_pnode;
   int                        serrno;
   int                        ret;

   memset(&dbt_inode, 0, sizeof(dbt_inode));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   dbt_inode.data = (char *)inode;
   dbt_inode.size = sizeof(*inode);

   dbt_pnode.data = (char *)pnode;
   dbt_pnode.size = sizeof(pnode_t);

   ret = g_p2idb->db->get(g_p2idb->db, txn,
                          &dbt_pnode, &dbt_inode,
                          0);
   if ( ret != 0 ) {
      serrno = errno;
      wdb_log( "p2i->get failed %s\n", db_strerror(ret) );

      errno = serrno;
      return ret;
   }

   assert( sizeof(ino_t) == dbt_inode.size );

   memcpy(inode, dbt_inode.data, dbt_inode.size); 

   return ret;
}

int wdb_get_max_pnode(pnode_t *pnode, DB_TXN *txn)
{
   DBT                        dbt_inode;
   DBT                        dbt_pnode;
   DBC                       *cursor;
   int                        serrno;
   int                        ret;

   /*
    * Sanity checks
    */
   if ( pnode == NULL ) {
       wdb_log( "get_max_pnode: called with NULL pointer\n" );

       errno = EINVAL;
       return -1;
   }

   /*
    * Initialization
    */
   memset(&dbt_inode, 0, sizeof(dbt_inode));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   /*
    * Start of real work
    */
   ret = g_p2idb->db->cursor(g_p2idb->db, txn, &cursor, 0);
   if (ret != 0) {
       serrno = errno;
       wdb_log( "get_max_pnode: create cursor failed %s\n",
                db_strerror(ret) );

       errno = serrno;
       return ret;
   }

   ret = cursor->get(cursor, &dbt_pnode, &dbt_inode, DB_LAST);
   if ( ret != 0 ) {
       serrno = errno;
       wdb_log( "get_max_pnode: cursor get failed %s\n",
                db_strerror(ret) );

       errno = serrno;
       return ret;
   }

   memcpy(pnode, dbt_pnode.data, dbt_pnode.size); 

   return ret;
}
