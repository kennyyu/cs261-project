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
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <pass/dpapi.h>
#include "libpass.h"

#include "org_harvard_pass_DPAPI.h"


/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    _init
 * Signature: ()V
 */
JNIEXPORT jint JNICALL Java_org_harvard_pass_DPAPI__1init
  (JNIEnv * env, jclass c)
{
   return dpapi_init();
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    openFile
 * Signature: (Ljava/lang/String;Z)I
 */
JNIEXPORT jint JNICALL Java_org_harvard_pass_DPAPI_openFile
  (JNIEnv * env, jclass c, jstring jname, jboolean forwriting)
{
   jboolean iscopy;
   const char* name = (*env)->GetStringUTFChars(env, jname, &iscopy);
   
   int fd = open(name, forwriting ? O_RDWR : O_RDONLY, 0644);
   if (fd < 0) fd = -1;
   
   (*env)->ReleaseStringUTFChars(env, jname, name);
   return fd;
}


/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    closeHandle
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_harvard_pass_DPAPI_closeHandle
  (JNIEnv * env, jclass c, jint fd)
{
   close(fd);
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    _createNode
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_org_harvard_pass_DPAPI__1createNode
  (JNIEnv * env, jclass c, jint fd)
{
   return dpapi_mkphony(fd);
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    freeze
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_harvard_pass_DPAPI_freeze
  (JNIEnv * env, jclass c, jint fd)
{
   dpapi_freeze(fd);
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    addXRef
 * Signature: (ILjava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_org_harvard_pass_DPAPI_addXRef__ILjava_lang_String_2I
  (JNIEnv * env, jclass c, jint fd, jstring jkey, jint xref_fd)
{
   struct dpapi_addition rec;
   uint32_t version;
   __pnode_t  pnode;
   int err;

   if (LIBPASS_CHECKINIT()) {
      return;
   }

   jboolean iscopy;
   const char* key = (*env)->GetStringUTFChars(env, jkey, &iscopy);


   err = paread(xref_fd, NULL, 0, &pnode, &version);
   if ( err < 0 ) {
      fprintf(stderr, "addXRef: PA read error on file descriptior %d\n", xref_fd);
      version = 0;
   }

   (void)pnode; // XXX should do something with the pnode number
   rec.da_target = fd;
   rec.da_precord.dp_flags = PROV_IS_ANCESTRY;
   rec.da_precord.dp_attribute = key;
   rec.da_precord.dp_value.dv_type = PROV_TYPE_OBJECTVERSION;
   rec.da_precord.dp_value.dv_fd = xref_fd;
   rec.da_precord.dp_value.dv_version = version;
   rec.da_conversion = PROV_CONVERT_NONE;

   int r = pawrite(fd, NULL, 0, &rec, 1);
   if (r < 0) fprintf(stderr, "addXRef: PA write error on file descriptior %d (xref = %d)\n", fd, xref_fd);
   
   (*env)->ReleaseStringUTFChars(env, jkey, key);
   
   (void)r;
   //return r
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    addXRef
 * Signature: (ILjava/lang/String;II)V
 */
JNIEXPORT void JNICALL Java_org_harvard_pass_DPAPI_addXRef__ILjava_lang_String_2II
  (JNIEnv * env, jclass c, jint fd, jstring jkey, jint xref_fd, jint ver)
{
   struct dpapi_addition rec;

   if (LIBPASS_CHECKINIT()) {
      return;
   }

   jboolean iscopy;
   const char* key = (*env)->GetStringUTFChars(env, jkey, &iscopy);

   rec.da_target = fd;
   rec.da_precord.dp_flags = PROV_IS_ANCESTRY;
   rec.da_precord.dp_attribute = key;
   rec.da_precord.dp_value.dv_type = PROV_TYPE_OBJECTVERSION;
   rec.da_precord.dp_value.dv_fd = xref_fd;
   rec.da_precord.dp_value.dv_version = ver;
   rec.da_conversion = PROV_CONVERT_NONE;

   int r = pawrite(fd, NULL, 0, &rec, 1);
   if (r < 0) fprintf(stderr, "addXRef(ver): PA write error on file descriptior %d\n", fd);
   
   (*env)->ReleaseStringUTFChars(env, jkey, key);
   
   (void)r;
   //return r
}

/*
 * Class:     org_harvard_pass_DPAPI
 * Method:    addStr
 * Signature: (ILjava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_harvard_pass_DPAPI_addStr
  (JNIEnv * env, jclass c, jint fd, jstring jkey, jstring jvalue)
{
   struct __pass_pawrite_args a;
   struct dpapi_addition rec;

   if (LIBPASS_CHECKINIT()) {
      return;
   }

   jboolean iscopy;
   const char* key = (*env)->GetStringUTFChars(env, jkey, &iscopy);
   const char* value = (*env)->GetStringUTFChars(env, jvalue, &iscopy);
   
   char* mvalue = (char*) malloc(strlen(value) + 4);
   strcpy(mvalue, value);

   rec.da_target = fd;
   rec.da_precord.dp_flags = PROV_IS_ANCESTRY;
   rec.da_precord.dp_attribute = key;
   rec.da_precord.dp_value.dv_type = PROV_TYPE_STRING;
   rec.da_precord.dp_value.dv_string = mvalue;
   rec.da_conversion = PROV_CONVERT_NONE;

   a.fd = fd;
   a.data = NULL;
   a.datalen = 0;
   a.records = &rec;
   a.numrecords = 1;
   
   int r;

   if (ioctl(__libpass_hook_fd, PASSIOCWRITE, &a) == -1) {
      r = -1;
   }
   else {
      r = a.datalen_ret;
   }
   
   if (r < 0) fprintf(stderr, "addStr: PA write error on file descriptior %d\n", fd);

   free(mvalue);
   (*env)->ReleaseStringUTFChars(env, jkey, key);
   (*env)->ReleaseStringUTFChars(env, jvalue, value);
   
   (void)r;
   //return r;
}
