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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "twig.h"

static void twig_print_rec_header(const struct twig_rec_header *header);
static void twig_print_rec_begin(const struct twig_rec_begin *begin);
static void twig_print_rec_end(const struct twig_rec_end *end);
static void twig_print_rec_wap(const struct twig_rec_wap *wap);
static void twig_print_rec_cancel(const struct twig_rec_cancel *cancel);
static void twig_print_precord(const struct twig_precord *prov);

/**
 * Print a single provenance record.
 */
void twig_print_rec(const struct twig_rec *rec)
{
   enum twig_rectype          rectype;

   assert( rec != NULL );

   rectype = (enum twig_rectype)(rec->rectype);

   switch ( rectype ) {
      case TWIG_REC_HEADER:
         twig_print_rec_header((struct twig_rec_header *)rec);
         break;

      case TWIG_REC_BEGIN:
         twig_print_rec_begin((struct twig_rec_begin *)rec);
         break;

      case TWIG_REC_END:
         twig_print_rec_end((struct twig_rec_end *)rec);
         break;

      case TWIG_REC_WAP:
         twig_print_rec_wap((struct twig_rec_wap *)rec);
         break;

      case TWIG_REC_CANCEL:
         twig_print_rec_cancel((struct twig_rec_cancel *)rec);
         break;

      case TWIG_REC_PROV:
         twig_print_precord((struct twig_precord *)rec);
         break;
   }
}

/**
 * Print a log file header.
 *
 * @note Treat a version mismatch as a warning. All this function does is print
 *       the version so it really need not overreact.
 */
static void twig_print_rec_header(const struct twig_rec_header *header)
{
   assert( header != NULL );
   if ( header->version != TWIG_VERSION ) {
      fprintf( stderr, "Warning: TWIG file version mismatch: "
               "file is version %u this software is version %u\n",
               header->version, TWIG_VERSION );
   }

   printf( "VERSION: %u\n", (unsigned)(header->version) );
}

/**
 * Print a begin record.
 */
static void
twig_print_rec_begin(const struct twig_rec_begin *begin)
{
   assert( begin != NULL );

   printf( "BEGIN: %llu\n", (unsigned long long)(begin->lsn) );
}

/**
 * Print an end record.
 */
static void
twig_print_rec_end(const struct twig_rec_end *end)
{
   assert( end != NULL );

   printf( "END: %llu\n", (unsigned long long)(end->lsn) );
}

/**
 * Print an wap record.
 */
static void
twig_print_rec_wap(const struct twig_rec_wap *wap)
{
   size_t byte;

   assert( wap != NULL );

   printf( "WAP <off, len> = value <%llu, %lu> = ",
           (unsigned long long)wap->off, (unsigned long)(wap->len) );

   for ( byte = 0; sizeof(wap->md5) > byte; ++byte ) {
      printf( "%02X", wap->md5[byte] );
   }

   printf( "\n" );
}

/**
 * Print a cancel record.
 */
static void
twig_print_rec_cancel(const struct twig_rec_cancel *cancel)
{
   assert( cancel != NULL );

   printf( "CANCEL: %llu\n", (unsigned long long)(cancel->lsn) );
}

/**
 * Print a provenance record.
 */
static void
twig_print_precord(const struct twig_precord *prov)
{
   const char                     *attr;
   const void                     *value;
   const char                     *valuestr;
   uint32_t                        total;
   const struct prov_timestamp    *pts;
   const struct prov_pnodeversion *ppv;

   assert( prov != NULL );

   attr = TWIG_PRECORD_ATTRIBUTE(prov);
   value = TWIG_PRECORD_VALUE(prov);

   printf( "%llu.%u "
           "%.*s "
	   "%s",
           (unsigned long long)(prov->tp_pnum), (unsigned)(prov->tp_version),
           (int)(prov->tp_attrlen), attr,
	   (prov->tp_flags & PROV_IS_ANCESTRY) ? "[ANC] " : "");


   switch (prov->tp_valuetype) {
      case PROV_TYPE_NIL:
         printf("---\n");
         break;

      case PROV_TYPE_STRING:
         valuestr = value;
         printf( "%.*s\n", prov->tp_valuelen, valuestr );
         break;

      case PROV_TYPE_MULTISTRING:
         valuestr = value;
         total = 0;
         while ( total < prov->tp_valuelen ) {
            printf("[%s]", valuestr + total);
            total += strlen(valuestr + total) + 1;
         }
         printf("\n");
         break;

      case PROV_TYPE_INT:
         printf( "%d\n", (int)*(int32_t *)value );
         break;

      case PROV_TYPE_REAL:
         printf( "%g\n", *(double *)value );
         break;

      case PROV_TYPE_TIMESTAMP:
         pts = value;
         printf( "%u.%09u\n",
                 (unsigned)(pts->pt_sec), (unsigned)(pts->pt_nsec) );
         break;

      case PROV_TYPE_INODE:
         printf( "%u\n", (unsigned)*(uint32_t *)value );
         break;

      case PROV_TYPE_PNODEVERSION:
         ppv = value;
         printf( "%llu.%u \n",
                 (unsigned long long)(ppv->pnode),
                 (unsigned)(ppv->version) );
         break;

      case PROV_TYPE_OBJECT:
      case PROV_TYPE_OBJECTVERSION:
         // ignore the version if present - these records shouldn't appear anyway
         printf( "fd %d (should not appear in a twig file!)\n", *(int *)value );
         break;

      default:
         printf( "unknown value type\n" );
         fprintf( stderr, "Unknown provenance value type %u\n",
                  (unsigned)(prov->tp_valuetype) );
         break;
   }
}
