#ifndef _LASAGNA_H
#define _LASAGNA_H

#define LASAGNA_DIR               ".lasagna_stuff/"

#define LASAGNA_METADB_FILENAME   (LASAGNA_DIR "lasagna.metadb")
#define LASAGNA_CLEAN_FILENAME    (LASAGNA_DIR "clean")
#define LASAGNA_NEXTNUMS_FILENAME (LASAGNA_DIR "lasagna.state")

/* provenance object state: either frozen or thawed */
enum prov_pnode_state {
    PROV_STATE_THAWED,
    PROV_STATE_FROZEN
};

struct lasagna_metadb_entry {
    unsigned long ino;
    pnode_t       pnode;
    version_t     version;
    icapi_flags_t icapi_flags; /* unused */
    enum prov_pnode_state state;
};

struct lasagna_nextnums {
    pnode_t   pnode;
    lsn_t     lsn;
    uint32_t  lognum;
} __attribute__((__packed__));

#endif /* _LASAGNA_H */
