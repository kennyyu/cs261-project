TOP=.
include $(TOP)/mk/base.mk

SUBDIRS=include libpass libpasstest libpass-python libtwig \
	twig_dump libwdb libcyclecheck \
	waldo wdb_dump osage libpql libpqlutil sage convert2xml man
# libpass-java
# wdb_s3dump

SUBDIRS.linux=\
	mkfs.lasagna fsck.lasagna mount.lasagna umount.lasagna \
	mount_lasagna_helper umount_lasagna_helper

SUBDIRS.netbsd=# empty


include $(TOP)/mk/os.mk
SUBDIRS+=$(SUBDIRS.$(OS))

include $(TOP)/mk/subdir.mk

