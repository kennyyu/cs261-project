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
#include <string.h>
#include <assert.h>
#include <db.h>

#include "schema.h"

#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

#ifndef MIN
#define MIN(A,B) ( ((A) < (B)) ? (A) : (B) )
#endif /* MIN */

// We pack common attribute names into a uint16_t
// This code maps between the packed and unpacked forms of these attribute names
struct packed_attrnames {
   const char                *name; // unpacked format a "string"
   enum prov_attrname_packed  id;   // packed format -- a uint16_t
};

// This table maintains the mapping between the packed and unpacked forms
// of attribute names
static const struct packed_attrnames packed[] =
{
   { PROV_KEY_TYPE      , PROV_ATTR_TYPE       },
   { PROV_KEY_NAME      , PROV_ATTR_NAME       },
   { PROV_KEY_INODE     , PROV_ATTR_INODE      },
   { PROV_KEY_PATH      , PROV_ATTR_PATH       },
   { PROV_KEY_ARGV      , PROV_ATTR_ARGV       },
   { PROV_KEY_ENV       , PROV_ATTR_ENV        },
   { PROV_KEY_FREEZETIME, PROV_ATTR_FREEZETIME },
   { PROV_KEY_INPUT     , PROV_ATTR_INPUT      }
};

#define NUM_PACKED (sizeof(packed) / sizeof(packed[0]))

/**
 * Convert a packed representation into its unpacked equivalent.
 *
 * @returns on success the unpacked form -- a string
 *          on failure (not found) NULL
 */
const char *packed_get_name(enum prov_attrname_packed id)
{
   uint32_t i;

   for ( i = 0; i < NUM_PACKED; ++i ) {
      if ( packed[i].id == id ) {
         return packed[i].name;
      }
   }

   return NULL;
}

/**
 * Convert an unpacked representation into its packed equivalent.
 */
enum prov_attrname_packed packed_get_attrcode(const char *name, uint16_t len)
{
   uint32_t i;

   for ( i = 0; NUM_PACKED > i; ++i ) {
      if ( 0 == strncmp(packed[i].name, name, len) ) {
         return packed[i].id;
      }
   }

   return PROV_ATTR_INVALID;
}

const char *provdb_val_attr(struct provdb_val *pdb) {
   if (pdb->pdb_flags & PROVDB_PACKED) {
      // Packed.
      return packed_get_name(pdb->pdb_attrcode);
   }

   // Not packed.
   static char *retptr;
   static size_t retptrlen;

   // make space
   if (pdb->pdb_attrlen >= retptrlen) {
      size_t newlen = retptrlen ? retptrlen*2 : 16;
      while (newlen <= pdb->pdb_attrlen) {
	 newlen *= 2;
      }
      char *tmp = realloc(retptr, newlen);
      if (!tmp) {
	 return NULL;
      }
      retptr = tmp;
      retptrlen = newlen;
   }

   // copy and null-terminate
   memcpy(retptr, pdb->pdb_data, pdb->pdb_attrlen);
   retptr[pdb->pdb_attrlen] = 0;

   return retptr;
}

/*
 * Compare two blocks of possibly different sizes.
 * If one is a prefix of the other, the shorter block sorts first.
 */
static int domemcmp(const void *p1, size_t len1, const void *p2, size_t len2) {
   size_t commonlen;
   int ret;

   commonlen = MIN(len1, len2);
   ret = memcmp(p1, p2, commonlen);
   if (ret) {
      return ret;
   }

   /* Same prefix, go by size */
   if (len1 < len2) {
      return -1;
   }
   if (len1 > len2) {
      return 1;
   }

   return 0;
}

/**
 * BDB comparison function for inodes.
 */
int inode_cmp(DB *db UNUSED, const DBT *dbt1, const DBT *dbt2)
{
   lasagna_ino_t   inode1;
   lasagna_ino_t   inode2;

   assert(sizeof(inode1) == dbt1->size);
   assert(sizeof(inode2) == dbt2->size);

   memcpy(&inode1, dbt1->data, dbt1->size);
   memcpy(&inode2, dbt2->data, dbt2->size);

   if ( inode1 < inode2 ) {
      return -1;
   }

   if ( inode1 > inode2 ) {
      return 1;
   }

   return 0;
}

int rev_inode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2)
{
   // notice negative sign
   return - inode_cmp(db, dbt1, dbt2);
}

/**
 * BDB comparison function for pnodes.
 */
int pnode_cmp(DB *db UNUSED, const DBT *dbt1, const DBT *dbt2)
{
   pnode_t         pnode1;
   pnode_t         pnode2;

   assert(sizeof(pnode1) == dbt1->size);
   assert(sizeof(pnode2) == dbt2->size);

   memcpy(&pnode1, dbt1->data, dbt1->size);
   memcpy(&pnode2, dbt2->data, dbt2->size);

   if ( pnode1 < pnode2 ) {
      return -1;
   }

   if ( pnode1 > pnode2 ) {
      return 1;
   }

   return 0;
}

int rev_pnode_cmp(DB *db, const DBT *dbt1, const DBT *dbt2)
{
   // notice negative sign
   return - pnode_cmp(db, dbt1, dbt2);
}

/**
 * BDB comparison function for tnums.
 */
int tnum_cmp(DB *db UNUSED, const DBT *dbt1, const DBT *dbt2)
{
   tnum_t     tnum1;
   tnum_t     tnum2;

   assert(sizeof(tnum1) == dbt1->size);
   assert(sizeof(tnum2) == dbt2->size);

   memcpy(&tnum1, dbt1->data, dbt1->size);
   memcpy(&tnum2, dbt2->data, dbt2->size);

   if ( tnum1 < tnum2 ) {
      return -1;
   }

   if ( tnum1 > tnum2 ) {
      return 1;
   }

   return 0;
}

int rev_tnum_cmp(DB *db UNUSED, const DBT *dbt1, const DBT *dbt2)
{
   // notice negative sign
   return - tnum_cmp(db, dbt1, dbt2);
}

/**
 * BDB btree comparison function for provenance database.
 *
 * Ordering the entries by <pnum,version> should perform better than a
 * lexographical sort.
 * Just a guess, but I would be suprised if I am wrong.
 *
 * @todo check that this really improves performance
 */
int pnode_ver_cmp(DB *db UNUSED, const DBT *key1, const DBT *key2)
{
   provdb_key pdb_key1;
   provdb_key pdb_key2;
   int32_t    diff;
   int16_t    pnum_diff;
   int16_t    version_diff;

   assert(sizeof(pdb_key1) == key1->size);
   assert(sizeof(pdb_key2) == key2->size);

   memcpy(&pdb_key1, key1->data, key1->size);
   memcpy(&pdb_key2, key2->data, key2->size);

   pnum_diff    = pdb_key1.pnum - pdb_key2.pnum;
   version_diff = pdb_key1.version - pdb_key2.version;

   diff = (pnum_diff << 16) + version_diff;

   return (int)diff;
}

int rev_pnode_ver_cmp(DB *db, const DBT *key1, const DBT *key2)
{
   // notive negative sign
   return - pnode_ver_cmp(db, key1, key2);
}

/**
 * BDB sort comparison function for provenance database.
 *
 * Specify an ordering for the duplicates.
 */
int provdb_val_cmp(DB *db UNUSED, const DBT *val1, const DBT *val2)
{
   provdb_val *pdbval1;
   provdb_val *pdbval2;
   const char *attrname1;
   const char *attrname2;
   size_t      attrlen1;
   size_t      attrlen2;
   unsigned	type1;
   unsigned	type2;
   unsigned char *value1;
   unsigned char *value2;
   size_t      valuelen1;
   size_t      valuelen2;

   int         ret;

   /*
    * Sanity checks
    */
   assert( sizeof(*pdbval1) < val1->size );
   assert( sizeof(*pdbval2) < val2->size );

   pdbval1 = val1->data;
   pdbval2 = val2->data;

   PROVDB_VAL_GETATTRPTR(pdbval1, &attrname1, &attrlen1);
   PROVDB_VAL_GETATTRPTR(pdbval2, &attrname2, &attrlen2);

   /* Note that attrname{1,2} are not null-terminated. */

   /* Compare the attribute names. */
   ret = domemcmp(attrname1, attrlen1, attrname2, attrlen2);
   if (ret) {
      return ret;
   }

   /* Attributes are the same; now we need to do the values. Type first. */
   type1 = PROVDB_VAL_VALUETYPE(pdbval1);
   type2 = PROVDB_VAL_VALUETYPE(pdbval2);
   if (type1 < type2) {
      return -1;
   }
   if (type1 > type2) {
      return 1;
   }

   /*
    * Same type; compare the data. I'm just going to do memcmp rather than
    * switching by type and comparing the values as values.
    */
   valuelen1 = PROVDB_VAL_VALUELEN(pdbval1);
   valuelen2 = PROVDB_VAL_VALUELEN(pdbval2);
   value1 = PROVDB_VAL_VALUE(pdbval1);
   value2 = PROVDB_VAL_VALUE(pdbval2);

   ret = domemcmp(value1, valuelen1, value2, valuelen2);
   if (ret) {
      return ret;
   }

   /* Same value; all we can go by is the flags. Doesn't mean much. */
   if (pdbval1->pdb_flags < pdbval2->pdb_flags) {
      return -1;
   }
   if (pdbval1->pdb_flags > pdbval2->pdb_flags) {
      return 1;
   }

   return 0;
}

int rev_provdb_val_cmp(DB *db, const DBT *val1, const DBT *val2)
{
   // notice negative sign
   return - provdb_val_cmp(db, val1, val2);
}
