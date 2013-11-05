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
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>

#include "wdb.h"
#include "twig.h"
#include "twig_file.h"
#include "ptr_array.h"
#include "cleanpages.h"
#include "md5.h"
#include "i2n.h"
#include "log.h"
#include "process.h"
#include "pass/lasagna.h"
#include "recover.h"

enum recover_file_state {FILE_STATE_MATCH,
                         FILE_STATE_MISMATCH,
                         FILE_STATE_DELETED };

extern int process_prov(struct twig_precord *prov, DB_TXN *txn, uint8_t xflags);

static int recover_logfile(const char *logpath);
static int recover_bundle(struct twig_rec **rec_array,
                          int32_t begin, int32_t last,
                          int mismatch, DB_TXN *txn);
static int recover_wap(struct twig_rec_wap *wap,
                       enum recover_file_state *state);

static int recover_metadb(const char *metadbpath, DB_TXN *txn);
static int recover_nextnums(const char *numspath, DB_TXN *txn);

static int is_metadb_clean(void);
static int create_metadb_clean(void);

static int write_nextnums(const char *numsfilename,
                          const struct lasagna_nextnums *nextnums);


/**
 * Perform waldo recovery (of the log files).
 *
 * @param[in]     fsroot     path to volume mount point
 * @param[in]     logpath    path of log files
 *
 * @returns       0 on success
 */
int
waldo_recover(const char *fsroot, const char *logpath, DB_TXN *txn)
{
    uint64_t                 lognum;
    enum log_state           logstate;
    uint64_t                 nextlognum;
    enum log_state           nextlogstate;
    char                    *metadbfilename;
    int                      metadbfilenamelen;
    char                    *nextnumsfilename;
    int                      nextnumsfilenamelen;
    int                      numfiles;
    int                      err;

   // create an inode -> filename index
   i2n_init(fsroot);

#ifdef DEBUG
   i2n_dump();
#endif /* DEBUG */
 
   log_startup(logpath);

   // get all the log files
   numfiles = log_find_files(logpath);
   if ( numfiles < 0 ) {
      // TODO: yell
       i2n_shutdown();
      return err;
   }

   if ( numfiles == 0 ) {
       // no files
       i2n_shutdown();
       return 0;
   }

   err = log_next_filename(logpath, 0, &lognum, &logstate);
   if ( err != 0 ) {
      i2n_shutdown();
      return 0;
   }

   if ( numfiles > 1 ) {
       // Process old log files (all but the most recent)
       while ( 1 ) {
           err = log_next_filename( logpath, 0, &nextlognum, &nextlogstate );
           if ( err != 0 ) {
               break;
           }

           waldo_process_log(logpath,lognum,logstate,txn);

           lognum = nextlognum;
           logstate = nextlogstate;
       }

       lognum = nextlognum;
       logstate = nextlogstate;
   }

   switch ( logstate ) {
       case LOG_STATE_KERNEL:
       case LOG_STATE_ACTIVE:
       case LOG_STATE_BACKUP:
           break;

       default:
           assert( "waldo_recover: logstate invalid" == 0 );
   }

   // the real work goes here
//   recover_logfile(logpath,lognum,logstate);
   char *logfilename = log_make_filename(logpath,lognum,logstate);
   recover_logfile(logfilename);
   free(logfilename), logfilename = NULL;

   i2n_shutdown();

   // write out i2p mapping
   metadbfilenamelen = strlen(logpath) +
                       strlen("/") +
                       strlen(LASAGNA_METADB_FILENAME);

   metadbfilename = malloc(metadbfilenamelen + 1);
   if ( metadbfilename == NULL ) {
       // TODO log
       return -1;
   }

   sprintf(metadbfilename, "%s/%s", logpath, LASAGNA_METADB_FILENAME);
   recover_metadb(metadbfilename, txn);
   free(metadbfilename), metadbfilename = NULL;

   // write out next nums
   nextnumsfilenamelen = strlen(logpath) + 
                         strlen("/") +
                         strlen(LASAGNA_NEXTNUMS_FILENAME);

   nextnumsfilename = malloc(nextnumsfilenamelen + 1);
   if ( nextnumsfilename == NULL ) {
       // TODO: log
       return -1;
   }

   sprintf(nextnumsfilename, "%s/%s", logpath, LASAGNA_NEXTNUMS_FILENAME);
   recover_nextnums(nextnumsfilename, txn);
   free(nextnumsfilename), nextnumsfilename = NULL;

   return 0;
}

/**
 * Recover the metadb file.
 *
 * @param[in]     metadbpath      path for i2p file
 * @param[in]     txn             BDB txn within which this operation occurs
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
static int recover_metadb(const char *metadbpath, DB_TXN *txn)
{
   DBC                      *dbc;
   DBT                       dbt_key;
   DBT                       dbt_value;
   struct lasagna_metadb_entry
      metadbentry;
   FILE                     *metadbfile;
   unsigned long             inode;
   pnode_version             pv;
   long                      offset;
   int                       ret;
   int                       err = 0;

   /*
    * Sanity checks
    */
   if ( metadbpath == NULL ) {
       fprintf( stderr, "recover_metadb(NULL, ?): "
                "passed a NULL filename\n" );

       errno = EINVAL;
       return -1;
   }

   // txn == NULL is valid
   // it just means this is _not_ running within a transactional environment

   if ( is_metadb_clean() == 1 ) {
       // Already clean
       fprintf( stderr, "recover_metadb: already clean\n" );
       return 0;
   }

   /*
    * Initialization
    */
   memset(&dbt_key  , 0, sizeof(dbt_key));
   memset(&dbt_value, 0, sizeof(dbt_value));

   // Get a cursor
   ret = g_i2pdb->db->cursor(g_i2pdb->db, txn, &dbc, 0);
   if (ret < 0) {
      // TODO: log
      return ret;
   }

   // open and truncate the current file
   metadbfile = fopen(metadbpath, "w");
   if (metadbfile == NULL) {
      // TODO log
      dbc->c_close(dbc);
      return -1;
   }

   /*
    * Beginning of real work
    */

   for (ret = dbc->c_get(dbc, &dbt_key, &dbt_value, DB_NEXT);
        ret == 0;
        ret = dbc->c_get(dbc, &dbt_key, &dbt_value, DB_NEXT))
   {
      assert(sizeof(inode) == dbt_key.size);
      memcpy(&inode, dbt_key.data, sizeof(inode));

      assert(sizeof(pv) == dbt_value.size);
      memcpy(&pv, dbt_value.data, sizeof(pv));

      memset(&metadbentry, 0, sizeof(metadbentry));
      metadbentry.ino = inode;
      metadbentry.pnode = pv.pnum;
      metadbentry.version = pv.version;
      metadbentry.icapi_flags = 0;
      metadbentry.state = PROV_STATE_FROZEN;

      offset = sizeof(metadbentry) * inode;
      ret = fseek(metadbfile, offset, SEEK_SET);
      if (ret != 0) {
         // TODO: log
         err = ret;
         break;
      }

      ret = fwrite(&metadbentry, sizeof(metadbentry), 1, metadbfile);
      if (ret != 0) {
         // TODO: log
         err = ret;
         break;
      }
   }

   /*
    * Clean up
    */
   if ( err == 0 ) {
       // Mark the metadb clean
       create_metadb_clean();
   }

   ret = fclose(metadbfile);
   if (ret != 0) {
      // TODO: log
      if (err == 0) {
         err = ret;
      }
   }

   ret = dbc->c_close(dbc);
   if (ret != 0) {
      // TODO: log
      if (err == 0) {
         err = ret;
      }
   }

   return err;
}

/**
 * Recover the nextnums file (also known as lasagna.state).
 *
 * @param[in]     numspath        path to next nums file
 * @param[in]     txn             BDB txn within which this operation occurs
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
static int
recover_nextnums(const char *numspath, DB_TXN *txn)
{
    struct lasagna_nextnums nextnums;
    pnode_t                 max_pnode;
    uint64_t                lognum;
    int                     ret;

    /*
     * Sanity checks
     */
    if ( numspath == NULL ) {
        fprintf( stderr, "recover_nextnums(NULL,?): numspath is NULL\n" );

        errno = EINVAL;
        return -1;
    }

    // txn == NULL is valid
    // it just means this is _not_ running within a transactional environment
    
    /*
     * Initialization
     */
    memset(&nextnums, 0, sizeof(nextnums));

    /*
     * Beginning of real work
     */
    ret = wdb_get_max_pnode(&max_pnode, txn);
    if ( ret != 0 ) {
        // TODO: log
        return -1;
    }

    ret = log_last_lognum(&lognum);
    if ( ret != 0 ) {
        // TODO: log
        return -1;
    }

    nextnums.pnode  = max_pnode + 1;
    nextnums.lsn    = 1;
    nextnums.lognum = lognum + 1;

    return write_nextnums(numspath, &nextnums);
}


/**
 * Recover the potentially problematic log file.
 *
 * @param[in]     logpath    full path of log file to recover
 *
 * @returns       0 on success
 */
static int recover_logfile(const char *logpath)
{
   struct twig_file         *file;
   struct twig_rec          *rec;
   struct twig_rec         **rec_array;
   uint32_t                  numrecs;
   struct pnode_version    **mismatches;
   uint32_t                  nummismatches;
   struct pnode_version    **matches;
   uint32_t                  nummatches;
   uint32_t                  index;
   int32_t                   recidx;
   int32_t                   lastrec;
   DB_TXN                   *txn;
   enum recover_file_state   state;
   int                       mismatch;
   int                       found;
   int                       serrno;
   int                       ret;

   file = twig_open(logpath, TWIG_RDONLY);
   rec = NULL;
   rec_array = NULL;
   numrecs = 0;

   mismatches = NULL;
   nummismatches = 0;

   matches = NULL;
   nummatches = 0;

   while ( twig_read(file, &rec) != EOF ) {
      ret = ptr_array_add((void ***)&rec_array, &numrecs, rec);
      if ( ret != 0 ) {
         return ret;
      }
   }

   // Ignore records that do not have an end record
   for (lastrec = numrecs-1; lastrec >= 0; lastrec-- ) {
      if ( rec_array[lastrec]->rectype == TWIG_REC_END ) {
         break;
      }
   }

   // Do all recovery inside a transaction
   g_dbenv->txn_begin(g_dbenv, NULL, &txn, 0);

   mismatch = 0;
   for (recidx = lastrec; recidx >= 0; recidx--) {
      rec = rec_array[recidx];

      switch ( rec->rectype ) {
         case TWIG_REC_HEADER:
            break;

         case TWIG_REC_BEGIN:
            ret = recover_bundle(rec_array, recidx, lastrec, mismatch, txn);
            if ( ret != 0 ) {
               // TODO: log
               goto recover_logfile_cleanup;
            }
            break;

         case TWIG_REC_END:
            mismatch = 0;
            break;

         case TWIG_REC_WAP:
            ret = recover_wap((struct twig_rec_wap *)rec, &state);
            if ( ret != 0 ) {
                serrno = errno;
                syslog( LOG_ERR, "Reading recovery info failed %s\n",
                        strerror(serrno) );

            } else {
                struct pnode_version pv;
                pv.pnum    = ((struct twig_rec_wap *)rec)->pnum;
                pv.version = ((struct twig_rec_wap *)rec)->version;

                if ( state == FILE_STATE_MATCH ) {
                    ptr_array_add_nodup( (void ***)&matches, &nummatches, &pv );
                } else {
                    // It is not a mismatch if we later write to the pnode and
                    // that one matches. Since we are going through the log
                    // backwards, did we see a match yet (in the future)?
                    found = 0;
                    for ( index = 0; index < nummatches; ++index ) {
                        if ( matches[index]->pnum == pv.pnum ) {
                            found = 1;
                        }
                    }

                    if ( found == 0 ) {
                        // No later record explains this mismatch
                        ptr_array_add_nodup( (void ***)&mismatches, &nummismatches, &pv );
                    }
                }
            }
            break;

         case TWIG_REC_CANCEL:
            // TODO:
            break;

         case TWIG_REC_PROV:
            break;

// WANT A COMPILER WARNING NOT A RUNTIME ERROR
// The compiler will warn us if the switch does not handle all possible cases
//
//      default:
//         printf( "Unknown rectype %u\n", rectype );
//         return -1;
      }
   }

   ret = txn->commit(txn, 0), txn = NULL;

recover_logfile_cleanup:
   twig_close(file);

   // Syslog all mismatches
   for ( index = 0; index < nummismatches; ++index ) {
       syslog( LOG_WARNING, " mismatch detected in: %" PRIu64 ".%" PRIu32,
               mismatches[index]->pnum, mismatches[index]->version );
   }

   free(rec_array), rec_array = NULL;
   free(mismatches), mismatches = NULL;
   free(matches), matches = NULL;

   return ret;
}

/**
 * Run recovery on a bundle.
 *
 * @param[in]     rec_array  array of twig records
 * @param[in]     begin      index of begin record
 * @param[in]     mismatch   does bundle contain a mismatch
 * @param[in]     txn        bdb transaction this is part of
 *
 * @returns       0 on success
 */
static int recover_bundle(struct twig_rec **rec_array,
                          int32_t begin, int32_t last,
                          int mismatch, DB_TXN *txn)
{
   struct twig_rec          *rec;
   uint8_t                   xflags;
   int32_t                   idx;

   assert( rec_array != NULL );

   xflags = 0;
   if ( mismatch ) {
      xflags |= PROVDB_MISMATCH;
   }

   idx = begin + 1;
   while ( idx <= last ) {
      rec = rec_array[idx];

      switch ( rec->rectype ) {
         case TWIG_REC_HEADER:
         case TWIG_REC_BEGIN:
            // should not have a header or begin inside a begin/end bundle
            // TODO: log
            return 1;

         case TWIG_REC_END:
            return 0;

         case TWIG_REC_WAP:
            break;

         case TWIG_REC_PROV:
            process_prov( (struct twig_precord *)rec, txn, xflags );
            break;

// WANT A COMPILER WARNING NOT A RUNTIME ERROR
// The compiler will warn us if the switch does not handle all possible cases
//
//      default:
//         printf( "Unknown rectype %u\n", rectype );
//         return -1;
      }

      idx++;
   }

   // TODO: log
   // should never get here!
   return -1;
}

/**
 * Run recocery on a WAP record.
 *
 * @param[in]     wap        wap record to process
 * @param[out]    mismatch   is there a mismatch in this bundle
 *
 * @returns       0 on success
 *
 * @note if it finds a mismatch it will set mismatch to 1
 *       however, if it does not find a mismatch it leaves mismatch untouched
 *       that way mismatch tracks whether the entire bundle contains a mismatch
 */
static int
recover_wap(struct twig_rec_wap *wap, enum recover_file_state *state)
{
   uint8_t                   md5[MD5_BYTES];
   lasagna_ino_t             inode;
   char                     *filepath;
   uint8_t                  *buf;
   FILE                     *file;
   size_t                    nread;
   int                       ret;

   assert( wap != NULL );
   assert( state != NULL );

   *state = FILE_STATE_MATCH;

   ret = wdb_lookup_inode(&(wap->pnum), NULL, &inode);
   if ( ret == DB_NOTFOUND ) {
      *state = FILE_STATE_MISMATCH;
   } else if ( ret != 0 ) {
      // TODO: log
      return ret;
   }

   // Has this page already been marked clean?
   if ( cleanpages_lookup(inode, wap->off) == 0 ) {
      // already processed this page
      return 0;
   }

   // Compute md5

   // read in page
   buf = malloc(wap->len);
   if ( buf == NULL ) {
      // TODO: log
      return -ENOMEM;
   }

   filepath = i2n_lookup(inode);
   if ( filepath == NULL ) {
      // Assume this means the file was deleted
      fprintf( stderr, "RECOVERY mismatch: no file with inode %lu\n",
               (unsigned long) inode );
      *state = FILE_STATE_DELETED;
      free(buf), buf = NULL;
      return 0;
   }

   file = fopen(filepath, "r");
   if ( file == NULL ) {
      fprintf( stderr, "RECOVERY ERROR: open file %s failed %s\n",
               filepath, strerror(errno) );

      free(filepath), filepath = NULL;
      free(buf), buf = NULL;
      return -errno;
   }

   ret = fseek(file, wap->off, SEEK_SET);
   if ( ret != 0 ) {
      // Assume this means the file has shrunk
      fprintf( stderr, "RECOVERY mismatch: unable to seek to %llu in file %s\n",
               (unsigned long long)wap->off, filepath );

      *state = FILE_STATE_MISMATCH;
      fclose(file), file = NULL;
      free(filepath), filepath = NULL;
      free(buf), buf = NULL;
      return 0;
   }

   nread = fread(buf, wap->len, 1, file);
   if (nread != wap->len) {
      // Assume this means the file has shrunk
      fprintf( stderr, "RECOVERY mismatch: unable to read %u bytes "
               "at offset %llu from file %s\n",
               wap->len, (unsigned long long)wap->off, filepath );
      *state = FILE_STATE_MISMATCH;
      fclose(file), file = NULL;
      free(filepath), filepath = NULL;
      free(buf), buf = NULL;
      return -errno;
   }

   fclose(file), file = NULL;
   free(filepath), filepath = NULL;

   compute_md5(buf, wap->len, md5);

   free(buf), buf = NULL;

   assert( sizeof(md5) == sizeof(wap->md5) );
   if ( memcmp(md5, wap->md5, sizeof(md5)) == 0 ) {
      cleanpages_add(inode,wap->off);
   } else {
      fprintf( stderr, "RECOVERY mismatch: md5s mismatch for file %s "
               "at offset %llu length %u\n",
               filepath, (unsigned long long)wap->off, wap->len );

      *state = FILE_STATE_MISMATCH;
   }

   return 0;
}

/**
 * Is metadb up to date?
 *
 * We can tell by looking for the "clean" file.
 *
 * @retvsl 0      no
 * @retval 1      yes
 * @retval -1     and sets errno on error
 *
 * @see LASAGNA_CLEAN_FILENAME
 */
static int is_metadb_clean(void)
{
    int                      serrno;
    struct stat              sb;

    if ( stat(LASAGNA_CLEAN_FILENAME, &sb) != 0 ) {
        if ( errno == ENOENT ) {
            return 0;
        }

        serrno = errno;
        fprintf( stderr, "stat failed %s\n",
                 strerror(serrno) );
        errno = serrno;

        return -1;
    }

    return 1;
}

/**
 * Create the metadb clean file.
 * This file (should) denote that the metadb file is up to date.
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
static int create_metadb_clean(void)
{
    FILE                    *fclean;
    int                      serrno;
    time_t                   rawtime;
    char                    *timestr;

    // Create the file
    fclean = fopen(LASAGNA_CLEAN_FILENAME,"a");
    if ( fclean == NULL ) {
        serrno = errno;
        fprintf( stderr, "Create %s failed %s\n",
                 LASAGNA_CLEAN_FILENAME, strerror(serrno) );

        errno = serrno;
        return -1;
    }

    // Write the data
    rawtime = time( NULL );
    timestr = ctime( &rawtime );
    fprintf(fclean, "Cleaned at %s\n", timestr );

    // close the file
    fclose(fclean), fclean = NULL;

    return 0;
}

/**
 * Write out the next nums file.
 *
 * @param[in]     numspath        path to next nums file
 * @param[in]     nextnums        next numbers
 *
 * @returns       0 on success, -1 and sets errno on failure
 */
static int
write_nextnums(const char *numspath,
               const struct lasagna_nextnums *nextnums)
{
    FILE                   *nextnumsfile;
    size_t                  nwritten;

    /*
     * Sanity checks
     */
    if ( numspath == NULL ) {
        // TODO: log
        errno = EINVAL;
        return -1;
    }

    if ( nextnums == NULL ) {
        // TODO: log
        errno = EINVAL;
        return -1;
    }

    nextnumsfile = fopen(numspath, "w");
    if (nextnumsfile == NULL) {
        // TODO: log
        return -1;
    }

    nwritten = fwrite(&nextnums, sizeof(nextnums), 1, nextnumsfile);
    if (nwritten != 1) {
        // TODO: log
        return -1;
    }

    fclose(nextnumsfile), nextnumsfile = NULL;

    return 0;
}

/**
 * Compute the md5 sum for a buffer of the given length.
 *
 * @param[in]     buf        buffer whose md5 we want to compute
 * @param[in]     len        length of the buffer in bytes
 * @param[out]    md5        md5 we computed
 *
 * @returns       0 on success
 */
int compute_md5(const uint8_t *buf, const int len, uint8_t *md5)
{
   md5_state_t               state;

   assert( buf != NULL );
   assert( md5 != NULL );

   md5_init(&state);
   md5_append(&state, buf, len);
   md5_finish(&state, md5);

   return 0;
}
