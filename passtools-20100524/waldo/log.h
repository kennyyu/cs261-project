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
#ifndef LOG_H
#define LOG_H

#include <stdint.h>

enum log_state { LOG_STATE_KERNEL, LOG_STATE_ACTIVE, LOG_STATE_BACKUP };

int log_startup(const char *logpath);
int log_shutdown(void);
int log_find_files(const char *logpath);
int log_last_lognum(uint64_t *num);
int log_get_number(const char *name, uint64_t *number);
int log_filename_to_state(const char *filename, enum log_state *logstate);
int log_next_filename(const char *logpath, int more,
                      uint64_t *lognum, enum log_state *logstate);

char *log_make_filename(const char *logpath, uint64_t lognum,
                        enum log_state log_state);
char *log_kernel_filename(const char *dir, const uint64_t lognum);
char *log_active_filename(const char *dir, const uint64_t lognum);
char *log_backup_filename(const char *dir, const uint64_t lognum);

#endif /* LOG_H */
