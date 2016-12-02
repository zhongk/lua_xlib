/**************************************************************************
local zlog = require('zlog')

zlog.init([string confpath="", string category=""])
zlog.reload([string confpath="", string category=""])
zlog.put_mdc(string key, string value)
zlog.get_mdc(string key) return string value or nil
zlog.del_mdc(string key)
zlog.clean_mdc()
zlog.debug(string log_message)
zlog.info(string log_message)
zlog.notice(string log_message)
zlog.warn(string log_message)
zlog.error(string log_message)
zlog.fatal(string log_message)
zlog.set_record(string record_name, function(string message, string path))
**************************************************************************/
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <string.h>
#include <assert.h>
#include <zlog.h>

static const char *lzlog_err_load = "Detailed error log will be recorded to the log file indicated by $ZLOG_PROFILE_ERROR.";

static int report_error(lua_State *L, const char *errmsg)
{
	lua_pushboolean(L, 0);
	lua_pushstring(L, errmsg);
	return 2;
}

int lzlog_reload(lua_State *L)
{
	const char *confpath = luaL_optstring(L, 1, NULL);
	const char *category = luaL_optstring(L, 2, NULL);
	if (zlog_reload(confpath) == -1)
		return report_error(L, lzlog_err_load);
	if (category && *category)
	{
		if (dzlog_set_category(category) == -1)
			return report_error(L, lzlog_err_load);
	}
	lua_pushboolean(L, 1);
	return 1;
}

static int lzlog_putmdc(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	const char *value = luaL_checkstring(L, 2);
	lua_pushboolean(L, zlog_put_mdc(key, value)==0 ? 1 : 0);
	return 1;
}

static int lzlog_getmdc(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	char *value = zlog_get_mdc(key);
	if (value)
		lua_pushstring(L, value);
	else
		lua_pushnil(L);
	return 1;
}

static int lzlog_delmdc(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	zlog_remove_mdc(key);
	lua_pushboolean(L, 1);
	return 1;
}

static int lzlog_clrmdc(lua_State *L)
{
	zlog_clean_mdc();
	lua_pushboolean(L, 1);
	return 1;
}

static int lua_dzlog(lua_State *L, int level)
{
	const char *logmsg = luaL_checkstring(L, 1);

	lua_Debug stack;
	lua_getstack(L, 1, &stack);
	lua_getinfo(L, "nSl", &stack);
	const char *file = stack.short_src;
	const char *func = stack.name ? stack.name : stack.what;
	int line = stack.currentline;
	dzlog(file, strlen(file), func, strlen(func), line, level, "%s", logmsg);
	lua_pushboolean(L, 1);
	return 1;
}

static int lzlog_fatal(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_FATAL);
}

static int lzlog_error(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_ERROR);
}

static int lzlog_warn(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_WARN);
}

static int lzlog_notice(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_NOTICE);
}

static int lzlog_info(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_INFO);
}

static int lzlog_debug(lua_State *L)
{
	return lua_dzlog(L, ZLOG_LEVEL_DEBUG);
}

/*************************************************************************/

#define LZLOG_RECORD_FUNC_NUM 20
static zlog_record_fn lzlog_record_functions[LZLOG_RECORD_FUNC_NUM];
static struct LZLOG_RECORD_STATE
{
	int in_used;
	lua_State *L;
	int ref_fn;
	char rname[60];
} LZLOG_RECORD[LZLOG_RECORD_FUNC_NUM];

#define ZLOG_RECORD_FUNC(num) lzlog_record_##num
#define DECL_ZLOG_RECORD_FUNC(num) \
	static int ZLOG_RECORD_FUNC(num)(zlog_msg_t *msg) \
	{ \
		struct LZLOG_RECORD_STATE *record = &LZLOG_RECORD[num]; \
		assert( lzlog_record_functions[num] && record->in_used && record->L && record->ref_fn!=-1 ); \
		lua_rawgeti(record->L, LUA_REGISTRYINDEX, record->ref_fn); \
		lua_pushlstring(record->L, msg->buf, msg->len); \
		lua_pushstring(record->L, msg->path); \
		lua_call(record->L, 2, 1); \
		int rc = luaL_optinteger(record->L, -1, -1); \
		lua_pop(record->L, 1); \
		return rc; \
	}

DECL_ZLOG_RECORD_FUNC(0)
DECL_ZLOG_RECORD_FUNC(1)
DECL_ZLOG_RECORD_FUNC(2)
DECL_ZLOG_RECORD_FUNC(3)
DECL_ZLOG_RECORD_FUNC(4)
DECL_ZLOG_RECORD_FUNC(5)
DECL_ZLOG_RECORD_FUNC(6)
DECL_ZLOG_RECORD_FUNC(7)
DECL_ZLOG_RECORD_FUNC(8)
DECL_ZLOG_RECORD_FUNC(9)
DECL_ZLOG_RECORD_FUNC(10)
DECL_ZLOG_RECORD_FUNC(11)
DECL_ZLOG_RECORD_FUNC(12)
DECL_ZLOG_RECORD_FUNC(13)
DECL_ZLOG_RECORD_FUNC(14)
DECL_ZLOG_RECORD_FUNC(15)
DECL_ZLOG_RECORD_FUNC(16)
DECL_ZLOG_RECORD_FUNC(17)
DECL_ZLOG_RECORD_FUNC(18)
DECL_ZLOG_RECORD_FUNC(19)

static int lzlog_record_init()
{
	memset(LZLOG_RECORD, 0, sizeof(LZLOG_RECORD));
	memset(lzlog_record_functions, 0, sizeof(lzlog_record_functions));
	lzlog_record_functions[0] = ZLOG_RECORD_FUNC(0);
	lzlog_record_functions[1] = ZLOG_RECORD_FUNC(1);
	lzlog_record_functions[2] = ZLOG_RECORD_FUNC(2);
	lzlog_record_functions[3] = ZLOG_RECORD_FUNC(3);
	lzlog_record_functions[4] = ZLOG_RECORD_FUNC(4);
	lzlog_record_functions[5] = ZLOG_RECORD_FUNC(5);
	lzlog_record_functions[6] = ZLOG_RECORD_FUNC(6);
	lzlog_record_functions[7] = ZLOG_RECORD_FUNC(7);
	lzlog_record_functions[8] = ZLOG_RECORD_FUNC(8);
	lzlog_record_functions[9] = ZLOG_RECORD_FUNC(9);
	lzlog_record_functions[10] = ZLOG_RECORD_FUNC(10);
	lzlog_record_functions[11] = ZLOG_RECORD_FUNC(11);
	lzlog_record_functions[12] = ZLOG_RECORD_FUNC(12);
	lzlog_record_functions[13] = ZLOG_RECORD_FUNC(13);
	lzlog_record_functions[14] = ZLOG_RECORD_FUNC(14);
	lzlog_record_functions[15] = ZLOG_RECORD_FUNC(15);
	lzlog_record_functions[16] = ZLOG_RECORD_FUNC(16);
	lzlog_record_functions[17] = ZLOG_RECORD_FUNC(17);
	lzlog_record_functions[18] = ZLOG_RECORD_FUNC(18);
	lzlog_record_functions[19] = ZLOG_RECORD_FUNC(19);
}

static int lzlog_set_record(lua_State *L)
{
	const char *rname = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	int i = 0, index = -1;
	for(i=0; i<LZLOG_RECORD_FUNC_NUM; i++)
	{
		if (!LZLOG_RECORD[i].in_used)
			index = i;
		else
		if (L==LZLOG_RECORD[i].L && strcmp(rname, LZLOG_RECORD[i].rname)==0)
			return report_error(L, "Duplicate set the record name");
	}
	if (index == -1) return report_error(L, "Count of record function reach the limit");
	if (zlog_set_record(rname, lzlog_record_functions[index]))
		return report_error(L, "C function zlog_set_record error");
	lua_pushvalue(L, 2);
	int ref_fn = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);
	struct LZLOG_RECORD_STATE *record = &LZLOG_RECORD[index];
	record->in_used = 1;
	record->L = L;
	record->ref_fn = ref_fn;
	strcpy(record->rname, rname);
	lua_pushboolean(L, 1);
	return 1;
}

static int lzlog_reset_record(lua_State *L)
{
	const char *rname = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	int i;
	for(i=0; i<LZLOG_RECORD_FUNC_NUM; i++)
	{
		struct LZLOG_RECORD_STATE *record = &LZLOG_RECORD[i];
		if (record->in_used && record->L==L && strcmp(record->rname, rname)==0)
		{
			luaL_unref(L, LUA_REGISTRYINDEX, record->ref_fn);
			lua_pushvalue(L, 2);
			record->ref_fn = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pop(L, 1);
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	return report_error(L, "No found the record name");
}

/*************************************************************************/

static struct luaL_Reg zloglib[] = {
	{"init", lzlog_reload},
	{"reload", lzlog_reload},
	{"put_mdc", lzlog_putmdc},
	{"get_mdc", lzlog_getmdc},
	{"del_mdc", lzlog_delmdc},
	{"clean_mdc", lzlog_clrmdc},
	{"fatal", lzlog_fatal},
	{"error", lzlog_error},
	{"warn", lzlog_warn},
	{"notice", lzlog_notice},
	{"info", lzlog_info},
	{"debug", lzlog_debug},
	{"set_record", lzlog_set_record},
	{NULL, NULL}
};

#ifdef __cplusplus
extern "C"
#endif
int luaopen_zlog(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, zloglib);
#else
	luaL_register(L, "zlog", zloglib);
#endif
	lzlog_record_init();
	dzlog_init(NULL, "");
	return 1;
}