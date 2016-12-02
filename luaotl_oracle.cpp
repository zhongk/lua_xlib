/*	g++ -O --shared -o otldb.so luaotl_oracle.cpp -llua \
	-I$(ORACLE_HOME)/rdbms/demo -I$(ORACLE_HOME)/rdbms/public \
	-L$(ORACLE_HOME)/lib -lclntsh `cat $(ORACLE_HOME)/lib/sysliblist` */
#define LUAOTL_CONNECT "OTL.Oracle.Connect"
#define LUAOTL_STREAM  "OTL.Oracle.Stream"

#define OTL_ORA9I

#define OTL_CONNECTION_CLOSED(err_code) \
	(err_code==1003 || err_code==1012 || err_code==1033 || \
	 err_code==1034 || err_code==1041 || err_code==1089 || \
	 err_code==1092 || err_code==3113 || err_code==3114 || \
	 (12150<=err_code && err_code<=12285) || \
	 (12500<=err_code && err_code<=12699))

#include "lua_otl.hpp"

DECLARE_LUAOTL_EXPORT_FUNCTION(oracle)
