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

/*
 * lexer.
 */

#include <stdbool.h>
#include <string.h>

#include "ptprivate.h"
#include "passes.h"
#include "pqlcontext.h"


////////////////////////////////////////////////////////////
// token list

  // this must match {t,}parse.syn
  // (it is an annoying issue with AG that it can neither import nor export
  // this list in some automated fashion)
  enum tokencodes {
    eof,
    AT,		/* punctuation */
    COLON,
    COMMA,
    DOT,
    EQ,
    GT,
    GTEQ,
    HASH,
    LBRACE,
    LBRACKBRACK,
    LPAREN,
    LT,
    LTEQ,
    LTGT,
    MINUS,
    PIPE,
    PLUS,
    PLUSPLUS,
    QUES,
    RBRACE,
    RBRACKBRACK,
    RPAREN,
    SEMIC,
    SLASH,
    STAR,
    ABS,	/* keywords */
    ALL,
    AND,
    ANY,
    AS,
    AVG,
    BY,
    COUNT,
    DISTINCT,
    ELEMENT,
    EXCEPT,
    EXISTS,
    FALSE,
    FOR,
    FROM,
    GLOB,
    GREP,
    GROUP,
    HAVING,
    IN,
    INTERSECT,
    LIKE,
    MAX,
    MIN,
    MOD,
    NEW,
    NIL,
    NOT,
    OF,
    OR,
    PATHOF,
    SELECT,
    SET,
    SOME,
    SOUNDEX,
    SUM,
    TRUE,
    UNGROUP,
    UNION,
    UNQUOTE,
    WHERE,
    WITH,
    IDENTIFIER,			/* basic token types */
    INTEGER_LITERAL,
    QUOTED_STRING_LITERAL,
    REAL_LITERAL,
    FUNCNAME			/* function name */
  };

////////////////////////////////////////////////////////////
// keywords

static const struct {
   const char *name;
   enum tokencodes code;
} keywords[] = {
   { "as", AS },
   { "by", BY },
   { "in", IN },
   { "or", OR },
   { "of", OF },
   { "OF", OF },
   { "abs", ABS },
   { "all", ALL },
   { "and", AND },
   { "any", ANY },
   { "avg", AVG },
   { "for", FOR },
   { "max", MAX },
   { "min", MIN },
   { "mod", MOD },
   { "new", NEW },
   { "nil", NIL },
   { "not", NOT },
   { "set", SET },
   { "sum", SUM },
   { "from", FROM },
   { "glob", GLOB },
   { "grep", GREP },
   { "like", LIKE },
   { "some", SOME },
   { "true", TRUE },
   { "with", WITH },
   { "count", COUNT },
   { "false", FALSE },
   { "group", GROUP },
   { "union", UNION },
   { "where", WHERE },
   { "except", EXCEPT },
   { "exists", EXISTS },
   { "having", HAVING },
   { "pathof", PATHOF },
   { "select", SELECT },
   { "element", ELEMENT },
   { "soundex", SOUNDEX },
   { "ungroup", UNGROUP },
   { "unquote", UNQUOTE },
   { "distinct", DISTINCT },
   { "intersect", INTERSECT },
};
static const unsigned nkeywords = sizeof(keywords)/sizeof(keywords[0]);

/*
 * XXX this currently needs to be synchronized with functions.c
 */
static const struct {
   const char *name;
} functions[] = {
   { "ctime",    },
   { "tostring", },
};
static const unsigned nfunctions = sizeof(functions)/sizeof(functions[0]);

/*
 * Send off an identifier, converting it into a reserved word if
 * necessary.
 */
static void sendword(struct pqlcontext *pql,
		     unsigned line, unsigned col, const char *buf, size_t len){
   unsigned i;

   for (i=0; i<nkeywords; i++) {
      if (len == strlen(keywords[i].name) &&
	  !strncasecmp(buf, keywords[i].name, len)) {
	 parser_send(pql, line, col, keywords[i].code, NULL, 0);
	 return;
      }
   }
   for (i=0; i<nfunctions; i++) {
      if (len == strlen(functions[i].name) &&
	  !memcmp(buf, functions[i].name, len)) {
	 parser_send(pql, line, col, FUNCNAME, buf, len);
	 return;
      }
   }
   parser_send(pql, line, col, IDENTIFIER, buf, len);
}

////////////////////////////////////////////////////////////
// character classification

static inline bool is_ws(int ch) {
   return ch==' ' || ch=='\t';
}

static inline bool is_digit(int ch) {
   return ch>='0' && ch<='9';
}

static inline bool is_hexdigit(int ch) {
   return is_digit(ch) || (ch>='a' && ch<='f') || (ch>='A' && ch<='F');
}

static inline bool is_octaldigit(int ch) {
   return ch>='0' && ch<='7';
}

static inline bool is_letter(int ch) {
   return (ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || 
      (ch == '_') || (ch == '%');
}

static inline bool is_letterdigit(int ch) {
   return is_letter(ch) || is_digit(ch);
}

static inline bool is_glyph(int ch) {
   return ch > 32 && ch < 127;
}

static inline bool is_commentchar(int ch) {
   return ch!='\n';
}

////////////////////////////////////////////////////////////
// main logic

/*
 * Find how many characters at POS match PRED, without charging off the
 * end of the buffer S of length MAX.
 */
static size_t count(const char *s, size_t pos, size_t max, bool (*pred)(int)) {
   size_t n;
   for (n=pos; n<max && pred(s[n]); n++);
   return n-pos;
}

/*
 * Count the length (that is, find the endpoint) of a quoted string.
 */
static size_t quoted_len(const char *s, size_t pos, size_t max) {
   size_t k;
   bool esc;

   esc = false;
   for (k=pos+1; k<max; k++) {
      if (esc) {
	 esc = false;
      }
      else if (s[k] == '\\') {
	 esc = true;
      }
      else if (s[k] == '"') {
	 return (k+1) - pos;
      }
   }
   // no closing quote; this is an error.
   // because we include the surrounding quotes, 0 is not a valid successful
   // return.
   return 0;
}

/*
 * Check if the characters at position POS of string S of length MAX
 * are a punctuation token, and if so return its length and leave its
 * identity in TOK_RET. If not, return 0.
 */
static size_t matchpunc(const char *s, size_t pos, size_t max, int *tok_ret) {

   /* 2-character punctuation tokens ("--" for comment is handled elsewhere) */
   static const struct {
      const char *chars;
      int tok;
   } punc2[] = {
      { "==", EQ },
      { "[[", LBRACKBRACK },
      { "]]", RBRACKBRACK },
      { ">=", GTEQ },
      { "<=", LTEQ },
      { "<>", LTGT },
      { "++", PLUSPLUS },
   };
   static const unsigned npunc2 = sizeof(punc2)/sizeof(punc2[0]);

   /* 1-character punctuation tokens */
   static const struct {
      int ch;
      int tok;
   } punc1[] = {
      { '=', EQ },
      { ':', COLON },
      { ';', SEMIC },
      { ',', COMMA },
      { '.', DOT },
      { '=', EQ },
      { '@', AT },
      { '#', HASH },
      { '-', MINUS },
      { '|', PIPE },
      { '+', PLUS },
      { '?', QUES },
      { '/', SLASH },
      { '*', STAR },
      { '{', LBRACE },
      { '}', RBRACE },
      { '(', LPAREN },
      { ')', RPAREN },
      { '>', GT },
      { '<', LT },
   };
   static const unsigned npunc1 = sizeof(punc1)/sizeof(punc1[0]);

   unsigned i;

   if (pos + 2 <= max) {
      for (i=0; i<npunc2; i++) {
	 if (s[pos] == punc2[i].chars[0] && s[pos+1] == punc2[i].chars[1]) {
	    *tok_ret = punc2[i].tok;
	    return 2;
	 }
      }
   }

   for (i=0; i<npunc1; i++) {
      if (s[pos] == punc1[i].ch) {
	 *tok_ret = punc1[i].tok;
	 return 1;
      }
   }

   return 0;
}

/*
 * Lexer.
 *
 * Given a string, slice it into tokens and pass the tokens to the
 * parser.
 */
static void lex(struct pqlcontext *pql, const char *buf, size_t buflen) {
   size_t len;
   size_t pos;
   unsigned line;
   unsigned column;
   int tok;

   pos = 0;
   line = 1;
   column = 1;
   while (pos < buflen) {

      /* blank line */
      if (buf[pos] == '\n') {
	 pos++;
	 line++;
	 column = 1;
	 continue;
      }

      /* whitespace */
      if (is_ws(buf[pos])) {
	 len = count(buf, pos, buflen, is_ws);
	 pos += len;
	 column += len;
	 continue;
      }

      /* comment (not including terminating newline) */
      if (buf[pos] == '-' && buf[pos+1] == '-') {
	 len = 2 + count(buf, pos+2, buflen, is_commentchar);
	 pos += len;
	 column += len;
	 continue;
      }

      /* identifiers */
      if (is_letter(buf[pos])) {
	 len = 1 + count(buf, pos+1, buflen, is_letterdigit);
	 sendword(pql, line, column, buf+pos, len);
	 pos += len;
	 column += len;
	 continue;
      }

      /* integers */
      if (is_digit(buf[pos])) {
	 if (buf[pos]=='0' && (buf[pos+1]=='x' || buf[pos+1]=='X')) {
	    len = 2 + count(buf, pos+2, buflen, is_hexdigit);
	 }
	 else {
	    len = count(buf, pos, buflen, is_digit);
	    if (buf[pos+len]=='.') {
	       len++;
	       goto common_float;
	    }
	    if (buf[pos]=='0') {
	       // "0129.0" is a valid float, but if we get "0129+6" we
	       // need to not pick up the 9.
	       len = count(buf, pos, buflen, is_octaldigit);
	    }
	 }
	 parser_send(pql, line, column, INTEGER_LITERAL, buf+pos, len);
	 pos += len;
	 column += len;
	 continue;
      }

      /* quoted strings */
      if (buf[pos]=='"') {
	 len = quoted_len(buf, pos, buflen);
	 if (len == 0) {
	    complain(pql, line, column, "Unterminated quoted string");
	    parser_fail(pql);
	    len = 1;
	 }
	 parser_send(pql, line, column, QUOTED_STRING_LITERAL, buf+pos, len);
	 pos += len;
	 column += len;
	 continue;
      }

      /* floats: [0-9]*\.[0-9]*([eE][+-]?[0-9]+)? */
      if (buf[pos]=='.' && is_digit(buf[pos+1])) {
	 len = 1;
common_float:
	 len += count(buf, pos+len, buflen, is_digit);
	 if (buf[pos+len]=='e' || buf[pos+len]=='E') {
	    len++;
	    if (buf[pos+len]=='+' || buf[pos+len]=='-') {
	       len++;
	    }
	    len += count(buf, pos+len, buflen, is_digit);
	 }
	 parser_send(pql, line, column, REAL_LITERAL, buf+pos, len);
	 pos += len;
	 column += len;
	 continue;
      }

      /* punctuation */
      len = matchpunc(buf, pos, buflen, &tok);
      if (len > 0) {
	 parser_send(pql, line, column, tok, NULL, 0);
	 pos += len;
	 column += len;
	 continue;
      }

      /* anything left is an invalid character */
      if (is_glyph(buf[pos])) {
	 complain(pql, line, column, "Illegal character '%c' in input",
		  buf[pos]);
      }
      else {
	 complain(pql, line, column, "Illegal character %d in input",
		  buf[pos]);
      }
      parser_fail(pql);
      pos++;
      column++;
   }

   /* give the parser its EOF token */
   parser_send(pql, line, column, eof, NULL, 0);
}

struct ptexpr *parse(struct pqlcontext *pql, const char *buf, size_t buflen) {
   parser_begin(pql);
   lex(pql, buf, buflen);
   return parser_end(pql);
}
