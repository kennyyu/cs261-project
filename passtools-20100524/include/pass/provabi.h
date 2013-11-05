#ifndef _PROVABI_H
#define _PROVABI_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#include <stdint.h>
#define __user
#endif

#define PROV_ABI_VERSION 2

/*
 * Base types.
 */

#ifdef __KERNEL__
typedef __u64	__pnode_t;          /* provenance node */
typedef __u32   icapi_flags_t;      /* icapi flags */
typedef __u32	version_t;	    /* provenance version */
typedef __u64   provtxn_t;          /* provenance transaction id */
#else
typedef uint64_t __pnode_t;         /* provenance node */
typedef uint32_t icapi_flags_t;     /* icapi flags */
typedef uint32_t version_t;         /* provenance version */
typedef uint64_t provtxn_t;          /* provenance transaction id */
#endif

/*
 * Constants.
 */

/* Provenance record value types for dv_type et al. */
#define PROV_TYPE_NIL		0	/* nothing */
#define PROV_TYPE_STRING	1	/* string */
#define PROV_TYPE_MULTISTRING	2	/* zero or more strings */
#define PROV_TYPE_INT		3	/* int32_t */
#define PROV_TYPE_REAL		4	/* double */
#define PROV_TYPE_TIMESTAMP	5	/* struct prov_timestamp */
#define PROV_TYPE_INODE		6	/* uint32_t */
#define PROV_TYPE_PNODE		7	/* pnode_t */
#define PROV_TYPE_PNODEVERSION	8	/* struct prov_pnodeversion */
#define PROV_TYPE_OBJECT	9	/* context-dependent object type */
#define PROV_TYPE_OBJECTVERSION	10	/* same, plus version_t */
/* XXX these three should go away */
#define PROV_TYPE_FREEZE        11      /* bump the object up to next version */
#define PROV_TYPE_SUB           12      /* belongs to a sub transction */
#define PROV_TYPE_ENDSUB        13      /* end sub transction */

/* Flags for DPAPI dp_flags */
#define PROV_IS_ANCESTRY  1	/* Ancestry record (otherwise, identity) */

/* Conversions for DPAPI da_conversion */
typedef enum {
   PROV_CONVERT_NONE,			/* nothing */
   PROV_CONVERT_REFER_SRC,		/* make value a ref to data source */
   PROV_CONVERT_REFER_DST,		/* make value a ref to data dest */
} dpapi_conversion;

/*
 * Common types.
 */

/* Timestamp - like struct timespec but with fixed size */
struct prov_timestamp {
   int32_t pt_sec;
   int32_t pt_nsec;
};

/* pnode and version */
struct prov_pnodeversion {
   __pnode_t pnode;
   version_t version;
} __attribute__((__packed__));

/*
 * DPAPI types.
 */

/* DPAPI-level value */
/* (twig, schema, and sage use a different form without pointers) */
struct dpapi_value {
   unsigned dv_type;
   union {
      char *dv_string;			/* PROV_TYPE_STRING */
      char **dv_multistring;		/* PROV_TYPE_MULTISTRING */
      int32_t dv_int;			/* PROV_TYPE_INT */
      double dv_real;			/* PROV_TYPE_REAL */
      struct prov_timestamp dv_timestamp; /* PROV_TYPE_TIMESTAMP */
      struct {
	 int dv_fd;			/* PROV_TYPE_FD */
	 version_t dv_version;		/* PROV_TYPE_FD{,VERSION} */
      };
   };
};

/* DPAPI-level provenance record */
struct dpapi_precord {
   uint32_t dp_flags;		/* miscellaneous info */
   const char *dp_attribute;	/* name of provenance attribute */
   struct dpapi_value dp_value;	/* value for this attribute */
};

/* Provenance addition record */
struct dpapi_addition {
   int da_target;			/* file handle to add provenance to */
   struct dpapi_precord da_precord;	/* record to add */
   dpapi_conversion da_conversion;	/* adjustment to make */
};

/*
 * prov records passed from user-level
 */

//enum valtype {PROV_VALUE, PROV_REFERENCE, PROV_IMPLICIT} ;
//enum rectype {PROV_ANCESTOR, PROV_DESCRIPTIVE} ;

//struct prov_value {
//    size_t size;
//    char valptr[0];
//};
//
//struct user_prov_record {
//    int target;         /* file handle */
//    enum valtype vtype;
//    enum rectype rtype;
//    const char *   key;
//    union {
//	struct prov_value *val;
//	struct {
//	    int       ref;     /* file handle */
//	    version_t version;
//	};
//    };
//};

////////////////////////////////////////////////////////

// Common keys and values for convenience

// Values
#define PROV_KEY_TYPE          "TYPE"
#define PROV_KEY_NAME          "NAME"
#define PROV_KEY_INODE         "INODE"
#define PROV_KEY_PATH          "PATH"
#define PROV_KEY_ARGV          "ARGV"
#define PROV_KEY_ENV           "ENV"
#define PROV_KEY_FREEZETIME    "FREEZETIME"
#define PROV_KEY_EXECTIME      "EXECTIME"
#define PROV_KEY_FORKPARENT    "FORKPARENT"
#define PROV_KEY_PID           "PID"
#define PROV_KEY_KERNEL_PROV   "KERN_PROV"
#define PROV_KEY_KERNEL_MOD    "KERN_MODULE"
#define PROV_KEY_CREAT         "CREATE"
#define PROV_KEY_INODE         "INODE"
#define PROV_KEY_UNLINK        "UNLINK"

// Cross refs
#define PROV_KEY_INPUT         "INPUT"


// Basic types of provenanced objects
#define PROV_TYPE_PROC         "PROC"
#define PROV_TYPE_FILE         "FILE"
#define PROV_TYPE_NONPASS_FILE "NP_FILE"
#define PROV_TYPE_PIPE         "PIPE"
#define PROV_TYPE_DIR          "DIR"

//for transactions (XXX should go away)
#define PROV_TYPE_TRANSACTION  "TXN"

////////////////////////////////////////////////////////

// ioctl-related bits for /dev/provenance
#define __PASSIOC       ('P'+128)
#define PASSIOCGETABI   _IOR (__PASSIOC, 0, int)
#define PASSIOCFREEZE   _IOW (__PASSIOC, 1, int)
#define PASSIOCMKPHONY  _IOWR(__PASSIOC, 2, int)
#define PASSIOCREAD     _IOWR(__PASSIOC, 3, struct __pass_paread_args)
#define PASSIOCWRITE    _IOWR(__PASSIOC, 4, struct __pass_pawrite_args)
#define PASSIOCREVIVEPHONY _IOWR(__PASSIOC, 5, struct __pass_revive_phony_args)
#define PASSIOCSYNC     _IOW (__PASSIOC, 6, int)

struct __pass_paread_args {
   /* in */
   int fd;
   void __user *data;
   size_t datalen;
   const struct dpapi_addition __user *records;
   unsigned numrecords;

   /* out */
   __pnode_t pnode_ret;
   version_t version_ret;

   size_t datalen_ret;
};

struct __pass_pawrite_args {
   /* in */
   int fd;
   const void __user *data;
   size_t datalen;
   const struct dpapi_addition __user *records;
   unsigned numrecords;

   /* out */
   size_t datalen_ret;
};

struct __pass_revive_phony_args {
   /* in */
   int reference_fd;
   __pnode_t pnode;
   version_t version;

   /* out */
    int ret_fd;
};

#endif /* _PROVABI_H */
