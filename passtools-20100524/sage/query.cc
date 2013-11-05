/*
 * Copyright 2010
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

#include <stddef.h>
#include <err.h>

#include "remote.h"
#include "local.h"
#include "query.h"

static bool doremote;

void query_init(const char *dbpath, const char *socketpath) {
   if (socketpath != NULL) {
      doremote = true;
      if (remote_init(socketpath)) {
	 err(1, "Error contacting query daemon");
      }
   }
   else {
      doremote = false;
      local_init(dbpath);
   }
}

void query_shutdown(void) {
   if (doremote) {
      remote_shutdown();
   }
   else {
      local_shutdown();
   }
}


void query_dodumps(bool val) {
   if (doremote) {
      remote_dodumps(val);
   }
   else {
      local_dodumps(val);
   }
}

void query_dotrace(bool val) {
   if (doremote) {
      remote_dotrace(val);
   }
   else {
      local_dotrace(val);
   }
}

////////////////////////////////////////////////////////////

void query_submit_file(const char *file, struct result *res) {
   if (doremote) {
      remote_submit_file(file, res);
   }
   else {
      local_submit_file(file, res);
   }
}

void query_submit_string(const char *cmd, struct result *res) {
   if (doremote) {
      remote_submit_string(cmd, res);
   }
   else {
      local_submit_string(cmd, res);
   }
}
