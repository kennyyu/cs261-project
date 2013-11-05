main.o: main.cc interact.h user.h server.h query.h
interact.o: interact.cc interact.h user.h query.h
user.o: user.cc ../include/pql.h result.h ../include/ptrarray.h query.h \
  user.h
server.o: server.cc ../include/pql.h ../include/pqlutil.h \
  ../include/ptrarray.h utils.h socketpath.h result.h query.h server.h
query.o: query.cc remote.h local.h query.h
local.o: local.cc ../include/pql.h ../include/pqlutil.h utils.h result.h \
  ../include/ptrarray.h backend.h local.h
remote.o: remote.cc ../include/pql.h ../include/pqlutil.h utils.h \
  socketpath.h result.h ../include/ptrarray.h remote.h
backend.o: backend.cc ../include/pql.h ../include/pqlutil.h \
  ../include/wdb.h  ../include/schema.h \
  ../include/twig.h ../include/pass/provabi.h ../include/primarray.h \
  backend.h
result.o: result.cc ../include/pql.h result.h ../include/ptrarray.h
socketpath.o: socketpath.cc pathnames.h socketpath.h
utils.o: utils.cc utils.h
