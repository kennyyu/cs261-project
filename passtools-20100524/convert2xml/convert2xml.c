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
#include <unistd.h>
#include <getopt.h>
#include <err.h>
#include <assert.h>
#include <db.h>

#include "debug.h"
#include "schema.h"
#include "wdb.h"

#ifndef MIN
#define MIN(A,B) ( ((A) < (B)) ? (A) : (B) )
#endif /* MIN */

////////////////////////////////////////////////////////////////////////////////

struct string_repl {
   const char *old;
   const char *new;
};

struct string_repl xml_escapes[] =
{
   { "&" , "&amp;"  },
   { "<" , "&lt;"   },
   { ">" , "&gt;"   },
   { "'" , "&apos;" },
   { "\"", "&quot;" }
};

struct string_repl cntrl_chars[] =
{
   { "\a", "&#07;"  },
   { "\b", "&#08;"  },
   { "\f", "&#0C;"  },
   { "\n", "&#0A;"  },
   { "\r", "&#0D;"  },
   { "\t", "&#09;"  },
   { "\v", "&#0B;"  }
};

struct string_repl data_substs[] =
{
   { "<pipe>"  , "PIPE"   },
   { "<stdin>" , "STDIN"  },
   { "<stdout>", "STDOUT" },
   { "<stderr>", "STDERR" },
   { "<socket>", "SOCKET" }
};

#define NUM_XML_ESC      (sizeof(xml_escapes) / sizeof(xml_escapes[0]))
#define NUM_CNTRL_CHARS  (sizeof(cntrl_chars) / sizeof(cntrl_chars[0]))
#define NUM_DATA_SUBSTS  (sizeof(data_substs) / sizeof(data_substs[0]))

////////////////////////////////////////////////////////////////////////////////

/**
 * Replace all occurences of 'old' with 'new' in 'orig'.
 *
 * @param[in]  orig      original string on which to make substitutions
 * @param[in]  old       string to search for
 * @param[in]  new       string to insert in its place
 */
static char *strrepl(char* orig, const char *old, const char *new)
{
    char                     *temp;
    char                     *found;
    char                     *curstr;
    char                     *p;
    int                       idx;

    curstr = malloc( 1 + strlen(orig) );
    assert( NULL != curstr );

    strcpy(curstr, orig);

    p = curstr;
    while ( 0 != (found = strstr(p, old)) ) {

	idx = found - curstr;

	temp = malloc(strlen(curstr) - strlen(old) + strlen(new) + 1);
	assert( NULL != temp );

	strncpy(temp, curstr, idx);
	strcpy(temp + idx, new);
	strcpy(temp + idx + strlen(new), curstr + idx + strlen(old));
	
	free(curstr), curstr = NULL;
	curstr = temp;
	p = curstr + idx + strlen(new);
    }

    return curstr;
}

/**
 * Make a series of subsitutions in a string.
 *
 * @param[in]  str       original string on which to make substitutions
 * @param[in]  tbl       table of replacements
 * @param[in]  num_rows  number of rows in table of replacements
 */
static char *replace_strings(char *str, struct string_repl tbl[], int num_rows)
{
   int                        i;
   char                      *retstr;

   retstr = str;

   for ( i = 0; i < num_rows; ++i ) {
      retstr = strrepl(retstr, tbl[i].old, tbl[i].new);
   }

   return retstr;
}

/**
 * Make substitutions in xml escapes table.
 *
 * @param[in]  str       original string on which to make substitutions
 */
static char *subst_xml_escapes(char *str)
{
   return replace_strings(str, xml_escapes, NUM_XML_ESC);
}

/**
 * Make substitutions in control characters table.
 *
 * @param[in]  str       original string on which to make substitutions
 */
static char *subst_cntrl_chars(char *str)
{
   return replace_strings(str, cntrl_chars, NUM_CNTRL_CHARS);
}

/**
 * Make substitutions in data subst table.
 *
 * @param[in]  str       original string on which to make substitutions
 */
static char *subst_data(char *str)
{
   return replace_strings(str, data_substs, NUM_DATA_SUBSTS);
}

////////////////////////////////////////////////////////////////////////////////

static void db_mapcar(DB *db, 
		      void (*pf)(void *kp, size_t kl, void *vp, size_t vl))
{
    DBC                      *dbc;
    DBT                       key;
    DBT                       data;
    int                       r;

    r = db->cursor(db, NULL, &dbc, 0);
    if (r) {
	db->err(db, r, "db->cursor");
	db->close(db, 0);
	exit(1);
    }

    memset(&key , 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    while ( 0 == (r = dbc->c_get(dbc, &key, &data, DB_NEXT)) ) {
	pf(key.data, key.size, data.data, data.size);
    }

    dbc->c_close(dbc);
}

////////////////////////////////////////////////////////////////////////////////

static void print_string(const char *data, size_t datalen) {
   char *tmp; // so.. sue me

   tmp  = malloc(datalen + 1);
   assert (tmp != NULL);
   memcpy(tmp, data, datalen);
   tmp[datalen] = '\0';

   tmp = subst_data(tmp);
   tmp = subst_xml_escapes(tmp);
#if 1
   tmp = subst_cntrl_chars(tmp);
#endif
   printf( "%s\n", tmp );

   free(tmp), tmp = NULL;
}

static void print_strings(const char *block, size_t blocklen) {
   const char *data = block;
   size_t datalen = blocklen;
   const char *s;
   size_t len;

   while ((s = memchr(data, 0, datalen))!= NULL) {
      len = s-data;
      print_string(data, len);
      s += len+1;
      assert(datalen >= len+1);
      datalen -= len+1;
   }
   print_string(data, datalen);
}

static void print_tokens(tnum_t *toks_x, int ntoks) {
   tnum_t   *toks;
   int       i;
   char     *token;

   /* Copy the tokens in case db activity drops the buffer */
   toks = malloc ( ntoks * sizeof(tnum_t) );
   assert( NULL != toks );
   memcpy( toks, toks_x, ntoks * sizeof(tnum_t) );

   for (i=0; i<ntoks; i++) {
      wdb_lookup_token( &toks[i], NULL, &token );

      token = subst_xml_escapes(token);
#if 1
      token = subst_cntrl_chars(token);
#endif
      printf( "%s ", token );

      free( token ), token = NULL;
   }
   printf("\n");


   free(toks), toks = NULL;
}


/*
 * pass.db:    pass_key -> string or strings
 *
 * The pass_key is a pnode number plus a one-byte attribute code.
 * All strings are stored null-terminated.
 *
 * The pnode number is a u64.
 */
static void print_pass(void *kptr, size_t klen, void *vptr, size_t vlen) {
   provdb_key                *key;
   provdb_val                *val;
   void                      *data;
   size_t                     datalen;
   static provdb_key          static_key;
   static int                 first_record = 1;
   const char                *rectype;

   //
   // Initialization
   //
   key = kptr;
   val = vptr;

   //
   // Sanity checks
   //
   assert( sizeof(*key) == klen );
   assert( sizeof(*val) <= vlen );

   //
   // Beginning of real work
   //
   if (first_record) {
      printf( "<provenance pnode=\"%llu\" version=\"%u\">\n",
              (uint64_t)(key->pnum), (uint32_t)(key->version) );
      first_record = 0;
      static_key = *key;
   } 

   if ((key->pnum != static_key.pnum)||(key->version != static_key.version)) {
      printf("</provenance>\n"
             "<provenance pnode=\"%llu\" version=\"%u\">\n",
             (uint64_t) key->pnum, (uint32_t) key->version);
      static_key = *key;
   }


   rectype = PROVDB_VAL_ATTR(val);
   data = PROVDB_VAL_VALUE(val);
   datalen = PROVDB_VAL_VALUELEN(val);
   
   printf( "<record>\n"
           "<record-type>%s</record-type>\n"
           "<record-data>\n",
//           "  <record-type>%s</record-type>\n"
//           "  <record-data>\n",
           rectype );

   switch (PROVDB_VAL_VALUETYPE(val)) {

    case PROV_TYPE_NIL:
     break;

    case PROV_TYPE_STRING:
    case PROV_TYPE_MULTISTRING:
     printf( "<data>\n" );
     if (PROVDB_VAL_ISTOKENIZED(val)) {
	assert(datalen % sizeof(tnum_t) == 0);
	print_tokens(data, datalen / sizeof(tnum_t));
     }
     else {
	print_strings(data, datalen);
     }
     printf( "</data>\n" );
     break;

    case PROV_TYPE_INT:
     printf("<data>\n%d\n</data>\n", *(int32_t *)data);
     break;

    case PROV_TYPE_REAL:
     printf("<data>\n%g\n</data>\n", *(double *)data);
     break;

    case PROV_TYPE_TIMESTAMP:
     printf("<data>\n%lld.%09ld\n</data>\n", 
	    (long long) ((struct prov_timestamp *)data)->pt_sec,
	    (long) ((struct prov_timestamp *)data)->pt_nsec);
     break;

    case PROV_TYPE_INODE:
     printf("<data>\n%u\n</data>\n", *(uint32_t *)data);
     break;

    case PROV_TYPE_PNODE:
     printf( "<xref pnode=\"%llu\" version=\"0\"/>\n",
	     (unsigned long long) *(pnode_t *)data);
     break;

    case PROV_TYPE_PNODEVERSION:
    {
      provdb_key data_key;
      assert( sizeof(data_key) == datalen );
      memcpy(&data_key, data, datalen);

//      printf( "    <xref pnode=\"%llu\" version=\"%u\"/>\n",
      printf( "<xref pnode=\"%llu\" version=\"%u\"/>\n",
              (unsigned long long) data_key.pnum,
	      (unsigned) data_key.version );
    }
    break;

    case PROV_TYPE_OBJECT:
    case PROV_TYPE_OBJECTVERSION:
    default:
     assert(0);
     break;
   }

//   printf( "  </record-data>\n"
   printf( "</record-data>\n"
           "</record>\n" );
}

////////////////////////////////////////////////////////////////////////////////

static void print_ancestry(void *kptr, size_t klen,  void *vptr, size_t vlen)
{
   provdb_key             *key;
   provdb_val             *val;
   provdb_key             *data_key;

   //
   // Sanity checks
   //
   assert( sizeof(*key) == klen );
   assert( sizeof(*val) <= vlen );

   //
   // Initialization
   //
   key = kptr;
   val = vptr;

   //
   // Beginning of real work
   //
   if ( (PROVDB_VAL_VALUETYPE(val) == PROV_TYPE_PNODEVERSION) && 
        (PROVDB_VAL_ISANCESTRY(val)) )
   {
      assert(PROVDB_VAL_VALUELEN(val) == sizeof(*data_key));
      data_key = PROVDB_VAL_VALUE(val);
	
      printf( "%llu:%u --> %llu:%u\n",
              (uint64_t)key->pnum, (uint32_t)key->version,
              (uint64_t)data_key->pnum, (uint32_t)data_key->version );
   }
}

////////////////////////////////////////////////////////////////////////////////

static void print_pnodes(void *kptr, size_t klen, 
			 void *vptr __attribute__((__unused__)), 
			 size_t vlen __attribute__((__unused__)))
{
   provdb_key   *key;

   //
   // Sanity checks
   //
   assert( sizeof(*key) == klen );

   //
   // Initialization
   //
   key = kptr;

   printf( "%llu:%u\n",
           (uint64_t)(key->pnum), (uint32_t)(key->version) );
}

////////////////////////////////////////////////////////////////////////////////

static void print_identity(void *kptr, size_t klen, void *vptr, size_t vlen)
{
   provdb_key                *key;
   provdb_val                *val;

   //
   // Sanity checks
   //
   assert( sizeof(*key) == klen );
   assert( sizeof(*val) <= vlen );

   //
   // Initialization
   //
   key = kptr;
   val = vptr;

   if (PROVDB_VAL_VALUETYPE(val) == PROV_TYPE_PNODEVERSION) {
      printf( "%llu:%u --> ",
              (uint64_t)key->pnum, (uint32_t)key->version );

      printf("%s : %.*s\n", PROVDB_VAL_ATTR(val),
	     PROVDB_VAL_VALUELEN(val), (const char *) PROVDB_VAL_VALUE(val));
   }
}

////////////////////////////////////////////////////////////////////////////////

/*
 * i2pnum.db:    inode number -> pnode number+version
 */
static void print_i2pnum(void *kptr, size_t klen, void *vptr, size_t vlen) {
   uint32_t           *key;
   provdb_key         *val;

   //
   // Sanity checks
   //
   assert( sizeof(*key) == klen );
   assert( sizeof(*val) == vlen );

   //
   // Initialization
   //
   key = kptr;
   val = vptr;

   printf("<inode-to-pnode>\n"
//          "  <inode>%llu</inode>\n"
//          "  <pnode>%llu</pnode>\n"
          "<inode>%llu</inode>\n"
          "<pnode>%llu</pnode>\n"
          "</inode-to-pnode>",
          *((uint64_t*)key),
          (uint64_t)val->pnum );
}

////////////////////////////////////////////////////////////////////////////////

static void dump_pass()
{
    printf("<pass-data>\n");

    /*
    printf( "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
            "xsi:spaceSchemaLocation="
            "\"http://www.eecs.harvard.edu/syrah/pass/ipaw-challenge2/"
            "xml-schema.xs\""
            ">\n" );
    */

    db_mapcar( g_provdb->db, print_pass );

    printf( "</provenance>\n"
            "</pass-data>\n" );
}

static void dump_ancestry()
{
   db_mapcar( g_provdb->db, print_ancestry );
}

static void dump_pnodes()
{
   db_mapcar( g_provdb->db, print_pnodes);
}

static void dump_identity()
{
   db_mapcar( g_provdb->db, print_identity );
}

static void dump_i2pnum()
{
   db_mapcar( g_i2pdb->db, print_i2pnum );
}

////////////////////////////////////////////////////////////////////////////////

static void usage(const char *av0)
{
    fprintf(stderr,
            "Usage: %s [-a] [-d mode(s)] /mountpoint\n"
            "   -a           equivalent to -d pibnc\n"
            "   -d           dump modes (default p)\n"
            "modes:\n"
            "    p           dump provenance in xml\n"
            "    i           dump inode to pnode in xml \n"
            "    b           dump ancestry \n"
            "    n           dump pnodes and versions \n"
            "    c           dump indentity info \n",
            av0);
    exit(1);
}

int main( int argc, char *argv[] )
{
   const char                *dumps = "p";
   int                        ch;
   int                        i;

   while ((ch = getopt(argc, argv, "ad:")) != -1) {
      switch (ch) {
         case 'a': dumps = "pibnc";  break;
         case 'd': dumps = optarg;   break;
         default:  usage( argv[0] ); break;
      }
   }

   if (optind != argc-1) {
      usage(argv[0]);
   }

   if ( strspn(dumps, "pibnc") != strlen(dumps) ) {
      usage(argv[0]);
   }

   wdb_startup(argv[optind], WDB_O_RDONLY);

   for (i=0; dumps[i]; i++) {
      switch (dumps[i]) {
         case 'p': dump_pass();     break;
         case 'i': dump_i2pnum();   break;
         case 'b': dump_ancestry(); break;
         case 'n': dump_pnodes();   break;
         case 'c': dump_identity(); break;
      }
   }

   wdb_shutdown();

   return 0;
}
