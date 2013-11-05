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
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "debug.h"

#include "schema.h"
#include "twig_file.h"
#include "wdb.h"
#include "log.h"
#include "process.h"

#define LSN_INVALID          (-1ULL)

extern sig_atomic_t g_shutdown;
extern sig_atomic_t g_offline;
extern sig_atomic_t g_backup;

static lsn_t   g_lsn      = LSN_INVALID;

static int process_header(struct twig_rec_header *header);
static int process_begin(struct twig_rec_begin *begin, DB_TXN **txn);
static int process_end(struct twig_rec_end *end, DB_TXN **txn);
static int process_wap(struct twig_rec_wap *wap);
static int process_cancel(struct twig_rec_cancel *cancel);
int process_prov(struct twig_precord *prov, DB_TXN *txn, uint8_t xflags);

static int process_name( provdb_key *key, provdb_val *val,
                         uint32_t length, DB_TXN *txn );
static int process_inode( provdb_key *key, provdb_val *val,
                          uint32_t length, DB_TXN *txn );
static int process_argv( provdb_key *key, provdb_val **val,
                         uint32_t length, DB_TXN *txn );
static int process_env( provdb_key *key, provdb_val **val,
                        uint32_t length, DB_TXN *txn );
static int process_input( provdb_key *key, provdb_val *val,
                          uint32_t length, DB_TXN *txn );

static int dbrecno_cmp(const void *rec1, const void *rec2);
static uint32_t count_strings(char *buf, uint32_t size);

/**
 * Process all twig files in the given directory.
 *
 * @param[in]     dirpath    directory containing twig files
 * @param[in]     txn        BDB transaction to run in
 *
 * @returns       0 on success, an error code on failure
 */
int waldo_process_dir(const char *dirpath, DB_TXN *txn)
{
   char                    *logfile;
   uint64_t                 lognum;
   enum log_state           logstate;
   int                      ret;

   ret = 0;

   fprintf( stderr, "waldo_process_dir: processing directory %s\n", dirpath );

   while ( ! g_shutdown ) {
      // if g_offline is true, do not wait for more files
      // and vice versa
       ret = log_next_filename(dirpath, ! g_offline, &lognum, &logstate);
       if ( ret != 0 ) {
           if ( g_offline == 1 ) {
               ret = 0;
           } else {
               ret = -1;
           }
           break;
       }

       fprintf( stderr, "waldo_process_dir: "
                "about to process file %s\n",
                logfile );

       ret = waldo_process_log(dirpath, lognum, logstate, txn);
       if ( ret != 0 ) {
           return ret;
       }
   }

   return ret;
}

/**
 * Process all the records in a give twig file.
 *
 * @param[in]     fullpath   name of the twig file
 * @param[in]     txn        BDB transaction to run in
 *
 * @returns       0 on success, an error code on failure
 */
int waldo_process_fullpath(const char *fullpath, DB_TXN *txn)
{
   const char           *filename;
   const char           *dir;
   char                 *allocdir;
   int                   dirlen = 0;
   int                   ret;

   filename = strrchr(fullpath, '/');
   if ( filename == NULL ) {
       filename = fullpath;
       dir = ".";
   } else {
       dirlen = filename - fullpath;
       allocdir = malloc( dirlen + 1);
       strncpy( allocdir, fullpath, dirlen );
       allocdir[dirlen] = '\0';

       dir = allocdir;
   }

   ret = waldo_process_file(dir, filename, txn);

   if ( dirlen != 0 ) {
       free(allocdir), allocdir = NULL;
   }

   return ret;
}

/**
 * Process a log file.
 *
 * @param[in]     dir        directory portion of path
 * @param[in]     filename   filename portion of path
 * @param[in]     txn        BDB transaction to run in
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
int waldo_process_file(const char *dir, const char *filename, DB_TXN *txn)
{
    uint64_t                lognum;
    enum log_state          log_state;
    int                     serrno;
    int                     ret;

    /*
     * Sanity checks
     */
    if ( dir == NULL ) {
        fprintf( stderr, "waldo_process_file(NULL,?): "
                 "dir is NULL\n" );

        errno = EINVAL;
        return -1;
    }

    if ( filename == NULL ) {
        fprintf( stderr, "waldo_process_file(%s,NULL): "
                 "filename is NULL\n", dir );

        errno = EINVAL;
        return -1;
    }

    /*
     * Beginning of real work
     */

    /* Convert filename into <log number, log state> */
    ret = log_get_number(filename, &lognum);
    if ( ret != 0 ) {
        serrno = errno;
        fprintf( stderr, "waldo_process_file(%s,%s): "
                 "log_get_number(%s,%p) failed %s\n",
                 dir, filename,
                 filename, &lognum,
                 strerror(serrno) );
        errno = serrno;
        return ret;
    }

    ret = log_filename_to_state(filename, &log_state);
    if ( ret != 0 ) {
        serrno = errno;
        fprintf( stderr, "waldo_process_file(%s,%s): "
                 "log_filename_to_state(%s,%p) failed %s\n",
                 dir, filename,
                 filename, &log_state,
                 strerror(serrno) );
        errno = serrno;
        return ret;
    }

    /* pass it on to process_log to do the remaining work */
    return waldo_process_log(dir, lognum, log_state, txn);
}

/**
 * Process the specified log file.
 *
 * @param[in]     dir        directory portion of path
 * @param[in]     lognum     log number
 * @param[in]     logstate   log state
 * @param[in]     txn        BDB transaction to run in
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
int
waldo_process_log(const char *dir, uint64_t lognum, enum log_state logstate,
                  DB_TXN *txn)
{
   struct stat           sb;
   char                 *filename;
   char                 *activefilename;
   char                 *backupfilename;
   int                   serrno;
   int                   dorename = 0;
   int                   ret;

   /*
    * Sanity checks
    */
   switch ( logstate ) {
       case LOG_STATE_BACKUP:
           fprintf( stderr, "waldo_process_log(%s,%" PRIu64 ",BACKUP): "
                    "no support for processing backup logs\n",
                    dir, lognum );

           errno = EINVAL;
           return -1;

       case LOG_STATE_KERNEL:
           dorename = 1;
           break;

       case LOG_STATE_ACTIVE:
           break;

       default:
           assert( "waldo_process_log: invalid logstate" == 0 );
   }

   /*
    * Setup
    */

   // Construct filename
   filename = log_make_filename(dir, lognum, logstate);
   if ( filename == NULL ) {
       serrno = errno;
       fprintf( stderr, "waldo_process_log: "
                "log_kernel_filename(%s, %" PRIu64 ") failed %s\n",
                dir, lognum, strerror(serrno) );
       errno = serrno;
       return -1;
   }

   // Make sure the file exists
   ret = stat(filename, &sb);
   if ( ret != 0 ) {
       serrno = errno;
       fprintf( stderr, "waldo_process_log: "
                "stat of file %s failed %s\n",
                filename, strerror(serrno) );
       errno = serrno;
       free(filename), filename = NULL;
       return ret;
   }

   /*
    * Beginning of real work
    */

   fprintf( stderr, "waldo_process_log: processing file %s\n", filename );

   // Do we need to rename the log file (from kernel to active)
   if ( dorename ) {
       activefilename = log_active_filename(dir, lognum);
       if ( activefilename == NULL ) {
           serrno = errno;
           fprintf( stderr, "waldo_process_log: "
                    "creating active filename for %s failed %s\n",
                    filename, strerror(serrno) );
           errno = serrno;
           free(filename), filename = NULL;
           return -1;
       }

       ret = rename(filename, activefilename);
       if ( ret != 0 ) {
           serrno = errno;
           fprintf( stderr, "waldo_process_log: "
                    "rename %s -> %s failed %s\n",
                    filename, activefilename, strerror(serrno) );
           errno = serrno;
           return ret;
       }

       free(filename), filename = NULL;
       filename = activefilename;
   } else {
       assert( logstate == LOG_STATE_ACTIVE );
   }

   /*
    * Start processing the file
    */
   ret = waldo_process_file_norename(filename, txn);
   if ( ret != 0 ) {
       serrno = errno;
       fprintf( stderr, "waldo_process_log(%s,%p): "
                "failed %s\n",
                filename, txn, strerror(serrno) );

       free(filename), filename = NULL;
       errno = serrno;
       return ret;
   }

   /*
    * Clean up
    */

   // Do we remove the file or rename it (save a backup)?
   if ( ! g_backup ) {
       // Remove it.
       ret = remove(filename);
       if ( ret != 0 ) {
           serrno = errno;
           fprintf( stderr, "waldo_process_log(?,%p): "
                    "remove(%s) failed %s\n",
                    txn, filename, strerror(serrno) );
           errno = serrno;
       }
   } else {
       // Rename it

       // create the name for the backup
       backupfilename = log_backup_filename(dir, lognum);
       if ( backupfilename == NULL ) {
           serrno = errno;
           fprintf( stderr, "waldo_process_file: "
                    "creating backup filename for %s failed %s\n",
                    activefilename, strerror(serrno) );
           errno = serrno;
           return -1;
       }

       // perform the rename
       ret = rename(filename, backupfilename);
       if ( ret != 0 ) {
           serrno = errno;
           fprintf( stderr, "waldo_process_file: "
                    "rename for backup %s -> %s failed %s\n",
                    activefilename, backupfilename, strerror(serrno) );
           errno = serrno;
       }
       free(backupfilename), backupfilename = NULL;
   }

   free(filename), filename = NULL;
   return 0;
}

/**
 * Process the filename without doing any of the rename and remove or
 * backup we usually do.
 *
 * @param[in]     filename        filename to process
 * @param[in]     txn             BDB transaction to run in
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
int waldo_process_file_norename(const char *filename, DB_TXN *txn)
{
    struct twig_file       *file;
    struct twig_rec        *rec = NULL;
    int                     serrno;
    int                     ret;

    /*
     * Sanity checks
     */
    if ( filename == NULL ) {
        fprintf( stderr, "waldo_process_file_norename(NULL,?): "
                 "called with NULL filename\n" );

        errno = EINVAL;
        return -1;
    }

    // txn == NULL is valid
    // it just means this is _not_ running within a transactional environment

    /*
     * Beginning of real work
     */

    // open the log file
    file = twig_open(filename, TWIG_RDONLY);

    if ( file == NULL ) {
        serrno = errno;
        fprintf( stderr, "waldo_process_file_norename: "
                 "twig_open(%s) failed %s\n",
                 filename, strerror(errno) );
        errno = serrno;
        return -1;
   }

   // twig_read returns rec but does _NOT_ allocate memory
   //   hence no need to free rec
   while ( twig_read(file, &rec) != EOF ) {
      ret = waldo_process_rec(rec, &txn);

      rec = NULL;

      if ( ret != 0 ) {
          twig_close(file);
          return ret;
      }
   }

   twig_close(file);
   fprintf( stderr, "waldo_process_file_norename: "
            "done processing %s\n", filename );

   return 0;
}

/**
 * Read a provenance record and store it in the database.
 *
 * @param[in]     rec        record to process
 * @param[in]     txn        transaction within which this operation occurs
 *
 * @returns       0 on success, an error on failure
 */
int waldo_process_rec(struct twig_rec *rec, DB_TXN **txn)
{
   enum twig_rectype rectype;

   rectype = (enum twig_rectype)(rec->rectype);

#ifdef DEBUG
   twig_print_rec(rec);
#endif

   switch ( rectype ) {
      case TWIG_REC_HEADER:
         return process_header((struct twig_rec_header *)rec);
      case TWIG_REC_BEGIN:
         return process_begin((struct twig_rec_begin *)rec, txn);

      case TWIG_REC_END:
         return process_end((struct twig_rec_end *)rec, txn);

      case TWIG_REC_WAP:
         return process_wap((struct twig_rec_wap *)rec);

      case TWIG_REC_CANCEL:
         return process_cancel((struct twig_rec_cancel *)rec);

      case TWIG_REC_PROV:
         return process_prov((struct twig_precord *)rec, *txn, 0);

// WANT A COMPILER WARNING NOT A RUNTIME ERROR
// The compiler will warn us if the switch does not handle all possible cases
//
//      default:
//         printf( "Unknown rectype %u\n", rectype );
//         return -1;
   }

   errno = EINVAL;
   return -1;
}

/**
 * process_header - Process per file header.
 *
 * @param[in]     header     header to process
 *
 * @returns       0 on success, -1 on failure
 *
 * At present only confirms that the version matches what we expect.
 */
static int process_header(struct twig_rec_header *header)
{
   if ( header->version != TWIG_VERSION ) {
      fprintf( stderr, "FATAL process_header: header version %u expected %u\n",
               header->version, TWIG_VERSION );

      return -1;
   }

   return 0;
}

/**
 * process_begin - Process a begin record.
 *
 * @param[in]     begin      begin record to process
 * @param[in]     txn        transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 */
static int process_begin(struct twig_rec_begin *begin, DB_TXN **txn)
{
   assert( begin != NULL );
   assert( g_lsn == LSN_INVALID );
   assert( *txn == NULL );

   g_lsn = begin->lsn;
   g_dbenv->txn_begin(g_dbenv, NULL, txn, 0);

   return 0;
}

/**
 * process_end - Process an end record.
 *
 * @param[in]     end        end record to process
 * @param[in]     txn        transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 */
static int process_end(struct twig_rec_end *end, DB_TXN **txn)
{
   int                       ret;

   assert( end != NULL );
   assert( g_lsn == end->lsn );

   if ( *txn == NULL ) {
      fprintf( stderr,
               "ERROR in input end does not match begin. Expected end %llu\n",
               (unsigned long long)end->lsn );

      return -1;
   }

   ret = (*txn)->commit(*txn, 0), *txn = NULL;
   wdb_check_err(ret);

   g_lsn = LSN_INVALID;

   return 0;
}

/**
 * process_wap - Process a wap record.
 *
 * @param[in]     wap        wap record to process
 *
 * @returns       0 on success, an error code on failure
 */
static int process_wap(struct twig_rec_wap *wap)
{
   // Just drop it on the floor
   assert( wap != NULL );

   return 0;
}

/**
 * process_cancel - Process a wap cancel record.
 * A cancel record indicates that an user's operation failed
 * therefore the provenance it describes should be cancelled.
 *
 * @param[in]     cancel     cancel record to process.
 *
 * @returns       0 on success, an error code on failure
 */
static int process_cancel(struct twig_rec_cancel *cancel)
{
   // Just drop it on the floor
   assert( cancel != NULL );

   return 0;
}

/**
 * Record a provenance record.
 *
 * @param[in]     prec       provenance record to process
 * @param[in]     txn        transaction within which this operation occurs
 * @param[in]     xflags     extra flags (currently mismatch)
 *
 * @returns       0 on success, an error code on failure
 */
int process_prov(struct twig_precord *prec, DB_TXN *txn, uint8_t xflags)
{
   uint32_t                   twig_flags;
   const char                *twig_attr;
   uint32_t                   twig_attrlen;
   const void                *twig_value;
   uint32_t                   twig_valuelen;
   unsigned                   twig_valuetype;

   uint8_t                    db_xflags;
   enum prov_attrname_packed  db_attrcode;
   const char                *db_attr;
   uint32_t                   db_attrlen;
   const void                *db_value = NULL;
   uint32_t                   db_valuelen = 0;
   unsigned                   db_valuetype;

   provdb_key                 db_key;
   provdb_val                *db_val;

   DBT                        db_key_dbt;
   DBT                        db_val_dbt;

   int                        ret;

   twig_flags       = prec->tp_flags;
   twig_attr        = TWIG_PRECORD_ATTRIBUTE(prec);
   twig_attrlen     = prec->tp_attrlen;
   twig_value       = TWIG_PRECORD_VALUE(prec);
   twig_valuelen    = prec->tp_valuelen;
   twig_valuetype   = prec->tp_valuetype;

   // XXX: TODO: URI: Add source indicator once we figure out how

   db_xflags = xflags;
   if (twig_flags & PROV_IS_ANCESTRY) {
      db_xflags |= PROVDB_ANCESTRY;
   }

   // We encode the most common attribute names as integers in order
   // to save space.  Search the packed table for the string; if found
   // we use the packed representation.
   db_attrcode = packed_get_attrcode(twig_attr, twig_attrlen);
   if ( db_attrcode != PROV_ATTR_INVALID ) {
      db_xflags |= PROVDB_PACKED;
      db_attr = NULL;
      db_attrlen = 0;
   }
   else {
      db_attr = twig_attr;
      db_attrlen = twig_attrlen;
   }

   db_valuetype = twig_valuetype;
   db_value = twig_value;
   db_valuelen = twig_valuelen;

   // Database table key (pnode/version pair)
   db_key.pnum      = prec->tp_pnum;
   db_key.version   = prec->tp_version;

   if ((twig_flags & PROV_IS_ANCESTRY) == 0) {
      // identity information is attached to version 0
      db_key.version = 0;
   }

   // Database table value (see twig.h/schema.h/provabi.h for details)
   db_val = malloc(sizeof(*db_val) + db_attrlen + db_valuelen);
   db_val->pdb_flags = db_xflags;
   if (db_attrcode != PROV_ATTR_INVALID) {
      db_val->pdb_attrcode = db_attrcode;
   } else {
      db_val->pdb_attrlen = db_attrlen;
      memcpy(db_val->pdb_data, twig_attr, db_attrlen);
   }
   db_val->pdb_valuetype = db_valuetype;
   db_val->pdb_valuelen = db_valuelen;
   memcpy(db_val->pdb_data + db_attrlen, db_value, db_valuelen);

   // Do not put the DBTs into the prov database yet
   // This allows us to modify what we actually insert in the code below.

   /////////////////////////////////////////////////////////////////////
   //
   // Process indices
   //

   if (PROVDB_VAL_VALUETYPE(db_val) == PROV_TYPE_PNODEVERSION &&
       PROVDB_VAL_ISANCESTRY(db_val)) {
      process_input(&db_key, db_val, db_valuelen, txn);
   }
   
   if ( PROVDB_VAL_ISPACKED(db_val) ) {
      switch ( db_val->pdb_attrcode ) {
         case PROV_ATTR_NAME:
            process_name(&db_key, db_val, db_valuelen, txn);
            break;

         case PROV_ATTR_INODE:
            process_inode(&db_key, db_val, db_valuelen, txn);
            break;

            //
            // Arguments and Environments
            //
         case PROV_ATTR_ARGV:
            process_argv(&db_key, &db_val, db_valuelen, txn);
            break;

         case PROV_ATTR_ENV:
            process_env(&db_key, &db_val, db_valuelen, txn);
            break;

         default:
            // Nothing to do.. no special indices for this type
            break;
      }
   }

   // Initialize the DBTs in preparation for DB insert
   memset(&db_key_dbt, 0, sizeof(db_key_dbt));
   memset(&db_val_dbt, 0, sizeof(db_val_dbt));

   db_key_dbt.data = &db_key;
   db_key_dbt.size = sizeof(db_key);

   // caution - cannot use db_valuelen here, as db_val may have
   // been rearranged under us. but *must* use db_attrlen, not
   // db_val->pdbv_attrlen, because of the union the latter is
   // part of. bleah.
   db_val_dbt.data = db_val;
   db_val_dbt.size = sizeof(*db_val) 
      + db_attrlen
      + db_val->pdb_valuelen;

   // Put the records into PROVDB
   ret = g_provdb->db->put(g_provdb->db, txn, &db_key_dbt, &db_val_dbt,
                           g_provdb->put_flags);
   wdb_check_err(ret);

   free(db_val), db_val = NULL;

   return 0;
}

/**
 * process_name - Constructs a secondary index on records of type name.
 *
 * @param[in]     key         provdb key
 * @param[in]     val         provdb value
 * @param[in]     length      provdb value length
 * @param[in]     txn         transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 *
 * @note Cannot use BDB secondary indices because the keys in provdb can have
 *       duplicates.
 */
static int process_name( provdb_key *key, provdb_val *val,
                         uint32_t length, DB_TXN *txn )
{
   DBT                        dbt_name_key;
   DBT                        dbt_name_val;
   int                        ret;


   memset(&dbt_name_key, 0, sizeof(dbt_name_key));
   memset(&dbt_name_val, 0, sizeof(dbt_name_val));

   dbt_name_key.data = val->pdb_data;
   dbt_name_key.size = length;

   dbt_name_val.data = &(key->pnum);
   dbt_name_val.size = sizeof(pnode_t);

   ret = g_namedb->db->put(g_namedb->db, txn,
                           &dbt_name_key, &dbt_name_val,
                           g_namedb->put_flags);

   if ( ret == DB_KEYEXIST ) {
#ifdef DEBUG
       fprintf( stderr,
	        "name index: key %*s for pnode %llu already recorded\n",
                length, val->pdb_data, key->pnum );
#endif /* DEBUG */
   } else {
       wdb_check_err(ret);
   }

   return ret;
}

/**
 * process_inode - Add mapping between inode and pnode in both directions.
 *
 * @param[in]     key         pnode and version
 * @param[in]     val         inode
 * @param[in]     length      sizeof(inode)
 * @param[in]     txn         transaction this operations is part of
 *
 * @returns       0 on success, an error code on failure
 */
static int process_inode( provdb_key *key, provdb_val *val,
                          uint32_t length, DB_TXN *txn )
{
   DBT                        dbt_inode;
   DBT                        dbt_pnode;
   DBT                        dbt_pv;
   int                        ret;

   assert( length == sizeof(lasagna_ino_t) );

   //
   // Initialize DBTs
   //

   memset(&dbt_inode, 0, sizeof(dbt_inode));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));
   memset(&dbt_pv   , 0, sizeof(dbt_pv)   );

   dbt_inode.data = val->pdb_data;
   dbt_inode.size = sizeof(lasagna_ino_t);

   dbt_pnode.data = &(key->pnum);
   dbt_pnode.size = sizeof(key->pnum);

   dbt_pv.data = key;
   dbt_pv.size = sizeof(*key);

   // Add to inode -> pnode
   ret = g_i2pdb->db->put(g_i2pdb->db, txn,
                          &dbt_inode, &dbt_pv,
                          g_i2pdb->put_flags);
   wdb_check_err(ret);

   // Add to pnode -> inode
   ret = g_p2idb->db->put(g_p2idb->db, txn,
                          &dbt_pnode, &dbt_inode,
                          g_p2idb->put_flags);
   wdb_check_err(ret);

   return ret;
}

/**
 * tokenize_multistring - Convert a collection of strings into
 *                        a collection of tokens.
 *
 * For example, replace "cat foo bar foo" with something like: {1,2,3,2}.
 * This is used to process argv and env records.
 *
 * @param[in]     key         provdb key
 * @param[in]     val         provdb value
 * @param[in]     length      provdb value length
 * @param[in]     txn         transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 *
 */
static void tokenize_multistring(provdb_key *key, provdb_val **val,
				 uint32_t length, DB_TXN *txn, int isargv)
{
   DBT                        dbt_token;
   DBT                        dbt_tnum;
   DBT                        dbt_pnode;
   tnum_t                     tnum;
   uint32_t                   item;
   uint32_t                   numitems;
   db_recno_t                *items;
   db_recno_t                *sorted_items;
   uint32_t                   offset;
   uint32_t                   len;
   int                        ret;
   char                      *value;
   size_t                     newsize;

   // Sanity checks
   assert( key != NULL );
   assert( sizeof(key->pnum) == sizeof(pnode_t) );

   // Initialize DBTs
   memset(&dbt_token, 0, sizeof(dbt_token));
   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   dbt_pnode.data = &(key->pnum);
   dbt_pnode.size = sizeof(pnode_t);

   // How many arguments are there?
   value = PROVDB_VAL_VALUE(*val);
   numitems = count_strings(value, PROVDB_VAL_VALUELEN(*val));

   // Allocate an array to keep the ids
   items        = malloc(numitems * sizeof(*items));
   assert( items != NULL );

   sorted_items = malloc(numitems * sizeof(*sorted_items));
   assert( sorted_items != NULL );

   // Fill in the ids
   // item keeps track of which argument we are on
   // i is the offset into (*val)->data
   offset = 0;
   for ( item = 0; item < numitems; ++item ) {
      len = strlen(value + offset) + 1;

      wdb_lookup_or_add_tnum(value + offset, txn, &tnum);

      // Add the id to the items array
      // We will later write this args array into provdb
      items[item] = tnum;

      offset += len;
   }

   //
   // {arg/env}tnum2pnode
   //
   // Before constructing the {arg/env}tnum2pnode index we need to uniquify
   // the set of tokens. If --quiet appears more than once we only want
   // one index mapping the tnum for --quiet to the relevant pnum.

   memcpy(sorted_items, items, numitems * sizeof(*items));
   qsort(sorted_items, numitems, sizeof(*sorted_items), dbrecno_cmp );

   for ( item = 0; item < numitems; ++item ) {
      if ( (0 == item) || (sorted_items[item] != sorted_items[item-1]) ) {
          memset(&dbt_tnum, 0, sizeof(dbt_tnum));
         dbt_tnum.data = &(sorted_items[item]);
         dbt_tnum.size = sizeof(sorted_items[item]);

	 struct waldo_db *wdb = isargv ? g_arg2pdb : g_env2pdb;
         ret = wdb->db->put(wdb->db, txn,
			    &dbt_tnum, &dbt_pnode,
			    wdb->put_flags);

         if ( ret != DB_KEYEXIST ) {
            wdb_check_err(ret);
         }
      }
   }

   assert(length == (*val)->pdb_valuelen);
   (*val)->pdb_valuelen = numitems * sizeof(*items);
   newsize = PROVDB_VAL_TOTSIZE(*val);
   *val = realloc(*val, newsize);
   assert(*val); // XXX shouldn't crash

   memcpy(PROVDB_VAL_VALUE(*val), items, numitems * sizeof(*items));
   (*val)->pdb_flags |= PROVDB_TOKENIZED;
}

/**
 * process_argv - Process an argv record.
 *
 * @param[in]     key         provdb key
 * @param[in]     val         provdb value
 * @param[in]     length      provdb value length
 * @param[in]     txn         transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 *
 * @see tokenize_multistring
 */
static int process_argv( provdb_key *key, provdb_val **val,
                         uint32_t length, DB_TXN *txn )
{
   tokenize_multistring(key, val, length, txn, 1 /* argv */);

   return 0;
}

/**
 * process_env - Process an environment record.
 *
 * @param[in]     key         provdb key
 * @param[in]     val         provdb value
 * @param[in]     length      provdb value length
 * @param[in]     txn         transaction within which this operation occurs
 *
 * @returns       0 on success, an error code on failure
 *
 * @see tokenize_multistring
 */
static int process_env( provdb_key *key, provdb_val **val,
                        uint32_t length, DB_TXN *txn )
{
   tokenize_multistring(key, val, length, txn, 0 /* not argv */);

   return 0;
}

/**
 * process_input - Process an input record.
 *
 * @param[in]     key         child pnode and version
 * @param[in]     val         provdb_val containing a ref
 * @param[in]     length      sizeof(struct pnode_version)
 * @param[in]     txn         transaction within which this operation occurs
 *
 * @returns       0 on success, an error on failure
 */
static int process_input( provdb_key *key, provdb_val *val,
                          uint32_t length, DB_TXN *txn )
{
   DBT                        dbt_parent;
   DBT                        dbt_child;
   provdb_key                 parent;
   struct pnode_version      *ref;
   int                        ret;

   // Sanity checks
   assert(PROVDB_VAL_ISANCESTRY(val));
   assert(PROVDB_VAL_VALUETYPE(val) == PROV_TYPE_PNODEVERSION);
   assert( length == sizeof(*ref) );

   // Initialize ref, parent
   ref = PROVDB_VAL_VALUE(val);
   parent.pnum     = ref->pnum;
   parent.version  = ref->version;

   // Initialize DBTs
   memset(&dbt_parent, 0, sizeof(dbt_parent));
   memset(&dbt_child, 0, sizeof(dbt_child));

   dbt_parent.data = &parent;
   dbt_parent.size = sizeof(parent);

   dbt_child.data = key;
   dbt_child.size = sizeof(*key);

   // Add child to child index
   ret = g_childdb->db->put(g_childdb->db, txn,
                            &dbt_parent, &dbt_child,
                            g_childdb->put_flags);

   if ( ret != DB_KEYEXIST ) {
      wdb_check_err(ret);
   }

   // Add parent to parent index
   ret = g_parentdb->db->put(g_parentdb->db, txn,
                             &dbt_child, &dbt_parent,
                             g_parentdb->put_flags);

   if ( ret != DB_KEYEXIST ) {
      wdb_check_err(ret);
   }

   return ret;
}

/**
 * count_strings - Count how many null terminated strings occur in a buffer.
 *
 * @param[in]     buf         buffer to search
 * @param[in]     size        size of buffer in bytes
 *
 * @returns       numbers of strings found in buf
 */
static uint32_t count_strings(char *buf, uint32_t size)
{
   uint32_t len;
   uint32_t numstrings;
   uint32_t offset;

   numstrings = 0;

   for ( offset = 0; offset < size; offset += len ) {

      len = strlen(buf + offset) + 1;

      numstrings++;
   }

   return numstrings;
}

/**
 * dbrecno_cmp - Compare two records for sorting (qsort).
 *
 * @param[in]     rec1        one of the records to compare
 * @param[in]     rec2        a second record to compare
 *
 * @returns       0  if rec1 == rec2
 *                -1 if rec1 <  rec2
 *                1  if rec1 >  rec2
 *
 * @note Cannot simply subtract the two values because they are unsigned
 * (and even if they were signed there is a whole).
 */
static int dbrecno_cmp(const void *rec1, const void *rec2)
{
   db_recno_t                 dbr1;
   db_recno_t                 dbr2;

   assert( NULL != rec1 );
   assert( NULL != rec2 );

   dbr1 = *(db_recno_t *)rec1;
   dbr2 = *(db_recno_t *)rec2;

   return ((dbr1 < dbr2) ? -1 :
           ((dbr1 > dbr2) ? 1 : 0));
}
