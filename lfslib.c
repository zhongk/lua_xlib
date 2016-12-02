/**************************************************************************
local fs = require('posix.fs')

fs.umask(mask)
fs.access(path[, mode=F_OK])
fs.stat(path[, tolink=false])
fs.chroot(path)
fs.chdir(dir)
fs.getcwd()
fs.listdir([dir='.'[, pattern='*']]) return iterator(filename, inode)
fs.fnmatch(filename, pattern[, flags=FNM_PATHNAME|FNM_PERIOD])
fs.mkfifo(path[, mode=0640])
fs.mkdir(dir[, mode=0755])
fs.rmdir(dir)
fs.chmod(path, mode)
fs.chown(path[, uid=-1, gid=-1])
fs.rename(oldpath, newpath)
fs.remove(path)
fs.truncate(path, length)
fs.link(oldpath, newpath)
fs.symlink(oldpath, newpath)
fs.unlink(path)
fs.readlink(linkpath)
fs.realpath(path)

fs.mode.S_*
fs.mode.S_ISREG(mode)
fs.mode.S_ISDIR(mode)
fs.mode.S_ISCHR(mode)
fs.mode.S_ISBLK(mode)
fs.mode.S_ISFIFO(mode)
fs.mode.S_ISLNK(mode)
fs.mode.S_ISSOCK(mode)
**************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include "lua_util.h"

#define FUNCNAME(Func) lfs_##Func
#define DEF_METHOD(Method, L) static int FUNCNAME(Method)(lua_State *L)

// fs.umask(mask)
DEF_METHOD(umask, L)
{
	int mask = luaL_checkinteger(L, 1);
	lua_pushinteger(L, umask(mask));
	return 1;
}

// fs.access(path[, mode])
DEF_METHOD(access, L)
{
	const char *path = luaL_checkstring(L, 1);
	int mode = luaL_optinteger(L, 2, F_OK);
	if (access(path, mode) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.stat(path[, tolink])
DEF_METHOD(stat, L)
{
	const char *path = luaL_checkstring(L, 1);
	int tolink = lua_toboolean(L, 2);
	struct stat sb;
	memset(&sb, 0, sizeof(sb));
	if ((tolink ? stat : lstat)(path, &sb) == -1)
		return report_errno(L, LRET_NIL);
	lua_newtable(L);
	lua_pushinteger(L, sb.st_dev);     lua_setfield(L, -2, "st_dev");
	lua_pushinteger(L, sb.st_ino);     lua_setfield(L, -2, "st_ino");
	lua_pushinteger(L, sb.st_mode);    lua_setfield(L, -2, "st_mode");
	lua_pushinteger(L, sb.st_nlink);   lua_setfield(L, -2, "st_nlink");
	lua_pushinteger(L, sb.st_uid);     lua_setfield(L, -2, "st_uid");
	lua_pushinteger(L, sb.st_gid);     lua_setfield(L, -2, "st_gid");
	lua_pushinteger(L, sb.st_rdev);    lua_setfield(L, -2, "st_rdev");
	lua_pushinteger(L, sb.st_size);    lua_setfield(L, -2, "st_size");
	lua_pushinteger(L, sb.st_blksize); lua_setfield(L, -2, "st_blksize");
	lua_pushinteger(L, sb.st_blocks);  lua_setfield(L, -2, "st_blocks");
	lua_pushinteger(L, sb.st_atime);   lua_setfield(L, -2, "st_atime");
	lua_pushinteger(L, sb.st_mtime);   lua_setfield(L, -2, "st_mtime");
	lua_pushinteger(L, sb.st_ctime);   lua_setfield(L, -2, "st_ctime");
	return 1;
}

// fs.chroot(path)
DEF_METHOD(chroot, L)
{
	const char *path = luaL_checkstring(L, 1);
	if (chroot(path) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.chdir(path)
DEF_METHOD(chdir, L)
{
	const char *path = luaL_checkstring(L, 1);
	if (chdir(path) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.getcwd()
DEF_METHOD(getcwd, L)
{
	char pwd[PATH_MAX];
	if (!getcwd(pwd, sizeof(pwd)))
		return report_errno(L, LRET_NIL);
	lua_pushstring(L, pwd);
	return 1;
}

// fs.listdir([path[, pattern]])
static int lua_lsnext(lua_State* L);
DEF_METHOD(listdir, L)
{
	DIR *dir;
	const char* path = luaL_optstring(L, 1, ".");
	const char* pattern = luaL_optstring(L, 2, "*");
	if ((dir = opendir(path)) == NULL)
	{
		return report_errno(L, LRET_NIL);
	}
	lua_pushlightuserdata(L, (void *)dir);
	lua_pushstring(L, pattern);
	lua_pushcclosure(L, lua_lsnext, 2);
	return 1;
}
int lua_lsnext(lua_State* L)
{
	DIR *dir = (DIR *)lua_touserdata(L, lua_upvalueindex(1));
	const char *pattern = lua_tostring(L, lua_upvalueindex(2));
	struct dirent *dp = readdir(dir);
	while(dp != NULL)
	{
		if (!(pattern[0]=='*' && pattern[1]=='\0'))
		{
			if (fnmatch(pattern, dp->d_name, FNM_PATHNAME|FNM_PERIOD) != 0)
			{
				dp = readdir(dir);
				continue;
			}
		}
		lua_pushstring(L, dp->d_name);
		lua_pushinteger(L, dp->d_ino);
		return 2;
	}
	closedir(dir);
	return 0;
}

// fs.fnmatch(filename, pattern[, flags])
DEF_METHOD(fnmatch, L)
{
	const char *filename = luaL_checkstring(L, 1);
	const char *pattern = luaL_checkstring(L, 2);
	int flags = luaL_optinteger(L, 3, FNM_PATHNAME|FNM_PERIOD);
	int retcode = fnmatch(pattern, filename, flags);
	if (retcode != 0)
	{
		lua_pushboolean(L, 0);
		lua_pushinteger(L, retcode);
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}

// fs.mkfifo(path[, mode])
DEF_METHOD(mkfifo, L)
{
	const char *path = luaL_checkstring(L, 1);
	int mode = luaL_optinteger(L, 2, 0640);
	if (mkfifo(path, mode) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.mkdir(path[, mode])
DEF_METHOD(mkdir, L)
{
	const char *path = luaL_checkstring(L, 1);
	int mode = luaL_optinteger(L, 2, 0755);
	if (mkdir(path, mode) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.rmdir(path)
DEF_METHOD(rmdir, L)
{
	const char *path = luaL_checkstring(L, 1);
	if (rmdir(path) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.chmod(path, mode)
DEF_METHOD(chmod, L)
{
	const char *path = luaL_checkstring(L, 1);
	int mode = luaL_checkinteger(L, 2);
	if (chmod(path, mode) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.chown(path[, uid, gid])
DEF_METHOD(chown, L)
{
	const char *path = luaL_checkstring(L, 1);
	uid_t uid = (uid_t)luaL_optinteger(L, 2, -1);
	gid_t gid = (gid_t)luaL_optinteger(L, 3, -1);
	if (chown(path, uid, gid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.rename(oldpath, newpath)
DEF_METHOD(rename, L)
{
	const char *oldpath = luaL_checkstring(L, 1);
	const char *newpath = luaL_checkstring(L, 2);
	if (rename(oldpath, newpath) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.remove(path)
DEF_METHOD(remove, L)
{
	const char *path = luaL_checkstring(L, 1);
	if (remove(path) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.truncate(path, length)
DEF_METHOD(truncate, L)
{
	const char *path = luaL_checkstring(L, 1);
	off_t length = (off_t)luaL_checkinteger(L, 2);
	if (truncate(path, length) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.link(oldpath, newpath)
DEF_METHOD(link, L)
{
	const char *oldpath = luaL_checkstring(L, 1);
	const char *newpath = luaL_checkstring(L, 2);
	if (link(oldpath, newpath) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.symlink(oldpath, newpath)
DEF_METHOD(symlink, L)
{
	const char *oldpath = luaL_checkstring(L, 1);
	const char *newpath = luaL_checkstring(L, 2);
	if (symlink(oldpath, newpath) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.unlink(path)
DEF_METHOD(unlink, L)
{
	const char *path = luaL_checkstring(L, 2);
	if (unlink(path) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fs.readlink(linkpath)
DEF_METHOD(readlink, L)
{
	char realpath[PATH_MAX];
	const char *linkpath = luaL_checkstring(L, 1);
	if (readlink(linkpath, realpath, sizeof(realpath)) == -1)
		return report_errno(L, LRET_NIL);
	lua_pushstring(L, realpath);
	return 1;
}

// fs.realpath(path)
DEF_METHOD(realpath, L)
{
	char real_path[PATH_MAX];
	const char *path = luaL_checkstring(L, 1);
	if (!realpath(path, real_path))
		return report_errno(L, LRET_NIL);
	lua_pushstring(L, real_path);
	return 1;
}

#define REG_METHOD(Method) {#Method, FUNCNAME(Method)}
static struct luaL_Reg fslib[] = {
	REG_METHOD(umask),
	REG_METHOD(access),
	REG_METHOD(stat),
	REG_METHOD(chroot),
	REG_METHOD(chdir),
	REG_METHOD(getcwd),
	REG_METHOD(listdir),
	REG_METHOD(fnmatch),
	REG_METHOD(mkfifo),
	REG_METHOD(mkdir),
	REG_METHOD(rmdir),
	REG_METHOD(chmod),
	REG_METHOD(chown),
	REG_METHOD(rename),
	REG_METHOD(remove),
	REG_METHOD(truncate),
	REG_METHOD(link),
	REG_METHOD(symlink),
	REG_METHOD(unlink),
	REG_METHOD(readlink),
	REG_METHOD(realpath),
	{NULL, NULL}
};

static void lfs_AddMacro(lua_State *L, int t);
static void push_filemode(lua_State *L);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_fs(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, fslib);
#else
	luaL_register(L, "posix_fs", fslib);
#endif
	lfs_AddMacro(L, lua_gettop(L));
	push_filemode(L);
	lua_setfield(L, -2, "mode");
	return 1;
}

void lfs_AddMacro(lua_State *L, int t)
{
	// for access
	SET_CONSTANT(L, t, R_OK);
	SET_CONSTANT(L, t, W_OK);
	SET_CONSTANT(L, t, X_OK);
	SET_CONSTANT(L, t, F_OK);
	// for fnmatch
	SET_CONSTANT(L, t, FNM_NOESCAPE);
	SET_CONSTANT(L, t, FNM_PATHNAME);
	SET_CONSTANT(L, t, FNM_PERIOD);
#ifdef FNM_FILE_NAME
	SET_CONSTANT(L, t, FNM_FILE_NAME);
#endif
#ifdef FNM_LEADING_DIR
	SET_CONSTANT(L, t, FNM_LEADING_DIR);
#endif
#ifdef FNM_CASEFOLD
	SET_CONSTANT(L, t, FNM_CASEFOLD);
#endif
}

#define DECL_MODEFUNC(L, Method) \
	static int lfm_##Method(lua_State *L) \
	{ \
		mode_t mode = (mode_t)luaL_checkinteger(L, 1); \
		lua_pushboolean(L, Method(mode)); \
		return 1; \
	}
DECL_MODEFUNC(L, S_ISREG)
DECL_MODEFUNC(L, S_ISDIR)
DECL_MODEFUNC(L, S_ISCHR)
DECL_MODEFUNC(L, S_ISBLK)
DECL_MODEFUNC(L, S_ISFIFO)
DECL_MODEFUNC(L, S_ISLNK)
DECL_MODEFUNC(L, S_ISSOCK)

#define REG_MODEFUNC(Method) {#Method, lfm_##Method}
static struct luaL_Reg fmlib[] = {
	REG_MODEFUNC(S_ISREG),
	REG_MODEFUNC(S_ISDIR),
	REG_MODEFUNC(S_ISCHR),
	REG_MODEFUNC(S_ISBLK),
	REG_MODEFUNC(S_ISFIFO),
	REG_MODEFUNC(S_ISLNK),
	REG_MODEFUNC(S_ISSOCK),
	{NULL, NULL}
};

void push_filemode(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, fmlib);
#else
	lua_newtable(L);
	luaL_register(L, NULL, fmlib);
#endif

#define SET_MODEBITS(macro_name) \
	(lua_pushinteger(L, macro_name), lua_setfield(L, -2, #macro_name))
	SET_MODEBITS(S_IFMT);
	SET_MODEBITS(S_IFSOCK);
	SET_MODEBITS(S_IFLNK);
	SET_MODEBITS(S_IFREG);
	SET_MODEBITS(S_IFBLK);
	SET_MODEBITS(S_IFDIR);
	SET_MODEBITS(S_IFCHR);
	SET_MODEBITS(S_IFIFO);
	SET_MODEBITS(S_ISUID);
	SET_MODEBITS(S_ISGID);
	SET_MODEBITS(S_ISVTX);
	SET_MODEBITS(S_IRWXU);
	SET_MODEBITS(S_IRUSR);
	SET_MODEBITS(S_IWUSR);
	SET_MODEBITS(S_IXUSR);
	SET_MODEBITS(S_IRWXG);
	SET_MODEBITS(S_IRGRP);
	SET_MODEBITS(S_IWGRP);
	SET_MODEBITS(S_IXGRP);
	SET_MODEBITS(S_IRWXO);
	SET_MODEBITS(S_IROTH);
	SET_MODEBITS(S_IWOTH);
	SET_MODEBITS(S_IXOTH);
}