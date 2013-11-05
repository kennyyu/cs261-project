#ifndef _PASS_DPAPI_H_
#define _PASS_DPAPI_H_

#include <pass/provabi.h>

/*
 * Function calls
 */

/* Initialize and check for disclosed provenance support */
int dpapi_init(void);

/* Freeze the target object. Returns 0 or -1 on error. */
int dpapi_freeze(int fd);

/* Make a new object. Returns a file handle. */
int dpapi_mkphony(int reference_fd);

/* revive a phony given its pnode and version */
int dpapi_revive_phony(int reference_fd, __pnode_t pnode, version_t version);

/* sync a given phony */
int dpapi_sync(int fd);

/* I/O. */
ssize_t paread(int fd, void *data, size_t datalen,
	       __pnode_t *pnode_ret, version_t *version_ret);

ssize_t pawrite(int fd, const void *data, size_t datalen,
		const struct dpapi_addition *records, unsigned numrecords);

#endif /* _PASS_DPAPI_H_ */
