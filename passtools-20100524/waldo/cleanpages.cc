/*
 * Copyright 2006-2008
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
#include <stdlib.h>
#include <string.h>
#include <map>

#include "cleanpages.h"

using namespace std;

/** Our idea of a "page" */
struct cleanpage {
   ino_t    inode;
   uint64_t off;
};

/** Compare class for cleanpage struct (used by map) */
struct classcomp {
   bool operator() (const cleanpage& lhs, const cleanpage& rhs) const
   {
      if ( lhs.inode != rhs.inode ) {
         return lhs.inode < rhs.inode;
      }

      return lhs.off < rhs.off;
   }
};

typedef map<struct cleanpage,int,classcomp> cleanpage_map_t;
static cleanpage_map_t cleanpage_tbl;

/**
 * Is the given page clean?
 *
 * @param[in]     inode      page consists of inode and offset
 * @param[in]     off        page consists of inode and offset
 *
 * @retval        1          found (page is clean)
 * @retval        0          not found
 */
int cleanpages_lookup(ino_t inode, uint64_t off)
{
   cleanpage_map_t::iterator iter;
   struct cleanpage          cp;

   memset( &cp, 0, sizeof(cp) );
   cp.inode = inode;
   cp.off   = off;

   iter = cleanpage_tbl.find(cp);

   return (iter != cleanpage_tbl.end());
}

/**
 * Add the given page to the known clean pages.
 *
 * @param[in]     inode      page consists of inode and offset
 * @param[in]     off        page consists of inode and offset
 *
 * @retval        1          page already known to be clean
 * @retval        0          page added succesfully
 */
int cleanpages_add(ino_t inode, uint64_t off)
{
   struct cleanpage          cp;
   pair<cleanpage_map_t::iterator,bool> ret;

   memset( &cp, 0, sizeof(cp) );
   cp.inode = inode;
   cp.off   = off;

   ret = cleanpage_tbl.insert( pair<struct cleanpage,int>(cp,0) );
   if ( ret.second == false ) {
      return 1;
   }

   return 0;
}
