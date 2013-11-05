/*
 * Copyright 2008, 2010
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

#ifndef PQLUTIL_H
#define PQLUTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


struct pqlcontext;
struct pqlquery;
struct pqlvalue;

struct pqlquery *pql_compile_file(struct pqlcontext *, const char *path);
struct pqlquery *pql_compile_string(struct pqlcontext *, const char *text);

void pql_printdumps(struct pqlcontext *);
void pql_printtrace(struct pqlcontext *);

void pql_setprinterrorname(const char *name);
void pql_printerrors(struct pqlcontext *);

void pql_interact(struct pqlcontext *pql, bool forceprompt,
		  void (*print_result)(struct pqlvalue *));

//////////////////////////////

struct pqlpickleblob {
   unsigned char *data;	/* pointer to the data */
   size_t len;		/* length of the data */
   size_t maxlen;	/* max allocated length - caller needn't care */
};

/*
 * Calling pqlpickle() fills in the pqlpickleblob; the data block can
 * then be written to disk or sent across the network or whatever. The
 * blob should be released with pqlpickleblob_cleanup afterwards.
 *
 * pqlunpickle() inverts this and returns a new pqlvalue, which should
 * be passed to pqlvalue_destroy when no longer needed.
 */
void pqlpickle(const struct pqlvalue *val, struct pqlpickleblob *);
void pqlpickleblob_cleanup(struct pqlpickleblob *);
struct pqlvalue *pqlunpickle(struct pqlcontext *pql,
			     const unsigned char *data, size_t len);

//////////////////////////////

struct tdb;

struct tdb *tdb_create(void);
void tdb_destroy(struct tdb *);

pqloid_t tdb_newobject(struct tdb *);
int tdb_assign(struct pqlcontext *,
	       struct tdb *, pqloid_t oid,
	       const struct pqlvalue *edge, const struct pqlvalue *val);
struct pqlvalue *tdb_follow(struct pqlcontext *,
			    struct tdb *t, pqloid_t oid,
			    const struct pqlvalue *edge, bool reversed);
struct pqlvalue *tdb_followall(struct pqlcontext *,
			       struct tdb *t, pqloid_t oid,
			       bool reversed);


#ifdef __cplusplus
}
#endif

#endif /* PQLUTIL_H */
