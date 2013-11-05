/*
 * Copyright 2008, 2009
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

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "pql.h"
#include "pqlutil.h"

#include "wdb.h"

#include "primarray.h"
#include "backend.h"

/*
 * Database numbers
 */
#define DBNUM_TEMP  0
#define DBNUM_MAGIC 1
#define DBNUM_ARGV  2
#define DBNUM_MAIN  3
#define DBNUM_VERS  4

#define MAGIC_PROVENANCE 0

static tdb *temp_tdb;
static tdb *prov_argv_tdb;

////////////////////////////////////////////////////////////
// Berkeley DB iteration tools

#if 0 /* not currently used */

/*
 * Visit duplicate records (same key, different values).
 *
 * Records matching KEY are extracted from database DB. User data CLO
 * is passed through to FUNC, which is passed each value in turn.
 * 
 * Visitation stops immediately if FUNC returns nonzero. The number of
 * records visited is returned.
 */
static int dbvisit_dups(DB *db, DBT *key, void *clo,
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
#endif /* 0 */

/*
 * Visit records via DB_SETRANGE.
 *
 * Records starting at the one matching KEY and continuing forward are
 * extracted from database DB. User data CLO is passed through to
 * FUNC, which is passed each key and value in turn.
 * 
 * Visitation stops immediately if FUNC returns nonzero. The number of
 * records visited is returned.
 */
static int dbvisit_setrange(DB *db, DBT *key, void *clo,
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

/*
 * Visit the entire contents of the database.
 *
 * Records are extracted from database DB. User data CLO is passed
 * through to FUNC, which is passed each key and value in turn.
 * 
 * Visitation stops immediately if FUNC returns nonzero. The number of
 * records visited is returned.
 */
static int dbvisit_everything(DB *db, void *clo,
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

////////////////////////////////////////////////////////////
// Other Berkeley DB retrieval tools

#if 0 /* not used */
/*
 * Look up the token number for a given string WORD.
 */
static int db_get_tnum(const char *word, tnum_t *ret) {
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
#endif /* 0 */

/*
 * Get the string expansion for the token number TN. The string is
 * returned as a pqlvalue.
 */
static struct pqlvalue *db_get_token(pqlcontext *pql, tnum_t tn) {
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

   return pqlvalue_string_bylen(pql, (const char *) val.data, val.size);
}

////////////////////////////////////////////////////////////
// Value representation tools

/*
 * Instantiate a struct, in either the whole-object graph or the
 * per-version graph.
 */
static struct pqlvalue *makestruct(struct pqlcontext *pql,
				   pnode_t pnum, version_t version,
				   bool do_versions) {
   if (do_versions) {
      return pqlvalue_struct(pql, DBNUM_VERS, pnum, version);
   }
   else {
      return pqlvalue_struct(pql, DBNUM_MAIN, pnum, 0);
   }
}

/*
 * Convert a stored provdb value V (of length VLEN) to a pqlvalue.
 */
static struct pqlvalue *dbval_to_pql(struct pqlcontext *pql,
				     struct provdb_val *v, size_t vlen,
				     bool do_versions) {
   const void *valptr;
   size_t vallen;

   assert(PROVDB_VAL_TOTSIZE(v) == vlen);
   valptr = PROVDB_VAL_VALUE(v);
   vallen = PROVDB_VAL_VALUELEN(v);

   switch (PROVDB_VAL_VALUETYPE(v)) {
    case PROV_TYPE_NIL:
      return pqlvalue_nil(pql);

    case PROV_TYPE_STRING:
      // XXX should not assume this
      assert(!PROVDB_VAL_ISTOKENIZED(v));
      return pqlvalue_string_bylen(pql, (const char *) valptr, vallen);

    case PROV_TYPE_MULTISTRING:
    { 
      // XXX should not assume this
      assert(PROVDB_VAL_ISTOKENIZED(v));

      if (prov_argv_tdb == NULL) {
	 prov_argv_tdb = tdb_create();
      }

      // XXX need to index and reuse these

      pqloid_t o = tdb_newobject(prov_argv_tdb);
      pqlvalue *v = pqlvalue_struct(pql, DBNUM_ARGV, o, 0);

      int ntokens = vallen / sizeof(tnum_t);
      for (int i=0; i<ntokens; i++) {
	 pqlvalue *thisedge = pqlvalue_int(pql, i);
	 pqlvalue *thisval = db_get_token(pql, ((const tnum_t *) valptr)[i] );
	 int r = tdb_assign(pql, prov_argv_tdb, o, thisedge, thisval);
	 // XXX handle errors?
	 assert(r == 0);
	 pqlvalue_destroy(thisedge);
	 pqlvalue_destroy(thisval);
      }
      return v;
    }

    case PROV_TYPE_INT:
     assert(vallen == sizeof(int32_t));
     return pqlvalue_int(pql, *(int32_t *)valptr);

    case PROV_TYPE_REAL:
     assert(vallen == sizeof(double));
     return pqlvalue_float(pql, *(double *)valptr);

    case PROV_TYPE_TIMESTAMP:
     // don't currently have time values in sage (XXX)
     assert(vallen == sizeof(struct prov_timestamp));
     {
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%lld",
		 (long long) ((struct prov_timestamp *)valptr)->pt_sec);
	return pqlvalue_string(pql, tmp);
     }

    case PROV_TYPE_INODE:
     assert(vallen == sizeof(uint32_t));
     // XXX might overflow (our ints are ordinary signed int)
     return pqlvalue_int(pql, *(uint32_t *)valptr);

    case PROV_TYPE_PNODE:
    {
      assert(vallen == sizeof(pnode_t));
      pnode_t pnum = *(pnode_t *)valptr;
      return makestruct(pql, pnum, 0, do_versions);
    }

    case PROV_TYPE_PNODEVERSION:
    {
      assert(vallen == sizeof(pnode_version));
      pnode_version *pv = (pnode_version *)valptr;
      return makestruct(pql, pv->pnum, pv->version, do_versions);
    }

    case PROV_TYPE_OBJECT:
    case PROV_TYPE_OBJECTVERSION:
     assert(0);
    default:
     break;

   }
   assert(0);
   return NULL;
}

/*
 * Return true if the on-disk attribute name SEENATTR matches the
 * edge name EDGE that came from the PQL engine.
 */
static bool edge_matches(const char *seenattr, const pqlvalue *edge) {
   if (edge == NULL) {
      /* all edges */
      return true;
   }

   if (pqlvalue_isstring(edge)) {
      const char *edgename = pqlvalue_string_get(edge);
      if (!strcmp(seenattr, edgename)) {
	 return true;
      }
   }
   else if (pqlvalue_isint(edge)) {
      char *t;
      errno = 0;
      long x = strtol(seenattr, &t, 0);
      if (errno == 0 && strlen(t) == 0) {
	 if (x == pqlvalue_int_get(edge)) {
	    return true;
	 }
      }
   }
   return false;
}

/*
 * Return true if the dbvalue DBVAL points to the object PNUM.
 */
static bool pnode_matches(provdb_val *dbval, pnode_t pnum, version_t version,
			  bool do_versions) {
   pnode_version *pv;

   switch (PROVDB_VAL_VALUETYPE(dbval)) {

    case PROV_TYPE_PNODE:
     return *(pnode_t *)PROVDB_VAL_VALUE(dbval) == pnum;

    case PROV_TYPE_PNODEVERSION:
     pv = (pnode_version *)PROVDB_VAL_VALUE(dbval);
     if (do_versions) {
	return pv->pnum == pnum && pv->version == version;
     }
     else {
	return pv->pnum == pnum;
     }

    default:
     break;
   }

   return false;
}

////////////////////////////////////////////////////////////
// Search: scan whole database

/*
 * Scan the database of all objects and return a set referring to
 * each one. If the EDGENAME passed in is not null, pair each object
 * with that edge name as needed for implementing followall().
 */

/*
 * Operation context.
 */
struct scan_state {
   struct pqlcontext *pql;		/* PQL context */
   const char *edgename;		/* Edge name to insert, if any */
   pnode_t prev_pnode;			/* Last pnode number seen */
   version_t prev_version;		/* Last version number seen */
   bool do_versions;			/* Are we distinguishing versions? */
   struct pqlvalue *ret;		/* Result set */
};

/*
 * Visitor function: add each distinct object (different pnode number,
 * or pnode number and version) to the result set.
 */
static int scan_sub(void *sv, DBT *key, DBT * /*val*/) {
   struct scan_state *s = (struct scan_state *)sv;
   struct pnode_version *dbkey;
   struct pqlvalue *pqlval;

   assert(key->size == sizeof(struct pnode_version));
   dbkey = (struct pnode_version *)key->data;

   if (dbkey->pnum != s->prev_pnode ||
       (s->do_versions && dbkey->version != s->prev_version)) {
      /*
       * We're now seeing the records for a different object, so add
       * that object to the result set.
       */
      pqlval = makestruct(s->pql, dbkey->pnum, dbkey->version, s->do_versions);
      if (s->edgename != NULL) {
	 pqlval = pqlvalue_pair(s->pql,
				pqlvalue_string(s->pql, s->edgename), pqlval);
      }
      pqlvalue_set_add(s->ret, pqlval);
      s->prev_pnode = dbkey->pnum;
   }
   return 0;
}

/*
 * Driver function; initialize and use visit_everything.
 */
static struct pqlvalue *scan(struct pqlcontext *pql, const char *edgename,
			     bool do_versions) {
   scan_state s;
   s.pql = pql;
   s.edgename = edgename;
   s.prev_pnode = 0;
   s.prev_version = 0;
   s.do_versions = do_versions;
   s.ret = pqlvalue_emptyset(pql);
   dbvisit_everything(g_provdb->db, &s, scan_sub);
   return s.ret;
}

////////////////////////////////////////////////////////////
// Search: forward.

/*
 * Retrieve the values of forward edges (fields) of an object. If the
 * EDGE passed in is not null, retrieve only values for that edge
 * name. Otherwise, return edge name / value pairs for *all* records
 * associated with the object.
 */

/*
 * Operation context.
 */
struct forward_state {
   struct pqlcontext *pql;		/* PQL context */
   pnode_t pnode;			/* object we're examining */
   version_t thisversion;		/* version we're examining */
   version_t highversion;		/* highest version seen */
   const struct pqlvalue *edge;		/* edge name to follow */
   bool do_versions;			/* Are we distinguishing versions? */
   struct pqlvalue *ret;		/* result set */
};

/*
 * Visitor function: add matching values to result set, stop when we
 * reach the next pnode.
 */
static int follow_forward_sub(void *sv, DBT *key, DBT *val) {
   struct forward_state *s = (forward_state *)sv;
   provdb_key *dbkey;
   provdb_val *dbval;
   size_t dbval_len;
   const char *attr;
   struct pqlvalue *pqlval;

   dbkey = (provdb_key *) key->data;
   dbval = (provdb_val *) val->data;
   dbval_len = val->size;

   if (dbkey->pnum != s->pnode) {
      /* We've reached the next pnode; stop. */
      return 1;
   }

   if (s->do_versions && dbkey->version != s->thisversion) {
      /* We've reached the next pnode; stop. */
      return 1;
   }

   if (dbkey->version > s->highversion) {
      s->highversion = dbkey->version;
   }

   attr = PROVDB_VAL_ATTR(dbval);
   if (edge_matches(attr, s->edge)) {
      pqlval = dbval_to_pql(s->pql, dbval, dbval_len, s->do_versions);
      if (s->edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(s->pql, pqlvalue_string(s->pql, attr), pqlval);
      }
      pqlvalue_set_add(s->ret, pqlval);
   }

   return 0;
}

/*
 * Driver function; initialize and use visit_by_setrange.
 */
static struct pqlvalue *follow_forward(struct pqlcontext *pql,
				       const struct pqlvalue *obj,
				       const struct pqlvalue *edge,
				       bool do_versions) {
   struct forward_state s;
   DBT key;
   struct pnode_version k;
   const char *attr;
   struct pqlvalue *pqlval;

   s.pql = pql;
   s.pnode = pqlvalue_struct_getoid(obj);
   s.thisversion = pqlvalue_struct_getsubid(obj);
   s.highversion = 1;
   s.edge = edge;
   s.do_versions = do_versions;
   s.ret = pqlvalue_emptyset(pql);

   bzero(&key, sizeof(key));
   k.pnum = s.pnode;
   k.version = do_versions ? s.thisversion : 0;
   key.data = &k;
   key.size = sizeof(k);

   dbvisit_setrange(g_provdb->db, &key, &s, follow_forward_sub);

   /*
    * Magic edges.
    */
   attr = "LATEST_VERSION";
   if (!do_versions && edge_matches(attr, edge)) {
      pqlval = pqlvalue_int(pql, s.highversion);
      if (edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(pql, pqlvalue_string(pql, attr), pqlval);
      }
      pqlvalue_set_add(s.ret, pqlval);
   }
   attr = "PNODE";
   if (edge_matches(attr, edge)) {
      // XXX this won't necessarily fit, PQL should have either a u64 type
      // of some kind or an exposed oid type.
      pqlval = pqlvalue_int(pql, s.pnode);
      if (edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(pql, pqlvalue_string(pql, attr), pqlval);
      }
      pqlvalue_set_add(s.ret, pqlval);
   }
   attr = "VERSION";
   if (do_versions && edge_matches(attr, edge)) {
      pqlval = pqlvalue_int(pql, s.thisversion);
      if (edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(pql, pqlvalue_string(pql, attr), pqlval);
      }
      pqlvalue_set_add(s.ret, pqlval);
   }
   attr = "PREV_VERSION";
   if (do_versions && edge_matches(attr, edge) && s.thisversion > 1) {
      pqlval = pqlvalue_struct(pql, DBNUM_VERS, s.pnode, s.thisversion-1);
      if (edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(pql, pqlvalue_string(pql, attr), pqlval);
      }
      pqlvalue_set_add(s.ret, pqlval);
   }
   attr = "BASE_VERSION";
   if (do_versions && edge_matches(attr, edge)) {
      pqlval = pqlvalue_struct(pql, DBNUM_VERS, s.pnode, 0);
      if (edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(pql, pqlvalue_string(pql, attr), pqlval);
      }
      pqlvalue_set_add(s.ret, pqlval);
   }

   return s.ret;
}

/*
 * Wrapper for followall.
 */
static struct pqlvalue *followall_forward(struct pqlcontext *pql,
					  const struct pqlvalue *obj,
					  bool do_versions) {
   return follow_forward(pql, obj, NULL, do_versions);
}

////////////////////////////////////////////////////////////
// Search: backwards.

/*
 * Retrieve the values of backward edges of (fields pointing to) an
 * object. If the EDGE passed in is not null, retrieve only values for
 * that edge name. Otherwise, return edge name / value pairs for *all*
 * matching records.
 */

/*
 * Operation context for the first pass.
 */
struct backward_state {
   pnode_t pnode;			/* Object we're looking for. */
   version_t version;			/* Version we're looking for. */
   bool do_versions;			/* Are we distinguishing versions? */
   primarray<pnode_t> children;		/* Objects that refer to it. */
   primarray<version_t> childvers;	/* Versions thereof. */
};

/*
 * Visitor function: collect the objects pointing to us and stop when
 * we see data for the next object.
 */
static int follow_backward_sub(void *sv, DBT *key, DBT *val) {
   struct backward_state *s = (backward_state *)sv;
   struct pnode_version *k, *v;

   assert(key->size == sizeof(pnode_version));
   assert(val->size == sizeof(pnode_version));

   k = (pnode_version *)key->data;
   v = (pnode_version *)val->data;

   if (k->pnum != s->pnode) {
      /* reached next object; stop */
      return 1;
   }

   if (s->do_versions && k->version != s->version) {
      /* reached next version; stop */
      return 1;
   }

   s->children.add(v->pnum);
   if (s->do_versions) {
      s->childvers.add(v->version);
   }

   return 0;
}

/*
 * Operation context for the second pass.
 */
struct backward_check_state {
   struct pqlcontext *pql;		/* PQL context */
   pnode_t pnode;			/* Object we're checking. */
   version_t version;			/* Version we're checking. */
   const pqlvalue *edge;		/* Edge we're looking for, or null. */
   pnode_t target;			/* Object we're looking for. */
   version_t targetver;			/* Version we're looking for. */
   bool do_versions;			/* Are we distinguishing versions? */
   struct pqlvalue *ret;		/* Result set. */
};

/*
 * Visitor function: check for records that point from PNODE to TARGET
 * with edge name EDGE. Stop when we've seen all the records for PNODE.
 */
static int follow_backward_check_sub(void *sv, DBT *key, DBT *val) {
   struct backward_check_state *s = (backward_check_state *)sv;
   pnode_version *dbkey;
   provdb_val *dbval;
   const char *attr;
   struct pqlvalue *pqlval;

   assert(key->size == sizeof(pnode_version));

   dbkey = (provdb_key *)key->data;
   dbval = (provdb_val *)val->data;

   if (dbkey->pnum != s->pnode) {
      /* reached next pnode; stop */
      return 1;
   }

   if (s->do_versions && dbkey->version != s->version) {
      /* reached next version; stop */
      return 1;
   }

   attr = PROVDB_VAL_ATTR(dbval);
   if (edge_matches(attr, s->edge) &&
       pnode_matches(dbval, s->target, s->targetver, s->do_versions)) {
      pqlval = makestruct(s->pql, s->pnode, s->version, s->do_versions);
      if (s->edge == NULL) {
	 /* followall case */
	 pqlval = pqlvalue_pair(s->pql, pqlvalue_string(s->pql, attr), pqlval);
      }
      pqlvalue_set_add(s->ret, pqlval);
   }

   return 0;
}

/*
 * Driver function.
 *
 * Because the child index currently has the wrong stuff in it (it
 * doesn't have edge names) we have to first go backwards using the
 * child index, which retrieves all pnodes that point to OBJ, and then
 * search forwards again to see which of them point to OBJ with edge
 * name EDGE. (Or, if EDGE is null, to find out what edge names they
 * point to OBJ under.)
 *
 * Blah.
 */
static struct pqlvalue *follow_backward(struct pqlcontext *pql,
					const struct pqlvalue *obj,
					const struct pqlvalue *edge,
					bool do_versions) {
   DBT key;
   backward_state s;
   pnode_version k;
   struct pqlvalue *ret;

   /*
    * Backwards step: get list of pnodes.
    */

   s.pnode = pqlvalue_struct_getoid(obj);
   s.version = pqlvalue_struct_getsubid(obj);
   s.do_versions = do_versions;

   k.pnum = s.pnode;
   k.version = 0;

   bzero(&key, sizeof(key));
   key.data = &k;
   key.size = sizeof(k);

   dbvisit_setrange(g_childdb->db, &key, &s, follow_backward_sub);

   /*
    * Forwards step: we have the list of pnodes; now check which ones
    * have a suitable edge and/or extract the edge names.
    */

   ret = pqlvalue_emptyset(pql);
   for (int i=0; i<s.children.num(); i++) {
      backward_check_state s2;

      k.pnum = s.children[i];
      k.version = 0;

      bzero(&key, sizeof(key));
      key.data = &k;
      key.size = sizeof(k);

      s2.pql = pql;
      s2.pnode = s.children[i];
      s2.version = do_versions ? s.childvers[i] : 0;
      s2.edge = edge;
      s2.target = s.pnode;
      s2.targetver = s.version;
      s2.do_versions = do_versions;
      s2.ret = ret;

      dbvisit_setrange(g_provdb->db, &key, &s2, follow_backward_check_sub);
   }

   return ret;
}

/*
 * Wrapper for followall case.
 */
static struct pqlvalue *followall_backward(struct pqlcontext *pql,
					   const struct pqlvalue *obj,
					   bool do_versions) {
   return follow_backward(pql, obj, NULL, do_versions);
}

////////////////////////////////////////////////////////////
// Implementation of magic (dynamically generated) objects

/*
 * The "Provenance" object.
 */

static struct pqlvalue *magic_provenance_follow(struct pqlcontext *pql,
						const struct pqlvalue *edge,
						bool backwards) {
   const char *edgestr;

   if (backwards || !pqlvalue_isstring(edge)) {
      return pqlvalue_emptyset(pql);
   }

   edgestr = pqlvalue_string_get(edge);
   if (!strcmp(edgestr, "obj")) {
      return scan(pql, NULL, false);
   }
   // else ... // Provenance.file? Provenance.proc?
   // XXX think about what we want.
   return pqlvalue_emptyset(pql);
}

static struct pqlvalue *magic_provenance_followall(struct pqlcontext *pql,
						   bool backwards) {
   if (backwards) {
      return pqlvalue_emptyset(pql);
   }
   return scan(pql, "obj", false);
}

/*
 * Dispatchers.
 */

static struct pqlvalue *magic_follow(struct pqlcontext *pql,
				     const struct pqlvalue *obj,
				     const struct pqlvalue *edge,
				     bool backwards) {
   pqloid_t oid;

   oid = pqlvalue_struct_getoid(obj);
   switch (oid) {
    case MAGIC_PROVENANCE:
     return magic_provenance_follow(pql, edge, backwards);
    default:
     return pqlvalue_emptyset(pql);
   }
}

static struct pqlvalue *magic_followall(struct pqlcontext *pql,
					const struct pqlvalue *obj,
					bool backwards) {
   pqloid_t oid;

   oid = pqlvalue_struct_getoid(obj);
   switch (oid) {
    case MAGIC_PROVENANCE:
     return magic_provenance_followall(pql, backwards);
    default:
     return pqlvalue_emptyset(pql);
   }
}

////////////////////////////////////////////////////////////
// Entry points for PQL backend operations

/*
 * Initialize the backend.
 */
int backend_init(const char *dbpath) {
   return wdb_startup(dbpath, WDB_O_RDONLY);
}

/*
 * Shut down the back end and tidy up.
 */
int backend_shutdown(void) {
   return wdb_shutdown();
}

/*
 * Read a global variable.
 */
static struct pqlvalue *backend_read_global(struct pqlcontext *pql,
					    const char *name) {
   if (!strcmp(name, "Provenance")) {
      struct pqlvalue *ret, *val;

      val = pqlvalue_struct(pql, DBNUM_MAGIC, MAGIC_PROVENANCE, 0);

      ret = pqlvalue_emptyset(pql);
      pqlvalue_set_add(ret, val);
      return ret;
   }
   else if (!strcmp(name, "VERSIONS")) {
      return scan(pql, NULL, true);
   }
   else {
      return pqlvalue_nil(pql);
   }
}

/*
 * Create a new (temporary) object.
 */
static struct pqlvalue *backend_newobject(struct pqlcontext *pql) {
   pqloid_t oid;

   if (temp_tdb == NULL) {
      temp_tdb = tdb_create();
   }

   oid = tdb_newobject(temp_tdb);
   if (oid == PQLOID_INVALID) {
      return NULL;
   }

   return pqlvalue_struct(pql, DBNUM_TEMP, oid, 0);
}

/*
 * Assign a field of a (temporary) object.
 */
static int backend_assign(struct pqlcontext *pql, struct pqlvalue *obj,
		   const struct pqlvalue *edge, const struct pqlvalue *val) {
   int dbnum;

   assert(pqlvalue_isstruct(obj));

   dbnum = pqlvalue_struct_getdbnum(obj);

   if (dbnum == DBNUM_TEMP) {
      assert(temp_tdb != NULL);
      return tdb_assign(pql, temp_tdb, pqlvalue_struct_getoid(obj), edge, val);
   }
   else {
      errno = EROFS;
      return -1;
   }
}

/*
 * Follow an edge from an object.
 */
static struct pqlvalue *backend_follow(struct pqlcontext *pql,
				       const struct pqlvalue *obj,
				       const struct pqlvalue *edge,
				       bool backwards) {
   int dbnum;

   assert(pqlvalue_isstruct(obj));

   dbnum = pqlvalue_struct_getdbnum(obj);
   switch (dbnum) {
    case DBNUM_TEMP:
     assert(temp_tdb != NULL);
     return tdb_follow(pql, temp_tdb, pqlvalue_struct_getoid(obj), edge,
		       backwards);

    case DBNUM_MAGIC:
     return magic_follow(pql, obj, edge, backwards);

    case DBNUM_ARGV:
     return tdb_follow(pql, prov_argv_tdb, pqlvalue_struct_getoid(obj), edge,
		       backwards);

    case DBNUM_MAIN:
     if (backwards) {
	return follow_backward(pql, obj, edge, false);
     }
     else {
	return follow_forward(pql, obj, edge, false);
     }

    case DBNUM_VERS:
     if (backwards) {
	return follow_backward(pql, obj, edge, true);
     }
     else {
	return follow_forward(pql, obj, edge, true);
     }

    default:
     assert(0);
     return pqlvalue_emptyset(pql);
   }
}

static struct pqlvalue *backend_followall(struct pqlcontext *pql,
					  const struct pqlvalue *obj,
					  bool backwards) {
   int dbnum;

   assert(pqlvalue_isstruct(obj));

   dbnum = pqlvalue_struct_getdbnum(obj);
   switch (dbnum) {

    case DBNUM_TEMP:
     assert(temp_tdb != NULL);
     // XXX this leaks memory
     return tdb_followall(pql, temp_tdb, pqlvalue_struct_getoid(obj),
			  backwards);

    case DBNUM_MAGIC:
     return magic_followall(pql, obj, backwards);

    case DBNUM_ARGV:
     return tdb_followall(pql, prov_argv_tdb, pqlvalue_struct_getoid(obj),
			  backwards);
    case DBNUM_MAIN:
     if (backwards) {
	return followall_backward(pql, obj, false);
     }
     else {
	return followall_forward(pql, obj, false);
     }

    case DBNUM_VERS:
     if (backwards) {
	return followall_backward(pql, obj, true);
     }
     else {
	return followall_forward(pql, obj, true);
     }

    default:
     assert(0);
     return pqlvalue_emptyset(pql);
   }
}

static pqlvalue *backend_read_global(pqlcontext *, const char *name);
static pqlvalue *backend_newobject(pqlcontext *);
static int backend_assign(pqlcontext *,
			  pqlvalue *obj, const pqlvalue *edge, const pqlvalue *val);
static pqlvalue *backend_follow(pqlcontext *,
				const pqlvalue *obj, const pqlvalue *edge,
				bool reversed);
static pqlvalue *backend_followall(pqlcontext *, const pqlvalue *obj, bool reversed);

const struct pqlbackend_ops myops = {
   backend_read_global,
   backend_newobject,
   backend_assign,
   backend_follow,
   backend_followall,
};
