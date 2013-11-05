#ifndef TWIG_H
#define TWIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__
#include <linux/prov.h>
#include <linux/types.h>

/*
 * NOTE: lsn_t must for now be the same as provtxn_t; but lsn_t is
 * expected to go away entirely with the upcoming twig rework. At
 * that point provtxn_t should probably be made 32 bits instead of 64.
 */
typedef __u64   lsn_t;              /* provenance lsn number */

#else /* not __KERNEL__ */
#include <stdint.h>
#include <sys/socket.h>
#include "pass/provabi.h"

typedef __pnode_t   pnode_t;        /* provenance node number */
typedef uint64_t lsn_t;             /* provenance lsn number */
typedef uint32_t lasagna_ino_t;     /* inode number */
#endif /* __KERNEL__ */

/**
 *  We had some debate as to whether the structures in this file
 *  should be packed. To address this I have used a #define here.
 */
//#define PACKED
#define PACKED __attribute__((__packed__))

/*
 * Provenance
 */

/**
 * Constants used in twig file header
 */
#define TWIG_MAGIC_NUMBER (('T' << 24) | ('W' << 16) | ('I' << 8) | 'G')
//#define TWIG_MAGIC_NUMBER (('G' << 24) | ('I' << 16) | ('W' << 8) | 'T')
#define TWIG_VERSION      2

/**
 * Each record is of one of these types.
 */
enum twig_rectype { TWIG_REC_BEGIN,
                    TWIG_REC_END,
                    TWIG_REC_WAP,
                    TWIG_REC_CANCEL=4,
                    TWIG_REC_PROV,
		    TWIG_REC_BEGINSUB,
		    TWIG_REC_SUB,
		    TWIG_REC_ENDSUB,
                    TWIG_REC_HEADER = TWIG_MAGIC_NUMBER };

/**
 * All records share the following form.
 *
 * They differ in the size and contents of the data that follows.
 */
struct twig_rec {
   uint32_t                rectype;
   uint32_t                reclen;    // TODO: remove
} PACKED;

////////////////////////////////////////////////////////////////////////////////
//
// Convenience types -- cast twig_rec.data to one of these
//

/**
 * Per file header.
 */
struct twig_rec_header {
   struct twig_rec         rec;
   version_t               version;
} PACKED;

/**
 * Beginning of a group of records.
 *
 * Each group of records is associated with a single log sequence number.
 */
struct twig_rec_begin {
   struct twig_rec         rec;
   lsn_t                   lsn;
} PACKED;

/**
 * End of a group of records.
 *
 * Each group of records is associated with a single log sequence number.
 */
struct twig_rec_end {
   struct twig_rec         rec;
   lsn_t                   lsn;
} PACKED;

/**
 * Write ahead provenance record.
 *
 * We store a checksum and range with each write.
 */
struct twig_rec_wap {
   struct twig_rec         rec;
   pnode_t                 pnum;      // pnode number
   version_t               version;   // version

   uint64_t                off;
   uint32_t                len;
   uint8_t                 md5[16];
} PACKED;

/**
 * A data write was *not* completed succesfully -- it failed either
 * fully or partially.
 *
 * Data write(s) associated with a chunk of provenance was not
 * completely and sucesfully written to disk. The lsn specifies the
 * provenance for the failed write(s).
 */
struct twig_rec_cancel {
   struct twig_rec         rec;
   lsn_t                   lsn;
} PACKED;

/**
 * Twig-level provenance record.
 */
struct twig_precord {
   struct twig_rec         rec;

   // Object
   pnode_t                 tp_pnum;	// pnode number
   version_t               tp_version;	// version

   // Flags (PROV_IS_* from provabi.h)
   uint16_t                tp_flags;	// precord flags

   // Attribute
   union {
      uint16_t             tp_attrlen;	// length of attribute
      uint16_t             tp_attrcode; // code version of attribute
   };

   // Value
   uint32_t                tp_valuelen;	// length of value
   uint8_t		   tp_valuetype;// type of value

   uint8_t                 data[0];
} PACKED;

#define TWIG_PRECORD_ATTRIBUTE(pr) ((const char *) ((pr)->data))
#define TWIG_PRECORD_VALUE(pr) ((const void *) ((pr)->data + (pr)->tp_attrlen))

////////////////////////////////////////////////////////////////////////////////

#ifndef __KERNEL__
/**
 * Twig add provenance API.
 */

void twig_print_rec(const struct twig_rec *rec);
#endif /* __KERNEL__ */

#ifdef __cplusplus
};
#endif

#endif /* TWIG_H */
