/*
 * Copyright 2010
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

#include <sys/types.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include "pql.h"
#include "pqlutil.h"

/*
 * linux
 */
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif
#ifndef ERPCMISMATCH
#define ERPCMISMATCH EINVAL
#endif

/*
 * Representation version
 */
#define PQLPICKLEVERSION 0

/*
 * Type encoding codes
 */
#define TC_NIL		0x00
#define TC_BOOL		0x01
#define TC_POSINT	0x02
#define TC_NEGINT	0x03
#define TC_FLOAT	0x04
#define TC_STRING	0x05
#define TC_STRUCT	0x06
#define TC_PATHELEMENT	0x07
#define TC_TUPLE	0x08
#define TC_SET		0x09
#define TC_SEQUENCE	0x0a

/*
 * Info used during encoding/decoding
 */
struct pqlpickleinfo {
   bool vaxfloats;			/* float values are VAX floats */
   bool forceswapfloats;		/* see discussion below */
};

/*
 * Context for encoding
 */
struct pqlpicklecontext {
   struct pqlpickleinfo info;
   struct pqlpickleblob *blob;
};

/*
 * Context for decoding
 */
struct pqlunpicklecontext {
   struct pqlcontext *pql;
   struct pqlpickleinfo info;
   size_t pos;
   const unsigned char *data;
   size_t len;
};

////////////////////////////////////////////////////////////

/*
 * Figure out stuff that depends on the machine type.
 */
static void pqlpickle_getinfo(struct pqlpickleinfo *info) {
   struct utsname u;
   bool little_endian;

   /* defaults for ordinary machine */
   info->vaxfloats = false;
   info->forceswapfloats = false;

   if (uname(&u) < 0) {
      /* assume ordinary machine */
      return;
   }

   if (!strcmp(u.machine, "vax") || !strcmp(u.machine, "VAX")) {
      info->vaxfloats = true;
   }

   little_endian = htons(0x1234) != 0x1234;
   if (!strcmp(u.machine, "mipsel") |\
       (!strcmp(u.machine, "mips") && !little_endian)) {
      /* As I recall all mips floats are always LE (XXX check this!) */
      info->forceswapfloats = true;
   }
}

////////////////////////////////////////////////////////////

static void pqlpicklecontext_init(struct pqlpicklecontext *ctx,
				  struct pqlpickleblob *blob) {
   ctx->blob = blob;
   pqlpickle_getinfo(&ctx->info);
}

static void pqlpicklecontext_cleanup(struct pqlpicklecontext *ctx) {
   /* nothing */
   (void)ctx;
}

static void pqlunpicklecontext_init(struct pqlunpicklecontext *ctx,
				    struct pqlcontext *pql,
				    const void *data, size_t len) {
   ctx->pql = pql;
   ctx->pos = 0;
   ctx->data = data;
   ctx->len = len;
   pqlpickle_getinfo(&ctx->info);
}

static void pqlunpicklecontext_cleanup(struct pqlunpicklecontext *ctx) {
   /* nothing */
   (void)ctx;
}

////////////////////////////////////////////////////////////

static void pqlpickleblob_init(struct pqlpickleblob *blob) {
   blob->data = NULL;
   blob->len = 0;
   blob->maxlen = 0;
}

void pqlpickleblob_cleanup(struct pqlpickleblob *blob) {
   free(blob->data);
   blob->data = NULL;
   blob->len = 0;
   blob->maxlen = 0;
}

static int pqlpickleblob_setsize(struct pqlpickleblob *blob, size_t newlen) {
   void *newptr;
   size_t newmax;

   if (newlen > blob->maxlen) {
      newmax = blob->maxlen;
      while (newlen > newmax) {
	 newmax = newmax ? newmax * 2 : 8;
      }
      errno = 0;
      newptr = realloc(blob->data, newmax);
      if (newptr == NULL) {
	 if (errno == 0) {
	    errno = ENOMEM;
	 }
	 return -1;
      }
      blob->data = newptr;
      blob->maxlen = newmax;
   }
   blob->len = newlen;
   return 0;
}

////////////////////////////////////////////////////////////

static int pqlpickle_putchars(const struct pqlpicklecontext *ctx,
			      const char *str, size_t len) {
   size_t pos;

   pos = ctx->blob->len;
   if (pqlpickleblob_setsize(ctx->blob, pos + len)) {
      return -1;
   }
   memcpy((unsigned char *)ctx->blob->data + pos, str, len);
   return 0;
}

static int pqlpickle_put8(struct pqlpicklecontext *ctx, uint8_t val) {
   size_t pos;

   pos = ctx->blob->len;
   if (pqlpickleblob_setsize(ctx->blob, pos + 1)) {
      return -1;
   }
   ctx->blob->data[pos] = val;
   return 0;
}

static int pqlpickle_put32(struct pqlpicklecontext *ctx, uint32_t val) {
   size_t pos;

   pos = ctx->blob->len;
   if (pqlpickleblob_setsize(ctx->blob, pos + 4)) {
      return -1;
   }
   ctx->blob->data[pos] = val >> 24;
   ctx->blob->data[pos+1] = (val >> 16) & 0xff;
   ctx->blob->data[pos+2] = (val >> 8) & 0xff;
   ctx->blob->data[pos+3] = val & 0xff;
   return 0;
}

static int pqlpickle_put64(struct pqlpicklecontext *ctx, uint64_t val) {
   size_t pos;

   pos = ctx->blob->len;
   if (pqlpickleblob_setsize(ctx->blob, pos + 8)) {
      return -1;
   }
   ctx->blob->data[pos] = val >> 56;
   ctx->blob->data[pos+1] = (val >> 48) & 0xff;
   ctx->blob->data[pos+2] = (val >> 40) & 0xff;
   ctx->blob->data[pos+3] = (val >> 32) & 0xff;
   ctx->blob->data[pos+4] = (val >> 24) & 0xff;
   ctx->blob->data[pos+5] = (val >> 16) & 0xff;
   ctx->blob->data[pos+6] = (val >> 8) & 0xff;
   ctx->blob->data[pos+7] = val & 0xff;
   return 0;
}

static int pqlunpickle_getchars(struct pqlunpicklecontext *ctx,
				char *buf, size_t len) {
   if (ctx->pos + len > ctx->len) {
      /* unexpected EOT */
      errno = EINVAL;
      return -1;
   }
   memcpy(buf, ctx->data + ctx->pos, len);
   ctx->pos += len;
   return 0;
}

static int pqlunpickle_get8(struct pqlunpicklecontext *ctx, uint8_t *val) {
   if (ctx->pos + 1 > ctx->len) {
      /* unexpected EOT */
      errno = EINVAL;
      return -1;
   }
   *val = ctx->data[ctx->pos];
   ctx->pos++;
   return 0;
}

static int pqlunpickle_get32(struct pqlunpicklecontext *ctx, uint32_t *val) {
   if (ctx->pos + 4 > ctx->len) {
      /* unexpected EOT */
      errno = EINVAL;
      return -1;
   }
   *val =
      (((uint32_t)ctx->data[ctx->pos]) << 24) |
      (((uint32_t)ctx->data[ctx->pos+1]) << 16) |
      (((uint32_t)ctx->data[ctx->pos+2]) << 8) |
      (((uint32_t)ctx->data[ctx->pos+3]));
   ctx->pos += 4;
   return 0;
}

static int pqlunpickle_get64(struct pqlunpicklecontext *ctx, uint64_t *val) {
   if (ctx->pos + 8 > ctx->len) {
      /* unexpected EOT */
      errno = EINVAL;
      return -1;
   }
   *val =
      (((uint64_t)ctx->data[ctx->pos]) << 56) |
      (((uint64_t)ctx->data[ctx->pos+1]) << 48) |
      (((uint64_t)ctx->data[ctx->pos+2]) << 40) |
      (((uint64_t)ctx->data[ctx->pos+3]) << 32) |
      (((uint64_t)ctx->data[ctx->pos+4]) << 24) |
      (((uint64_t)ctx->data[ctx->pos+5]) << 16) |
      (((uint64_t)ctx->data[ctx->pos+6]) << 8) |
      (((uint64_t)ctx->data[ctx->pos+7]));
   ctx->pos += 8;
   return 0;
}

static int pqlpickle_putlength(struct pqlpicklecontext *ctx, size_t len) {
   if (len < 0xff) {
      if (pqlpickle_put8(ctx, len)) {
	 return -1;
      }
   }
   else if (len < 0xffffffff) {
      if (pqlpickle_put8(ctx, 0xff)) {
	 return -1;
      }
      if (pqlpickle_put32(ctx, len)) {
	 return -1;
      }
   }
   else {
      if (pqlpickle_put8(ctx, 0xff)) {
	 return -1;
      }
      if (pqlpickle_put32(ctx, 0xffffffff)) {
	 return -1;
      }
      if (pqlpickle_put64(ctx, len)) {
	 return -1;
      }
   }
   return 0;
}

static int pqlunpickle_getlength(struct pqlunpicklecontext *ctx, size_t *len) {
   uint8_t u8;
   uint32_t u32;
   uint64_t u64;

   if (pqlunpickle_get8(ctx, &u8)) {
      return -1;
   }
   if (u8 < 0xff) {
      *len = u8;
      return 0;
   }

   if (pqlunpickle_get32(ctx, &u32)) {
      return -1;
   }
   if (u32 < 0xffffffff) {
      *len = u32;
      return 0;
   }

   if (pqlunpickle_get64(ctx, &u64)) {
      return -1;
   }
   if (u64 > SIZE_MAX) {
      errno = EMSGSIZE;
      return -1;
   }
   *len = u64;
   return 0;
}

static int pqlpickle_putnum(struct pqlpicklecontext *ctx, unsigned num) {
   if (num < 0xff) {
      if (pqlpickle_put8(ctx, num)) {
	 return -1;
      }
   }
   else {
      if (pqlpickle_put8(ctx, 0xff)) {
	 return -1;
      }
      if (pqlpickle_put32(ctx, num)) {
	 return -1;
      }
   }
   return 0;
}

static int pqlunpickle_getnum(struct pqlunpicklecontext *ctx, unsigned *num) {
   uint8_t u8;
   uint32_t u32;

   if (pqlunpickle_get8(ctx, &u8)) {
      return -1;
   }
   if (u8 < 0xff) {
      *num = u8;
      return 0;
   }
   if (pqlunpickle_get32(ctx, &u32)) {
      return -1;
   }
   *num = u32;
   return 0;
}

////////////////////////////////////////////////////////////

static int pqlpickle_value(struct pqlpicklecontext *ctx,
			   const struct pqlvalue *val);
static struct pqlvalue *pqlunpickle_value(struct pqlunpicklecontext *ctx);

static int pqlpickle_header(struct pqlpicklecontext *ctx) {
   if (pqlpickle_putchars(ctx, "PQL", 4)) {
      return -1;
   }
   if (pqlpickle_put8(ctx, PQLPICKLEVERSION)) {
      return -1;
   }
   if (pqlpickle_put8(ctx, ctx->info.vaxfloats ? 1 : 0)) {
      return -1;
   }
   return 0;
}

static int pqlunpickle_header(struct pqlunpicklecontext *ctx) {
   char block[4];
   uint8_t ch;
   bool vaxfloats;

   if (pqlunpickle_getchars(ctx, block, 4)) {
      return -1;
   }
   if (memcmp("PQL", block, 4) != 0) {
      errno = EFTYPE;
      return -1;
   }
   if (pqlunpickle_get8(ctx, &ch)) {
      return -1;
   }
   if (ch != PQLPICKLEVERSION) {
      errno = ERPCMISMATCH;
      return -1;
   }
   if (pqlunpickle_get8(ctx, &ch)) {
      return -1;
   }
   vaxfloats = ch != 0;
   if (vaxfloats != ctx->info.vaxfloats) {
      errno = EPROTONOSUPPORT;
   }
   return 0;
}

static int pqlpickle_bool(struct pqlpicklecontext *ctx, bool b) {
   if (pqlpickle_put8(ctx, TC_BOOL)) {
      return -1;
   }
   if (pqlpickle_put8(ctx, b ? 1 : 0)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_bool(struct pqlunpicklecontext *ctx) {
   uint8_t ch;

   if (pqlunpickle_get8(ctx, &ch)) {
      return NULL;
   }
   return pqlvalue_bool(ctx->pql, ch != 0);
}

static int pqlpickle_int(struct pqlpicklecontext *ctx, int i) {
   uint32_t u;

   if (i < 0) {
      if (i == INT_MIN) {
	 /* avoid overflow, just in case */
	 u = (-(i+1)) + 1;
      }
      else {
	 u = -i;
      }
   }
   else {
      u = i;
   }

   if (pqlpickle_put8(ctx, i < 0 ? TC_NEGINT : TC_POSINT)) {
      return -1;
   }
   if (pqlpickle_put32(ctx, u)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_posint(struct pqlunpicklecontext *ctx) {
   uint32_t u;

   if (pqlunpickle_get32(ctx, &u)) {
      return NULL;
   }
   return pqlvalue_int(ctx->pql, u);
}

static struct pqlvalue *pqlunpickle_negint(struct pqlunpicklecontext *ctx) {
   uint32_t u;
   int i;

   if (pqlunpickle_get32(ctx, &u)) {
      return NULL;
   }
   if (u > 0) {
      i = (-(u-1) - 1);
   }
   else {
      i = -u;
   }
   return pqlvalue_int(ctx->pql, i);
}

static int pqlpickle_float(struct pqlpicklecontext *ctx, double f) {
   uint64_t u;

   if (sizeof(u) != sizeof(f)) {
      errno = EDOM;
      return -1;
   }

   memcpy(&u, &f, sizeof(f));
   if (ctx->info.forceswapfloats) {
      u = (((u & 0xff00000000000000ULL) >> 56) |
	   ((u & 0x00ff000000000000ULL) >> 40) |
	   ((u & 0x0000ff0000000000ULL) >> 24) |
	   ((u & 0x000000ff00000000ULL) >> 8) |
	   ((u & 0x00000000ff000000ULL) << 8) |
	   ((u & 0x0000000000ff0000ULL) << 24) |
	   ((u & 0x000000000000ff00ULL) << 40) |
	   ((u & 0x00000000000000ffULL) << 56));
   }
   if (pqlpickle_put8(ctx, TC_FLOAT)) {
      return -1;
   }
   if (pqlpickle_put64(ctx, u)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_float(struct pqlunpicklecontext *ctx) {
   uint64_t u;
   double f;

   if (sizeof(u) != sizeof(f)) {
      errno = EDOM;
      return NULL;
   }
   if (pqlunpickle_get64(ctx, &u)) {
      return NULL;
   }
   if (ctx->info.forceswapfloats) {
      u = (((u & 0xff00000000000000ULL) >> 56) |
	   ((u & 0x00ff000000000000ULL) >> 40) |
	   ((u & 0x0000ff0000000000ULL) >> 24) |
	   ((u & 0x000000ff00000000ULL) >> 8) |
	   ((u & 0x00000000ff000000ULL) << 8) |
	   ((u & 0x0000000000ff0000ULL) << 24) |
	   ((u & 0x000000000000ff00ULL) << 40) |
	   ((u & 0x00000000000000ffULL) << 56));
   }
   memcpy(&f, &u, sizeof(u));
   /* XXX is this enough to avoid croaking if sent garbage? */
   if (isnan(f)) {
      errno = ERANGE;
      return NULL;
   }
   return pqlvalue_float(ctx->pql, f);
}

static int pqlpickle_string(struct pqlpicklecontext *ctx, const char *s) {
   size_t len;

   len = strlen(s);
   if (pqlpickle_put8(ctx, TC_STRING)) {
      return -1;
   }
   if (pqlpickle_putlength(ctx, len)) {
      return -1;
   }
   if (pqlpickle_putchars(ctx, s, len)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_string(struct pqlunpicklecontext *ctx) {
   size_t len;
   char *buf;
   int sverrno;

   if (pqlunpickle_getlength(ctx, &len)) {
      return NULL;
   }

   errno = 0;
   buf = malloc(len+1);
   if (buf == NULL) {
      if (errno == 0) {
	 errno = ENOMEM;
      }
      return NULL;
   }
   if (pqlunpickle_getchars(ctx, buf, len)) {
      sverrno = errno;
      free(buf);
      errno = sverrno;
      return NULL;
   }
   buf[len] = 0;
   // XXX maybe we should expose this after all
   //return pqlvalue_string_consume(ctx->pql, buf);
   struct pqlvalue *ret;
   ret = pqlvalue_string(ctx->pql, buf);
   free(buf);
   return ret;
}

static int pqlpickle_struct(struct pqlpicklecontext *ctx,
			    const struct pqlvalue *val) {
   /*
    * XXX this should not assume it's a database object
    * (fix after internals are cleaned up in this regard)
    *
    * XXX should we be knowing that pqloid_t is a uint64?
    */
   int dbnum;
   pqloid_t oid;
   pqlsubid_t subid;

   dbnum = pqlvalue_struct_getdbnum(val);
   oid = pqlvalue_struct_getoid(val);
   subid = pqlvalue_struct_getsubid(val);

   if (pqlpickle_put8(ctx, TC_STRUCT)) {
      return -1;
   }
   if (pqlpickle_put32(ctx, dbnum)) {
      return -1;
   }
   if (pqlpickle_put64(ctx, oid)) {
      return -1;
   }
   if (pqlpickle_put64(ctx, subid)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_struct(struct pqlunpicklecontext *ctx) {
   uint32_t dbnum;
   uint64_t oid;
   uint64_t subid;

   if (pqlunpickle_get32(ctx, &dbnum)) {
      return NULL;
   }
   if (pqlunpickle_get64(ctx, &oid)) {
      return NULL;
   }
   if (pqlunpickle_get64(ctx, &subid)) {
      return NULL;
   }
   return pqlvalue_struct(ctx->pql, dbnum, oid, subid);
}

static int pqlpickle_pathelement(struct pqlpicklecontext *ctx,
				 const struct pqlvalue *val) {
   const struct pqlvalue *left, *edge, *right;

   left = pqlvalue_pathelement_getleftobj(val);
   edge = pqlvalue_pathelement_getedgename(val);
   right = pqlvalue_pathelement_getrightobj(val);

   if (pqlpickle_put8(ctx, TC_PATHELEMENT)) {
      return -1;
   }
   if (pqlpickle_value(ctx, left)) {
      return -1;
   }
   if (pqlpickle_value(ctx, edge)) {
      return -1;
   }
   if (pqlpickle_value(ctx, right)) {
      return -1;
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_pathelement(struct pqlunpicklecontext*ctx){
   struct pqlvalue *left, *edge, *right;

   left = pqlunpickle_value(ctx);
   if (left == NULL) {
      return NULL;
   }
   edge = pqlunpickle_value(ctx);
   if (edge == NULL) {
      pqlvalue_destroy(left);
      return NULL;
   }
   right = pqlunpickle_value(ctx);
   if (right == NULL) {
      pqlvalue_destroy(left);
      pqlvalue_destroy(edge);
      return NULL;
   }

   return pqlvalue_pathelement(ctx->pql, left, edge, right);
}

static int pqlpickle_tuple(struct pqlpicklecontext *ctx,
			   const struct pqlvalue *val) {
   unsigned i, num;

   if (pqlpickle_put8(ctx, TC_TUPLE)) {
      return -1;
   }
   num = pqlvalue_tuple_getarity(val);
   if (pqlpickle_putnum(ctx, num)) {
      return -1;
   }
   for (i=0; i<num; i++) {
      if (pqlpickle_value(ctx, pqlvalue_tuple_get(val, i))) {
	 return -1;
      }
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_tuple(struct pqlunpicklecontext *ctx) {
   unsigned i, num;
   struct pqlvalue *ret, *sub;

   if (pqlunpickle_getnum(ctx, &num)) {
      return NULL;
   }
   ret = pqlvalue_tuple_begin(ctx->pql, num);
   for (i=0; i<num; i++) {
      sub = pqlunpickle_value(ctx);
      if (sub == NULL) {
	 pqlvalue_destroy(ret);
	 return NULL;
      }
      pqlvalue_tuple_assign(ctx->pql, ret, i, sub);
   }
   pqlvalue_tuple_end(ctx->pql, ret);
   return ret;
}

static int pqlpickle_set(struct pqlpicklecontext *ctx,
			 const struct pqlvalue *val) {
   unsigned i, num;

   if (pqlpickle_put8(ctx, TC_SET)) {
      return -1;
   }
   num = pqlvalue_set_getnum(val);
   if (pqlpickle_putnum(ctx, num)) {
      return -1;
   }
   for (i=0; i<num; i++) {
      if (pqlpickle_value(ctx, pqlvalue_set_get(val, i))) {
	 return -1;
      }
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_set(struct pqlunpicklecontext *ctx) {
   unsigned i, num;
   struct pqlvalue *ret, *sub;

   if (pqlunpickle_getnum(ctx, &num)) {
      return NULL;
   }
   ret = pqlvalue_emptyset(ctx->pql);
   for (i=0; i<num; i++) {
      sub = pqlunpickle_value(ctx);
      if (sub == NULL) {
	 pqlvalue_destroy(ret);
	 return NULL;
      }
      pqlvalue_set_add(ret, sub);
   }
   return ret;
}

static int pqlpickle_sequence(struct pqlpicklecontext *ctx,
			      const struct pqlvalue *val) {
   unsigned i, num;

   if (pqlpickle_put8(ctx, TC_SEQUENCE)) {
      return -1;
   }
   num = pqlvalue_sequence_getnum(val);
   if (pqlpickle_putnum(ctx, num)) {
      return -1;
   }
   for (i=0; i<num; i++) {
      if (pqlpickle_value(ctx, pqlvalue_sequence_get(val, i))) {
	 return -1;
      }
   }
   return 0;
}

static struct pqlvalue *pqlunpickle_sequence(struct pqlunpicklecontext *ctx) {
   unsigned i, num;
   struct pqlvalue *ret, *sub;

   if (pqlunpickle_getnum(ctx, &num)) {
      return NULL;
   }
   ret = pqlvalue_emptysequence(ctx->pql);
   for (i=0; i<num; i++) {
      sub = pqlunpickle_value(ctx);
      if (sub == NULL) {
	 pqlvalue_destroy(ret);
	 return NULL;
      }
      pqlvalue_sequence_add(ret, sub);
   }
   return ret;
}

static int pqlpickle_value(struct pqlpicklecontext *ctx,
			    const struct pqlvalue *val) {
   if (pqlvalue_isnil(val)) {
      return pqlpickle_put8(ctx, TC_NIL);
   }
   else if (pqlvalue_isbool(val)) {
      return pqlpickle_bool(ctx, pqlvalue_bool_get(val));
   }
   else if (pqlvalue_isint(val)) {
      return pqlpickle_int(ctx, pqlvalue_int_get(val));
   }
   else if (pqlvalue_isfloat(val)) {
      return pqlpickle_float(ctx, pqlvalue_float_get(val));
   }
   else if (pqlvalue_isstring(val)) {
      return pqlpickle_string(ctx, pqlvalue_string_get(val));
   }
   else if (pqlvalue_isstruct(val)) {
      return pqlpickle_struct(ctx, val);
   }
   else if (pqlvalue_ispathelement(val)) {
      return pqlpickle_pathelement(ctx, val);
   }
   else if (pqlvalue_istuple(val)) {
      return pqlpickle_tuple(ctx, val);
   }
   else if (pqlvalue_isset(val)) {
      return pqlpickle_set(ctx, val);
   }
   else if (pqlvalue_issequence(val)) {
      return pqlpickle_sequence(ctx, val);
   }
   else {
      assert(!"invalid pqlvalue type in pqlpickle");
      errno = EINVAL;
      return -1;
   }
}

static struct pqlvalue *pqlunpickle_value(struct pqlunpicklecontext *ctx) {
   uint8_t code;

   if (pqlunpickle_get8(ctx, &code)) {
      return NULL;
   }
   switch (code) {
    case TC_NIL:
     return pqlvalue_nil(ctx->pql);
    case TC_BOOL:
     return pqlunpickle_bool(ctx);
    case TC_POSINT:
     return pqlunpickle_posint(ctx);
    case TC_NEGINT:
     return pqlunpickle_negint(ctx);
    case TC_FLOAT:
     return pqlunpickle_float(ctx);
    case TC_STRING:
     return pqlunpickle_string(ctx);
    case TC_STRUCT:
     return pqlunpickle_struct(ctx);
    case TC_PATHELEMENT:
     return pqlunpickle_pathelement(ctx);
    case TC_TUPLE:
     return pqlunpickle_tuple(ctx);
    case TC_SET:
     return pqlunpickle_set(ctx);
    case TC_SEQUENCE:
     return pqlunpickle_sequence(ctx);
    default:
     break;
   }
   errno = EFTYPE;
   return NULL;
}

////////////////////////////////////////////////////////////

void pqlpickle(const struct pqlvalue *val, struct pqlpickleblob *blob) {
   struct pqlpicklecontext ctx;

   pqlpickleblob_init(blob);
   pqlpicklecontext_init(&ctx, blob);

   pqlpickle_header(&ctx);
   pqlpickle_value(&ctx, val);

   pqlpicklecontext_cleanup(&ctx);
}

struct pqlvalue *pqlunpickle(struct pqlcontext *pql,
			     const unsigned char *data, size_t len) {
   struct pqlunpicklecontext ctx;
   struct pqlvalue *ret;

   pqlunpicklecontext_init(&ctx, pql, data, len);

   if (pqlunpickle_header(&ctx)) {
      pqlunpicklecontext_cleanup(&ctx);
      return NULL;
   }

   ret = pqlunpickle_value(&ctx);

   pqlunpicklecontext_cleanup(&ctx);
   return ret;
}
