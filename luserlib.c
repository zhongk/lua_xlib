/**************************************************************************
local user = require('posix.user')

user.getuid()
user.geteuid()
user.setuid(uid)
user.seteuid(euid)
user.setreuid(ruid, euid)

user.getgroups()
user.getgid()
user.getegid()
user.setgid(gid)
user.setegid(egid)
user.setregid(rgid, egid)

user.getpwnam(username)
user.getpwuid(uid)
user.getpwall() return iterator(passwd)

user.getgrnam(groupname)
user.getgrgid(gid)
user.getgrall() return iterator(group)

user.getspnam(username)
user.getspall() return iterator(spwd)
**************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#ifndef __CYGWIN__
#include <shadow.h>
#endif
#include "lua_util.h"

static void push_passwd(lua_State *L, struct passwd *pwd)
{
	lua_newtable(L);
	lua_pushstring(L, pwd->pw_name);   lua_setfield(L, -2, "pw_name");
	lua_pushstring(L, pwd->pw_passwd); lua_setfield(L, -2, "pw_passwd");
	lua_pushinteger(L, pwd->pw_uid);   lua_setfield(L, -2, "pw_uid");
	lua_pushinteger(L, pwd->pw_gid);   lua_setfield(L, -2, "pw_gid");
	lua_pushstring(L, pwd->pw_gecos);  lua_setfield(L, -2, "pw_gecos");
	lua_pushstring(L, pwd->pw_dir);    lua_setfield(L, -2, "pw_dir");
	lua_pushstring(L, pwd->pw_shell);  lua_setfield(L, -2, "pw_shell");
}

static void push_group(lua_State *L, struct group *grp)
{
	lua_newtable(L);
	lua_pushstring(L, grp->gr_name);   lua_setfield(L, -2, "gr_name");
	lua_pushstring(L, grp->gr_passwd); lua_setfield(L, -2, "gr_passwd");
	lua_pushinteger(L, grp->gr_gid);   lua_setfield(L, -2, "gr_gid");
	int i = 0;
	lua_newtable(L);
	while(grp->gr_mem[i])
	{
		lua_pushstring(L, grp->gr_mem[i]); lua_rawseti(L, -2, ++i);
	}
	lua_setfield(L, -2, "gr_mem");
}

#ifndef __CYGWIN__
static void push_spwd(lua_State *L, struct spwd *sp)
{
	lua_newtable(L);
	lua_pushstring(L, sp->sp_namp);    lua_setfield(L, -2, "sp_namp");
	lua_pushstring(L, sp->sp_pwdp);    lua_setfield(L, -2, "sp_pwdp");
	lua_pushinteger(L, sp->sp_lstchg); lua_setfield(L, -2, "sp_lstchg");
	lua_pushinteger(L, sp->sp_min);    lua_setfield(L, -2, "sp_min");
	lua_pushinteger(L, sp->sp_max);    lua_setfield(L, -2, "sp_max");
	lua_pushinteger(L, sp->sp_warn);   lua_setfield(L, -2, "sp_warn");
	lua_pushinteger(L, sp->sp_inact);  lua_setfield(L, -2, "sp_inact");
	lua_pushinteger(L, sp->sp_expire); lua_setfield(L, -2, "sp_expire");
	lua_pushinteger(L, sp->sp_flag);   lua_setfield(L, -2, "sp_flag");
}
#endif

#define FUNCNAME(Func) luser_##Func
#define DEF_METHOD(Method, L) static int FUNCNAME(Method)(lua_State *L)

// user.getpwnam(username)
DEF_METHOD(getpwnam, L)
{
	const char *name = luaL_checkstring(L, 1);
	struct passwd *pwd = getpwnam(name);
	if (!pwd)
		return report_errno(L, LRET_NIL);
	push_passwd(L, pwd);
	return 1;
}

// user.getpwuid(uid)
DEF_METHOD(getpwuid, L)
{
	uid_t uid = luaL_checkinteger(L, 1);
	struct passwd *pwd = getpwuid(uid);
	if (!pwd)
		return report_errno(L, LRET_NIL);
	push_passwd(L, pwd);
	return 1;
}

// user.getpwall() return iterator(passwd)
static int next_passwd(lua_State* L);
DEF_METHOD(getpwall, L)
{
	setpwent();	
	lua_pushcclosure(L, next_passwd, 0);
	return 1;
}
int next_passwd(lua_State* L)
{
	struct passwd *pwd = getpwent();
	if (pwd)
	{
		push_passwd(L, pwd);
		return 1;
	}
	endpwent();
	return 0;
}

// user.getgrnam(groupname)
DEF_METHOD(getgrnam, L)
{
	const char *name = luaL_checkstring(L, 1);
	struct group *grp = getgrnam(name);
	if (!grp)
		return report_errno(L, LRET_NIL);
	push_group(L, grp);
	return 1;
}

// user.getgrgid(gid)
DEF_METHOD(getgrgid, L)
{
	uid_t gid = luaL_checkinteger(L, 1);
	struct group *grp = getgrgid(gid);
	if (!grp)
		return report_errno(L, LRET_NIL);
	push_group(L, grp);
	return 1;
}

// user.getgrall() return iterator(group)
static int next_group(lua_State* L);
DEF_METHOD(getgrall, L)
{
	setgrent();	
	lua_pushcclosure(L, next_group, 0);
	return 1;
}
int next_group(lua_State* L)
{
	struct group *grp = getgrent();
	if (grp)
	{
		push_group(L, grp);
		return 1;
	}
	endgrent();
	return 0;
}

#ifndef __CYGWIN__
// user.getspnam(username)
DEF_METHOD(getspnam, L)
{
	const char *name = luaL_checkstring(L, 1);
	struct spwd *sp = getspnam(name);
	if (!sp)
		return report_errno(L, LRET_NIL);
	push_spwd(L, sp);
	return 1;
}

// user.getspall()
static int next_spwd(lua_State* L);
DEF_METHOD(getspall, L)
{
	setspent();	
	lua_pushcclosure(L, next_spwd, 0);
	return 1;
}
int next_spwd(lua_State* L)
{
	struct spwd *sp = getspent();
	if (sp)
	{
		push_spwd(L, sp);
		return 1;
	}
	endspent();
	return 0;
}
#endif

// user.getuid()
DEF_METHOD(getuid, L)
{
	lua_pushinteger(L, getuid());
	return 1;
}

// user.geteuid()
DEF_METHOD(geteuid, L)
{
	lua_pushinteger(L, geteuid());
	return 1;
}

// user.setuid(uid)
DEF_METHOD(setuid, L)
{
	uid_t uid = (uid_t)luaL_checkinteger(L, 1);
	if (setuid(uid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// user.seteuid(uid)
DEF_METHOD(seteuid, L)
{
	uid_t euid = (uid_t)luaL_checkinteger(L, 1);
	if (seteuid(euid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// user.setreuid(ruid, euid)
DEF_METHOD(setreuid, L)
{
	uid_t ruid = (uid_t)luaL_checkinteger(L, 1);
	uid_t euid = (uid_t)luaL_checkinteger(L, 2);
	if (setreuid(ruid, euid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// user.getgroups()
DEF_METHOD(getgroups, L)
{
	int i = 0, count = getgroups(0, NULL);
	if (count == -1)
		return report_errno(L, LRET_NIL);
	gid_t *list = (gid_t *)calloc(count, sizeof(gid_t));
	count = getgroups(count, list);
	if (count == -1)
	{
		int nresult = report_errno(L, LRET_NIL);
		free(list);
		return nresult;
	}
	lua_newtable(L);
	for(i=0; i<count; i++)
	{
		lua_pushinteger(L, list[i]);
		lua_rawseti(L, -2, i+1);
	}
	free(list);
	return 1;
}

// user.getgid()
DEF_METHOD(getgid, L)
{
	lua_pushinteger(L, getgid());
	return 1;
}

// user.getegid()
DEF_METHOD(getegid, L)
{
	lua_pushinteger(L, getegid());
	return 1;
}

// user.setgid(gid)
DEF_METHOD(setgid, L)
{
	gid_t gid = (uid_t)luaL_checkinteger(L, 1);
	if (setgid(gid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// user.setegid(gid)
DEF_METHOD(setegid, L)
{
	gid_t egid = (uid_t)luaL_checkinteger(L, 1);
	if (setgid(egid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// user.setregid(rgid, egid)
DEF_METHOD(setregid, L)
{
	gid_t rgid = (uid_t)luaL_checkinteger(L, 1);
	gid_t egid = (uid_t)luaL_checkinteger(L, 2);
	if (setregid(rgid, egid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

#define REG_METHOD(Method) {#Method, FUNCNAME(Method)}
static struct luaL_Reg userlib[] = {
	REG_METHOD(getuid),
	REG_METHOD(geteuid),
	REG_METHOD(setuid),
	REG_METHOD(seteuid),
	REG_METHOD(setreuid),
	REG_METHOD(getgroups),
	REG_METHOD(getgid),
	REG_METHOD(getegid),
	REG_METHOD(setgid),
	REG_METHOD(setegid),
	REG_METHOD(setregid),
	REG_METHOD(getpwnam),
	REG_METHOD(getpwuid),
	REG_METHOD(getpwall),
	REG_METHOD(getgrnam),
	REG_METHOD(getgrgid),
	REG_METHOD(getgrall),
#ifndef __CYGWIN__
	REG_METHOD(getspnam),
	REG_METHOD(getspall),
#endif
	{NULL, NULL}
};

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_user(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, userlib);
#else
	luaL_register(L, "posix_user", userlib);
#endif
	return 1;
}