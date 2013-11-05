main.o: main.cc ../include/ptrarray.h ../include/primarray.h dump.h ast.h \
  operators.h value.h main.h ../include/wdb.h  \
  ../include/schema.h ../include/twig.h ../include/pass/provabi.h
parse.o: parse.cc ptnode.h utils.h operators.h parse.h
ptnode.o: ptnode.cc ../include/ptrarray.h utils.h ptnode.h operators.h \
  ast.h value.h builtins.h main.h
ast.o: ast.cc dump.h ast.h operators.h value.h ../include/ptrarray.h
baseopt.o: baseopt.cc ast.h operators.h value.h ../include/ptrarray.h \
  main.h
indexify.o: indexify.cc ../include/ptrarray.h ../include/primarray.h \
  dump.h main.h ast.h operators.h value.h
output.o: output.cc ast.h operators.h value.h ../include/ptrarray.h \
  builtins.h dbops.h main.h
eval.o: eval.cc utils.h dump.h ast.h operators.h value.h \
  ../include/ptrarray.h builtins.h dbops.h main.h
path.o: path.cc utils.h dump.h ast.h operators.h value.h \
  ../include/ptrarray.h dbops.h main.h
dbops.o: dbops.cc ../include/primarray.h ast.h operators.h value.h \
  ../include/ptrarray.h dbops.h main.h  \
  ../include/wdb.h ../include/schema.h ../include/twig.h \
  ../include/pass/provabi.h
builtins.o: builtins.cc utils.h dump.h ast.h operators.h value.h \
  ../include/ptrarray.h builtins.h main.h
value.o: value.cc utils.h dump.h value.h ../include/ptrarray.h ast.h \
  operators.h builtins.h
operators.o: operators.cc operators.h
dump.o: dump.cc ../include/primarray.h dump.h
utils.o: utils.cc utils.h
