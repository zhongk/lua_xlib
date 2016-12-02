CC = gcc -std=gnu99
CXX = g++
CFLAGS = -O2 -g -fPIC
LDFLAGS = -shared
LIBRARY = -llua
LIBDIR = ./libs
LIBDIR_POSIX = $(LIBDIR)/posix
LIBDIR_OTLDB = $(LIBDIR)/otldb
LIBPATH_AMQP = third_party/rabbitmq-c-0.8.0/librabbitmq
LIBPATH_ZLOG = third_party/zlog-1.2.9/src

MAKE_MODULE = $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(INCLUDE) $(LIBRARY)
CXX_MAKE_MODULE = $(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $< $(INCLUDE) $(LIBRARY)

modules : $(LIBDIR)/socket.so $(LIBDIR)/bigint.so $(LIBDIR)/svipc.so \
	$(LIBDIR)/zlog.so $(LIBDIR)/rapidxml.so $(LIBDIR)/python.so \
	$(LIBDIR_POSIX)/errno.so $(LIBDIR_POSIX)/sys.so $(LIBDIR_POSIX)/ipc.so \
	$(LIBDIR_POSIX)/user.so $(LIBDIR_POSIX)/fs.so $(LIBDIR_POSIX)/fd.so \
	$(LIBDIR_OTLDB)/oracle.so $(LIBDIR_OTLDB)/odbc.so \
	$(LIBDIR)/rabbitmq.so

$(LIBDIR_POSIX)/errno.so : lerrno.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE)

$(LIBDIR_POSIX)/fs.so : lfslib.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE)

$(LIBDIR_POSIX)/fd.so : lfdlib.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE)

$(LIBDIR_POSIX)/user.so : luserlib.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE)

$(LIBDIR_POSIX)/sys.so : lsyslib.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE) -DHAVE_ICONV -liconv -lm

$(LIBDIR_POSIX)/ipc.so : lipclib.c
	if [ ! -d $(LIBDIR_POSIX) ]; then mkdir $(LIBDIR_POSIX); fi
	$(MAKE_MODULE) -lrt -lm

$(LIBDIR)/svipc.so : lsvipc.c
	$(MAKE_MODULE)

$(LIBDIR)/socket.so : lsocklib.c
	$(MAKE_MODULE)

$(LIBDIR)/python.so : lua_py.c
	$(MAKE_MODULE) `pkg-config --cflags --libs python`

$(LIBDIR_OTLDB)/odbc.so : luaotl_odbc.cpp lua_otl.hpp third_party/otlv4_h/otlv4.h
	if [ ! -d $(LIBDIR_OTLDB) ]; then mkdir $(LIBDIR_OTLDB); fi
	$(CXX_MAKE_MODULE) -Ithird_party/otlv4_h -lodbc

$(LIBDIR_OTLDB)/oracle.so : luaotl_oracle.cpp lua_otl.hpp third_party/otlv4_h/otlv4.h
	if [ ! -d $(LIBDIR_OTLDB) ]; then mkdir $(LIBDIR_OTLDB); fi
	$(CXX_MAKE_MODULE) -Ithird_party/otlv4_h \
	-I$(ORACLE_HOME)/rdbms/demo -I$(ORACLE_HOME)/rdbms/public \
	-L$(ORACLE_HOME)/lib -lclntsh `cat $(ORACLE_HOME)/lib/sysliblist`

$(LIBDIR)/bigint.so : lbnlib.c third_party/mini-gmp/mini-gmp.c
	$(MAKE_MODULE) third_party/mini-gmp/mini-gmp.c -Ithird_party/mini-gmp

$(LIBDIR)/rapidxml.so : rapidxml.cpp third_party/rapidxml/rapidxml.hpp
	$(CXX_MAKE_MODULE) -Ithird_party/rapidxml

$(LIBDIR)/zlog.so : lzlog.c $(LIBPATH_ZLOG)/libzlog.a
	$(MAKE_MODULE) -I$(LIBPATH_ZLOG) $(LIBPATH_ZLOG)/libzlog.a -lpthread

$(LIBDIR)/rabbitmq.so : lamqplib.c $(LIBPATH_AMQP)/.libs/librabbitmq.a
	$(MAKE_MODULE) -I$(LIBPATH_AMQP) $(LIBPATH_AMQP)/.libs/librabbitmq.a -DAMQP_WITH_SSL -lrt -lssl -lcrypto

#############################################################################################################

third_party/otlv4_h/otlv4.h :
	cd third_party && tar zxvf otlv4_h.tar.gz

third_party/mini-gmp/mini-gmp.c:
	cd third_party && tar zxvf mini-gmp.tar.gz

third_party/rapidxml/rapidxml.hpp:
	cd third_party && tar zxvf rapidxml.tar.gz

$(LIBPATH_ZLOG)/libzlog.a :
	if [ ! -d $(LIBPATH_ZLOG) ]; then \
		cd third_party && tar zxvf zlog-1.2.9.tar.gz; \
	fi
	cd $(LIBPATH_ZLOG) && CFLAGS=-fPIC make -f zlog.mk

$(LIBPATH_AMQP)/.libs/librabbitmq.a :
	if [ ! -d $(LIBPATH_AMQP) ]; then \
		cd third_party && tar zxvf rabbitmq-c-0.8.0.tar.gz; \
	fi
	if [ ! -f third_party/rabbitmq-c-0.8.0/Makefile ]; then \
		cd third_party/rabbitmq-c-0.8.0 && \
		./configure --with-pic --disable-shared --disable-docs --disable-tools --disable-examples; \
	fi
	cd third_party/rabbitmq-c-0.8.0 && make
