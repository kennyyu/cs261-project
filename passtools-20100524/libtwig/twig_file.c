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
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "twig.h"
#include "twig_file.h"

/* linux doesn't have EFTYPE (!?) */
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif

//
// Constants
//
#define TWIG_MAX_WRITE_ERRORS     4

// This is defined here to avoid the user knowing anything about its internals
struct twig_file {
   // Common
   FILE                   *fp;
   enum twig_role          role;

   // For writer
   int                     fd;

   // For reader
   char                   *mmap_addr;   // mem address where file is mmapped
   size_t                  offset;      // current offset into file
   size_t                  len;         // length of file
   size_t                  page_aligned_len;
};

//
// Static functions
//
static size_t twig_sizeof_rec(const struct twig_rec *rec);

static size_t fwriteall(struct twig_file *file, const void * ptr, size_t size);
//static size_t freadall(int fd, void ** ptr, size_t size);

////////////////////////////////////////////////////////////////////////////////

/**
 * Open the given file in the given mode.
 *
 * @param[in]     filename   name of file to open
 * @param[in]     role       read, write, or debug
 */
struct twig_file *twig_open(const char *filename, enum twig_role role)
{
   struct twig_file *file = NULL;
   struct stat       sb;
   size_t            page_size;
   int               fd;
   int               serrno;
   int               ret;

   // Sanity checks
   assert(filename != NULL);

   //
   // Check permission
   //
   if ( role == TWIG_INVALID ) {
      fprintf( stderr,
               "twig_open: attempt to open %s in invalid mode TWIG_INVALID\n",
               filename );
      errno = EINVAL;
      return NULL;
   }

   file = malloc(sizeof(*file));
   if ( file == NULL ) {
      errno = ENOMEM;
      return NULL;
   }

   file->role = role;
   file->fp = NULL;


   if ( role == TWIG_RDONLY ) {
      // read case
      fd = open(filename, O_RDONLY, 0);
      if ( fd < 0 ) {
         serrno = errno;

         fprintf( stderr, "twig_open: open(%s) failed %s\n",
                  filename, strerror(serrno) );

         free(file), file = NULL;
         errno = serrno;
         return NULL;
      }

      ret = fstat(fd, &sb);
      if ( ret != 0 ) {
         serrno = errno;

         fprintf( stderr, "twig_open: fstat(%s) failed %s\n",
                  filename, strerror(serrno) );

         // clean up
         close(fd), fd = -1;
         free(file), file = NULL;

         // Use whatever errno fstat set
         errno = serrno;
         return NULL;
      }
                  
      file->len = (size_t)sb.st_size;

      page_size = getpagesize();
      file->page_aligned_len = file->len + (page_size - (file->len % page_size));

      file->mmap_addr = mmap(0, file->page_aligned_len, PROT_READ, MAP_SHARED, fd, 0);
      if ( file->mmap_addr == MAP_FAILED ) {
         serrno = errno;

         fprintf( stderr, "twig_open: mmap(%s) failed %s\n",
                  filename, strerror(serrno) );

         close(fd);
         free(file), file = NULL;

         errno = serrno;
         return NULL;
      }

      // Memory remains mapped even after file is closed so we close the file.
      close(fd), fd = -1;

      file->offset = 0;

   } else {
      assert( role == TWIG_WRONLY );

      // write case
      file->fp = fopen( filename, "w");
      if ( file->fp == NULL ) {
         serrno = errno;

         free(file), file = NULL;

         errno = serrno;
         return NULL;
      }
   }

   return file;
}

/**
 * Close.
 *
 * Writes updated parameters and statistics on close.
 *
 * @param[in,out] file       file to close
 */
int twig_close(struct twig_file *file)
{
   int retval;
   int serrno;

   assert( file != NULL );

   if ( file->mmap_addr != NULL ) {
      retval = munmap( file->mmap_addr, file->page_aligned_len );
      if ( retval < 0 ) {
         serrno = errno;
         fprintf( stderr, "twig_close: munmap: %s\n",
                  strerror(serrno) );

         // Use whatever errno munmap set
         errno = serrno;
         return -1;
      }

      file->mmap_addr = NULL;

   } else if ( file->fp != NULL ) {
      retval = fclose(file->fp), file->fp = NULL;
   } else {
      retval = 0;
   }

   serrno = errno;
   free(file), file = NULL;
   errno = serrno;

   return retval;
}

/**
 * Write a collection of records to the given file.
 *
 * @param[in]     file       file to write to
 * @param[in]     rec_count  number of records to write
 * @param[in]     rec        records to write
 */
int twig_writev(struct twig_file *file, const unsigned rec_count,
                const struct twig_rec *rec[])
{
   unsigned recno;
   ssize_t  ret;

   assert( file != NULL );
   assert( file->fp != NULL );
   assert( rec != NULL );
   assert( rec_count != 0 );

   for ( recno = 0; rec_count > recno; ++recno ) {
      ret = twig_write(file, rec[recno]);

      if ( (size_t)ret != twig_sizeof_rec(rec[recno]) ) {
         return -1;
      }
   }

   return rec_count;
}

/**
 * Write one record to the given file.
 *
 * @param[in]     file       file to write too
 * @param[in]     rec        record to write
 */
ssize_t twig_write(struct twig_file *file, const struct twig_rec *rec)
{
   ssize_t                   reclen;

   assert( file != NULL );
   assert( file->fp != NULL );
   assert( rec != NULL );

   // TODO: remove this
   // cast is hacky but it is going away
   assert( (size_t)rec->reclen == twig_sizeof_rec(rec) );

   //
   // Check permission
   //
   switch ( file->role ) {
      case TWIG_WRONLY:
         break;

      case TWIG_RDONLY:
      case TWIG_INVALID:
         errno = EACCES;
         return -1;

         //default:
         // *NO* default case, that way we get a compiler warning if
         // somebody adds a role above
   }

   //
   // Write the data to the file
   //

   // Determine the size of rec
   reclen = twig_sizeof_rec(rec);

   // Trying to write an unsupported record type
   assert( reclen != 0 );

   return fwriteall(file, rec, reclen);
}

/**
 * Read a record from the given file.
 *
 * @param[in]     file       file to read from
 * @param[out]    rec        record we read
 */
ssize_t twig_read(struct twig_file *file, struct twig_rec **rec)
{
   struct twig_rec *rectmp;
   ssize_t          reclen;

   //
   // Sanity, parameter and security checks
   //

   assert( file != NULL );
   assert( file->mmap_addr != NULL );
   assert( rec != NULL );

   // Check permission
   switch ( file->role ) {
      case TWIG_RDONLY:
         break;

      case TWIG_WRONLY:
      case TWIG_INVALID:
         // TODO: print error message here
         errno = EACCES;
         return -1;

         //default:
         // *NO* default case, that way we get a compiler warning if
         // somebody adds a role above
   }

   //
   // Beginning of real work 
   //
   if ( file->offset >= file->len ) {
      return EOF;
   }

   rectmp = (struct twig_rec *)(file->mmap_addr + file->offset);
   reclen = twig_sizeof_rec(rectmp);

   // We read a record type we do not recognize
   if ( reclen == 0 ) {
      errno = EINVAL;
      return -1;
   }

   if ( rectmp->reclen != (size_t) reclen ) {
      /*
       * Size we expect is not the same as the size stored in the file.
       * Twig file is corrupt.
       */
      errno = EFTYPE;
      return -1;
   }

   *rec = rectmp;

   file->offset += reclen;

   return reclen;
}

/**
 * Wrapper for write.
 *
 * Has two purposes:
 *   avoid partial writes
 *   modularity -- to allow any data access routines
 *
 * @param[in]     fd         file-descriptor of file
 * @param[in]     ptr        pointer to data to write
 * @param[in]     size       size of data buffer (in bytes)
 *
 * @return        number of bytes written success, -1 and sets errno on failure
 */
static size_t fwriteall(struct twig_file *file, const void * ptr, size_t size)
{
   size_t                    ret;

   ret = fwrite(ptr, 1, size, file->fp);

   if ( ferror(file->fp) ) {
      return 0;
   }

   assert( ret == size );

   return size;
}

/**
 * Compute the size of a record.
 *
 * @param[in]     rec        record whose size we want to know
 *
 * @returns       the size of the record on success
 * @returns       0 on failure
 */
static size_t twig_sizeof_rec(const struct twig_rec *rec)
{
   struct twig_precord *prec;
   size_t ret = 0;

   switch ( rec->rectype ) {
      case TWIG_REC_HEADER:
         ret = sizeof(struct twig_rec_header);
         break;

      case TWIG_REC_BEGIN:
         ret = sizeof(struct twig_rec_begin);
         break;

      case TWIG_REC_END:
         ret = sizeof(struct twig_rec_end);
         break;

      case TWIG_REC_WAP:
         ret = sizeof(struct twig_rec_wap);
         break;

      case TWIG_REC_CANCEL:
         ret = sizeof(struct twig_rec_cancel);
         break;

      case TWIG_REC_PROV:
         prec = (struct twig_precord *)rec;

         // XXX: TODO: revisit this when we use attrcode
         ret = sizeof(*prec);

         ret += prec->tp_attrlen;
         ret += prec->tp_valuelen;
         break;

         //default:
         // *NO* default case, that way we get a compiler warning if
         // somebody adds a record type

         // We do not expect to get here.
         // If we got here from the write path, we want to assert(0)
         // If we got here from the read path,
         //   we have a corrupted or unsupported twig file
         // In either case, a zero sized record is meaningless and hence an error
   }

   return ret;
}
