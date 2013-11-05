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

#ifndef TWIG_FILE_H
#define TWIG_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Provides an interface to twig format files.
 *
 * Supports the standard open/close/read/write.
 */
#include <stdint.h>

/**
 * There are two valid open modes:
 *   TWIG_RDONLY  read the data
 *   TWIG_WRONLY  write data
 */
enum twig_role { TWIG_INVALID, TWIG_WRONLY, TWIG_RDONLY };

struct twig_file; /* Opaque */

struct twig_file *twig_open(const char *filename, enum twig_role role);

int twig_close(struct twig_file *file);

int twig_writev(struct twig_file *file, const unsigned rec_count,
                const struct twig_rec *recs[] );
ssize_t twig_write(struct twig_file *file, const struct twig_rec *rec);
ssize_t twig_read(struct twig_file *file, struct twig_rec **rec);


#ifdef __cplusplus
};
#endif

#endif /* TWIG_FILE_H */
