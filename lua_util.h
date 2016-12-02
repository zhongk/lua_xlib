#ifndef __LUA_UTIL_H__
#define __LUA_UTIL_H__

#include <errno.h>
#ifndef __cplusplus
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#else
#include <lua.hpp>
#endif

#define LRET_NIL       -1
#define LRET_BOOLEAN   0
#define LRET_NEGATIVE  1
#define LRET_EMPTYSTR  2

#if LUA_VERSION_NUM <= 501
	#define lua_rawlen lua_objlen
#endif
#define report_errno(L, flag)        report_errmsg(L, flag, errno, NULL)
#define report_error(L, flag, ecode) report_errmsg(L, flag, ecode, NULL)
#define SET_CONSTANT(L, t, name)    (lua_pushinteger(L, (long)name), lua_setfield(L, t, #name))

static int report_errmsg(lua_State *L, int flag, int errcode, const char* msg)
{
	switch(flag)
	{
	case LRET_BOOLEAN  : lua_pushboolean(L, 0); break;
	case LRET_NEGATIVE : lua_pushinteger(L, -1); break;
	case LRET_EMPTYSTR : lua_pushstring(L, ""); break;
	case LRET_NIL      :
	default            : lua_pushnil(L); break;
	}
	if (msg)
	{
		if (errcode)
			lua_pushfstring(L, "%s : %s", msg, strerror(errcode));
		else
			lua_pushstring(L, msg);
	}else
		lua_pushstring(L, strerror(errcode));
	lua_pushinteger(L, errcode);
	return 3;
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
	lua_pop(L, 1);
	return 0;
}

#endif
