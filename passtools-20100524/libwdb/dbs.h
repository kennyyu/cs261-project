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

#ifndef DBS_H
#define DBS_H

#include <db.h>

#include "schema.h"
#include "wdb.h"

/**
 * Table of databases settings.
 *
 * @note Description of these settings
 *
 * provdb is a btree with unsorted duplicates
 *  the key is the pnode,version pair and there will likely be multiple records
 *    per pnode,version pair
 *  we sort the keys by pnode then version
 *
 * tnum2tok is a recno that assigns a tnum for each unique token
 *  clearly all new entries are appended
 *
 * tok2tnum is a btree used to look up a tnum when you have a token
 *  a given token resolves to exactly one tnum hence there are no duplicates
 *  to enforce this a given key cannot be overwritten
 *  tokens are sorted lexographically (default sort function)
 *
 * {arg/env}2p is a btree used to lookup all pnodes with the
 *    given {arg/env} tnum
 *  a given {arg/env} tnum may resolve to one or more pnodes thus duplicates
 *    may occur
 *  sort keys by tnum values by pnode
 *
 * i2p is a btree used to lookup the pnodes associated with a given inode
 *  since inodes are recycled an inode can resolve to multiple pnodes
 *  sort keys by inode values by pnode
 *
 * p2i is a btree used to lookup the inode associated with a given pnode
 *  a given pnode resolves to exactly one inode hence there are no duplicates
 *  sort keys by pnode
 *
 * name is a btree used to lookup a pnode given the file/process name
 *  a given name may resolve to one or more pnodes thus duplicates may occur
 *  sort names lexographically (default sort function) values by pnode
 *
 * {child, parent} is a btree used to lookup {child, parent}
 *    given {parent, child}
 *  a given {parent, child} may have any number of {children, parents}
 *  sort key and value by pnode then version
 */

#define W(TYPE, DUP, DUPSORT, PUT_FLAGS, KEY_CMP, DUP_CMP, NAME)               \
   { DB_ ## TYPE,                                                              \
     (DUP ? DB_DUP : 0) | (DUPSORT ? DB_DUPSORT : 0),                          \
     PUT_FLAGS,                                                                \
     KEY_CMP, DUP_CMP, NULL, &g_ ## NAME ## db, #NAME }

struct waldo_db g_databases[] = {
   /* type   dup sort put flags       key compare    dup compare    name */
   W( BTREE, 1 , 0  , 0             , pnode_ver_cmp, NULL         , prov     ),
   W( RECNO, 0 , 0  , DB_APPEND     , NULL         , NULL         , tnum2tok ),
   W( BTREE, 0 , 0  , DB_NOOVERWRITE, NULL         , NULL         , tok2tnum ),
   W( BTREE, 1 , 1  , DB_NODUPDATA  , tnum_cmp     , pnode_cmp    , arg2p    ),
   W( BTREE, 1 , 1  , DB_NODUPDATA  , tnum_cmp     , pnode_cmp    , env2p    ),
   W( BTREE, 0 , 0  , 0             , inode_cmp    , NULL         , i2p      ),
   W( BTREE, 0 , 0  , 0             , pnode_cmp    , NULL         , p2i      ),
   W( BTREE, 1 , 1  , DB_NODUPDATA  , NULL         , pnode_cmp    , name     ),
   W( BTREE, 1 , 1  , DB_NODUPDATA  , pnode_ver_cmp, pnode_ver_cmp, child    ),
   W( BTREE, 1 , 1  , DB_NODUPDATA  , pnode_ver_cmp, pnode_ver_cmp, parent   )
};

#undef W

/** Number of databases in the database table */
#define NUM_DATABASES (sizeof(g_databases) / sizeof(g_databases[0]))

#endif /* DBS_H */
