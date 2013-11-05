/*
 * Copyright 2009
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

#include <stdbool.h>

#include "array.h"
#include "utils.h"

enum dumpstages {
   DUMP_PARSER = 0,
   DUMP_RESOLVE = 1,
   DUMP_NORMALIZE = 2,
   DUMP_UNIFY = 3,
   DUMP_MOVEPATHS = 4,
   DUMP_BINDNIL = 5,
   DUMP_DEQUANTIFY = 6,
   DUMP_TUPLIFY = 7,
   DUMP_TYPEINF = 8,
   DUMP_NORENAMES = 9,
   DUMP_BASEOPT = 10,
   DUMP_STEPJOINS = 11,
};
#define NUM_DUMPSTAGES 12

struct pqlcontext {
   /*
    * Client state
    */
   const struct pqlbackend_ops *ops;		/* Database operations */
   bool dodumps;				/* Dump intermediate state */
   unsigned dumpwidth;				/* Format width for dumps */
   bool dotrace;				/* Trace evaluation */

   /*
    * Global state and data management
    */
   struct datatypemanager *dtm;			/* Data types */
   struct ptmanager *ptm;			/* Parse tree memory */
   size_t meminuse;				/* Memory usage accounting */
   size_t peakmem;

   /*
    * Compilation state (all the way through)
    *
    * XXX decide whether/how to reset the IDs after each compile.
    */
   unsigned nextnameid;				/* For unique names */
   unsigned nextcolumnid;			/* For unique column ids */
   unsigned nextvarid;				/* For unique tuple vars */
   struct stringarray errors;			/* Compile error messages */
   struct stringarray dumps;			/* Diagnostic/debug dumps */
   struct stringarray trace;			/* Eval trace */

   /*
    * Parser state
    */
   struct ptparse_pcb_struct *pcb;		/* Parser control block */
};

void pqlcontext_getfreshname(struct pqlcontext *, char *buf, size_t maxlen);

void complain(struct pqlcontext *, unsigned line, unsigned col,
	      const char *msgfmt, ...) PF(4, 5);

void pqlcontext_adddump(struct pqlcontext *, enum dumpstages stage, char *txt);
void pqlcontext_addtrace(struct pqlcontext *, char *txt);
