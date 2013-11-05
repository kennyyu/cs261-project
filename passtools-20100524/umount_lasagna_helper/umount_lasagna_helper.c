#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

int
main(int argc, char **argv)
{
	int retval;
	FILE *fh, *fh_new;
	struct mntent *mnt_exist;

	if ((retval = umount(argv[1])) != 0) {
		perror("umount.lasagna");
		return retval;
	}

	fh = setmntent("/etc/mtab","r");
	if (fh == NULL) {
		perror("umount.lasagna");
		return EACCES;
	}
	fh_new = setmntent("/etc/mtab.pass.new", "w");
	if (fh_new == NULL) {
		endmntent(fh);
		perror("umount.lasagna");
		return EACCES;
	}

	while ((mnt_exist = getmntent(fh)) != NULL) {
		if (strcmp(mnt_exist->mnt_dir, argv[1]) || 
			strcmp(mnt_exist->mnt_type, "lasagna")) {
			
			addmntent(fh_new, mnt_exist);
		}
	}
	endmntent(fh_new);
	endmntent(fh);
	if ((retval = rename("/etc/mtab.pass.new", "/etc/mtab")) != 0) {
		perror("umount.lasagna");
		return retval;
	}
	
	return 0;
}
