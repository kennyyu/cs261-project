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
#include <errno.h>
#include "ptrarray.h"
#include "utils.h"
#include "ptnode.h"
#include "ast.h"
#include "builtins.h"
#include "main.h"

enum ptnodetypes {
   PTN_LIST,
   PTN_SCOPE,
   PTN_FORBIND,
   PTN_LETBIND,
   PTN_LAMBDA,
   PTN_FILTER,
   PTN_TUPLE,
   PTN_BOP,
   PTN_UOP,
   PTN_FIELDREF,
   PTN_FUNC,
   PTN_RANGE,
   PTN_LISTCONSTANT,
   PTN_NUMBER,
   PTN_STRING,
   PTN_VARNAME,
   PTN_FIELD,
   PTN_ALL,
};

struct ptnode {
   ptnodetypes type;
   location loc;
   ptrarray<ptnode> subnodes;
   char *str;
   long num;
   ops op;
};

static ptnode *mknode(ptnodetypes t, const char *s) {
   ptnode *n = new ptnode;
   n->loc = parser_whereis();
   n->type = t;
   n->str = s ? xstrdup(s) : NULL;
   n->num = 0;
   n->op = OP_NOP;
   return n;
}

static ptnode *add(ptnode *n, ptnode *sn) {
   return n->subnodes.add(sn), n;
}

static ptnode *pt0(ptnodetypes t) {
   return mknode(t,NULL);
}

static ptnode *pt0s(ptnodetypes t, const char *s) {
   return mknode(t,s);
}

static ptnode *pt0i(ptnodetypes t, long i) {
   ptnode *n = mknode(t,NULL);
   n->num = i;
   return n;
}

static ptnode *pt1(ptnodetypes t, ptnode *n) {
   return add(mknode(t, NULL), n);
}

static ptnode *pt1s(ptnodetypes t, ptnode *n, const char *s) {
   return add(pt0s(t, s), n);
}

//static ptnode *pt1i(ptnodetypes t, ptnode *sn, long i) {
//   return add(pt0i(t, i), sn);
//}

static ptnode *pt2(ptnodetypes t, ptnode *a, ptnode *b) {
   return add(add(mknode(t,NULL), a), b);
}

//////////////////////////////

ptnode *pt_mklist(ptnode *first) {
   return add(mknode(PTN_LIST, NULL), first);
}

ptnode *pt_addlist(ptnode *list, ptnode *next) {
   return add(list, next);
}

ptnode *pt_scope(ptnode *binding, ptnode *block) {
   return pt2(PTN_SCOPE, binding, block);
}

ptnode *pt_forbind(ptnode *sym, ptnode *values) {
   return pt2(PTN_FORBIND, sym, values);
}

ptnode *pt_letbind(ptnode *sym, ptnode *value) {
   return pt2(PTN_LETBIND, sym, value);
}

ptnode *pt_lambda(ptnode *sym) {
   return pt1(PTN_LAMBDA, sym);
}

ptnode *pt_filter(ptnode *expr, ptnode *condition) {
   return pt2(PTN_FILTER, expr, condition);
}

ptnode *pt_tuple(ptnode *elements) {
   if (!elements) {
      // empty tuple
      return pt0(PTN_TUPLE);
   }
   assert(elements->type == PTN_LIST);
   if (elements->subnodes.num() > 1) {
      elements->type = PTN_TUPLE;
      return elements;
   }
   ptnode *ret = elements->subnodes[0];
   elements->subnodes.setsize(0);
   delete elements;
   return ret;
}

ptnode *pt_bop(ptnode *l, ops o, ptnode *r) {
   ptnode *n = pt2(PTN_BOP, l, r);
   n->op = o;
   return n;
}

ptnode *pt_uop(ops o, ptnode *val) {
   ptnode *n = pt1(PTN_UOP, val);
   n->op = o;
   return n;
}

ptnode *pt_fieldref(ptnode *obj, const char *field) {
   return pt1s(PTN_FIELDREF, obj, field);
}

ptnode *pt_func(ptnode *fn, ptnode *arg) {
   return pt2(PTN_FUNC, fn, arg);
}

ptnode *pt_range(ptnode *l, ptnode *r) {
   return pt2(PTN_RANGE, l, r);
}

ptnode *pt_listconstant(ptnode *elements) {
   if (!elements) {
      // empty list
      return pt0(PTN_LISTCONSTANT);
   }
   assert(elements->type == PTN_LIST);
   elements->type = PTN_LISTCONSTANT;
   return elements;
}

ptnode *pt_number(const char *val) {
   char *foo;
   errno = 0;
   long lval = strtol(val, &foo, 0);
   if (errno || *foo != 0) {
      whine(parser_whereis(), "Invalid number %s", val);
      lval = 0;
   }
   return pt0i(PTN_NUMBER, lval);
}

ptnode *pt_string(const char *val) {
   return pt0s(PTN_STRING, val);
}

ptnode *pt_varname(const char *sym) {
   return pt0s(PTN_VARNAME, sym);
}

ptnode *pt_field(const char *sym) {
   return pt0s(PTN_FIELD, sym);
}

ptnode *pt_all(void) {
   return pt0s(PTN_ALL, NULL);
}

////////////////////////////////////////////////////////////

static void del(ptnode *n) {
   for (int i=0; i<n->subnodes.num(); i++) {
      del(n->subnodes[i]);
   }
   n->subnodes.setsize(0);
   if (n->str) {
      delete []n->str;
   }
   delete n;
}

////////////////////////////////////////////////////////////

static void ind(int indent) {
   printf("%-*s", indent*3, "");
}

#if 0
static void printindented(const char *s, int indent) {
   int atbol = 1;
   for (size_t i=0; s[i]; i++) {
      if (atbol) {
	 ind(indent);
      }

      putchar(s[i]);

      if (s[i]=='\n') {
	 atbol = 1;
      }
      else {
	 atbol = 0;
      }
   }
   if (!atbol) {
      putchar('\n');
   }
}
#endif

static void dumprec(ptnode *n, int indent) {
   switch (n->type) {

    case PTN_LIST:
     ind(indent);
     printf("LIST\n");
     for (int i=0; i<n->subnodes.num(); i++) {
	dumprec(n->subnodes[i], indent+1);
     }
     break;

#if 0
    case PTN_RETURN:
     ind(indent);
     printf("COMPUTE\n");
     dumprec(n->subnodes[0], indent+1);
     ind(indent);
     printf("RETURN\n");
     dumprec(n->subnodes[1], indent+1);
     break;
#endif

    case PTN_SCOPE:
     ind(indent);
     printf("BIND\n");
     dumprec(n->subnodes[0], indent+1);
     ind(indent);
     printf("IN\n");
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_FORBIND:
     ind(indent);
     printf("FOR %s\n", n->subnodes[0]->str);
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_LETBIND:
     ind(indent);
     printf("LET %s\n", n->subnodes[0]->str);
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_LAMBDA:
     ind(indent);
     printf("LAMBDA %s\n", n->subnodes[0]->str);
     break;

    case PTN_FILTER:
     ind(indent);
     printf("COMPUTE\n");
     dumprec(n->subnodes[0], indent+1);
     ind(indent);
     printf("FILTER\n");
     dumprec(n->subnodes[1], indent+1);
     break;

#if 0
    case PTN_ORDER:
     ind(indent);
     printf("COMPUTE\n");
     dumprec(n->subnodes[0], indent+1);
     ind(indent);
     printf("ORDER BY\n");
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_ONEORDER:
     ind(indent);
     printf("%s\n", 
	    n->num == -1 ? "DESCENDING" : 
	    n->num == 1 ? "ASCENDING" : "DEFAULT");
     dumprec(n->subnodes[0], indent+1);
     break;
#endif

    case PTN_TUPLE:
     ind(indent);
     printf("TUPLE (%d)\n", n->subnodes.num());
     for (int i=0; i<n->subnodes.num(); i++) {
	ind(indent);
	dumprec(n->subnodes[i], indent+1);
     }
     break;

    case PTN_BOP:
     ind(indent);
     printf("BOP %s\n", opstr(n->op));
     dumprec(n->subnodes[0], indent+1);
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_UOP:
     ind(indent);
     printf("UOP %s\n", opstr(n->op));
     dumprec(n->subnodes[0], indent+1);
     break;

    case PTN_FIELDREF:
     ind(indent);
     printf("FIELDREF %s\n", n->str);
     dumprec(n->subnodes[0], indent+1);
     break;

    case PTN_FUNC:
     ind(indent);
     printf("CALL\n");
     dumprec(n->subnodes[0], indent+1);
     ind(indent);
     printf("WITH\n");
     dumprec(n->subnodes[1], indent+1);
     break;

    case PTN_RANGE:
     ind(indent);
     printf("RANGE %ld TO %ld\n", n->subnodes[0]->num, n->subnodes[1]->num);
     break;

    case PTN_LISTCONSTANT:
     ind(indent);
     printf("LIST [[\n");
     for (int i=0; i<n->subnodes.num(); i++) {
	dumprec(n->subnodes[i], indent+1);
     }
     printf("]]\n");
     break;

    case PTN_NUMBER:
     ind(indent);
     printf("NUMBER %ld\n", n->num);
     break;

    case PTN_STRING:
     ind(indent);
     printf("STRING %s\n", n->str);
     break;

    case PTN_VARNAME:
     ind(indent);
     printf("VARIABLE %s\n", n->str);
     break;

    case PTN_FIELD:
     ind(indent);
     printf("FIELD @%s\n", n->str);
     break;

    case PTN_ALL:
     ind(indent);
     printf("EVERYTHING\n");
     break;

   }
}

static void dump(ptnode *n) {
   dumprec(n, 0);
}

////////////////////////////////////////////////////////////

// because of the way we're doing let-binding, no scope has more than
// one name in it.
struct scope {
   scope *parent;
   char *name;
   var *target;
};

static scope *scope_create(scope *parent, const char *name, var *v) {
   scope *s = new scope;
   s->parent = parent;
   s->name = xstrdup(name);
   s->target = v;
   return s;
}

static void scope_destroy(scope *s) {
   delete []s->name;
   delete s;
}

static var *scope_lookup(scope *s, const char *name, size_t namelen) {
   while (s) {
      if (namelen > 0) {
	 if (!strncmp(s->name, name, namelen)) {
	    return s->target;
	 }
      }
      else {
	 if (!strcmp(s->name, name)) {
	    return s->target;
	 }
      }
      s = s->parent;
   }
   return NULL;
}

////////////////////////////////////////////////////////////

#define ALNUM \
	"abcdefghijklmnopqrstuvwxyz" \
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
	"0123456789"

#define FIELDS_NAME "this"

static expr *compile(scope *curscope, ptnode *n);

#if 0
static expr *interpolate_concat(expr *l, expr *r) {
   return l ? r : expr_op(OP_STRCAT, l, r);
}

static expr *interpolate(location loc, const char *text, scope *curscope) {
   assert(text != NULL);

   expr *val = NULL;

   const char *base = text;
   while (*base) {
      if (*base=='$') {
	 // $$ is a literal $
	 if (base[1]=='$') {
	    val = interpolate_concat(val, expr_val(value_str("$")));
	    base += 2;
	    continue;
	 }

	 // find the length of the variable name
	 base++;
	 size_t len;
	 const char *next;
	 if (*base=='{') {
	    base++;
	    const char *s = strchr(base, '}');
	    if (!s) {
	       whine(loc, "Unmatched '{' in embedded var reference");
	       len = strlen(base);
	       next = base+len;
	    }
	    else {
	       len = s-base;
	       next = s+1; // skip over close-brace
	    }
	 }
	 else {
	    len = strspn(base, ALNUM);
	    next = base+len;
	 }
	 var *ref = scope_lookup(curscope, base, len);
	 if (!ref) {
	    whine(loc, "Undefined variable $%.*s", len, base);
	 }
	 else {
	    val = interpolate_concat(val, expr_ref(ref));
	 }
	 base = next;
	 continue;
      }

      // find the length of the region until the next variable reference.
      size_t len = strcspn(base, "$");
      assert(len > 0);

      // make a string literal out of it and concatenate.
      val = interpolate_concat(val, expr_val(value_str_bylen(base, len)));
      base += len;
   }

   if (val == NULL) {
      // stupid but legal
      val = expr_val(value_str(""));
   }

   return val;
}
#endif

static expr *do_bind(scope *curscope, ptnode *binding, ptnode *innode) {
   ptnode *varnamenode;
   ptnode *bindnode;
   if (binding->type == PTN_FORBIND || binding->type == PTN_LETBIND) {
      assert(binding->subnodes.num()==2);
      varnamenode = binding->subnodes[0];
      bindnode = binding->subnodes[1];
   }
   else {
      assert(binding->type == PTN_LAMBDA);
      assert(binding->subnodes.num()==1);
      varnamenode = binding->subnodes[0];
      bindnode = NULL;
   }
   const char *varname = varnamenode->str;
   assert(varname != NULL);

   var *ref = var_create();
   scope *ns = scope_create(curscope, varname, ref);
   expr *bindexpr = compile(curscope, bindnode);
   expr *inexpr = compile(ns, innode);
   expr *ret;

   if (binding->type == PTN_FORBIND) {
      ret = expr_for(ref, bindexpr, expr_val(value_int(1)), inexpr);
   }
   else if (binding->type == PTN_LETBIND) {
      ret = expr_let(ref, bindexpr, inexpr);
   }
   else if (binding->type == PTN_LAMBDA) {
      ret = expr_lambda(ref, inexpr);
   }
   else {
      assert(0);
      ret = NULL;
   }
   scope_destroy(ns);
   return ret;
}

#if 0
static expr *do_order(scope *curscope, expr *valexpr, ptnode *ordering) {
   assert(ordering->type == PTN_LIST);
   assert(ordering->subnodes.num() > 0);

   int nexprs = ordering->subnodes.num();
   expr **exprs = new expr* [nexprs];

   for (int i=0; i<nexprs; i++) {
      ptnode *n = ordering->subnodes[i];
      assert(n->type == PTN_ONEORDER);
      assert(n->subnodes.num() == 1);
      ptnode *nn = n->subnodes[0];

      expr *e = compile(curscope, nn);
      if (n->num == -1) {
	 // descending order
	 e = expr_op(OP_REVSORT, e, NULL);
      }
      exprs[i] = e;
   }

   if (nexprs > 1) {
      expr *tuple = expr_tuple(exprs, nexprs);
      return expr_op(OP_SORT, valexpr, tuple);
   }
   else {
      expr *ret = expr_op(OP_SORT, valexpr, exprs[0]);
      delete []exprs;
      return ret;
   }
}
#endif

static expr *do_filter(scope *curscope, ptnode *valnode, ptnode *filtnode) {
   // val WHERE filt
   //    ==>>
   // FOR _where IN val SUCHTHAT (LET this = _where IN filt)
   // DO _where
   //
   // this allows free field references in the where expression.
   //
   // optimization: the scope and name for _where aren't needed
   // and are thus not created.

   var *whereref = var_create();	// _where
   var *thisref = var_create();		// this

   scope *thisscope = scope_create(curscope, FIELDS_NAME, thisref);

   // LET this = _where IN filt
   expr *filtexpr = compile(thisscope, filtnode);
   expr *thislet = expr_let(thisref, expr_ref(whereref), filtexpr); 

   // FOR _where IN val SUCHTHAT filtlet DO _where
   expr *valexpr = compile(curscope, valnode);
   expr *wherefor = expr_for(whereref, valexpr, thislet, expr_ref(whereref));

   return wherefor;
}

static expr *compile(scope *curscope, ptnode *n) {
   if (!n) {
      return NULL;
   }
   switch (n->type) {

    case PTN_LIST:
    case PTN_FORBIND:
    case PTN_LETBIND:
    case PTN_LAMBDA:
    //case PTN_ONEORDER:
     // not expected here
     assert(0);
     break;

#if 0
    case PTN_RETURN:
     assert(n->subnodes.num()==2);

     //
     // one of the charming things about the syntax is that this is
     // dead code; if you have a return statement the only things
     // that need to be evaluated are things that are for- or let-bound
     // and referenced by name in the return text.
     //
     //compile(curscope, n->subnodes[0]);

     // not any more
     //interpolate(n->subnodes[0]->loc, n->str, curscope);
     return compile(curscope, n->subnodes[1]);
#endif

    case PTN_SCOPE:
     assert(n->subnodes.num()==2);
     return do_bind(curscope, n->subnodes[0], n->subnodes[1]);

    case PTN_FILTER:
     assert(n->subnodes.num()==2);
     return do_filter(curscope, n->subnodes[0], n->subnodes[1]);

#if 0
    case PTN_ORDER:
     assert(n->subnodes.num()==2);
     return do_order(curscope, 
		     compile(curscope, n->subnodes[0]),
		     n->subnodes[1]);
#endif

    case PTN_TUPLE:
     {
	int nexprs = n->subnodes.num();
	expr **exprs = new expr* [nexprs];
	assert(nexprs > 1);
	for (int i=0; i<nexprs; i++) {
	   exprs[i] = compile(curscope, n->subnodes[i]);
	}
	return expr_tuple(exprs, nexprs);
     }

    case PTN_BOP:
     assert(n->subnodes.num()==2);
     return expr_op(n->op,
		    compile(curscope, n->subnodes[0]), 
		    compile(curscope, n->subnodes[1]));

    case PTN_UOP:
     assert(n->subnodes.num()==1);
     return expr_op(n->op, compile(curscope, n->subnodes[0]), NULL);
		 
    case PTN_FIELDREF:
     assert(n->subnodes.num()==1);
     assert(n->str != NULL);
     return expr_op(OP_FIELD,
		    compile(curscope, n->subnodes[0]),
		    expr_val(value_str(n->str)));
  
    case PTN_FUNC:
     assert(n->subnodes.num()==2);
     return expr_op(OP_FUNC,
		    compile(curscope, n->subnodes[0]),
		    compile(curscope, n->subnodes[1]));

    case PTN_RANGE:
     assert(n->subnodes.num()==2);
     assert(n->subnodes[0]->type == PTN_NUMBER);
     assert(n->subnodes[1]->type == PTN_NUMBER);
     return expr_val(value_range(n->subnodes[0]->num, n->subnodes[1]->num));

    case PTN_LISTCONSTANT: {
       ptrarray<expr> xs;
       int ok = 1;
       xs.setsize(n->subnodes.num());
       for (int i=0; i<n->subnodes.num(); i++) {
	  xs[i] = compile(curscope, n->subnodes[i]);
	  if (!xs[i] || xs[i]->type != ET_VAL) {
	     ok = 0;
	  }
       }
       if (ok) {
	  /* list of constants */
	  value *ret = value_list();
	  for (int i=0; i<xs.num(); i++) {
	     valuelist_add(ret->listval, xs[i]->val);
	     xs[i]->val = NULL;
	     expr_destroy(xs[i]);
	  }
	  xs.setsize(0);
	  return expr_val(ret);
       }
       /* otherwise, cons it up */
       expr *ret = NULL;
       for (int i=xs.num()-1; i>=0; i--) {
	  ret = ret ? expr_op(OP_CONS, ret, xs[i]) : ret;
       }
       xs.setsize(0);
       return ret;
    }

    case PTN_NUMBER:
     return expr_val(value_int(n->num));

    case PTN_STRING:
     assert(n->str != NULL);
     return expr_val(value_str(n->str));

    case PTN_VARNAME:
     assert(n->str != NULL);
     {
	var *ref = scope_lookup(curscope, n->str, 0);
	if (ref) {
	   return expr_ref(ref);
	}
	const builtin *b = builtin_lookup(n->str);
	if (b) {
	   return expr_val(value_builtin(b));
	}
	whine(n->loc, "Undefined variable %s", n->str);
	return NULL;
     }

    case PTN_FIELD:
     assert(n->str != NULL);
     {
	var *objref = scope_lookup(curscope, FIELDS_NAME, 0);
	if (!objref) {
	   whine(n->loc, "Free field ref @%s with no object in scope",
		 n->str);
	   return expr_val(value_str(""));
	}
	return expr_op(OP_FIELD, expr_ref(objref),
		       expr_val(value_str(n->str)));
     }
    
    case PTN_ALL:
     return expr_val(value_all());
   }

   return NULL;
}

static expr *do_compile(ptnode *n) {
   if (g_dodumps) {
      printf("------------------------------------------------------------\n");
      printf("Parse tree:\n");
      dump(n);
   }
   expr *e = compile(NULL, n);
   del(n);
   return e;
}

expr *compile_file(const char *path) {
   unwhine();
   ptnode *n = parse_file(path);
   if (!n) {
      return NULL;
   }
   return do_compile(n);
}

expr *compile_string(const char *string) {
   unwhine();
   ptnode *n = parse_string(string);
   if (!n) {
      return NULL;
   }
   return do_compile(n);
}
