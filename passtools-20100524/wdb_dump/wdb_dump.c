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
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <assert.h>

#include <db.h>

#include "twig.h"
#include "schema.h"
#include "wdb.h"

static void dump_provdb(void);
static void dump_tnum2tok(void);
static void dump_tok2tnum(void);
static void dump_arg2p(void);
static void dump_env2p(void);
static void dump_p2i(void);
static void dump_i2p(void);
static void dump_name(void);
static void dump_child(void);
static void dump_parent(void);

static void print_attrflags(uint8_t attrflags);

////////////////////////////////////////////////////////////////////////////////

static void usage(char *name)
{
   fprintf(stderr,
           "usage: %s [-h] [<path>]\n"
           "   -h             usage\n"
           "   if no file is specified assume \".\"",
           name );
}

/**
 * The main method for the wdb dump application.
 */
int
main(int argc, char *argv[])
{
   const  char          *path = ".";
   int                   ch;

   //
   // Parse options
   //
   while ( -1 != (ch = getopt(argc, argv, "h")) ) {
      switch ( ch ) {
         case 'h':
         default:
            usage(argv[0]);
            return 0;
      }
   }

   // extra params -> print usage
   if ( optind == argc - 1 ) {
      path = argv[optind];
   }

   //
   // Initialize all 'databases' (database & log)
   //

   // BDB
   wdb_startup(path, WDB_O_RDONLY);

   //
   // Dump everything
   //
   dump_tnum2tok();
   dump_tok2tnum();
   dump_arg2p();
   dump_env2p();
   dump_i2p();
   dump_p2i();
   dump_name();
   dump_child();
   dump_parent();
   dump_provdb();

   //
   // Shut everything down (database & log)
   //
   wdb_shutdown();

   exit(0);
}

static void printstring(const char *s, size_t len) {
   size_t i;
   putchar('"');
   for (i=0; i<len; i++) {
      unsigned char c = s[i];
      switch (c) {
       case '"':
       case '\\':
	putchar('\\');
	putchar(c);
	break;
       case '\a': putchar('\\'); putchar('a'); break;
       case '\b': putchar('\\'); putchar('b'); break;
       case '\t': putchar('\\'); putchar('t'); break;
       case '\n': putchar('\\'); putchar('n'); break;
       case '\v': putchar('\\'); putchar('v'); break;
       case '\f': putchar('\\'); putchar('f'); break;
       case '\r': putchar('\\'); putchar('r'); break;
       default:
	if (isascii(c) && (isprint(c) || isspace(c))) {
	   putchar(c);
	}
	else {
	   printf("\\%03o", c);
	}
	break;
      }
   }
   putchar('"');
}

static void printtoken(tnum_t tok) {
   DBT  dbt_tnum;
   DBT  dbt_token;
   int  ret;

   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_token, 0, sizeof(dbt_token));

   dbt_tnum.data = &tok;
   dbt_tnum.size = sizeof(tok);

   printf(" [%u]", tok);

   ret = g_tnum2tokdb->db->get(g_tnum2tokdb->db, NULL,
                               &dbt_tnum, &dbt_token,
                               0);
   wdb_check_err(ret);

   printstring(dbt_token.data, dbt_token.size);
}

#ifndef __NetBSD__ /* in netbsd's libc these days */
static size_t strnlen(const char *s, size_t maxlen) {
   const char *t = memchr(s, 0, maxlen);
   return t ? (size_t)(t-s) : maxlen;
}
#endif

void dump_provdb(void)
{
   DBC                       *pdbc = NULL;
   DBT                        dbt_key;
   DBT                        dbt_val;
   provdb_key                 key;
   provdb_val                *val;
   struct pnode_version      *ref;
   enum prov_attrname_packed  id = PROV_ATTR_INVALID;
   const char                *attrname = NULL;
   char                      *data;
   size_t                     i, datalen;
   int                        serrno;
   int                        ret;

   printf( "----------------------------------------\n"
           "| Dumping provdb                       |\n"
           "|                                      |\n"
           "----------------------------------------\n" );

   memset(&dbt_key, 0, sizeof(dbt_key));
   memset(&dbt_val, 0, sizeof(dbt_val));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_provdb->db->cursor(g_provdb->db, NULL, &pdbc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = pdbc->c_get(pdbc, &dbt_key, &dbt_val, DB_NEXT)) ) {
      id = PROV_ATTR_INVALID;
      val = malloc(dbt_val.size);
      if ( val == NULL ) {
	serrno = errno;
	fprintf( stderr, "Malloc of provdb record failed %s\n",
		 strerror(serrno) );
	errno = serrno;
	// XXX clean up such as closing the cursor
	return;
      }

      memcpy(&key, dbt_key.data, sizeof(key));
      memcpy(val, dbt_val.data, dbt_val.size);

      assert( sizeof(key) == dbt_key.size );
#if DEBUG
      if ( sizeof(*val) >= dbt_val.size ) {
          fprintf( stderr,
                   "sizeof(*val) %lu dbt_val.size %d sizeof(provdb_val) %lu\n"
                   "key: %lu.%u\n"
                   "val: flags %02X valtype %02X code/len %02X/%d vallen %u\n",
                   sizeof(*val), dbt_val.size, sizeof(provdb_val),
                   key.pnum, key.version,
                   val->pdb_flags, val->pdb_valuetype,
                   val->pdb_attrcode, val->pdb_attrlen,
                   val->pdb_valuelen );
      }
#endif /* DEBUG */
      assert( sizeof(*val) <= dbt_val.size );

      attrname = PROVDB_VAL_ATTR(val);
      data = PROVDB_VAL_VALUE(val);
      datalen = PROVDB_VAL_VALUELEN(val);

      print_attrflags(val->pdb_flags);

      printf("%llu.%u %s %u ",
	     (unsigned long long)key.pnum, key.version,
             attrname, (unsigned)datalen);

      switch (PROVDB_VAL_VALUETYPE(val)) {

       case PROV_TYPE_NIL:
	printf("---\n");
	break;

       case PROV_TYPE_STRING:
	if (PROVDB_VAL_ISTOKENIZED(val)) {
	   tnum_t tok = *(tnum_t *) data;

	   assert(datalen == sizeof(tnum_t));
	   tok = *(tnum_t *) data;
	   printtoken(tok);
	   printf("\n");
	}
	else {
	   printstring(data, datalen);
	   printf("\n");
	}
	break;

       case PROV_TYPE_MULTISTRING:
	if (PROVDB_VAL_ISTOKENIZED(val)) {
	   int ntoks, i;
	   tnum_t *toks;

	   assert(datalen % sizeof(tnum_t) == 0);
	   ntoks = datalen / sizeof(tnum_t);
	   toks = malloc(datalen);
	   memcpy(toks, data, datalen);

	   for (i=0; i<ntoks; i++) {
	      printtoken(toks[i]);
	   }
	   free(toks), toks = NULL;
	}
	else {
	   size_t x, y;
	   for (x = 0; x < datalen; x += y+1) {
	      if (x > 0) printf(" ");
	      y = strnlen(data+x, datalen-x);
	      printstring(data+x, y);
	   }
	}
	printf("\n");
	break;

       case PROV_TYPE_INT:
	assert(datalen == sizeof(int32_t));
	printf( "INT %d\n", *(int32_t *)data );
	break;

       case PROV_TYPE_REAL:
	assert(datalen == sizeof(double));
	printf( "REAL %g\n", *(double *)data );
	break;

       case PROV_TYPE_TIMESTAMP:
	assert(datalen == sizeof(struct prov_timestamp));
	printf( "TIME %u.%09u\n",
		((struct prov_timestamp *)data)->pt_sec,
		((struct prov_timestamp *)data)->pt_nsec);
	break;

       case PROV_TYPE_INODE:
	assert(datalen == sizeof(uint32_t));
	printf( "INODE %u\n", *(uint32_t *)data );
	break;

       case PROV_TYPE_PNODE:
	assert(datalen == sizeof(pnode_t));
	printf( "--> %llu\n", (unsigned long long)*(pnode_t *)data);
	break;

       case PROV_TYPE_PNODEVERSION:
	assert(datalen == sizeof(struct pnode_version));
	ref = (struct pnode_version *)data;
	printf( "--> %llu.%u\n",
		(unsigned long long)ref->pnum, ref->version );
	break;

       case PROV_TYPE_OBJECT:
       case PROV_TYPE_OBJECTVERSION:
       default:
	printf("[illegal value type %u]", PROVDB_VAL_VALUETYPE(val));
	for (i=0; i<datalen; i++) {
	   printf(" %02x", (unsigned char) data[i]);
	}
	printf("\n");
      }

      free(val), val = NULL;
   }

   // XXX shouldn't this, like, close the cursor?

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

static void print_attrflags(uint8_t attrflags) {
   printf("[%c%c%c] ",
	  ((attrflags & PROVDB_ANCESTRY)        ? 'A' : ' '),
	  ((attrflags & PROVDB_PACKED)          ? 'P' : ' '),
	  ((attrflags & PROVDB_TOKENIZED)       ? 'T' : ' '));
}

void dump_tnum2tok(void)
{
   DBC                  *tnum2tokc = NULL;
   DBT                   dbt_tnum;
   DBT                   dbt_token;
   db_recno_t            tnum;
   char                 *token;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping tnum2token                   |\n"
           "| tnum -> token                        |\n"
           "----------------------------------------\n" );

   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_token, 0, sizeof(dbt_token));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_tnum2tokdb->db->cursor(g_tnum2tokdb->db, NULL, &tnum2tokc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = tnum2tokc->c_get(tnum2tokc, &dbt_tnum, &dbt_token, DB_NEXT)) ) {
      assert( sizeof(tnum) == dbt_tnum.size );

      token = malloc(dbt_token.size + 1);
      memset(token, 0, dbt_token.size + 1);
      memcpy(&tnum, dbt_tnum.data, dbt_tnum.size);
      memcpy(token, dbt_token.data, dbt_token.size);

      printf( "%u %s\n", tnum, token );
      free(token), token = NULL;
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_tok2tnum(void)
{
   DBC                  *tok2tnumc = NULL;
   DBT                   dbt_tnum;
   DBT                   dbt_token;
   db_recno_t            tnum;
   char                 *token;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping token2tnum                   |\n"
           "| token -> tnum                        |\n"
           "----------------------------------------\n" );

   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_token, 0, sizeof(dbt_token));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_tok2tnumdb->db->cursor(g_tok2tnumdb->db, NULL, &tok2tnumc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = tok2tnumc->c_get(tok2tnumc, &dbt_token, &dbt_tnum, DB_NEXT)) ) {
      assert( sizeof(tnum) == dbt_tnum.size );

      token = malloc(dbt_token.size + 1);
      memset(token, 0, dbt_token.size + 1);
      memcpy(&tnum, dbt_tnum.data, dbt_tnum.size);
      memcpy(token, dbt_token.data, dbt_token.size);

      printf( "%s -> %u\n", token, tnum );
      free(token), token = NULL;
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_arg2p(void)
{
   DBC                  *arg2pc = NULL;
   DBT                   dbt_tnum;
   DBT                   dbt_pnode;
   db_recno_t            tnum;
   pnode_t               pnum;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping arg tnum -> pnode            |\n"
           "| arg tnum -> pnode                    |\n"
           "----------------------------------------\n" );

   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_arg2pdb->db->cursor(g_arg2pdb->db, NULL,
                               &arg2pc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = arg2pc->c_get(arg2pc, &dbt_tnum, &dbt_pnode, DB_NEXT)) ) {
      assert( sizeof(tnum) == dbt_tnum.size );
      assert( sizeof(pnum) == dbt_pnode.size );

      memcpy(&tnum, dbt_tnum.data, dbt_tnum.size);
      memcpy(&pnum , dbt_pnode.data, dbt_pnode.size);

      printf( "%u -> %llu\n", (unsigned)tnum, (unsigned long long)pnum );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_env2p(void)
{
   DBC                  *env2pc = NULL;
   DBT                   dbt_tnum;
   DBT                   dbt_pnode;
   db_recno_t            tnum;
   pnode_t               pnum;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping env tnum -> pnode            |\n"
           "| env tnum -> pnode                    |\n"
           "----------------------------------------\n" );

   memset(&dbt_tnum, 0, sizeof(dbt_tnum));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_env2pdb->db->cursor(g_env2pdb->db, NULL,
                               &env2pc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = env2pc->c_get(env2pc, &dbt_tnum, &dbt_pnode, DB_NEXT)) ) {
      assert( sizeof(tnum) == dbt_tnum.size );
      assert( sizeof(pnum) == dbt_pnode.size );

      memcpy(&tnum, dbt_tnum.data, dbt_tnum.size);
      memcpy(&pnum , dbt_pnode.data, dbt_pnode.size);

      printf( "%u -> %llu\n", (unsigned)tnum, (unsigned long long)pnum );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_name(void)
{
   DBC                  *namec = NULL;
   DBT                   dbt_name;
   DBT                   dbt_pnode;
   char                 *name;
   pnode_t               pnum;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping name                         |\n"
           "| name -> pnode                        |\n"
           "----------------------------------------\n" );

   memset(&dbt_name , 0, sizeof(dbt_name));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_namedb->db->cursor(g_namedb->db, NULL, &namec, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = namec->c_get(namec, &dbt_name, &dbt_pnode, DB_NEXT)) ) {
      assert( sizeof(pnum) == dbt_pnode.size );

      name = malloc(dbt_name.size + 1);
      memcpy(name, dbt_name.data, dbt_name.size);
      name[dbt_name.size] = 0;
      memcpy(&pnum, dbt_pnode.data, dbt_pnode.size);

      printf( "%s -> %llu\n", name, (unsigned long long)pnum );

      free(name), name = NULL;
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_i2p(void)
{
   DBC                  *i2pc = NULL;
   DBT                   dbt_inode;
   DBT                   dbt_pv;
   uint32_t              inode;
   struct pnode_version  pv;
   pnode_t               pnum;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping i2p                          |\n"
           "| ino -> pnode.version                 |\n"
           "----------------------------------------\n" );

   memset(&dbt_inode, 0, sizeof(dbt_inode));
   memset(&dbt_pv   , 0, sizeof(dbt_pv));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_i2pdb->db->cursor(g_i2pdb->db, NULL, &i2pc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = i2pc->c_get(i2pc, &dbt_inode, &dbt_pv, DB_NEXT)) ) {
      assert( sizeof(inode) == dbt_inode.size );
      assert( sizeof(pv)    == dbt_pv.size );

      memcpy(&inode, dbt_inode.data, dbt_inode.size);
      memcpy(&pnum , dbt_pv.data   , dbt_pv.size);

      printf( "%u -> %" PRIu64 ".%"  PRIu32 "\n",
              inode, pv.pnum, pv.version );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_p2i(void)
{
   DBC                  *p2ic = NULL;
   DBT                   dbt_inode;
   DBT                   dbt_pnode;
   uint32_t              inode;
   pnode_t               pnum;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping p2i                          |\n"
           "| pnode -> in                          |\n"
           "----------------------------------------\n" );

   memset(&dbt_inode, 0, sizeof(dbt_inode));
   memset(&dbt_pnode, 0, sizeof(dbt_pnode));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_p2idb->db->cursor(g_p2idb->db, NULL, &p2ic, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = p2ic->c_get(p2ic, &dbt_pnode, &dbt_inode, DB_NEXT)) ) {
      assert( sizeof(inode) == dbt_inode.size );
      assert( sizeof(pnum)  == dbt_pnode.size );

      memcpy(&inode, dbt_inode.data, dbt_inode.size);
      memcpy(&pnum , dbt_pnode.data, dbt_pnode.size);

      printf( "%llu -> %u\n", (unsigned long long)pnum, inode );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_child(void)
{
   DBC                  *childc = NULL;
   DBT                   dbt_parent;
   DBT                   dbt_child;
   provdb_key            parent;
   provdb_key            child;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping child                        |\n"
           "| parent -> child                      |\n"
           "----------------------------------------\n" );

   memset(&dbt_parent, 0, sizeof(dbt_parent));
   memset(&dbt_child , 0, sizeof(dbt_child));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_childdb->db->cursor(g_childdb->db, NULL, &childc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = childc->c_get(childc, &dbt_parent, &dbt_child, DB_NEXT)) ) {
      assert( sizeof(parent) == dbt_parent.size );
      assert( sizeof(child)  == dbt_child.size );

      memcpy(&parent, dbt_parent.data, dbt_parent.size);
      memcpy(&child , dbt_child.data , dbt_child.size);

      printf( "%llu.%u -> %llu.%u\n",
              (unsigned long long)parent.pnum, parent.version,
              (unsigned long long)child.pnum , child.version );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}

void dump_parent(void)
{
   DBC                  *parentc = NULL;
   DBT                   dbt_child;
   DBT                   dbt_parent;
   provdb_key            child;
   provdb_key            parent;
   int                   ret;

   printf( "----------------------------------------\n"
           "| Dumping parent                        |\n"
           "| child -> parent                      |\n"
           "----------------------------------------\n" );

   memset(&dbt_child, 0, sizeof(dbt_child));
   memset(&dbt_parent , 0, sizeof(dbt_parent));

   //
   // Process the entries
   //

   // Open a cursor
   ret = g_parentdb->db->cursor(g_parentdb->db, NULL, &parentc, 0);
   wdb_check_err(ret);

   // Iterate over the main database
   while ( 0 == (ret = parentc->c_get(parentc, &dbt_child, &dbt_parent, DB_NEXT)) ) {
      assert( sizeof(child) == dbt_child.size );
      assert( sizeof(parent)  == dbt_parent.size );

      memcpy(&child , dbt_child.data , dbt_child.size );
      memcpy(&parent, dbt_parent.data, dbt_parent.size);

      printf( "%llu.%u -> %llu.%u\n",
              (unsigned long long)child.pnum, child.version,
              (unsigned long long)parent.pnum , parent.version );
   }

   if ( DB_NOTFOUND != ret ) {
      wdb_check_err(ret);
   }
}
