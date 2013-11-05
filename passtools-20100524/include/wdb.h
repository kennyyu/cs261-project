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

#ifndef _WDB_H
#define _WDB_H

#include <db.h>
#include "schema.h"

//
// Constants
//

#define TRUE                 1
#define FALSE                0

//
// WDB Open flags
//

// open modes
#define WDB_O_RDONLY         (DB_AUTO_COMMIT | DB_RDONLY)
//#define WDB_O_WRONLY         DB_AUTO_COMMIT
#define WDB_O_RDWR           DB_AUTO_COMMIT

// additional open flags
#define WDB_O_CREAT          DB_CREATE
#define WDB_O_EXCL           (DB_CREATE | DB_EXCL)
#define WDB_O_TRUNC          DB_TRUNCATE

#define WDB_E_MEMONLY        1

extern int        g_aborting;
extern DB_ENV    *g_dbenv;

struct waldo_db {
   uint32_t          type;
   uint32_t          db_flags;
   uint32_t          put_flags;
   int               (*bt_compare_fcn)(DB *db, const DBT *dbt1, const DBT *dbt2);
   int               (*dup_compare_fcn)(DB *db, const DBT *dbt1, const DBT *dbt2);
   DB               *db;
   struct waldo_db **wdb;
   const char       *name;
};

////////////////////////////////////////////////////////////////////////////////
//
// Databases
//

extern struct waldo_db *g_provdb;
extern struct waldo_db *g_tnum2tokdb;
extern struct waldo_db *g_tok2tnumdb;
extern struct waldo_db *g_env2pdb;
extern struct waldo_db *g_arg2pdb;
extern struct waldo_db *g_i2pdb;
extern struct waldo_db *g_p2idb;
extern struct waldo_db *g_namedb;
extern struct waldo_db *g_childdb;
extern struct waldo_db *g_parentdb;

////////////////////////////////////////////////////////////////////////////////
//
// Waldo database API
//

#ifdef __cplusplus
extern "C" {
#endif

int wdb_startup(const char *path, uint32_t openflags);
int wdb_shutdown(void);

int wdb_open(struct waldo_db *wdb, uint32_t openflags, uint32_t extflags);
int wdb_close(struct waldo_db *wdb);

// check for error
#define wdb_check_err(err) wdb_check_err_(__FILE__, __LINE__, err)

void wdb_check_err_(const char* file, const int line, int err);

int wdb_lookup_token(const tnum_t *tnum, DB_TXN *txn, char **token);
int wdb_lookup_tnum(const char *token, DB_TXN *txn, tnum_t *tnum);
int wdb_lookup_or_add_tnum(const char *token, DB_TXN *txn, tnum_t *tnum);

int wdb_lookup_inode(const pnode_t *pnode, DB_TXN *txn, lasagna_ino_t *inode);

int wdb_get_max_pnode(pnode_t *pnode, DB_TXN *txn);

#ifdef __cplusplus
}
#endif

#endif /* _WDB_H */
