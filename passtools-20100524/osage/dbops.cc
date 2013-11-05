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
#include "primarray.h"
#include "ast.h"
#include "dbops.h"
#include "main.h" // for eval

//#define COMPILED_IN_TEST_DATA

////////////////////////////////////////////////////////////

static void doevalall(valuelist *result, uint64_t pnode,
		      var *ref, expr *where, expr *in) {
   value *pv = value_pnode(pnode);
   ref->val = pv;
   value *pred = eval(where);
   if (!pred || !value_istrue(pred)) {
      ref->val = NULL;
      value_destroy(pred);
      value_destroy(pv);
      return;
   }
   value *stuff = eval(in);
   if (stuff) {
      valuelist_add(result, stuff);
   }
   ref->val = NULL;
   value_destroy(pred);
   value_destroy(pv);
}

#ifdef COMPILED_IN_TEST_DATA
////////////////////////////////////////////////////////////
// dummy data for testing

struct prov_entry {
   uint64_t pnode;
   uint32_t version;
   const char *attr;
   const char *value;
};

struct prov_refentry {
   uint64_t pnode;
   uint32_t version;
   const char *attr;
   uint64_t other_pnode;
   uint32_t other_version;
};

static const prov_entry prov[] = {
   { 1, 0, "NAME", "foo" },
   { 1, 0, "TYPE", "file" },
   { 2, 0, "NAME", "cat" },
   { 2, 0, "TYPE", "proc" },
   { 2, 0, "PID",  "63" },
   { 2, 0, "ARGV", "cat foo" },
   { 2, 0, "ENV",  "PATH=/bin PRINTER=cork" },
   { 3, 0, "NAME", "bar" },
   { 3, 0, "TYPE", "file" },
};
static const unsigned nprov = sizeof(prov) / sizeof(prov[0]);

static const prov_refentry provr[] = {
   { 2, 1, "INPUT", 1, 1 },
   { 3, 1, "INPUT", 2, 1 },
};
static const unsigned nprovr = sizeof(provr) / sizeof(provr[0]);

//////////////////////////////

static int pnodecmp(const void *av, const void *bv) {
   uint64_t a = *(const uint64_t *) av;
   uint64_t b = *(const uint64_t *) bv;
   return a < b ? -1 : a > b ? 1 : 0;
}

value *db_evalallprov(var *ref, expr *where, expr *in) {
   primarray<uint64_t> pnodes;
   uint64_t last = 0;
   for (unsigned i=0; i<nprov; i++) {
      if (prov[i].pnode != last) {
	 pnodes.add(prov[i].pnode);
	 last = prov[i].pnode;
      }
   }
   for (unsigned i=0; i<nprovr; i++) {
      if (provr[i].pnode != last) {
	 pnodes.add(provr[i].pnode);
	 last = provr[i].pnode;
      }
   }
   qsort(pnodes.getdata(), pnodes.num(), sizeof(pnodes[0]), pnodecmp);

   value *result = value_list();
   last = 0;
   for (int i=0; i<pnodes.num(); i++) {
      if (pnodes[i] != last) {
	 doevalall(result->listval, pnodes[i], ref, where, in);
	 last = pnodes[i];
      }
   }
   return result;
}

// get from PROV table
value *db_get_attr(uint64_t pnode, const char *attribute) {
   for (unsigned i=0; i<nprov; i++) {
      if (prov[i].pnode == pnode && !strcmp(prov[i].attr, attribute)) {
	 return value_str(prov[i].value);
      }
   }
   for (unsigned i=0; i<nprovr; i++) {
      if (provr[i].pnode == pnode && !strcmp(provr[i].attr, attribute)) {
	 return value_pnode(provr[i].other_pnode);
      }
   }
   return NULL;
}

// get from NAME index
value *db_get_name(const char *name) {
   for (unsigned i=0; i<nprov; i++) {
      if (!strcmp(prov[i].attr, "NAME") && !strcmp(name, prov[i].value)) {
	 return value_pnode(prov[i].pnode);
      }
   }
   return NULL;
}

// get from I2P index
value *db_get_i2p(long val) {
   char valstr[64];
   snprintf(valstr, sizeof(valstr), "%ld", val);
   for (unsigned i=0; i<nprov; i++) {
      if (!strcmp(prov[i].attr, "INODE") && !strcmp(valstr, prov[i].value)) {
	 return value_pnode(prov[i].pnode);
      }
   }
   return NULL;
}

// get from ARGV index
value *db_get_argv(const char *word) {
   for (unsigned i=0; i<nprov; i++) {
      if (!strcmp(prov[i].attr, "ARGV") && strstr(prov[i].value, word)) {
	 return value_pnode(prov[i].pnode);
      }
   }
   return NULL;
}

// get from CHILD index
value *db_get_children(uint64_t pnode) {
   value *v = value_list();
   for (unsigned i=0; i<nprovr; i++) {
      if (provr[i].other_pnode == pnode && !strcmp(provr[i].attr, "INPUT")) {
	 valuelist_add(v->listval, value_pnode(provr[i].pnode));
      }
   }
   return v;
}

// get from PARENT index, or really main table
value *db_get_parents(uint64_t pnode) {
   value *v = value_list();
   for (unsigned i=0; i<nprovr; i++) {
      if (provr[i].pnode == pnode && !strcmp(provr[i].attr, "INPUT")) {
	 valuelist_add(v->listval, value_pnode(provr[i].other_pnode));
      }
   }
   return v;
}

#else  /* COMPILED_IN_TEST_DATA */
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include <db.h>
#include "wdb.h"

/*
 * XXX. none of this uses transactions, and should
 */


/* visitation stops in the middle if func returns nonzero */
static int visit_dups_op(DB *db, DBT *key, void *clo,
			 int (*func)(void *clo, DBT *val)) {
   DBC *cursor;
   DBT val;
   int result;
   int num = 0;

   bzero(&val, sizeof(val));

   result = db->cursor(db, NULL, &cursor, 0);
   if (result) {
      db->err(db, result, "cursor");
      exit(1); /* XXX */
   }

   result = cursor->c_get(cursor, key, &val, DB_SET);
   if (result && result != DB_NOTFOUND) {
      db->err(db, result, "cursor->c_get(DB_SET)");
      exit(1); /* XXX */
   }

   while (result == 0) {
      num++;
      if (func(clo, &val)) {
	 break;
      }

      result = cursor->c_get(cursor, key, &val, DB_NEXT_DUP);
      if (result && result != DB_NOTFOUND) {
	 db->err(db, result, "cursor->c_get(DB_NEXT_DUP)");
	 exit(1); /* XXX */
      }
   }

   result = cursor->c_close(cursor);
   if (result) {
      db->err(db, result, "cursor->c_close");
      exit(1); /* XXX */
   }

   return num;
}

/* visitation stops in the middle if func returns nonzero */
static int visit_by_setrange(DB *db, DBT *key, void *clo,
			     int (*func)(void *clo, DBT *key, DBT *val)) {
   DBC *cursor;
   DBT val;
   int result;
   int num = 0;

   bzero(&val, sizeof(val));

   result = db->cursor(db, NULL, &cursor, 0);
   if (result) {
      db->err(db, result, "cursor");
      exit(1); /* XXX */
   }

   result = cursor->c_get(cursor, key, &val, DB_SET_RANGE);
   if (result && result != DB_NOTFOUND) {
      db->err(db, result, "cursor->c_get(DB_SET_RANGE)");
      exit(1); /* XXX */
   }

   while (result == 0) {
      num++;
      if (func(clo, key, &val)) {
	 break;
      }

      result = cursor->c_get(cursor, key, &val, DB_NEXT);
      if (result && result != DB_NOTFOUND) {
	 db->err(db, result, "cursor->c_get(DB_NEXT)");
	 exit(1); /* XXX */
      }
   }

   result = cursor->c_close(cursor);
   if (result) {
      db->err(db, result, "cursor->c_close");
      exit(1); /* XXX */
   }

   return num;
}

/* visitation stops in the middle if func returns nonzero */
static int visit_all_op(DB *db, void *clo,
			int (*func)(void *clo, DBT *key, DBT *val)) {
   DBC *cursor;
   DBT key, val;
   int result;
   int num = 0;

   bzero(&key, sizeof(key));
   bzero(&val, sizeof(val));

   result = db->cursor(db, NULL, &cursor, 0);
   if (result) {
      db->err(db, result, "cursor");
      exit(1); /* XXX */
   }

   result = cursor->c_get(cursor, &key, &val, DB_FIRST);
   if (result && result != DB_NOTFOUND) {
      db->err(db, result, "cursor->c_get(DB_FIRST)");
      exit(1); /* XXX */
   }

   while (result == 0) {
      num++;
      if (func(clo, &key, &val)) {
	 break;
      }

      result = cursor->c_get(cursor, &key, &val, DB_NEXT);
      if (result && result != DB_NOTFOUND) {
	 db->err(db, result, "cursor->c_get(DB_NEXT)");
	 exit(1); /* XXX */
      }
   }

   result = cursor->c_close(cursor);
   if (result) {
      db->err(db, result, "cursor->c_close");
      exit(1); /* XXX */
   }

   return num;
}

static int accumulate_pnodes(void *vlv, DBT *val) {
   valuelist *vl = (valuelist *)vlv;
   pnode_t pnum;

   if (val->size == sizeof(pnode_t)) {
      pnum = *(pnode_t *)val->data;
   }
   else {
      assert(val->size == sizeof(pnode_version));
      pnode_version *pv = (pnode_version *)val->data;
      pnum = pv->pnum;
   }
   valuelist_add(vl, value_pnode(pnum));

   return 0;
}

////////////////////////////////////////////////////////////

static int get_tnum(const char *word, tnum_t *ret) {
   DBT key, val;
   int result;

   bzero(&key, sizeof(key));
   bzero(&val, sizeof(val));

   key.data = (char *)word;
   key.size = strlen(word);

   result = g_tok2tnumdb->db->get(g_tok2tnumdb->db, NULL, &key, &val, 0);
   if (result && result != DB_NOTFOUND) {
      g_tok2tnumdb->db->err(g_tok2tnumdb->db, result, "tok2tnumdb get");
      exit(1); /* XXX */
   }
   if (result == DB_NOTFOUND) {
      return -1;
   }
   assert(val.size == sizeof(tnum_t));
   *ret = *(tnum_t *)val.data;
   return 0;
}

static value *db_get_token(tnum_t tn) {
   DBT key, val;

   int result;

   bzero(&key, sizeof(key));
   bzero(&val, sizeof(val));

   key.data = (char *)&tn;
   key.size = sizeof(tn);

   result = g_tnum2tokdb->db->get(g_tnum2tokdb->db, NULL, &key, &val, 0);
   if (result && result != DB_NOTFOUND) {
      g_tok2tnumdb->db->err(g_tok2tnumdb->db, result, "tnum2tokdb get");
      exit(1); /* XXX */
   }
   if (result == DB_NOTFOUND) {
      return NULL;
   }

   return value_str_bylen((const char *) val.data, val.size);
}

static value *getvalue(provdb_val *v, size_t vlen) {
   assert(PROVDB_VAL_TOTSIZE(v) == vlen);

   const void *valptr = PROVDB_VAL_VALUE(v);
   size_t vallen = PROVDB_VAL_VALUELEN(v);

   switch (PROVDB_VAL_VALUETYPE(v)) {
    case PROV_TYPE_NIL:
      return NULL;

    case PROV_TYPE_STRING:
      // XXX should not assume this
      assert(!PROVDB_VAL_ISTOKENIZED(v));
      return value_str_bylen((const char *) valptr, vallen);

    case PROV_TYPE_MULTISTRING:
    { 
      // XXX should not assume this
      assert(PROVDB_VAL_ISTOKENIZED(v));

      int ntokens = vallen / sizeof(tnum_t);
      if (ntokens == 1) {
	 return db_get_token(* (const tnum_t *) valptr);
      }
      else {
	 value **vals = new value*[ntokens];
	 for (int i=0; i<ntokens; i++) {
	    vals[i] = db_get_token( ((const tnum_t *) valptr)[i] );
	 }
	 return value_tuple(vals, ntokens);
      }
    }

    case PROV_TYPE_INT:
     assert(vallen == sizeof(int32_t));
     return value_int(*(int32_t *)valptr);

    case PROV_TYPE_REAL:
     assert(vallen == sizeof(double));
     return value_float(*(double *)valptr);

    case PROV_TYPE_TIMESTAMP:
     // don't currently have time values in sage (XXX)
     assert(vallen == sizeof(struct prov_timestamp));
     {
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%lld",
		 (long long) ((struct prov_timestamp *)valptr)->pt_sec);
	return value_str(tmp);
     }

    case PROV_TYPE_INODE:
     assert(vallen == sizeof(uint32_t));
     // XXX might overflow (sage ints are signed long)
     return value_int(*(uint32_t *)valptr);

    case PROV_TYPE_PNODE:
    {
      assert(vallen == sizeof(pnode_t));
      pnode_t p = * (pnode_t *) valptr;

      /*
       * Rather than returning a pnode value, format a string.
       */

      //return value_pnode(p);

      char tmp[128];
      snprintf(tmp, sizeof(tmp), "pnode-%llu", p);
      return value_str(tmp);
    }

    case PROV_TYPE_PNODEVERSION:
    {
      assert(vallen == sizeof(pnode_version));
      pnode_version pv = * (pnode_version *) valptr;

      /*
       * Rather than returning a pnode value, format a string.
       */

      //return value_pnode(pv.pnum);

      char tmp[128];
      snprintf(tmp, sizeof(tmp), "pnode-%llu-v%u", pv.pnum, pv.version);
      return value_str(tmp);
    }

    case PROV_TYPE_OBJECT:
    case PROV_TYPE_OBJECTVERSION:
    default:
     break;

   }
   assert(0);
   return NULL;
}

struct evalallprov_stuff {
   var *ref;
   expr *where;
   expr *in;
   valuelist *vl;
   pnode_t last;
};

static int evalallprov_sub(void *sv, DBT *key, DBT *) {
   evalallprov_stuff *s = (evalallprov_stuff *)sv;
   provdb_key *k;

   assert(key->size = sizeof(provdb_key));
   k = (provdb_key *) key->data;
   if (k->pnum != s->last) {
      doevalall(s->vl, k->pnum, s->ref, s->where, s->in);
      s->last = k->pnum;
   }
   return 0;
}

value *db_evalallprov(var *ref, expr *where, expr *in) {
   value *out;
   evalallprov_stuff s;

   out = value_list();

   s.ref = ref;
   s.where = where;
   s.in = in;
   s.vl = out->listval;
   s.last = /* INVALID_PNUM */ 0; /* XXX? */

   visit_all_op(g_provdb->db, &s, evalallprov_sub);
   return out;
}

// get from PROV table
struct db_get_attr_stuff {
   pnode_t pnode;
   const char *target_attribute;
   value *output;
};

static int db_get_attr_sub(void *sv, DBT *key, DBT *val) {
   db_get_attr_stuff *s = (db_get_attr_stuff *)sv;
   provdb_key *k = (provdb_key *) key->data;
   provdb_val *v = (provdb_val *) val->data;
   size_t vlen = val->size;

   if (k->pnum != s->pnode) {
      /* stop when we reach the next pnode */
      return 1;
   }

   const char *seenattr = PROVDB_VAL_ATTR(v);

   if (!strcmp(seenattr, s->target_attribute)) {
      if (s->output) {
	 value_destroy(s->output);
      }
      s->output = getvalue(v, vlen);
   }
   return 0;
}

value *db_get_attr(uint64_t pnode, const char *attribute) {
   if (!strcmp(attribute, "PNODE")) {
      return value_int(pnode);
   }

   // XXX what about INODE?

   db_get_attr_stuff s;
   DBT key;
   provdb_key k;

   s.pnode = pnode;
   s.target_attribute = attribute;
   s.output = NULL;

   bzero(&key, sizeof(key));
   k.pnum = pnode;
   k.version = 0;
   key.data = &k;
   key.size = sizeof(k);

   visit_by_setrange(g_provdb->db, &key, &s, db_get_attr_sub);

   return s.output;
}

struct db_get_allattr_stuff {
   pnode_t pnode;
   value *output;
};

static int db_get_allattr_sub(void *sv, DBT *key, DBT *val) {
   db_get_allattr_stuff *s = (db_get_allattr_stuff *)sv;
   provdb_key *k = (provdb_key *) key->data;
   provdb_val *v = (provdb_val *) val->data;
   size_t vlen = val->size;

   if (k->pnum != s->pnode) {
      /* stop when we reach the next pnode */
      return 1;
   }

   const char *attr = PROVDB_VAL_ATTR(v);

   value *pk = value_str(attr);
   value *pv = getvalue(v, vlen);
   value **tupvals = new value*[2];
   tupvals[0] = pk;
   tupvals[1] = pv;
   value *tup = value_tuple(tupvals, 2);

   valuelist_add(s->output->listval, tup);

   return 0;
}

value *db_get_allattr(uint64_t pnode) {
   db_get_allattr_stuff s;
   DBT key;
   provdb_key k;

   s.pnode = pnode;
   s.output = value_list();

   bzero(&key, sizeof(key));
   k.pnum = pnode;
   k.version = 0;
   key.data = &k;
   key.size = sizeof(k);

   visit_by_setrange(g_provdb->db, &key, &s, db_get_allattr_sub);

   return s.output;
}

// get from NAME index
value *db_get_name(const char *name) {
   DBT key;
   value *out;

   bzero(&key, sizeof(key));
   key.data = (char *)name;
   key.size = strlen(name);

   out = value_list();
   visit_dups_op(g_namedb->db, &key, out->listval, accumulate_pnodes);
   return out;
}

// get from I2P index
value *db_get_i2p(long val) {
   DBT key;
   value *out;
   ino_t kv = val;

   bzero(&key, sizeof(key));
   key.data = &kv;
   key.size = sizeof(kv);

   out = value_list();
   visit_dups_op(g_i2pdb->db, &key, out->listval, accumulate_pnodes);
   return out;
}

// get from ARGV index
value *db_get_argv(const char *name) {
   DBT key;
   value *out;
   tnum_t id;

   out = value_list();

   if (get_tnum(name, &id)) {
      /* give back empty list, not NULL */
      return out;
   }

   bzero(&key, sizeof(key));
   key.data = &id;
   key.size = sizeof(id);

   visit_dups_op(g_arg2pdb->db, &key, out->listval, accumulate_pnodes);
   return out;
}

// for PARENT and CHILD

struct parent_child_stuff {
   pnode_t pnode;
   valuelist *vl;
};

static int db_parent_child_sub(void *sv, DBT *key, DBT *val) {
   assert(key->size == sizeof(pnode_version));
   assert(val->size == sizeof(pnode_version));

   parent_child_stuff *s = (parent_child_stuff *)sv;
   pnode_version *k = (pnode_version *)key->data;
   pnode_version *v = (pnode_version *)val->data;

   if (k->pnum != s->pnode) {
      /* reached next guy; stop */
      return 1;
   }

   valuelist_add(s->vl, value_pnode(v->pnum));

   return 0;
}

// get from CHILD index
value *db_get_children(uint64_t pnode) {
   DBT key;
   value *out;
   parent_child_stuff s;
   pnode_version k;

   k.pnum = pnode;
   k.version = 0;

   bzero(&key, sizeof(key));
   key.data = &k;
   key.size = sizeof(k);

   out = value_list();
   s.pnode = pnode;
   s.vl = out->listval;
   visit_by_setrange(g_childdb->db, &key, &s, db_parent_child_sub);

   //printf("children %llu: %d:", pnode, out->listval->members.num());
   //for (int i=0; i<out->listval->members.num(); i++) {
   //   printf(" %llu", out->listval->members[i]->pnodeval);
   //}
   //printf("\n");

   return out;
}

// get from PARENT index (well, via PROV)
value *db_get_parents(uint64_t pnode) {
   DBT key;
   value *out;
   parent_child_stuff s;
   pnode_version k;

   k.pnum = pnode;
   k.version = 0;

   bzero(&key, sizeof(key));
   key.data = &k;
   key.size = sizeof(k);

   out = value_list();
   s.pnode = pnode;
   s.vl = out->listval;
   visit_by_setrange(g_parentdb->db, &key, &s, db_parent_child_sub);

   //printf("parents %llu: %d:", pnode, out->listval->members.num());
   //for (int i=0; i<out->listval->members.num(); i++) {
   //   printf(" %llu", out->listval->members[i]->pnodeval);
   //}
   //printf("\n");

   return out;
}

#endif /* COMPILED_IN_TEST_DATA */
