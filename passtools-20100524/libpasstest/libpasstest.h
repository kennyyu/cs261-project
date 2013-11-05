extern int __libpasstest_outputfd;
extern int __libpasstest_initialized;

#define LIBPASSTEST_CHECKINIT() \
	__libpasstest_initialized ? 0 : __libpasstest_initialize()

int __libpasstest_initialize(void);

