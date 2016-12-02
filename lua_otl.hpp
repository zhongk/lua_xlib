/*************************************************************************************************************************
local otl_${db_name}_connect = require('otldb.${db_name}')

otl_XXX_connect(connect_str, auto_commit=false) return userdata otl_connect
otl_connect:good()
otl_connect:close()
otl_connect:connect(auto_commit=false)
otl_connect:commit()
otl_connect:rollback()
otl_connect:set_max_long_size(max_size)
otl_connect:get_max_long_size()
otl_connect:set_auto_commit(boolean auto_commit_on)
otl_connect:direct_exec(sql)
otl_connect:syntax_check(sql)
otl_connect:cursor() return otl_stream

otl_stream:good()
otl_stream:close()
otl_stream:execute(sql)
otl_stream:describe_out_vars()
otl_stream:describe_in_vars()
otl_stream:describe_select()
otl_stream:set_batch_size(batch_size)
otl_stream:get_batch_size()
# following methods for oracle
otl_stream:get_stream_type()
otl_stream:set_batch_error_mode(boolean mode)
otl_stream:get_batch_errors()
otl_stream.code_batch_errors = 24381

otl_stream:fetchone()
otl_stream:eof()
otl_stream:rows()
otl_stream:is_null(value)
otl_stream.null = userdata(0x0)
otl_stream:bind_values({value, ...})
otl_stream:bind_names({name=value, ...})
otl_stream:outvars()
otl_stream:flush()
otl_stream:clean(clean_up_error_flag=0)
otl_stream:get_rpc()
*************************************************************************************************************************/
#define OTL_ANSI_CPP
#define OTL_STL
#define OTL_BIGINT long long
#define OTL_ORA_MAP_BIGINT_TO_LONG

#include <otlv4.h>
#include <lua.hpp>
#include <map>
#include <string>

typedef struct
{
	int status;
	char connect_str[256];
	otl_connect *_connect;
} luaotl_connect_t;

typedef struct
{
	int connect;
	int batch_size;
	otl_stream *_stream;
} luaotl_stream_t;

static int report_otlexc(lua_State *L, otl_exception& e)
{
	lua_pushnil(L);
	lua_pushstring(L, (const char *)e.msg);
	lua_pushinteger(L, e.code);
	return 3;
}

static void close_otl_connect(luaotl_connect_t *conn)
{
	if (conn->_connect)
	{
		if (conn->_connect->connected)
		{
			try{
			#ifdef OTL_ORA8
				if (!*conn->connect_str)
					conn->_connect->session_end();
				else
			#endif
				conn->_connect->logoff();
			}catch(...) {}
		}
		conn->status = 0;
	}
}

static void close_otl_stream(luaotl_stream_t *stmt)
{
	if (stmt->_stream)
	{
		if (stmt->_stream->good())
		{
			try{ stmt->_stream->close(); }catch(...) {}
		}
		delete stmt->_stream;
		stmt->_stream = NULL;
	}
}

static luaotl_connect_t *get_otl_connect(lua_State *L, luaotl_stream_t *stmt)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, stmt->connect);
	luaotl_connect_t *conn = (luaotl_connect_t *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return conn;
}

static int handle_exception_and_return(lua_State *L, luaotl_connect_t *conn, otl_exception& e)
{
#ifdef OTL_CONNECTION_CLOSED
	if (OTL_CONNECTION_CLOSED(e.code))
	{
		conn->status = -1;
	}
#endif
	return report_otlexc(L, e);
}

// otl_XXX_connect(connect_str, auto_commit=false) return userdata otl_connect
static int luaotl_new_connect(lua_State *L)
{
	const char *connect_str = NULL;
#ifdef OTL_ORA8
	const char *username, *password, *dsn;
	if (lua_istable(L, 1))
	{
		lua_getfield(L, 1, "username"); username = luaL_checkstring(L, -1); lua_pop(L, 1);
		lua_getfield(L, 1, "password"); password = luaL_checkstring(L, -1); lua_pop(L, 1);
		lua_getfield(L, 1, "dsn"); dsn = luaL_optstring(L, -1, NULL); lua_pop(L, 1);
	}else
#endif
	connect_str = luaL_checkstring(L, 1);
	bool auto_commit = false;
	if (!lua_isnoneornil(L, 2))
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		auto_commit = (bool)lua_toboolean(L, 2);
	}

	otl_connect *new_conn = NULL;
	try{
	#ifdef OTL_ORA8
		if (!connect_str)
		{
			new_conn = new otl_connect();
			new_conn->server_attach(dsn);
			new_conn->session_begin(username, password);
		}else
	#endif
		new_conn = new otl_connect(connect_str, auto_commit ? 1 : 0);
	}catch(otl_exception& e)
	{
		if (new_conn) delete(new_conn);
		return report_otlexc(L, e);
	}
	
	luaotl_connect_t *conn = (luaotl_connect_t *)lua_newuserdata(L, sizeof(luaotl_connect_t));
	memset(conn, 0, sizeof(luaotl_connect_t));
	luaL_getmetatable(L, LUAOTL_CONNECT);
	lua_setmetatable(L, -2);
	conn->_connect = new_conn;
	if (connect_str) strcpy(conn->connect_str, connect_str);
	conn->status = 1;
	return 1;
}

// otl_connect:__gc()
static int luaotl_connect___gc(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	close_otl_connect(conn);
#ifdef OTL_ORA8
	if (!*conn->connect_str) conn->_connect->server_detach();
#endif
	delete conn->_connect;
	conn->_connect = NULL;
	return 0;
}

// otl_connect:good()
static int luaotl_connect_good(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	lua_pushboolean(L, (conn->status>0 && conn->_connect->connected));
	return 1;
}

// otl_connect:close()
static int luaotl_connect_close(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	close_otl_connect(conn);
	return 0;
}

// otl_connect:connect()
static int luaotl_connect_connect(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	luaL_argcheck(L, conn->status <= 0, 1, "connection not logoff");
	if (conn->status < 0) close_otl_connect(conn);
	
	bool auto_commit = false;
	if (!lua_isnoneornil(L, 2))
	{
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		auto_commit = (bool)lua_toboolean(L, 2);
	}
	
	try{
#ifdef OTL_ORA8
		if (!*conn->connect_str)
			conn->_connect->session_reopen();
		else
#endif
		conn->_connect->rlogon(conn->connect_str, auto_commit ? 1 : 0);
		conn->status = 1;
	}catch(otl_exception& e)
	{
		return report_otlexc(L, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

#define check_otl_connect(L, conn) \
	do{ \
		luaL_argcheck(L, conn->status>0 && conn->_connect->connected, 1, "otl_connect disconnect or abnormal status"); \
	}while(0)
#define check_otl_stream(L, stmt, conn) \
	do{ \
		check_otl_connect(L, conn); \
		luaL_argcheck(L, stmt->_stream && stmt->_stream->good(), 1, "otl_stream closed or abnormal status"); \
	}while(0)

// otl_connect:commit()
static int luaotl_connect_commit(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	
	try{
		conn->_connect->commit();
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_connect:rollback()
static int luaotl_connect_rollback(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	
	try{
		conn->_connect->rollback();
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_connect:set_max_long_size(max_size)
static int luaotl_connect_set_max_long_size(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	int max_size = luaL_checkinteger(L, 2);
	
	conn->_connect->set_max_long_size(max_size);
	lua_pushboolean(L, 1);
	return 1;
}

// otl_connect:get_max_long_size()
static int luaotl_connect_get_max_long_size(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	
	lua_pushinteger(L, conn->_connect->get_max_long_size());
	return 1;
}

// otl_connect:set_auto_commit(boolean auto_commit)
static int luaotl_connect_set_auto_commit(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	bool auto_commit = (bool)lua_toboolean(L, 2);
	
	try{
		if (auto_commit)
			conn->_connect->auto_commit_on();
		else
			conn->_connect->auto_commit_off();
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_connect:direct_exec(sql)
static int luaotl_connect_direct_exec(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	const char *sql = luaL_checkstring(L, 2);
	
	try{
		long rpc = conn->_connect->direct_exec(sql);
		lua_pushinteger(L, rpc);
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	return 1;
}

// otl_connect:syntax_check(sql)
static int luaotl_connect_syntax_check(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	const char *sql = luaL_checkstring(L, 2);
	
	try{
		conn->_connect->syntax_check(sql);
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_connect:cursor() return otl_stream
static int luaotl_connect_cursor(lua_State *L)
{
	luaotl_connect_t *conn = (luaotl_connect_t *)luaL_checkudata(L, 1, LUAOTL_CONNECT);
	check_otl_connect(L, conn);
	
	luaotl_stream_t *stmt = (luaotl_stream_t *)lua_newuserdata(L, sizeof(luaotl_stream_t));
	luaL_getmetatable(L, LUAOTL_STREAM);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, 1);
	stmt->connect = luaL_ref(L, LUA_REGISTRYINDEX);
	stmt->batch_size = 1;
	stmt->_stream = NULL;
	return 1;
}

static const char* get_var_typename(int type)
{
	static std::map<int, std::string> _var_typename;
	#define VAR_SET_TYPENAME(var_type) \
		_var_typename.insert(std::make_pair(otl_var_##var_type, std::string(#var_type)))
	if (_var_typename.empty())
	{
		VAR_SET_TYPENAME(bigint);
		VAR_SET_TYPENAME(blob);
		VAR_SET_TYPENAME(char);
		VAR_SET_TYPENAME(clob);
		VAR_SET_TYPENAME(double);
		VAR_SET_TYPENAME(float);
		VAR_SET_TYPENAME(int);
		VAR_SET_TYPENAME(long_int);
		VAR_SET_TYPENAME(ltz_timestamp);
		VAR_SET_TYPENAME(raw);
		VAR_SET_TYPENAME(raw_long);
		VAR_SET_TYPENAME(short);
		VAR_SET_TYPENAME(timestamp);
		VAR_SET_TYPENAME(tz_timestamp);
		VAR_SET_TYPENAME(ubigint);
		VAR_SET_TYPENAME(unsigned_int);
		VAR_SET_TYPENAME(varchar_long);
	}
	std::map<int,std::string>::const_iterator it = _var_typename.find(type);
	if (it == _var_typename.end()) return "unkown_type";
	return it->second.c_str();
}

static void lua_push_otl_column_desc(lua_State *L, const otl_column_desc& column)
{
	lua_newtable(L);
	lua_pushstring(L, column.name);
	lua_setfield(L, -2, "name");
	lua_pushstring(L, get_var_typename(column.otl_var_dbtype));
	lua_setfield(L, -2, "var_type");
	lua_pushinteger(L, column.dbsize);
	lua_setfield(L, -2, "dbsize");
	lua_pushinteger(L, column.scale);
	lua_setfield(L, -2, "scale");
	lua_pushinteger(L, column.prec);
	lua_setfield(L, -2, "prec");
	lua_pushinteger(L, column.nullok);
	lua_setfield(L, -2, "nullok");
}

static void lua_push_otl_var_desc(lua_State *L, const otl_var_desc& var_desc)
{
	lua_newtable(L);
	const char *param_typename[] = { "in", "out", "inout" };
	lua_pushstring(L, param_typename[var_desc.param_type]);
	lua_setfield(L, -2, "param_type");
	lua_pushstring(L, get_var_typename(var_desc.ftype));
	lua_setfield(L, -2, "ftype");
	lua_pushinteger(L, var_desc.elem_size);
	lua_setfield(L, -2, "elem_size");
	lua_pushinteger(L, var_desc.array_size);
	lua_setfield(L, -2, "array_size");
	lua_pushinteger(L, var_desc.pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, var_desc.name_pos);
	lua_setfield(L, -2, "name_pos");
	lua_pushstring(L, var_desc.name);
	lua_setfield(L, -2, "name");
}

static int lua_push_otl_datetime(lua_State *L, const otl_datetime& tm)
{
	lua_newtable(L);
	lua_pushinteger(L, tm.year);
	lua_setfield(L, -2, "year");
	lua_pushinteger(L, tm.month);
	lua_setfield(L, -2, "month");
	lua_pushinteger(L, tm.day);
	lua_setfield(L, -2, "day");
	lua_pushinteger(L, tm.hour);
	lua_setfield(L, -2, "hour");
	lua_pushinteger(L, tm.minute);
	lua_setfield(L, -2, "min");
	lua_pushinteger(L, tm.second);
	lua_setfield(L, -2, "sec");
}

static otl_datetime lua_check_otl_datetime(lua_State *L, int index)
{
	otl_datetime tm;
	#define CHECK_OTL_DATETIME_FIELD(Field, Name, DefaultValue) \
	do{ \
		lua_getfield(L, index, Name); \
		Field = luaL_optinteger(L, -1, DefaultValue); \
		lua_pop(L, 1); \
		if (Field < 0) \
			luaL_error(L, "timestamp missing '%s' or invalid type", Name); \
	}while(0)
	CHECK_OTL_DATETIME_FIELD(tm.year, "year", -1);
	CHECK_OTL_DATETIME_FIELD(tm.month, "month", -1);
	CHECK_OTL_DATETIME_FIELD(tm.day, "day", -1);
	CHECK_OTL_DATETIME_FIELD(tm.hour, "hour", 0);
	CHECK_OTL_DATETIME_FIELD(tm.minute, "min", 0);
	CHECK_OTL_DATETIME_FIELD(tm.second, "sec", 0);
	return tm;
}

static int luaotl_read_var(lua_State *L, int type, otl_stream* _stmt, otl_connect *_db, bool push_null=false)
{
	otl_stream &stmt = *_stmt;
	#define READ_OTL_VAR_TYPE(var_type, push_value) \
		do{ \
			var_type val; \
			stmt >> val; \
			if (stmt.is_null()) \
			{ \
				if (push_null) \
					lua_pushlightuserdata(L, NULL); \
				else \
					lua_pushnil(L); \
			}else \
				push_value(L, val); \
		}while(0)
	
	switch(type)
	{
	case otl_var_short:
		READ_OTL_VAR_TYPE(short, lua_pushinteger); break;
	case otl_var_int:
		READ_OTL_VAR_TYPE(int, lua_pushinteger); break;
	case otl_var_long_int:
		READ_OTL_VAR_TYPE(long, lua_pushinteger); break;
	case otl_var_unsigned_int:
		READ_OTL_VAR_TYPE(unsigned, lua_pushinteger); break;
	case otl_var_ubigint:
	case otl_var_bigint:
		READ_OTL_VAR_TYPE(OTL_BIGINT, lua_pushinteger); break;
	case otl_var_double:
		READ_OTL_VAR_TYPE(double, lua_pushnumber); break;
	case otl_var_float:
		READ_OTL_VAR_TYPE(float, lua_pushnumber); break;
	case otl_var_timestamp:
	case otl_var_ltz_timestamp:
	case otl_var_tz_timestamp:
		READ_OTL_VAR_TYPE(otl_datetime, lua_push_otl_datetime); break;
	case otl_var_char:{
		std::string val;
		stmt >> val;
		if (stmt.is_null())
		{
			if (push_null)
				lua_pushlightuserdata(L, NULL);
			else
				lua_pushnil(L);
		}else
			lua_pushlstring(L, val.c_str(), val.length());
		}break;
	case otl_var_blob:
	case otl_var_clob:
	case otl_var_raw:
	case otl_var_raw_long:
	case otl_var_varchar_long:{
		otl_long_string val(_db->get_max_long_size());
		stmt >> val;
		if (stmt.is_null())
		{
			if (push_null)
				lua_pushlightuserdata(L, NULL);
			else
				lua_pushnil(L);
		}else
			lua_pushlstring(L, (const char *)val.v, val.len());
		}break;
	default:
		luaL_error(L, "unsupport otl variable type(id=%d)", type);
	}
	return 1;
}

static void luaotl_write_var(lua_State *L, int type, otl_stream* _stmt, otl_connect *_db)
{
	otl_stream &stmt = *_stmt;
	if (lua_isnil(L, -1) || (lua_isuserdata(L, -1) && lua_touserdata(L, -1)==NULL))
	{
		stmt << otl_null();
		return;
	}
	
	#define WRITE_OTL_VAR_TYPE(var_type, check_value, pull_value) \
		do{ \
			if (!check_value(L, -1)) throw "type unmatched"; \
			var_type val = (var_type)pull_value(L, -1); \
			stmt << val; \
		}while(0)
	
	#if LUA_VERSION_NUM <= 501
		#define lua_isinteger lua_isnumber
	#endif
	
	switch(type)
	{
	case otl_var_short:
		WRITE_OTL_VAR_TYPE(short, lua_isinteger, lua_tointeger); break;
	case otl_var_int:
		WRITE_OTL_VAR_TYPE(int, lua_isinteger, lua_tointeger); break;
	case otl_var_long_int:
		WRITE_OTL_VAR_TYPE(long, lua_isinteger, lua_tointeger); break;
	case otl_var_unsigned_int:
		WRITE_OTL_VAR_TYPE(unsigned, lua_isinteger, lua_tointeger); break;
	case otl_var_ubigint:
	case otl_var_bigint:
		WRITE_OTL_VAR_TYPE(OTL_BIGINT, lua_isinteger, lua_tointeger); break;
	case otl_var_double:
		WRITE_OTL_VAR_TYPE(double, lua_isnumber, lua_tonumber); break;
	case otl_var_float:
		WRITE_OTL_VAR_TYPE(float, lua_isnumber, lua_tonumber); break;
	case otl_var_timestamp:
	case otl_var_ltz_timestamp:
	case otl_var_tz_timestamp:
		WRITE_OTL_VAR_TYPE(otl_datetime, lua_istable, lua_check_otl_datetime); break;
	case otl_var_char:
	case otl_var_blob:
	case otl_var_clob:
	case otl_var_raw:
	case otl_var_raw_long:
	case otl_var_varchar_long:{
			if (!lua_isstring(L, -1)) throw "type unmatched";
			size_t len;
			const char *s = lua_tolstring(L, -1, &len);
			if (type==otl_var_char)
				stmt << std::string(s, len);
			else
				stmt << otl_long_string((const void *)s, _db->get_max_long_size(), len);
		}break;
	default:
		luaL_error(L, "unsupport otl variable type(id=%d)", type);
	}
}

// otl_stream:__gc()
static int luaotl_stream___gc(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	close_otl_stream(stmt);
	luaL_unref(L, LUA_REGISTRYINDEX, stmt->connect);
	return 1;
}

// otl_stream:good()
static int luaotl_stream_good(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	lua_pushboolean(L, (conn->status>0 && conn->_connect->connected && stmt->_stream && stmt->_stream->good()));
	return 1;
}

// otl_stream:close()
static int luaotl_stream_close(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	close_otl_stream(stmt);
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:execute(sql)
static int luaotl_stream_execute(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_connect(L, conn);
	const char *sql = luaL_checkstring(L, 2);
	
	close_otl_stream(stmt);
	try{
		stmt->_stream = new otl_stream(stmt->batch_size, sql, *(conn->_connect));
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

#ifdef OTL_ORA8
// otl_stream:get_stream_type()
static int luaotl_stream_get_stream_type(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaL_argcheck(L, stmt->_stream, 1, "otl_stream closed");
	
	int type = stmt->_stream->get_stream_type();
	switch(type)
	{
	case otl_select_stream_type:
		lua_pushstring(L, "otl_select_stream_type"); break;
	case otl_inout_stream_type:
		lua_pushstring(L, "otl_inout_stream_type"); break;
	case otl_refcur_stream_type:
		lua_pushstring(L, "otl_refcur_stream_type"); break;
	case otl_no_stream_type:
	default:
		lua_pushstring(L, "otl_no_stream_type"); break;
	}
	return 1;
}
#endif

// otl_stream:describe_select()
static int luaotl_stream_describe_select(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaL_argcheck(L, stmt->_stream, 1, "otl_stream closed");
#ifdef OTL_ORA8
	luaL_argcheck(L, stmt->_stream->get_stream_type()==otl_select_stream_type, 1, "otl_stream is not SELECT statement");
#endif
	int desc_len = 0;
	otl_column_desc* column = stmt->_stream->describe_select(desc_len);
	
	lua_newtable(L);
	for(int n = 0; n < desc_len; n++)
	{
		lua_push_otl_column_desc(L, column[n]);
		lua_rawseti(L, 2, n+1);
	}
	return 1;
}

// otl_stream:describe_out_vars()
static int luaotl_stream_describe_out_vars(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaL_argcheck(L, stmt->_stream, 1, "otl_stream closed");
	int desc_len = 0;
	otl_var_desc* var_desc = stmt->_stream->describe_out_vars(desc_len);
	
	lua_newtable(L);
	for(int n = 0; n < desc_len; n++)
	{
		lua_push_otl_var_desc(L, var_desc[n]);
		lua_rawseti(L, 2, n+1);
	}
	return 1;
}

// otl_stream:describe_in_vars()
static int luaotl_stream_describe_in_vars(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaL_argcheck(L, stmt->_stream, 1, "otl_stream closed");
	int desc_len = 0;
	otl_var_desc* var_desc = stmt->_stream->describe_in_vars(desc_len);
	
	lua_newtable(L);
	for(int n = 0; n < desc_len; n++)
	{
		lua_push_otl_var_desc(L, var_desc[n]);
		lua_rawseti(L, 2, n+1);
	}
	return 1;
}

// otl_stream:set_batch_size(batch_size)
static int luaotl_stream_set_batch_size(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	int batch_size = luaL_checkinteger(L, 2);
	luaL_argcheck(L, batch_size>0, 2, "batch size > 0");
	stmt->batch_size = batch_size;
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:get_batch_size()
static int luaotl_stream_get_batch_size(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	lua_pushinteger(L, stmt->batch_size);
	return 1;
}

#ifdef OTL_ORA8
// otl_stream:set_batch_error_mode(boolean mode)
static int luaotl_stream_set_batch_error_mode(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	bool mode = (bool)lua_toboolean(L, 2);
	
	stmt->_stream->set_batch_error_mode(mode);
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:get_batch_errors()
static int luaotl_stream_get_batch_errors(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	
	int total_errors = 0;
	try{
		total_errors = stmt->_stream->get_number_of_errors_in_batch();
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}

	lua_newtable(L);
	for(int i=0; i<total_errors; i++)
	{
		int row_offset;
		otl_exception error;
		stmt->_stream->get_error(i, row_offset, error);
		lua_newtable(L);
		lua_pushinteger(L, row_offset); lua_setfield(L, -2, "row_offset");
		lua_pushinteger(L, error.code); lua_setfield(L, -2, "code");
	    lua_pushstring(L, (const char *)error.msg); lua_setfield(L, -2, "message");
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}
#endif

// otl_stream:fetchone()
static int luaotl_stream_fetchone(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
#ifdef OTL_ORA8
	luaL_argcheck(L, stmt->_stream->get_stream_type()==otl_select_stream_type, 1, "otl_stream is not SELECT statement");
#endif
	if (stmt->_stream->eof()) return 0;
	
	lua_newtable(L);
	try{
		int desc_len = 0;
		otl_column_desc* column = stmt->_stream->describe_select(desc_len);
		for(int n = 0; n < desc_len; n++)
		{
			luaotl_read_var(L, column[n].otl_var_dbtype, stmt->_stream, conn->_connect, true);
			lua_rawseti(L, -2, n+1);
		}
	}catch(otl_exception& e)
	{
		lua_pop(L, 1);
		return handle_exception_and_return(L, conn, e);
	}
	return 1;
}

// otl_stream:eof()
static int luaotl_stream_eof(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	
	try{
		lua_pushboolean(L, stmt->_stream->eof() ? 1 : 0);
	}catch(otl_exception& e)
	{
		int n = handle_exception_and_return(L, conn, e);
		lua_pushboolean(L, 1);
		lua_replace(L, -n - 1);
		return n;
	}
	return 1;
}

// otl_stream:rows()
static int luaotl_stream_rows(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
#ifdef OTL_ORA8
	luaL_argcheck(L, stmt->_stream->get_stream_type()==otl_select_stream_type, 1, "otl_stream is not SELECT statement");
#endif
	check_otl_stream(L, stmt, conn);
	
	lua_pushcfunction(L, luaotl_stream_fetchone);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	return 3;
}

// otl_stream:is_null(value)
static int luaotl_stream_is_null(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	lua_pushboolean(L, (lua_isnil(L, 2) || (lua_isuserdata(L, 2) && lua_touserdata(L, 2)==NULL)));
	return 1;
}

// otl_stream:bind_values({value, ...})
static int luaotl_stream_bind_values(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	luaL_checktype(L, 2, LUA_TTABLE);
	
	try{
		int desc_len = 0;
		otl_var_desc* var_desc = stmt->_stream->describe_in_vars(desc_len);
		for(int n = 0; n < desc_len; n++)
		{
			lua_rawgeti(L, 2, n+1);
			try{
				luaotl_write_var(L, var_desc[n].ftype, stmt->_stream, conn->_connect);
			}catch(const char *emsg)
			{
				lua_pop(L, 1);
				luaL_error(L, "%s - otl variable info: name='%s', pos=%d, type=(%d)%s",
				           emsg, var_desc[n].name, var_desc[n].pos, var_desc[n].ftype, get_var_typename(var_desc[n].ftype));
			}
			lua_pop(L, 1);
		}
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:bind_names({name=value, ...})
static int luaotl_stream_bind_names(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	luaL_checktype(L, 2, LUA_TTABLE);
	
	try{
		int desc_len = 0;
		otl_var_desc* var_desc = stmt->_stream->describe_in_vars(desc_len);
		for(int n = 0; n < desc_len; n++)
		{
			const char *varname = var_desc[n].name;
			lua_getfield(L, 2, varname+1);
			try{
				luaotl_write_var(L, var_desc[n].ftype, stmt->_stream, conn->_connect);
			}catch(const char *emsg)
			{
				lua_pop(L, 1);
				luaL_error(L, "%s - otl variable info: name='%s', pos=%d, type=(%d)%s",
				           emsg, var_desc[n].name, var_desc[n].pos, var_desc[n].ftype, get_var_typename(var_desc[n].ftype));
			}
			lua_pop(L, 1);
		}
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:outvars()
static int luaotl_stream_outvars(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
#ifdef OTL_ORA8
	luaL_argcheck(L, stmt->_stream->get_stream_type()==otl_inout_stream_type, 1, "otl_stream is not IN/OUT statement");
#endif
	if (stmt->_stream->eof()) return 0;
	
	lua_newtable(L);
	try{
		int desc_len = 0;
		otl_var_desc* var_desc = stmt->_stream->describe_out_vars(desc_len);
		for(int n = 0; n < desc_len; n++)
		{
			const char *varname = var_desc[n].name;
			luaotl_read_var(L, var_desc[n].ftype, stmt->_stream, conn->_connect);
			lua_setfield(L, -2, var_desc[n].name+1);
		}
	}catch(otl_exception& e)
	{
		lua_pop(L, 1);
		return handle_exception_and_return(L, conn, e);
	}
	return 1;
}

// otl_stream:flush()
static int luaotl_stream_flush(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);

	try{
		stmt->_stream->flush();
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:clean(clean_up_error_flag=0)
static int luaotl_stream_clean(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	int clean_up_error_flag = luaL_optinteger(L, 2, 0);

	try{
		stmt->_stream->clean(clean_up_error_flag);
	}catch(otl_exception& e)
	{
		return handle_exception_and_return(L, conn, e);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// otl_stream:get_rpc()
static int luaotl_stream_get_rpc(lua_State *L)
{
	luaotl_stream_t *stmt = (luaotl_stream_t *)luaL_checkudata(L, 1, LUAOTL_STREAM);
	luaotl_connect_t *conn = get_otl_connect(L, stmt);
	check_otl_stream(L, stmt, conn);
	lua_pushinteger(L, stmt->_stream->get_rpc());
	return 1;
}

static int create_metatable(lua_State *L, const char *name, const luaL_Reg *methods)
{
	if (!luaL_newmetatable(L, name))
		return -1;
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
#if LUA_VERSION_NUM > 501
	luaL_setfuncs(L, methods, 0);
#else
	luaL_register(L, NULL, methods);
#endif
	if (strcmp(name, LUAOTL_STREAM)==0)
	{
		lua_pushlightuserdata(L, NULL);
		lua_setfield(L, -2, "null");
#ifdef OTL_ORA8
		lua_pushinteger(L, 24381);
		lua_setfield(L, -2, "code_batch_errors");
#endif
	}
	lua_pop(L, 1);
	return 0;
}

#define REG_METHOD(class_name, func_name) {#func_name, class_name##_##func_name}

static struct luaL_Reg luaotl_connect_libs[] = {
	REG_METHOD(luaotl_connect, __gc),
	REG_METHOD(luaotl_connect, good),
	REG_METHOD(luaotl_connect, close),
	REG_METHOD(luaotl_connect, connect),
	REG_METHOD(luaotl_connect, commit),
	REG_METHOD(luaotl_connect, rollback),
	REG_METHOD(luaotl_connect, set_max_long_size),
	REG_METHOD(luaotl_connect, get_max_long_size),
	REG_METHOD(luaotl_connect, set_auto_commit),
	REG_METHOD(luaotl_connect, direct_exec),
	REG_METHOD(luaotl_connect, syntax_check),
	REG_METHOD(luaotl_connect, cursor),
	{NULL, NULL}
};

static struct luaL_Reg luaotl_stream_libs[] = {
	REG_METHOD(luaotl_stream, __gc),
	REG_METHOD(luaotl_stream, good),
	REG_METHOD(luaotl_stream, close),
	REG_METHOD(luaotl_stream, execute),
#ifdef OTL_ORA8
	REG_METHOD(luaotl_stream, get_stream_type),
#endif
	REG_METHOD(luaotl_stream, describe_out_vars),
	REG_METHOD(luaotl_stream, describe_in_vars),
	REG_METHOD(luaotl_stream, describe_select),
	REG_METHOD(luaotl_stream, set_batch_size),
	REG_METHOD(luaotl_stream, get_batch_size),
#ifdef OTL_ORA8
	REG_METHOD(luaotl_stream, set_batch_error_mode),
	REG_METHOD(luaotl_stream, get_batch_errors),
#endif
	REG_METHOD(luaotl_stream, fetchone),
	REG_METHOD(luaotl_stream, eof),
	REG_METHOD(luaotl_stream, rows),
	REG_METHOD(luaotl_stream, is_null),
	REG_METHOD(luaotl_stream, bind_values),
	REG_METHOD(luaotl_stream, bind_names),
	REG_METHOD(luaotl_stream, outvars),
	REG_METHOD(luaotl_stream, flush),
	REG_METHOD(luaotl_stream, clean),
	REG_METHOD(luaotl_stream, get_rpc),
	{NULL, NULL}
};

#define DECLARE_LUAOTL_EXPORT_FUNCTION(db_name) \
	extern "C" int luaopen_otldb_##db_name(lua_State *L) \
	{ \
		otl_connect::otl_initialize(); \
		create_metatable(L, LUAOTL_CONNECT, luaotl_connect_libs); \
		create_metatable(L, LUAOTL_STREAM, luaotl_stream_libs); \
		lua_pushcfunction(L, luaotl_new_connect); \
		return 1; \
	}
