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
#include <string.h>

#include "utils.h"
#include "layout.h"

DEFARRAY(layout, /*noinline*/);

static struct layout *mklayout(struct pqlcontext *pql, enum layouttypes type) {
   struct layout *l;

   (void)pql;
   l = domalloc(pql, sizeof(*l));
   l->type = type;
   return l;
}

struct layout *mklayout_newline(struct pqlcontext *pql) {
   struct layout *l;

   l = mklayout(pql, L_NEWLINE);
   return l;
}

struct layout *mklayout_text(struct pqlcontext *pql, const char *text) {
   struct layout *l;

   l = mklayout(pql, L_TEXT);
   l->text.string = dostrdup(pql, text);
   l->text.width = strlen(text);
   return l;
}

struct layout *mklayout_text_consume(struct pqlcontext *pql, char *text) {
   struct layout *l;

   l = mklayout(pql, L_TEXT);
   l->text.string = text;
   l->text.width = strlen(text);
   return l;
}

struct layout *mklayout_text_bylength(struct pqlcontext *pql,
				      const char *text, size_t len) {
   struct layout *l;

   l = mklayout(pql, L_TEXT);
   l->text.string = dostrndup(pql, text, len);
   l->text.width = len;
   return l;
}

struct layout *mklayout_text_withnewlines(struct pqlcontext *pql,
					  const char *text) {
   size_t start, pos;
   struct layout *seq = NULL;
   struct layout *dig;

   start = 0;
   for (pos=0; text[pos]; pos++) {
      if (text[pos] == '\n') {
	 if (seq == NULL) {
	    seq = mklayout_sequence_empty(pql);
	 }
	 if (pos > start) {
	    dig = mklayout_text_bylength(pql, text+start, pos-start);
	    layoutarray_add(pql, &seq->sequence, dig, NULL);
	 }
	 layoutarray_add(pql, &seq->sequence, mklayout_newline(pql), NULL);
	 start = pos+1;
      }
   }
   if (pos > start) {
      dig = mklayout_text_bylength(pql, text+start, pos-start);
      if (seq != NULL) {
	 layoutarray_add(pql, &seq->sequence, dig, NULL);
	 return seq;
      }
      else {
	 return dig;
      }
   }
   /* empty string passed in, shouldn't happen */
   PQLASSERT(0);
   return NULL;
}

struct layout *mklayout_sequence_empty(struct pqlcontext *pql) {
   struct layout *l;

   l = mklayout(pql, L_SEQUENCE);
   layoutarray_init(&l->sequence);
   return l;
}

struct layout *mklayout_pair(struct pqlcontext *pql,
			     struct layout *l1,
			     struct layout *l2) {
   struct layout *ret;

   ret = mklayout_sequence_empty(pql);
   layoutarray_add(pql, &ret->sequence, l1, NULL);
   layoutarray_add(pql, &ret->sequence, l2, NULL);
   return ret;
}

struct layout *mklayout_triple(struct pqlcontext *pql,
			       struct layout *l1,
			       struct layout *l2,
			       struct layout *l3) {
   struct layout *ret;

   ret = mklayout_pair(pql, l1, l2);
   layoutarray_add(pql, &ret->sequence, l3, NULL);
   return ret;
}

struct layout *mklayout_quad(struct pqlcontext *pql,
			     struct layout *l1,
			     struct layout *l2,
			     struct layout *l3,
			     struct layout *l4) {
   struct layout *ret;

   ret = mklayout_triple(pql, l1, l2, l3);
   layoutarray_add(pql, &ret->sequence, l4, NULL);
   return ret;
}

struct layout *mklayout_quint(struct pqlcontext *pql,
			      struct layout *l1,
			      struct layout *l2,
			      struct layout *l3,
			      struct layout *l4,
			      struct layout *l5) {
   struct layout *ret;

   ret = mklayout_quad(pql, l1, l2, l3, l4);
   layoutarray_add(pql, &ret->sequence, l5, NULL);
   return ret;
}

struct layout *mklayout_wrap(struct pqlcontext *pql,
			     const char *ltext,
			     struct layout *l,
			     const char *rtext) {
   return mklayout_indent(pql,
			  mklayout_text(pql, ltext),
			  l,
			  mklayout_text(pql, rtext));
}

struct layout *mklayout_leftalign_empty(struct pqlcontext *pql) {
   struct layout *l;

   l = mklayout(pql, L_LEFTALIGN);
   layoutarray_init(&l->leftalign);
   return l;
}

struct layout *mklayout_leftalign_pair(struct pqlcontext *pql,
				       struct layout *l1,
				       struct layout *l2) {
   struct layout *ret;

   ret = mklayout_leftalign_empty(pql);
   layoutarray_add(pql, &ret->leftalign, l1, NULL);
   layoutarray_add(pql, &ret->leftalign, l2, NULL);
   return ret;
}

struct layout *mklayout_leftalign_triple(struct pqlcontext *pql,
					 struct layout *l1,
					 struct layout *l2,
					 struct layout *l3) {
   struct layout *ret;

   ret = mklayout_leftalign_empty(pql);
   layoutarray_add(pql, &ret->leftalign, l1, NULL);
   layoutarray_add(pql, &ret->leftalign, l2, NULL);
   layoutarray_add(pql, &ret->leftalign, l3, NULL);
   return ret;
}

struct layout *mklayout_indent(struct pqlcontext *pql,
			       struct layout *startline,
			       struct layout *body,
			       struct layout *endline) {
   struct layout *l;

   l = mklayout(pql, L_INDENT);
   l->indent.startline = startline;
   l->indent.body = body;
   l->indent.endline = endline;
   return l;
}

////////////////////////////////////////////////////////////

static void layoutarray_destroymembers(struct pqlcontext *pql,
				       struct layoutarray *arr) {
   unsigned i, num;

   num = layoutarray_num(arr);
   for (i=0; i<num; i++) {
      layout_destroy(pql, layoutarray_get(arr, i));
   }
   layoutarray_setsize(pql, arr, 0);
}

void layout_destroy(struct pqlcontext *pql, struct layout *l) {
   if (l == NULL) {
      return;
   }

   switch (l->type) {

    case L_NEWLINE:
     break;

    case L_TEXT:
     dostrfree(pql, l->text.string);
     break;

    case L_SEQUENCE:
     layoutarray_destroymembers(pql, &l->sequence);
     layoutarray_cleanup(pql, &l->sequence);
     break;

    case L_LEFTALIGN:
     layoutarray_destroymembers(pql, &l->leftalign);
     layoutarray_cleanup(pql, &l->leftalign);
     break;

    case L_INDENT:
     layout_destroy(pql, l->indent.startline);
     layout_destroy(pql, l->indent.body);
     layout_destroy(pql, l->indent.endline);
     break;

   }
   dofree(pql, l, sizeof(*l));
}

////////////////////////////////////////////////////////////

#if 0 /* for debugging */

#define LAYOUTINDENT 3

static void layout_dump(struct layout *l, int indent) {
   unsigned i, num;

   switch (l->type) {
    case L_NEWLINE:
     printf("%*sNL\n", indent, "");
     break;

    case L_TEXT:
     printf("%*sTEXT %s\n", indent, "", l->text.string);
     break;

    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     printf("%*sSEQ\n", indent, "");
     for (i=0; i<num; i++) {
	layout_dump(layoutarray_get(&l->sequence, i), indent + LAYOUTINDENT);
     }
     break;

    case L_LEFTALIGN:
     num = layoutarray_num(&l->leftalign);
     printf("%*sLEFT\n", indent, "");
     for (i=0; i<num; i++) {
	layout_dump(layoutarray_get(&l->leftalign, i), indent + LAYOUTINDENT);
     }
     break;

    case L_INDENT:
     printf("%*sINDENT\n", indent, "");
     layout_dump(l->indent.startline, indent + LAYOUTINDENT);
     layout_dump(l->indent.body, indent+4);
     if (l->indent.endline != NULL) {
	layout_dump(l->indent.endline, indent + LAYOUTINDENT);
     }
     break;
   }
}

#endif /* debugging */

////////////////////////////////////////////////////////////

/* Default indent width */
#define INDENT 3

/*
 * Compute indent width, making sure it stays positive.
 */
static unsigned indentwidth(unsigned prevwidth, unsigned indent) {
   if (prevwidth <= indent) {
      return 1;
   }
   return prevwidth - indent;
}

/*
 * Create a layout for an indentation of size INDENT.
 *
 * Because adjacent text blocks are separated with spaces, generate
 * INDENT-1 spaces.
 */
static struct layout *mkindent(struct pqlcontext *pql, unsigned indent) {
   char *str;

   /* wonder if this is safe... XXX */
   PQLASSERT(indent > 1);

   str = domalloc(pql, indent - 1 + 1);
   memset(str, ' ', indent - 1);
   str[indent - 1] = 0;

   return mklayout_text_consume(pql, str);
}

/*
 * Return the width assuming a single-line layout.
 */
static unsigned layout_single_line_width(const struct layout *l) {
   unsigned width;
   unsigned i, num;
   const struct layout *l2;

   width = 0; // gcc 4.1

   switch (l->type) {

    case L_NEWLINE:
     return 0;

    case L_TEXT:
     return l->text.width;
     
    case L_SEQUENCE:
     width = 0;
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	if (i > 0) {
	   width++;
	}
	l2 = layoutarray_get(&l->sequence, i);
	width += layout_single_line_width(l2);
     }
     break;
     
    case L_LEFTALIGN:
     width = 0;
     num = layoutarray_num(&l->leftalign);
     for (i=0; i<num; i++) {
	if (i > 0) {
	   width++;
	}
	l2 = layoutarray_get(&l->leftalign, i);
	width += layout_single_line_width(l2);
     }
     break;

    case L_INDENT:
     width = layout_single_line_width(l->indent.startline);
     width++;
     width += layout_single_line_width(l->indent.body);
     if (l->indent.endline != NULL) {
	width++;
	width += layout_single_line_width(l->indent.endline);
     }
     break;
   }
   
   return width;
}

/*
 * Return the last-line width assuming a multi-line layout.
 */
static unsigned layout_multiline_width(const struct layout *l, unsigned pos) {
   unsigned i, num;
   const struct layout *l2;

   switch (l->type) {

    case L_NEWLINE:
     return 0;

    case L_TEXT:
     return pos + l->text.width;
     
    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	if (i > 0 && pos > 0) {
	   pos++;
	}
	l2 = layoutarray_get(&l->sequence, i);
	pos = layout_multiline_width(l2, pos);
     }
     break;
     
    case L_LEFTALIGN:
     return 0;

    case L_INDENT:
     return 0;
   }
   
   return pos;
}

/*
 * Check if a layout fits in a single line of size MAXWIDTH.
 */
static bool layout_is_single_line(const struct layout *l, unsigned maxwidth) {
   unsigned i, num;
   const struct layout *l2;

   switch (l->type) {

    case L_NEWLINE:
     return false;

    case L_TEXT:
     return true;
     
    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	l2 = layoutarray_get(&l->sequence, i);
	if (!layout_is_single_line(l2, maxwidth)) {
	   return false;
	}
     }
     break;
     
    case L_LEFTALIGN:
     num = layoutarray_num(&l->leftalign);
     for (i=0; i<num; i++) {
	l2 = layoutarray_get(&l->leftalign, i);
	if (!layout_is_single_line(l2, maxwidth)) {
	   return false;
	}
     }
     break;

    case L_INDENT:
     if (!layout_is_single_line(l->indent.startline, maxwidth)) {
	return false;
     }
     if (!layout_is_single_line(l->indent.body, maxwidth)) {
	return false;
     }
     if (l->indent.endline != NULL) {
	if (!layout_is_single_line(l->indent.endline, maxwidth)) {
	   return false;
	}
     }
     break;
   }

   return layout_single_line_width(l) < maxwidth;
}

/*
 * Return the width assuming a single-line layout.
 */
static bool layout_ends_in_newline(const struct layout *l) {
   unsigned num;

   switch (l->type) {

    case L_NEWLINE:
     return true;

    case L_TEXT:
     return false;
     
    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     if (num == 0) {
	return false;
     }
     return layout_ends_in_newline(layoutarray_get(&l->sequence, num-1));
     
    case L_LEFTALIGN:
     num = layoutarray_num(&l->leftalign);
     if (num == 0) {
	return false;
     }
     return layout_ends_in_newline(layoutarray_get(&l->leftalign, num-1));

    case L_INDENT:
     if (l->indent.endline != NULL) {
	return layout_ends_in_newline(l->indent.endline);
     }
     else {
	return layout_ends_in_newline(l->indent.body);
     }
   }

   PQLASSERT(0);
   return false;
}

/*
 * Append a newline, if there isn't one there already.
 */
static void layout_endofline(struct pqlcontext *pql, struct layoutarray *arr) {
   unsigned num;

   num = layoutarray_num(arr);
   if (num == 0 || !layout_ends_in_newline(layoutarray_get(arr, num-1))) {
      layoutarray_add(pql, arr, mklayout_newline(pql), NULL);
   }
}

/*
 * Concatenate an array of layouts of type L_TEXT into one L_TEXT,
 * separating with spaces.
 */
static struct layout *combine_text_layouts(struct pqlcontext *pql,
					   struct layoutarray *arr) {
   unsigned i, num;
   size_t len;
   struct layout *l2;
   char *text;

   len = 0;
   num = layoutarray_num(arr);
   for (i=0; i<num; i++) {
      if (i > 0) {
	 len++;
      }
      l2 = layoutarray_get(arr, i);
      PQLASSERT(l2->type == L_TEXT);
      len += l2->text.width;
   }

   text = domalloc(pql, len + 1);
   *text = 0;
   for (i=0; i<num; i++) {
      if (i > 0) {
	 strcat(text, " ");
      }
      l2 = layoutarray_get(arr, i);
      strcat(text, l2->text.string);
   }
   text[len] = 0;

   layoutarray_destroymembers(pql, arr);

   return mklayout_text_consume(pql, text);
}

/*
 * Fold a single-line layout tree into a single L_TEXT.
 */
static struct layout *layout_combine_single_line(struct pqlcontext *pql,
						 struct layout *l) {
   unsigned i, num;
   struct layout *l2;

   switch (l->type) {

    case L_NEWLINE:
     PQLASSERT(0);
     break;

    case L_TEXT:
     break;
     
    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	l2 = layoutarray_get(&l->sequence, i);
	l2 = layout_combine_single_line(pql, l2);
	layoutarray_set(&l->sequence, i, l2);
     }
     l2 = combine_text_layouts(pql, &l->sequence);
     layout_destroy(pql, l);
     return l2;
     break;
     
    case L_LEFTALIGN:
     num = layoutarray_num(&l->leftalign);
     for (i=0; i<num; i++) {
	l2 = layoutarray_get(&l->leftalign, i);
	l2 = layout_combine_single_line(pql, l2);
	layoutarray_set(&l->leftalign, i, l2);
     }
     l2 = combine_text_layouts(pql, &l->leftalign);
     layout_destroy(pql, l);
     return l2;

    case L_INDENT:
     {
	struct layoutarray tmp;

	layoutarray_init(&tmp);

	l2 = l->indent.startline;
	l->indent.startline = NULL;
	l2 = layout_combine_single_line(pql, l2);
	layoutarray_add(pql, &tmp, l2, NULL);

	l2 = l->indent.body;
	l->indent.body = NULL;
	l2 = layout_combine_single_line(pql, l2);
	layoutarray_add(pql, &tmp, l2, NULL);

	l2 = l->indent.endline;
	l->indent.endline = NULL;
	if (l2 != NULL) {
	   l2 = layout_combine_single_line(pql, l2);
	   layoutarray_add(pql, &tmp, l2, NULL);
	}

	l2 = combine_text_layouts(pql, &tmp);
	layoutarray_cleanup(pql, &tmp);
	layout_destroy(pql, l);
     }
     return l2;
   }

   return l;
}

/*
 * Indent a layout sequence by INDENT spaces, starting at position POS.
 * Return the new position.
 */
static unsigned layout_indent_sequence(struct pqlcontext *pql,
				       struct layout *l,
				       unsigned indent,
				       unsigned pos) {
   unsigned i, num;
   struct layout *l2;

   PQLASSERT(l->type == L_SEQUENCE);
   num = layoutarray_num(&l->sequence);
   for (i=0; i<num; i++) {
      l2 = layoutarray_get(&l->sequence, i);

      switch (l2->type) {
       case L_NEWLINE:
	pos = 0;
	break;

       case L_TEXT:
	if (pos > 0) {
	   pos++;
	}
	else {
	   layoutarray_insert(pql, &l->sequence, i);
	   layoutarray_set(&l->sequence, i, mkindent(pql, indent));
	   pos = indent;
	   /* continue to point at the text */
	   i++;
	   num++;
	}
	pos += l->text.width;
	break;

       case L_SEQUENCE:
	pos = layout_indent_sequence(pql, l2, indent, pos);
	break;

       case L_LEFTALIGN:
       case L_INDENT:
	/* Not allowed here */
	PQLASSERT(0);
	break;
      }
   }
   
   return pos;
}

/*
 * Indent a layout by INDENT spaces.
 *
 * Assumes what we get starts at the first column, and is the whole
 * block.
 */
static struct layout *layout_indent(struct pqlcontext *pql,
				    struct layout *l,
				    unsigned indent) {
   struct layout *seq;

   if (indent == 0) {
      return l;
   }

   switch (l->type) {
    case L_NEWLINE:
     break;

    case L_TEXT:
     seq = mklayout_sequence_empty(pql);
     layoutarray_add(pql, &seq->sequence, mkindent(pql, indent), NULL);
     layoutarray_add(pql, &seq->sequence, l, NULL);
     return seq;

    case L_SEQUENCE:
     layout_indent_sequence(pql, l, indent, 0);
     break;

    case L_LEFTALIGN:
    case L_INDENT:
     /* Not allowed here */
     PQLASSERT(0);
     break;
   }

   return l;
}

/*
 * Format a layout, including possibly multi-line constructs.
 */
static struct layout *layout_format_rec(struct pqlcontext *pql,
					struct layout *l,
					unsigned pos, unsigned maxwidth) {
   unsigned i, num;
   struct layout *l2;
   struct layout *seq;
   unsigned indent;

   /* fits on the current line */
   if (layout_is_single_line(l, maxwidth-pos)) {
      return layout_combine_single_line(pql, l);
   }

   /* does not fit on the current line, but will fit on the next line */
   if (layout_is_single_line(l, indentwidth(maxwidth, INDENT))) {
      l = layout_combine_single_line(pql, l);
      seq = mklayout_sequence_empty(pql);
      layout_endofline(pql, &seq->sequence);
      l = layout_indent(pql, l, INDENT);
      layoutarray_add(pql, &seq->sequence, l, NULL);
      return seq;
   }

   /* does not fit on one line */

   switch (l->type) {
    case L_NEWLINE:
     break;

    case L_TEXT:
     /* here we could try to wordwrap text that doesn't fit on a line */
     break;

    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	l2 = layoutarray_get(&l->sequence, i);
	l2 = layout_format_rec(pql, l2, pos, maxwidth);
	if (layout_is_single_line(l2, maxwidth-pos)) {
	   pos += layout_single_line_width(l2);
	}
	else {
	   pos = layout_multiline_width(l2, pos);
	}
	layoutarray_set(&l->sequence, i, l2);
     }
     break;

    case L_LEFTALIGN:
     seq = mklayout_sequence_empty(pql);
     num = layoutarray_num(&l->leftalign);
     PQLASSERT(num > 0);
     l2 = layoutarray_get(&l->leftalign, 0);
     if (layout_is_single_line(l2, maxwidth-pos)) {
	indent = pos;
	l2 = layout_combine_single_line(pql, l2);
	layoutarray_add(pql, &seq->sequence, l2, NULL);
     }
     else if (pos > 0) {
	indent = INDENT;
	l2 = layout_format_rec(pql, l2, 0, indentwidth(maxwidth, indent));
	l2 = layout_indent(pql, l2, indent);
	layout_endofline(pql, &seq->sequence);
	layoutarray_add(pql, &seq->sequence, l2, NULL);
     }
     else {
	l2 = layout_format_rec(pql, l2, 0, maxwidth);
	layoutarray_add(pql, &seq->sequence, l2, NULL);
	indent = 0;
     }
     layout_endofline(pql, &seq->sequence);
     for (i=1; i<num; i++) {
	l2 = layoutarray_get(&l->leftalign, i);
	l2 = layout_format_rec(pql, l2, 0, indentwidth(maxwidth, indent));
	l2 = layout_indent(pql, l2, indent);
	layoutarray_add(pql, &seq->sequence, l2, NULL);
	layout_endofline(pql, &seq->sequence);
     }
     layoutarray_setsize(pql, &l->leftalign, 0);
     layout_destroy(pql, l);
     return seq;

    case L_INDENT:
     seq = mklayout_sequence_empty(pql);

     l2 = l->indent.startline;
     l->indent.startline = NULL;
     if (layout_is_single_line(l2, maxwidth-pos)) {
	l2 = layout_combine_single_line(pql, l2);
     }
     else {
	layout_endofline(pql, &seq->sequence);
	l2 = layout_format_rec(pql, l2, 0, maxwidth);
	l2 = layout_indent(pql, l2, INDENT);
     }
     layoutarray_add(pql, &seq->sequence, l2, NULL);
     layout_endofline(pql, &seq->sequence);

     l2 = l->indent.body;
     l->indent.body = NULL;
     l2 = layout_format_rec(pql, l2, 0, indentwidth(maxwidth, INDENT));
     l2 = layout_indent(pql, l2, INDENT);
     layoutarray_add(pql, &seq->sequence, l2, NULL);
     layout_endofline(pql, &seq->sequence);

     if (l->indent.endline != NULL) {
	l2 = l->indent.endline;
	l->indent.endline = NULL;
	l2 = layout_format_rec(pql, l2, 0, maxwidth);
	layoutarray_add(pql, &seq->sequence, l2, NULL);
	layout_endofline(pql, &seq->sequence);
     }

     layout_destroy(pql, l);
     return seq;
   }

   return l;
}

/*
 * Count the string space required for a layout. POS tracks the
 * character position.
 */
static size_t layout_printsize(unsigned *pos, const struct layout *l) {
   unsigned i, num;
   size_t len;

   len = 0;
   switch (l->type) {
    case L_NEWLINE:
     len++;
     *pos = 0;
     break;

    case L_TEXT:
     if (*pos > 0) {
	len++;
	(*pos)++;
     }
     len += l->text.width;
     *pos += l->text.width;
     break;

    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	len += layout_printsize(pos, layoutarray_get(&l->sequence, i));
     }
     break;

    case L_LEFTALIGN:
    case L_INDENT:
     /* Not allowed here */
     PQLASSERT(0);
     break;
   }

   return len;
}

/*
 * Place layout into string buffer. POS tracks the
 * character position.
 */
struct layout_print_info {
   char *buf;		/* buffer */
   size_t maxlen;	/* remaining space */
   unsigned pos;	/* current line position */
};

static void layout_print_add(struct layout_print_info *lpi, const char *txt) {
   size_t len;

   len = strlen(txt);
   PQLASSERT(lpi->maxlen > len);
   strcpy(lpi->buf, txt);
   lpi->buf += len;
   lpi->maxlen -= len;
   lpi->pos += len;
}

static void layout_print_rec(struct layout_print_info *lpi,
			     const struct layout *l) {
   unsigned i, num;

   switch (l->type) {
    case L_NEWLINE:
     layout_print_add(lpi, "\n");
     lpi->pos = 0;
     break;

    case L_TEXT:
     if (lpi->pos > 0) {
	layout_print_add(lpi, " ");
     }
     layout_print_add(lpi, l->text.string);
     break;

    case L_SEQUENCE:
     num = layoutarray_num(&l->sequence);
     for (i=0; i<num; i++) {
	layout_print_rec(lpi, layoutarray_get(&l->sequence, i));
     }
     break;

    case L_LEFTALIGN:
    case L_INDENT:
     /* Not allowed here */
     PQLASSERT(0);
     break;
   }
}

static char *layout_print(struct pqlcontext *pql, const struct layout *l) {
   struct layout_print_info lpi;
   unsigned pos;
   char *retval;
   size_t len;

   (void)pql; // not currently needed

   pos = 0;
   len = layout_printsize(&pos, l);
   if (pos > 0) {
      len++;
   }

   retval = domalloc(pql, len+1);
   retval[0] = 0;  /* just in case */

   lpi.buf = retval;
   lpi.maxlen = len+1;
   lpi.pos = 0;
   layout_print_rec(&lpi, l);
   if (lpi.pos > 0) {
      layout_print_add(&lpi, "\n");
   }

   /* check we came out even */
   PQLASSERT(lpi.maxlen == 1);
   PQLASSERT(lpi.buf[0] == 0);

   /* return original pointer */
   return retval;
}

////////////////////////////////////////////////////////////

struct layout *layout_format(struct pqlcontext *pql,
			     struct layout *l,
			     unsigned maxwidth) {
   //layout_dump(l, 0);
   l = layout_format_rec(pql, l, 0, maxwidth);
   //layout_dump(l, 0);
   return l;
}

char *layout_tostring(struct pqlcontext *pql, const struct layout *l) {
   return layout_print(pql, l);
}
