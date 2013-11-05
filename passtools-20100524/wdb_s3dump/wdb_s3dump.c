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
#include <ctype.h>
#include <getopt.h>
#include <assert.h>

#include <db.h>

#include "twig.h"
#include "schema.h"
#include "wdb.h"

static const char hexAlphabet[] = "0123456789ABCDEF";

static void dump_s3(void);
static void dump_simpleDB(void);
static int last_version = 0; /* only dump the last version provenance,
				works with simpleDB mode */

/**
 * urlEncode a string
 * buf: result string
 * str: input strng
 * len : length of input string
 * assumes that buf has enough space for the encoded string
 */
static size_t urlEncode(char * buf, const char * s, size_t len) {
    size_t i;
    size_t size = 0;

    for (i = 0; i < len; ++i) {
	if (isalnum(s[i]))
	    buf[size++] = s[i];
	else if (s[i] == '.' || s[i] == '-' || s[i] == '*' || s[i] == '_' || s[i] == '\n')
	    buf[size++] = s[i];
	else if (s[i] == ' ')
	    buf[size++] = '+';
	else {
	    buf[size++] = '%';
	    buf[size++] = hexAlphabet[(unsigned char)(s[i]) / 16];
	    buf[size++] = hexAlphabet[(unsigned char)(s[i]) % 16];
	}
    }
    return size;
}



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
    int                   simpledb = 0;

    //
    // Parse options
    //
    while ( -1 != (ch = getopt(argc, argv, "hsl")) ) {
	switch ( ch ) {
	case 's':
	    simpledb = 1;
	    break;

	case 'l':
	    last_version = 1;
	    break;
	   
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
    // Dump everything in s3 format
    //
    if (!simpledb) {
	dump_s3();
    } else {
	dump_simpleDB();
    }

    //
    // Shut everything down (database & log)
    //
    wdb_shutdown();

    exit(0);
}

static void appendstring(char * buf, size_t * off, const char *s, size_t len) {
    size_t i = 0;
   
    buf[*off] = '"'; *off = *off + 1;
    for (i=0; i<len; i++) {
	unsigned char c = s[i];
	switch (c) {
	case '"':
	case '\\':
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = c; *off = *off + 1;
	    break;

	case '\a': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'a'; *off = *off + 1;
	    break;

	case '\b': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'b'; *off = *off + 1;
	    break;

	case '\t': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 't'; *off = *off + 1;
	    break;

	case '\n': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'n'; *off = *off + 1;

	case '\v': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'v'; *off = *off + 1;

	case '\f': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'f'; *off = *off + 1;
	   
	   
	case '\r': 
	    buf[*off] = '\\'; *off = *off + 1;
	    buf[*off] = 'r'; *off = *off + 1;
	   
	default:
	    if (isascii(c) && (isprint(c) || isspace(c))) {
		buf[*off] = c; *off = *off + 1;
	    }
	    else {
		printf("\\%03o", c);
	    }
	    break;
	}
    }
    buf[*off] = '"'; *off = *off + 1;
}

static void appendtoken(char * buf, size_t * off, tnum_t tok) {
    DBT  dbt_tnum;
    DBT  dbt_token;
    int  ret;

    memset(&dbt_tnum, 0, sizeof(dbt_tnum));
    memset(&dbt_token, 0, sizeof(dbt_token));

    dbt_tnum.data = &tok;
    dbt_tnum.size = sizeof(tok);

    //printf(" [%u]", tok);

    ret = g_tnum2tokdb->db->get(g_tnum2tokdb->db, NULL,
				&dbt_tnum, &dbt_token,
				0);
    wdb_check_err(ret);

    appendstring(buf, off, dbt_token.data, dbt_token.size);
}

static size_t strnlen(const char *s, size_t maxlen) {
    const char *t = memchr(s, 0, maxlen);
    return t ? (size_t)(t-s) : maxlen;
}

void dump_s3(void)
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
    int                        ret;
    char                       buf[65536]; 
    char                       resbuf[65536];
    size_t                     off = 0, len = 0;
    size_t                     small_objs = 0, small_objs_len = 0;
    size_t                     large_objs = 0, large_objs_len = 0;
    size_t                     slarge_objs = 0, slarge_objs_len = 0;

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

	memset(buf, 0, sizeof(buf));
	memset(resbuf, 0, sizeof(resbuf));
	off = len = 0;
       

	id = PROV_ATTR_INVALID;
	val = (provdb_val *)malloc(dbt_val.size);

	assert( sizeof(key) == dbt_key.size );
	assert( sizeof(val) < dbt_val.size );

	memcpy(&key, dbt_key.data, sizeof(key));
	memcpy(val, dbt_val.data, dbt_val.size);


	attrname = PROVDB_VAL_ATTR(val);
	data = PROVDB_VAL_VALUE(val);
	datalen = PROVDB_VAL_VALUELEN(val);


	len = snprintf(buf, sizeof(buf), "x-amz-meta-%s-%u-",
		       attrname, key.version);
	off = off + len;

	switch (PROVDB_VAL_VALUETYPE(val)) {

	case PROV_TYPE_NIL:
	    /* 	   len = snprintf(buf + off, sizeof(buf) - off, "\n"); */
	    /* 	   off = off + len; */
	    break;
	   
	case PROV_TYPE_STRING:
	    if (PROVDB_VAL_ISTOKENIZED(val)) {
		tnum_t tok = *(tnum_t *) data;
	       
		assert(datalen == sizeof(tnum_t));
		tok = *(tnum_t *) data;
		appendtoken(buf, &off,tok);
		//len = snprintf(buf + off, sizeof(buf) - off, "\n");
		//off = off + len;
	    }
	    else {
		appendstring(buf, &off,data, datalen);
		//len += snprintf(buf + off, sizeof(buf) - off, "\n");
		//off = off + len;
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
		    appendtoken(buf, &off, toks[i]);
		}
		free(toks), toks = NULL;
	    }
	    else {
		size_t x, y;
		for (x = 0; x < datalen; x += y+1) {
		    if (x > 0) appendstring(buf, &off, " ", strlen(" "));
		    y = strnlen(data+x, datalen-x);
		    appendstring(buf, &off, data+x, y);
		}
	    }
	    //len = snprintf(buf + off, sizeof(buf) - off, "\n");
	    //off = off + len;
	    break;

	case PROV_TYPE_INT:
	    assert(datalen == sizeof(int32_t));
	    len = snprintf(buf + off, sizeof(buf) - off,  "%d", *(int32_t *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_REAL:
	    assert(datalen == sizeof(double));
	    len = snprintf(buf + off, sizeof(buf) - off, "%g", *(double *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_TIMESTAMP:
	    assert(datalen == sizeof(struct prov_timestamp));
	    len = snprintf(buf + off, sizeof(buf) - off, "%u.%09u",
			   ((struct prov_timestamp *)data)->pt_sec,
			   ((struct prov_timestamp *)data)->pt_nsec);
	    off = off + len;
	    break;

	case PROV_TYPE_INODE:
	    assert(datalen == sizeof(uint32_t));
	    len = snprintf(buf + off, sizeof(buf) - off, "%u", *(uint32_t *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_PNODE:
	    assert(datalen == sizeof(pnode_t));
	    len = snprintf(buf + off, sizeof(buf) - off, "%llu", *(pnode_t *)data);
	    off = off + len;
	    break;

	case PROV_TYPE_PNODEVERSION:
	    assert(datalen == sizeof(struct pnode_version));
	    ref = (struct pnode_version *)data;
	    len = snprintf(buf + off, sizeof(buf) - off,
			   "%llu.%u",
			   ref->pnum, ref->version );
	    off = off + len;
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
       
	len = urlEncode(resbuf, buf, off);
	if (len > 8192) {
	    slarge_objs++;
	    slarge_objs_len += len;
	} else if (len > 1024) {
	    large_objs++;
	    large_objs_len += len;
	}  else {
	    small_objs++;
	    small_objs_len += len;
	}
	printf("%s\n", resbuf);
    }

    fprintf(stderr, "small: %u,%u, large: %u,%u, slarge: %u,%u\n",
	    small_objs, small_objs_len, large_objs, large_objs_len,
	    slarge_objs, slarge_objs_len);
   
    // XXX shouldn't this, like, close the cursor?
   
    if ( DB_NOTFOUND != ret ) {
	wdb_check_err(ret);
    }
}


void dump_simpleDB(void)
{
    DBC                       *pdbc = NULL;
    DBT                        dbt_key;
    DBT                        dbt_val;
    provdb_key                 key;
    provdb_key                 cur_pv;
    provdb_val                *val;
    struct pnode_version      *ref;
    enum prov_attrname_packed  id = PROV_ATTR_INVALID;
    const char                *attrname = NULL;
    char                      *data;
    size_t                     i, datalen;
    int                        ret;
    char                       buf[1024*1024], tmpbuf[1024*1024], resbuf[1024*1024];
    size_t                     off = 0, tmpoff = 0, len = 0;
    size_t                     small_objs = 0, small_objs_len = 0;
    size_t                     large_objs = 0, large_objs_len = 0;
    size_t                     slarge_objs = 0, slarge_objs_len = 0;
    size_t                     first = 1, attr_count = 0;

    memset(&dbt_key, 0, sizeof(dbt_key));
    memset(&dbt_val, 0, sizeof(dbt_val));
    memset(&cur_pv, 0, sizeof(cur_pv));
    tmpoff = 0;

    //
    // Process the entries
    //

    // Open a cursor
    ret = g_provdb->db->cursor(g_provdb->db, NULL, &pdbc, 0);
    wdb_check_err(ret);

    // Iterate over the main database
    while ( 0 == (ret = pdbc->c_get(pdbc, &dbt_key, &dbt_val, DB_NEXT)) ) {

	id = PROV_ATTR_INVALID;
	val = (provdb_val *)malloc(dbt_val.size);

	assert( sizeof(key) == dbt_key.size );
	assert( sizeof(val) < dbt_val.size );

	memcpy(&key, dbt_key.data, sizeof(key));
	memcpy(val, dbt_val.data, dbt_val.size);

	if ((key.pnum != cur_pv.pnum) ||
	    (key.version != cur_pv.version)) {
	    
	    if (first) {
		first = 0;
	    } else {
		if (last_version) {
		    if (key.pnum != cur_pv.pnum) {
			/* we're in the last version..so we need to
			   dump, else we ignore it */
			memset(resbuf, 0, sizeof(resbuf));
			urlEncode(resbuf, tmpbuf, tmpoff);
			printf("%s\n", resbuf);
		    }
		} else {
		    memset(resbuf, 0, sizeof(resbuf));
		    urlEncode(resbuf, tmpbuf, tmpoff);
		    printf("%s\n", resbuf);
		}
	    }

	    memset(tmpbuf, 0, sizeof(tmpbuf));
	    attr_count = 0;
	    tmpoff  = 0;
	    cur_pv.pnum = key.pnum;
	    cur_pv.version = key.version;
	    len = snprintf(tmpbuf, sizeof(tmpbuf), "&ItemName=%lld_%u",
			   cur_pv.pnum, cur_pv.version);
	    tmpoff = tmpoff + len;
	}

	memset(buf, 0, sizeof(buf)); off = 0;
	attrname = PROVDB_VAL_ATTR(val);
	data = PROVDB_VAL_VALUE(val);
	datalen = PROVDB_VAL_VALUELEN(val);

	len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, 
		       "&Attribute.%u.Name=%s&Attribute.%u.Value=",
		       attr_count, attrname, attr_count);
	tmpoff = tmpoff + len;
	attr_count++;

	switch (PROVDB_VAL_VALUETYPE(val)) {

	case PROV_TYPE_NIL:
	    /* 	   len = snprintf(buf + off, sizeof(buf) - off, "\n"); */
	    /* 	   off = off + len; */
	    break;
	   
	case PROV_TYPE_STRING:
	    if (PROVDB_VAL_ISTOKENIZED(val)) {
		tnum_t tok = *(tnum_t *) data;
	       
		assert(datalen == sizeof(tnum_t));
		tok = *(tnum_t *) data;
		appendtoken(buf, &off,tok);
	    }
	    else {
		appendstring(buf, &off,data, datalen);
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
		    appendtoken(buf, &off, toks[i]);
		}
		free(toks), toks = NULL;
	    }
	    else {
		size_t x, y;
		for (x = 0; x < datalen; x += y+1) {
		    if (x > 0) appendstring(buf, &off, " ", strlen(" "));
		    y = strnlen(data+x, datalen-x);
		    appendstring(buf, &off, data+x, y);
		}
	    }
	    break;

	case PROV_TYPE_INT:
	    assert(datalen == sizeof(int32_t));
	    len = snprintf(buf + off, sizeof(buf) - off,  "%d", *(int32_t *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_REAL:
	    assert(datalen == sizeof(double));
	    len = snprintf(buf + off, sizeof(buf) - off, "%g", *(double *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_TIMESTAMP:
	    assert(datalen == sizeof(struct prov_timestamp));
	    len = snprintf(buf + off, sizeof(buf) - off, "%u.%09u",
			   ((struct prov_timestamp *)data)->pt_sec,
			   ((struct prov_timestamp *)data)->pt_nsec);
	    off = off + len;
	    break;

	case PROV_TYPE_INODE:
	    assert(datalen == sizeof(uint32_t));
	    len = snprintf(buf + off, sizeof(buf) - off, "%u", *(uint32_t *)data );
	    off = off + len;
	    break;

	case PROV_TYPE_PNODE:
	    assert(datalen == sizeof(pnode_t));
	    len = snprintf(buf + off, sizeof(buf) - off, "%llu", *(pnode_t *)data);
	    off = off + len;
	    break;

	case PROV_TYPE_PNODEVERSION:
	    assert(datalen == sizeof(struct pnode_version));
	    ref = (struct pnode_version *)data;
	    len = snprintf(buf + off, sizeof(buf) - off,
			   "%llu_%u",
			   ref->pnum, ref->version );
	    off = off + len;
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

	memset(resbuf, 0, sizeof(resbuf));
	len = urlEncode(resbuf, buf, off);
	if (len > 8192) {
	    slarge_objs++;
	    slarge_objs_len += len;
	    len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, "NULL");
	    tmpoff = tmpoff + len;
	} else if (len > 1024) {
	    large_objs++;
	    large_objs_len += len;
	    len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, "NULL");
	    tmpoff = tmpoff + len;
	}  else {
	    small_objs++;
	    small_objs_len += len;
	    len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, "%s", buf);
	    tmpoff = tmpoff + len;
	}

#if 0       
	if (off > 1024) {
	    large_objs++;
	    large_objs_len += off;
	    len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, "NULL");
	    tmpoff = tmpoff + len;
	}  else {
	    len = snprintf(tmpbuf + tmpoff, sizeof(tmpbuf) - tmpoff, "%s", buf);
	    tmpoff = tmpoff + len;
	}
#endif 
    }

    memset(&resbuf, 0, sizeof(resbuf));
    urlEncode(resbuf, tmpbuf, tmpoff);
    printf("%s\n", resbuf);
    fprintf(stderr, "small: %u,%u, large: %u,%u, slarge: %u,%u\n",
	    small_objs, small_objs_len, large_objs, large_objs_len,
	    slarge_objs, slarge_objs_len);

    // XXX shouldn't this, like, close the cursor?
   
    if ( DB_NOTFOUND != ret ) {
	wdb_check_err(ret);
    }
}

