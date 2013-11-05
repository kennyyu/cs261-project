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

#ifndef LAYOUT_H
#define LAYOUT_H


#include "array.h"
struct pqlcontext;

DECLARRAY(layout);


/*
 * Formatted string tree, for printing dumps.
 */

enum layouttypes {
   L_NEWLINE,
   L_TEXT,
   L_SEQUENCE,
   L_LEFTALIGN,
   L_INDENT,
};
struct layout {
   enum layouttypes type;
   union {
      /* newline - nothing */
      struct {
	 char *string;
	 unsigned width;
      } text;
      struct layoutarray sequence;
      struct layoutarray leftalign;
      struct {
	 struct layout *startline;
	 struct layout *body;
	 struct layout *endline;
      } indent;
   };
};


struct layout *mklayout_newline(struct pqlcontext *pql);

struct layout *mklayout_text(struct pqlcontext *pql, const char *text);
struct layout *mklayout_text_consume(struct pqlcontext *pql, char *text);
struct layout *mklayout_text_bylength(struct pqlcontext *pql,
				      const char *text, size_t len);
struct layout *mklayout_text_withnewlines(struct pqlcontext *pql,
					  const char *text);

struct layout *mklayout_sequence_empty(struct pqlcontext *pql);
struct layout *mklayout_pair(struct pqlcontext *pql,
			     struct layout *l1, struct layout *l2);
struct layout *mklayout_triple(struct pqlcontext *pql,
			       struct layout *l1, struct layout *l2,
			       struct layout *l3);
struct layout *mklayout_quad(struct pqlcontext *pql,
			     struct layout *l1, struct layout *l2,
			     struct layout *l3, struct layout *l4);
struct layout *mklayout_quint(struct pqlcontext *pql,
			      struct layout *l1, struct layout *l2,
			      struct layout *l3, struct layout *l4,
			      struct layout *l5);
struct layout *mklayout_wrap(struct pqlcontext *pql,
			     const char *ltext,
			     struct layout *l,
			     const char *rtext);

struct layout *mklayout_leftalign_empty(struct pqlcontext *pql);
struct layout *mklayout_leftalign_pair(struct pqlcontext *pql,
				       struct layout *l1, struct layout *l2);
struct layout *mklayout_leftalign_triple(struct pqlcontext *pql,
					 struct layout *l1, struct layout *l2,
					 struct layout *l3);
struct layout *mklayout_indent(struct pqlcontext *pql,
			       struct layout *startline,
			       struct layout *body,
			       struct layout *endline);

void layout_destroy(struct pqlcontext *, struct layout *);

/* First call layout_format (which may change things), then layout_tostring. */
struct layout *layout_format(struct pqlcontext *pql,
			     struct layout *l, unsigned maxwidth);
char *layout_tostring(struct pqlcontext *pql, const struct layout *l);


#endif /* LAYOUT_H */
