extern int __libpass_hook_fd;
extern int __libpass_initialized;

#define LIBPASS_CHECKINIT() \
	__libpass_initialized ? 0 : __libpass_initialize()

int __libpass_initialize(void);

#define _PATH_DEV_PROVENANCE "/dev/provenance"
