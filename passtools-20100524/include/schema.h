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

#ifndef SCHEMA_H
#define SCHEMA_H
/**
 * schema.h
 *
 * Defines the key and value types for all databases in the Waldo
 * querying and index service.
 */

#include <sys/types.h>
#include <stdint.h>
#include <db.h>

#include "twig.h"

/**
 * Structure definitions used in multiple databases.
 */
struct pnode_version {
   pnode_t   pnum;
   version_t version;
} __attribute__((__packed__));

typedef struct pnode_version pnode_version;
typedef db_recno_t tnum_t;

struct tnum_pair {
   tnum_t    tnum1;
   tnum_t    tnum2;
} __attribute__((__packed__));

typedef uint32_t policy_t;
typedef uint32_t role_t;

/**
 * Provenance database and indices
 * @{
 */

/**
 * provdb
 *
 * Stores a collection of provenance records for each <pnode,version> pair. Some
 * attributes are per pnode and some are per version. The per pnode attributes
 * are attached to version 0 of the pnode.
 */

/**
 * Attribute flags used in provdb_val.pdbv_flags
 */
//#define PROVDB_XREF
//#define PROVDB_PASS
//#define PROVDB_REMOTE
#define PROVDB_TOKENIZED  0x1
#define PROVDB_PACKED     0x2
#define PROVDB_ANCESTRY   0x4
#define PROVDB_MISMATCH   0x8

/**
 * Packed attribute names.
 *
 * Used in provdb_val.attrname_packed (when PROVDB_PACKED is set).
 */
enum prov_attrname_packed {
   PROV_ATTR_INVALID,
   PROV_ATTR_TYPE,
   PROV_ATTR_NAME,
   PROV_ATTR_INODE,
   PROV_ATTR_PATH,
   PROV_ATTR_ARGV,
   PROV_ATTR_ENV,
   PROV_ATTR_FREEZETIME,
   PROV_ATTR_INPUT
};

/**
 * This complex structure is used to compactly represent a provenance
 * record in the main provenance database.
 *
 * If PROV_ATTRFLAG_PACKED is set,
 *  attrname_packed is set to a value from the enum prov_attrname_packed.
 */
struct provdb_val {
   uint8_t  pdb_flags;
   uint8_t  pdb_valuetype;
   union {
      uint16_t pdb_attrcode;	/* if PROV_ATTRFLAG_PACKED */
      uint16_t pdb_attrlen;	/* if not */
   } __attribute__((__packed__));
   uint32_t pdb_valuelen;
   uint8_t  pdb_data[0];
} __attribute__((__packed__));

// Ordinary readers shouldn't need these accessors
#define PROVDB_VAL_ISPACKED(pv) \
	(((pv)->pdb_flags & PROVDB_PACKED) ? 1 : 0)

#define PROVDB_VAL_GETATTRPTR(pv, ptr, len) \
	(PROVDB_VAL_ISPACKED(pv) ? \
	    (*(ptr) = packed_get_name((pv)->pdb_attrcode), \
	     *(len)=strlen(*(ptr))) : \
	  (*(ptr) = (const char *)(pv)->pdb_data, *(len) = (pv)->pdb_attrlen))

// Accessor macros (all ordinary readers should use these only)

#define PROVDB_VAL_ISANCESTRY(pv) \
	(((pv)->pdb_flags & PROVDB_ANCESTRY) ? 1 : 0)
#define PROVDB_VAL_ISTOKENIZED(pv) \
	(((pv)->pdb_flags & PROVDB_TOKENIZED) ? 1 : 0)

#define PROVDB_VAL_ATTR(pv) provdb_val_attr(pv)
#define PROVDB_VAL_VALUE(pv) \
	((void *)((pv)->pdb_data + \
		  (PROVDB_VAL_ISPACKED(pv) ? 0 : (pv)->pdb_attrlen)))
#define PROVDB_VAL_VALUELEN(pv) ((pv)->pdb_valuelen)
#define PROVDB_VAL_VALUETYPE(pv) ((pv)->pdb_valuetype)

#define PROVDB_VAL_TOTSIZE(pv) \
	(sizeof(*(pv)) \
	 + (PROVDB_VAL_ISPACKED(pv) ? 0 : (pv)->pdb_attrlen) \
	 + PROVDB_VAL_VALUELEN(pv))


typedef pnode_version        provdb_key;
typedef struct provdb_val    provdb_val;

/**
 * tnum2token: tnum to token string
 *
 * Assigns a tnum -- token number -- to each unique token string. Uses
 * a recno database hence there are no duplicates.
 */

//typedef tnum_t               tnum2tokendb_key;
//typedef char *               tnum2tokendb_val;

/**
 * token2tnum: token string to tnum
 *
 * Lookup the tnum assigned to a given token string.
 */

//typedef char *               token2tnumdb_key;
//typedef tnum_t               token2tnumdb_val;

/**
 * envtnum2pnodedb: environment tnum to pnode
 *
 * Find all pnodes that have an environment matching the given tnum
 * (i.e. that contain a token string associated with the given tnum in
 * their environment).
 */

//typedef tnum_t               envtnum2pnodedb_key;
//typedef pnode_t              envtnum2pnodedb_val;

/**
 * argtnum2pnodedb: argument tnum to pnode
 *
 * Find all pnodes that have arguments matching the given tnum
 * (i.e. that contain a token string associated with the given tnum in
 * their arguments).
 */

//typedef tnum_t               argtnum2pnodedb_key;
//typedef pnode_t              argtnum2pnodedb_val;

/**
 * argtnumbigramdb: argument token number bigram index
 *
 * Bigram index to facilitate searches on parameter pairs (such as -m 12).
 *
 * N.B. <b>NOT</b> yet implemented.
 */

//typedef tnum_pair            argtnumbigramdb_key;
//typedef pnode_t              argtnumbigramdb_val;

/*
 * i2pdb: inode-to-pnode index
 *
 * Lookup a pnode (value) by its inode (key). Since inodes are recycled over
 * time, a given inode may be associated with multiple pnodes. Thus this
 * database supports sorted duplicates.
 */
//typedef ino_t                i2pdb_key;
//typedef pnode_t              i2pdb_val;

/**
 * p2idb: pnode-to-inode index
 *
 * Given a pnode (key) find the inode (value) associated with the given pnode.
 * Since inodes are reused over time, more than one pnode could resolve to the
 * same inode. The reverse does not apply as pnodes are never recycled, thus a
 * pnode always maps to exactly one inode. As a result this database does
 * <b>NOT</b> support duplicates.
 */
//typedef pnode_t              p2idb_key;
//typedef ino_t                p2idb_val;

/**
 * namedb: Object name index
 *
 * Intended to facilitate looking up a pnode by file or process name. The key is
 * the name to lookup and the value is a pnode that matches the given name. This
 * database is sorted by lexographically (by name) and supports duplicates.
 *
 * N.B. The values should be <pnode,version> pairs but we do not presently
 * handle renames correctly. We presently assume that a files name never
 * changes, thus every pnode has exactly one name. We need to think about how we
 * want to do this as we probably do not want an entry in the name index for
 * every version of every named object.
 */

//typedef char *               namedb_key;
//typedef pnode_t              namedb_val;

/** TODO: Should be */
//typedef pnode_version        namedb_val;

/**
 * childdb: child index
 *
 * Intended to speed up descendant queries. The key contains the parent's
 * <pnode,version>. Each child appears as a separate tuple. This database is
 * sorted on pnode then version and allows duplicates.
 *
 */
//typedef pnode_version        childdb_key;
//typedef pnode_version        childdb_val;

/*
 * parentdb: parent index
 *
 * Intended to speed up ancestry queries. The key contains the child's
 * <pnode,version>. Each parent appears as a separate tuple. This database is
 * sorted on pnode then version and allows duplicates.
 */
//typedef pnode_version        parentdb_key;
//typedef pnode_version        parentdb_val;

/** @} */ /* Provenance */

/**
 * Security
 * @{
 */

/**
 * Map from relationship policy to a collection of relationship rules.
 *
 * A policy is a collection of rules. The relpolicydb associates each
 * policyid with a collection of rules for relationships. Since a rule
 * could be used in multiple policies, there is a many to many
 * relationship between policies and rules.
 */

//typedef policy_t             relpolicydb_key;
//typedef tnum_t               relpolicydb_val;

/**
 * Flags used in rel_rule_val.flags
 *
 * ADD              This is an attempt to add a new relationship
 */
#define REL_RULE_FLAG_ADD    (1<<0)

/**
 * Policy rules for relationship information.
 *
 * Revolves around the two relationship conditions: exists and traverse.
 * Each condition is in the sage query language and evaluates to a boolean
 * value. If exists returns false, the requestor has no permissions on this
 * relationship. Only if exists returns true is traverse tested. If traverse
 * returns false the user is limited to exists permissions whereas if both
 * exists and traverse return true the user has full traverse permissions on
 * this relationship.
 *
 * @note pv1 is guaranteed to not be greater than pv2.
 *       Ordering is determined by pnode with ties broken by version number.
 */
struct rel_rule_val {
   role_t                requestor;
//   role_t                recipient;
   pnode_version         pv1;
   pnode_version         pv2;
   uint32_t              flags;
   uint32_t              exists_len;
   char                 *exists_cond;
   char                 *traverse_cond;
} __attribute__((__packed__));

//typedef tnum_t               relruledb_key;
typedef struct rel_rule_val  relruledb_val;

/**
 * Map from attribute policy to a collection of attribute rules.
 *
 * A policy is a collection of rules. The attrpolicydb associates each
 * policyid with a collection of rules for attributes. Since a rule
 * could be used in multiple policies, there is a many to many
 * relationship between policies and rules.
 */

//typedef policy_t             attrpolicydb_key;
//typedef tnum_t               attrpolicydb_val;

/**
 * Flags used in attr_rule_val.flags
 *
 * ADD              This is an attempt to add a new attribute
 */
#define ATTR_RULE_FLAG_ADD    (1<<0)

/**
 * Policy rules for attribute information.
 *
 * Condition evaluates to a boolean, either the operation is allowed
 * or not.
 *
 * By default, this is an attempt to read an attribute. If the add flag is
 * set, this is an attempt to add an attribute.
 */
struct attr_rule_val {
   role_t                requestor;   /**< who is invoking the operation */
//   role_t                recipient;
   pnode_version         pv;          /**< pnode/value */
   provdb_val            val;         /**< the attribute */
   uint32_t              flags;       /**< flags */
   char                 *condition;   /**< condition evaluating to a boolean */
} __attribute__((__packed__));

//typedef tnum_t               attrruledb_key;
typedef struct attr_rule_val attrruledb_val;

/** @} */ /* Security */


#ifdef __cplusplus
extern "C" {
#endif

int inode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);
int rev_inode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);

int pnode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);
int rev_pnode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);

int tnum_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);
int rev_tnum_cmp(DB *db, const DBT *dbt1, const DBT *dbt2);

int pnode_ver_cmp(DB *db/* __attribute__((__unused__))*/,
                  const DBT *key1, const DBT *key2);

int rev_pnode_ver_cmp(DB *db, const DBT *key1, const DBT *key2);

int provdb_val_cmp(DB *db /*__attribute__((__unused__))*/,
                   const DBT *val1, const DBT *val2);

int rev_provdb_val_cmp(DB *db, const DBT *val1, const DBT *val2);

/* 
 * Get attribute name for the precord. The pointer returned may be to
 * local static storage overwritten on the next call.
 */
const char *provdb_val_attr(struct provdb_val *pdb);

const char *packed_get_name(enum prov_attrname_packed id);
enum prov_attrname_packed packed_get_attrcode(const char *name, uint16_t len);


#ifdef __cplusplus
}
#endif

#endif /* SCHEMA_H */
