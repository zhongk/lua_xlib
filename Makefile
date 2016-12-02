CC = gcc -std=gnu99
CXX = g++
CFLAGS = -O2 -g -fPIC
LDFLAGS = -shared
LIBRARY = -llua
LIBDIR = ./libs
LIBDIR_POSIX = $(LIBDIR)/posix
LIBDIR_OTLDB = $(LIBDIR)/otldb

MAKE_MODULE = $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(INCLUDE) $(LIBRARY)
CXX_MAKE_MODULE = $(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $< $(INCLUDE) $(LIBRARY)

modules : $(LIBDIR)/socket.so $(LIBDIR)/bigint.so $(LIBDIR)/svipc.so \
  $(LIBDIR)/zlog.so $(LIBDIR)/rapidxml.so $(LIBDIR)/python.so \
  $(LIBDIR_POSIX)/errno.so $(LIBDIR_POSIX)/sys.so $(LIBDIR_POSIX)/ipc.so \
  $(LIBDIR_POSIX)/user.so $(LIBDIR_POSIX)/fs.so $(LIBDIR_POSIX)/fd.so \
  $(LIBDIR_OTLDB)/oracle.so $(LIBDIR_OTLDB)/odbc.so \
  $(LIBDIR)/rabbitmq.so

$(LIBDIR)/socket.so : lsocklib.c
  $(MAKE_MODULE)

$(LIBDIR)/bigint.so : lbnlib.c
  $(MAKE_MODULE) third_party/mini-gmp/mini-gmp.c -Ithird_party/mini-gmp

$(LIBDIR_POSIX)/errno.so : lerrno.c
  $(MAKE_MODULE)

$(LIBDIR_POSIX)/fs.so : lfslib.c
  $(MAKE_MODULE)

$(LIBDIR_POSIX)/fd.so : lfdlib.c
  $(MAKE_MODULE)

$(LIBDIR_POSIX)/user.so : luserlib.c
  $(MAKE_MODULE)

$(LIBDIR_POSIX)/sys.so : lsyslib.c
  $(MAKE_MODULE) -DHAVE_ICONV -liconv -lm

$(LIBDIR_POSIX)/ipc.so : lipclib.c
  $(MAKE_MODULE) -lrt -lm

$(LIBDIR)/svipc.so : lsvipc.c
  $(MAKE_MODULE)

$(LIBDIR)/python.so : lua_py.c
  $(MAKE_MODULE) `pkg-config --cflags --libs python`

$(LIBDIR_OTLDB)/odbc.so : luaotl_odbc.cpp lua_otl.hpp
  $(CXX_MAKE_MODULE) -Ithird_party/otlv4_h -lodbc

$(LIBDIR_OTLDB)/oracle.so : luaotl_oracle.cpp lua_otl.hpp
  $(CXX_MAKE_MODULE) -Ithird_party/otlv4_h \
  -I$(ORACLE_HOME)/rdbms/demo -I$(ORACLE_HOME)/rdbms/public \
  -L$(ORACLE_HOME)/lib -lclntsh `cat $(ORACLE_HOME)/lib/sysliblist`

$(LIBDIR)/rapidxml.so : rapidxml.cpp
  $(CXX_MAKE_MODULE) -Ithird_party/rapidxml

$(LIBDIR)/zlog.so : lzlog.c
  $(MAKE_MODULE) -lzlog -lpthread

$(LIBDIR)/rabbitmq.so : lamqplib.c
  $(MAKE_MODULE) -DAMQP_WITH_SSL -lrabbitmq -lrt -lssl -lcrypto
