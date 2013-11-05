#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

int
main(int argc, char **argv)
{
	FILE *fh;
	struct mntent *mnt_exist;
	struct mntent m;

	if (argc != 5) {
		fprintf(stderr, "Usage: %s dev mountpoint . args\n",
			argv[0]);
		return EINVAL;
	}

	fh = setmntent("/etc/mtab","r");
	if (fh == NULL) {
		perror("mount.lasagna");
		return EACCES;
	}

	while ((mnt_exist = getmntent(fh)) != NULL) {
		if (!strcmp(mnt_exist->mnt_dir, argv[2]) && 
			!strcmp(mnt_exist->mnt_type, "lasagna")) {
			endmntent(fh);
			return EBUSY;
		}
	}
	endmntent(fh);

	if (strlen(argv[4]) == 2) {
		argv[4][0] = '\0';
	} else {
		memmove(argv[4], argv[4]+3, strlen(argv[4])-2);
	}

#if 0
	int i;
	for (i = 0; i <= argc; ++i) {
		printf("mount.lasagna: --%s--\n", argv[i]);
	}
#endif

	if (mount(argv[1], argv[2], "lasagna", 0, argv[4])) {
		int serrno = errno;
		perror("mount.lasagna");
		return serrno;
	} else {
		fh = setmntent("/etc/mtab","a");
		m.mnt_fsname = argv[1];
		m.mnt_dir = argv[2];
		m.mnt_type = "lasagna";
		m.mnt_opts = MNTOPT_DEFAULTS;
		m.mnt_freq = 0;
		m.mnt_passno = 0;
		if (addmntent(fh,&m) != 0) {
			perror("mount.lasagna");
			endmntent(fh);
			umount(argv[1]);
			return EACCES;
		}
		endmntent(fh);
		return 0;
	}
}
