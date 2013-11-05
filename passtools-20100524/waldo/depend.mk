waldo.o: waldo.c  ../include/twig.h \
  ../include/pass/provabi.h ../include/debug.h process.h recover.h log.h \
  ../include/wdb.h ../include/schema.h ../include/twig.h
process.o: process.c ../include/debug.h ../include/schema.h \
   ../include/twig.h ../include/pass/provabi.h \
  ../include/twig_file.h ../include/wdb.h ../include/schema.h log.h \
  process.h ../include/twig.h
recover.o: recover.c ../include/wdb.h  \
  ../include/schema.h ../include/twig.h ../include/pass/provabi.h \
  ../include/twig.h ../include/twig_file.h ptr_array.h \
  ../include/schema.h cleanpages.h md5.h i2n.h log.h process.h recover.h
log.o: log.c udev_sysdeps.h log.h
i2n.o: i2n.c  ../include/wdb.h \
  ../include/schema.h ../include/twig.h ../include/pass/provabi.h i2n.h
md5.o: md5.c md5.h
ptr_array.o: ptr_array.c ptr_array.h ../include/schema.h \
   ../include/twig.h ../include/pass/provabi.h
cleanpages.o: cleanpages.cc cleanpages.h
