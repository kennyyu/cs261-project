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

#include "utils.h"
#include "operators.h"

struct ptnode;


ptnode *pt_mklist(ptnode *first);
ptnode *pt_addlist(ptnode *list, ptnode *next);

ptnode *pt_scope(ptnode *binding, ptnode *block);
ptnode *pt_forbind(ptnode *sym, ptnode *values);
ptnode *pt_letbind(ptnode *sym, ptnode *value);
ptnode *pt_lambda(ptnode *ident);
ptnode *pt_filter(ptnode *expr, ptnode *condition);
ptnode *pt_tuple(ptnode *elements);
ptnode *pt_bop(ptnode *l, ops o, ptnode *r);
ptnode *pt_uop(ops o, ptnode *val);
ptnode *pt_func(ptnode *fn, ptnode *arg);
ptnode *pt_listconstant(ptnode *elts);
ptnode *pt_fieldref(ptnode *fn, const char *field);
ptnode *pt_range(ptnode *l, ptnode *r);
ptnode *pt_number(const char *val);
ptnode *pt_string(const char *val);
ptnode *pt_varname(const char *sym);
ptnode *pt_field(const char *sym);
ptnode *pt_all(void);

/* from parser.syn */
location parser_whereis(void);
ptnode *parse_string(const char *string);
ptnode *parse_file(const char *path);
