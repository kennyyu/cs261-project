pqlcontext.o: pqlcontext.c utils.h memdefs.h ../include/pql.h datatype.h \
  array.h inlinedefs.h pttree.h functions.h pqlcontext.h
pqlquery.o: pqlquery.c utils.h ../include/pql.h pttree.h array.h \
  inlinedefs.h memdefs.h functions.h tcalc.h passes.h pqlcontext.h
pttree.o: pttree.c pqlvalue.h array.h inlinedefs.h memdefs.h utils.h \
  ../include/pql.h layout.h passes.h pqlcontext.h pttree.h functions.h
ptlex.o: ptlex.c ptprivate.h passes.h pqlcontext.h array.h inlinedefs.h \
  memdefs.h utils.h
ptparse.o: ptparse.c pqlvalue.h array.h inlinedefs.h memdefs.h utils.h \
  ../include/pql.h pttree.h functions.h ptprivate.h pqlcontext.h ptpcb.h
resolvevars.o: resolvevars.c utils.h pttree.h array.h inlinedefs.h \
  memdefs.h functions.h passes.h pqlcontext.h
normalize.o: normalize.c utils.h pttree.h array.h inlinedefs.h memdefs.h \
  functions.h passes.h ../include/pql.h
unify.o: unify.c utils.h pttree.h array.h inlinedefs.h memdefs.h \
  functions.h passes.h
movepaths.o: movepaths.c pttree.h array.h inlinedefs.h memdefs.h utils.h \
  functions.h passes.h pqlcontext.h
bindnil.o: bindnil.c pttree.h array.h inlinedefs.h memdefs.h utils.h \
  functions.h passes.h
dequantify.o: dequantify.c pttree.h array.h inlinedefs.h memdefs.h \
  utils.h functions.h passes.h
tcalc.o: tcalc.c utils.h datatype.h array.h inlinedefs.h memdefs.h \
  layout.h pqlvalue.h ../include/pql.h pqlcontext.h tcalc.h functions.h
tuplify.o: tuplify.c utils.h ../include/pql.h pqlvalue.h array.h \
  inlinedefs.h memdefs.h pttree.h functions.h tcalc.h passes.h
typeinf.o: typeinf.c datatype.h array.h inlinedefs.h memdefs.h utils.h \
  pqlvalue.h ../include/pql.h tcalc.h functions.h passes.h
typecheck.o: typecheck.c datatype.h array.h inlinedefs.h memdefs.h \
  utils.h pqlvalue.h ../include/pql.h tcalc.h functions.h passes.h \
  pqlcontext.h
baseopt.o: baseopt.c datatype.h array.h inlinedefs.h memdefs.h utils.h \
  pqlvalue.h ../include/pql.h tcalc.h functions.h passes.h
stepjoins.o: stepjoins.c datatype.h array.h inlinedefs.h memdefs.h \
  utils.h pqlvalue.h ../include/pql.h tcalc.h functions.h passes.h
eval.o: eval.c utils.h datatype.h array.h inlinedefs.h memdefs.h \
  pqlvalue.h ../include/pql.h tcalc.h functions.h passes.h pqlcontext.h
functions.o: functions.c utils.h functions.h
pqlvalue.o: pqlvalue.c ../include/pql.h array.h inlinedefs.h memdefs.h \
  utils.h layout.h datatype.h functions.h pqlvalue.h
datatype.o: datatype.c array.h inlinedefs.h memdefs.h utils.h \
  pqlcontext.h datatype.h
layout.o: layout.c utils.h layout.h array.h inlinedefs.h memdefs.h
array.o: array.c array.h inlinedefs.h memdefs.h utils.h
memdefs.o: memdefs.c utils.h memdefs.h
utils.o: utils.c ../include/pql.h utils.h
