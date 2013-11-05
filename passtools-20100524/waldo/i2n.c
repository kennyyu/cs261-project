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
#include <ftw.h>
#include <assert.h>
#include <db.h>

#include "wdb.h"
#include "i2n.h"

#define NOPENFD 20

extern int inode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);

static struct waldo_db *i2ndb = NULL;

static struct waldo_db tmpdb =
{ DB_BTREE, 0, DB_NOOVERWRITE, inode_cmp, NULL, NULL, &i2ndb, NULL };

static int i2n_add(const char *fpath, const struct stat *sb, int typeflag);

/**
 * Initialize the inode-to-name subsystem.
 *
 * @param[in]     fsroot     root for this filesystem
 *
 * @returns       0 on success
 */
int i2n_init(const char *fsroot)
{
   int                       ret;

   ret = wdb_open(&tmpdb, WDB_O_CREAT | WDB_O_RDWR, WDB_E_MEMONLY);
   if ( ret != 0 ) {
      // TODO: deal with this
   }

   ret = ftw(fsroot, i2n_add, NOPENFD);
   if ( ret != 0 ) {
      // TODO: deal with this
   }

   return 0;
}

/**
 * Shutdown the inode-to-name subsystem.
 *
 * @returns       0 on success
 */
int i2n_shutdown(void)
{
   int                       ret;

   ret = wdb_close(i2ndb);
   i2ndb = NULL;

   return ret;
}

/**
 * Lookup a filename that matches the given inode.
 *
 * @param[in]     inode      inode we are looking for
 *
 * @returns       filename on success, NULL on failure
 *
 * @note A given inode could have multiple filenames that resolve to it
 *       due to hard links.
 */
char *i2n_lookup(const ino_t inode)
{
   DBT                       key_dbt;
   DBT                       val_dbt;
   int                       err;
   char                     *ret;
   lasagna_ino_t             linode;

   if ( i2ndb == NULL ) {
      return NULL;
   }

   memset( &key_dbt, 0, sizeof(key_dbt) );
   memset( &val_dbt, 0, sizeof(val_dbt) );

   linode = inode;
   key_dbt.data = &linode;
   key_dbt.size = sizeof(linode);

   err = i2ndb->db->get(i2ndb->db, NULL, &key_dbt, &val_dbt, 0);
   if ( err != 0 ) {
      // TODO: yell
      return NULL;
   }

   ret = malloc(val_dbt.size);
   if ( ret == NULL ) {
      return NULL;
   }

   memcpy(ret, val_dbt.data, val_dbt.size);
   ret[val_dbt.size-1] = 0;

   return ret;
}

/**
 * Dump the inode to filename mappings.
 *
 * @returns       0 on success
 */
int i2n_dump(void)
{
   DBC                      *i2nc = NULL;
   DBT                       key_dbt;
   DBT                       val_dbt;
   lasagna_ino_t             ino;
   int                       ret;

   printf( "----------------------------------------\n"
           "| Dumping i2n                          |\n"
           "| inode: name                          |\n"
           "----------------------------------------\n" );

   memset(&key_dbt, 0, sizeof(key_dbt));
   memset(&val_dbt , 0, sizeof(val_dbt));

   //
   // Process the entries
   //

   // Open a cursor
   ret = i2ndb->db->cursor(i2ndb->db, NULL, &i2nc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = i2nc->c_get(i2nc, &key_dbt, &val_dbt, DB_NEXT)) ) {
      assert( sizeof(ino) == key_dbt.size );

      memcpy(&ino, key_dbt.data, key_dbt.size);

      printf( "%lu: %.*s\n",
              (unsigned long) ino, val_dbt.size, (char *) val_dbt.data );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }

   return 0;
}

/**
 * Add an inode to string mapping.
 *
 * @param[in]     fpath      file path
 * @param[in]     sb         stat buffer for the given file
 * @param[in]     typeflag   file type information
 *
 * @returns       0 on success
 *
 * @note This is a callback invoked by fwt.
 */
static int i2n_add(const char *fpath, const struct stat *sb, int typeflag)
{
   DBT                       key_dbt;
   DBT                       val_dbt;
   int                       ret;
   lasagna_ino_t             linode;

   if ( typeflag != FTW_F ) {
      return 0;
   }

   memset( &key_dbt, 0, sizeof(key_dbt) );
   memset( &val_dbt, 0, sizeof(val_dbt) );

   linode = sb->st_ino;
   key_dbt.data = &linode;
   key_dbt.size = sizeof(linode);

   val_dbt.data = (char *)fpath;
   val_dbt.size = strlen(fpath) + 1;

   ret = i2ndb->db->put(i2ndb->db, NULL, &key_dbt, &val_dbt, i2ndb->put_flags);
   if ( (ret != 0) && (ret != DB_KEYEXIST) ) {
      // TODO: error
   }

   return 0;
}
