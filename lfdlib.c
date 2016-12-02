/**************************************************************************
local fd = require('posix.fd')

fd.open(path, flags[, mode=0644])
fd.creat(path[, mode=0644])
fd.pipe()
fd.close(fd)
fd.lseek(fd, offset[, whence=SEEK_SET])
fd.read(fd, size)
fd.write(fd, buf)
fd.pread(fd, size, offset)
fd.pwrite(fd, buf, offset)
fd.sendfile(out_fd, in_fd, size, offset=-1)
fd.sync(fd)
fd.truncate(fd, length)
fd.chmod(fd, mode)
fd.chown(fd[, uid=-1, gid=-1])
fd.stat(fd)
fd.pathconf(fd, name)
fd.dup(oldfd)
fd.dup2(oldfd, newfd)
fd.fileno(userdata io.file)

fd.fcntl(fd, cmd[, arg=0])
fd.flock(fd, cmd, lock_type[, len=0, start=0, whence=SEEK_SET])
fd.lockf(fd, cmd[, len=0])
fd.readable({fds...}[, timeout=-1])
fd.writable({fds...}[, timeout=-1])
**************************************************************************/
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include "lua_util.h"

#define FUNCNAME(Func) lfd_##Func
#define DEF_METHOD(Method, L) static int FUNCNAME(Method)(lua_State *L)

// fd.open(path, flags[, mode])
DEF_METHOD(open, L)
{
	const char *path = luaL_checkstring(L, 1);
	int flags = luaL_checkinteger(L, 2);
	mode_t mode = (mode_t)luaL_optinteger(L, 3, 0644);
	int fd = open(path, flags, mode);
	if (fd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, fd);
	return 1;
}

// fd.creat(path[, mode])
DEF_METHOD(creat, L)
{
	const char *path = luaL_checkstring(L, 1);
	mode_t mode = (mode_t)luaL_optinteger(L, 2, 0644);
	int fd = creat(path, mode);
	if (fd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, fd);
	return 1;
}

// fd.pipe()
DEF_METHOD(pipe, L)
{
	int pfd[2];
	if (pipe(pfd) == -1)
		return report_errno(L, LRET_NIL);
	lua_newtable(L);
	lua_pushinteger(L, pfd[0]);
	lua_rawseti(L, -2, 1);
	lua_pushinteger(L, pfd[1]);
	lua_rawseti(L, -2, 2);
	return 1;
}

// fd.close(fd)
DEF_METHOD(close, L)
{
	int fd = luaL_checkinteger(L, 1);
	if (close(fd) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fd.lseek(fd, offset[, whence])
DEF_METHOD(lseek, L)
{
	int fd = luaL_checkinteger(L, 1);
	int offset = luaL_checkinteger(L, 2);
	int whence = luaL_optinteger(L, 3, SEEK_SET);
	off_t loc = lseek(fd, offset, whence);
	if (loc == (off_t)-1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, loc);
	return 1;
}

// fd.truncate(fd, length)
DEF_METHOD(truncate, L)
{
	int fd = luaL_checkinteger(L, 1);
	off_t length = (off_t)luaL_checkinteger(L, 2);
	if (ftruncate(fd, length) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fd.stat(fd)
DEF_METHOD(stat, L)
{
	int fd = luaL_checkinteger(L, 1);
	struct stat sb;
	memset(&sb, 0, sizeof(sb));
	if (fstat(fd, &sb) == -1)
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

// fd.pathconf(fd, name)
DEF_METHOD(pathconf, L)
{
	int fd = luaL_checkinteger(L, 1);
	int name = luaL_checkinteger(L, 2);
	errno = 0;
	long value = fpathconf(fd, name);
	if (value==-1 && errno!=0)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, value);
	return 1;
}

// fd.sync(fd)
DEF_METHOD(sync, L)
{
	int fd = luaL_checkinteger(L, 1);
	if (fsync(fd) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fd.chmod(fd, mode)
DEF_METHOD(chmod, L)
{
	int fd = luaL_checkinteger(L, 1);
	int mode = luaL_checkinteger(L, 2);
	if (fchmod(fd, mode) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fd.chown(fd[, uid, gid])
DEF_METHOD(chown, L)
{
	int fd = luaL_checkinteger(L, 1);
	uid_t uid = (uid_t)luaL_optinteger(L, 2, -1);
	gid_t gid = (gid_t)luaL_optinteger(L, 3, -1);
	if (fchown(fd, uid, gid) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// fd.read(fd, size)
DEF_METHOD(read, L)
{
	char buf[8192];
	int fd = luaL_checkinteger(L, 1);
	size_t size = luaL_checkinteger(L, 2);
	luaL_argcheck(L, size<=sizeof(buf), 2, "size could not large than 8192");
	ssize_t len = read(fd, buf, size);
	if (len == -1)
		return report_errno(L, LRET_NIL);
	lua_pushlstring(L, buf, len);
	return 1;
}

// fd.write(fd, buf)
DEF_METHOD(write, L)
{
	int fd = luaL_checkinteger(L, 1);
	size_t count = 0;
	const char *buf = luaL_checklstring(L, 2, &count);
	ssize_t len = write(fd, buf, count);
	if (len == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, len);
	return 1;
}

extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
// fd.pread(fd, size, offset)
DEF_METHOD(pread, L)
{
	char buf[8192];
	int fd = luaL_checkinteger(L, 1);
	size_t size = luaL_checkinteger(L, 2);
	off_t offset = (off_t)luaL_checkinteger(L, 3);
	luaL_argcheck(L, size<=sizeof(buf), 2, "size could not large than 8192");
	ssize_t len = pread(fd, buf, size, offset);
	if (len == -1)
		return report_errno(L, LRET_NIL);
	lua_pushlstring(L, buf, len);
	return 1;
}

extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
// fd.pwrite(fd, buf, offset)
DEF_METHOD(pwrite, L)
{
	int fd = luaL_checkinteger(L, 1);
	size_t count = 0;
	const char *buf = luaL_checklstring(L, 2, &count);
	off_t offset = (off_t)luaL_checkinteger(L, 3);
	ssize_t len = pwrite(fd, buf, count, offset);
	if (len == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, len);
	return 1;
}

#ifdef HAVE_SENDFILE
#include <sys/sendfile.h>
// fd.sendfile(out_fd, in_fd, size, offset)
DEF_METHOD(sendfile, L)
{
	int out_fd = luaL_checkinteger(L, 1);
	int in_fd = luaL_checkinteger(L, 2);
	size_t size = luaL_optinteger(L, 3, 0);
	off_t offset = (off_t)luaL_optinteger(L, 4, -1);
	if (!size)
	{
		struct stat stbuf;
		if (fstat(in_fd, &stbuf) == -1)
			return report_errno(L, LRET_BOOLEAN);
		size = stbuf.st_size;
		if (offset < 0)
		{
			off_t off = lseek(in_fd, 0, SEEK_CUR);
			if (off == (off_t)-1)
				return report_errno(L, LRET_BOOLEAN);
			size -= off;
		}else
			size -= offset;
	}
	if (sendfile(out_fd, in_fd, (offset<0 ? NULL : &offset), size) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}
#endif

// fd.dup(oldfd)
DEF_METHOD(dup, L)
{
	int oldfd = luaL_checkinteger(L, 1);
	int newfd = dup(oldfd);
	if (newfd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, newfd);
	return 1;
}

// fd.dup2(oldfd, newfd)
DEF_METHOD(dup2, L)
{
	int oldfd = luaL_checkinteger(L, 1);
	int newfd = luaL_checkinteger(L, 2);
	int retfd = dup2(oldfd, newfd);
	if (retfd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, retfd);
	return 1;
}

// fd.fileno(io.file)
DEF_METHOD(fileno, L)
{
#if LUA_VERSION_NUM > 501
	luaL_Stream *p = (luaL_Stream *)luaL_checkudata(L, 1, LUA_FILEHANDLE);
	int fd = fileno(p->f);
#else
	FILE **pf = (FILE **)luaL_checkudata(L, 1, LUA_FILEHANDLE);
	int fd = fileno(*pf);
#endif
	if (fd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, fd);
	return 1;
}

// fd.fcntl(fd, cmd[, arg=0])
DEF_METHOD(fcntl, L)
{
	int fd = luaL_checkinteger(L, 1);
	int cmd = luaL_checkinteger(L, 2);
	long arg = luaL_optinteger(L, 3, 0);
	luaL_argcheck(L, (cmd!=F_GETLK && cmd!=F_SETLK && cmd!=F_SETLKW), 2, "please use fcntl(...)");
	int retval = fcntl(fd, cmd, arg);
	if (retval == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, retval);
	return 1;
}

// fd.flock(fd, cmd, lock_type[, {start=0, len=0, whence=SEEK_SET}])
DEF_METHOD(flock, L)
{
	int fd = luaL_checkinteger(L, 1);
	int cmd = luaL_checkinteger(L, 2);
	luaL_argcheck(L, (cmd==F_GETLK || cmd==F_SETLK || cmd==F_SETLKW), 2, "value in F_GETLK/F_SETLK/F_SETLKW");
	struct flock flk;
	memset(&flk, 0, sizeof(flk));
	flk.l_type = luaL_checkinteger(L, 3);
	flk.l_whence = SEEK_SET;
	if (lua_istable(L, 4))
	{
		lua_getfield(L, 4, "start");  flk.l_start = luaL_optinteger(L, -1, 0);  lua_pop(L, 1);
		lua_getfield(L, 4, "len");    flk.l_len = luaL_optinteger(L, -1, 0);    lua_pop(L, 1);
		lua_getfield(L, 4, "whence"); flk.l_whence = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
	}
	
	int retval = fcntl(fd, cmd, &flk);
	if (retval == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, retval);

	if (cmd != F_GETLK) return 1;
	lua_newtable(L);
	lua_pushinteger(L, flk.l_type);   lua_setfield(L, -2, "l_type");
	lua_pushinteger(L, flk.l_whence); lua_setfield(L, -2, "l_whence");
	lua_pushinteger(L, flk.l_start);  lua_setfield(L, -2, "l_start");
	lua_pushinteger(L, flk.l_len);    lua_setfield(L, -2, "l_len");
	lua_pushinteger(L, flk.l_pid);    lua_setfield(L, -2, "l_pid");
	return 2;
}

// fd.lockf(fd, cmd[, len=0])
DEF_METHOD(lockf, L)
{
	int fd = luaL_checkinteger(L, 1);
	int cmd = luaL_checkinteger(L, 2);
	off_t len = (off_t)luaL_optinteger(L, 3, 0);
	if (lockf(fd, cmd, len) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

static int lfd_select(lua_State *L, int to_read)
{
	int i, j, n, nfd = -1;
	luaL_checktype(L, 1, LUA_TTABLE);
	n = lua_rawlen(L, 1);
	double iptr, timeout = luaL_optnumber(L, 2, -1);
	struct timeval tv = { (long)timeout, (long)(modf(timeout, &iptr)*1000000) };
	
	fd_set fds;
	FD_ZERO(&fds);
	for(i=1; i<=n; i++)
	{
		lua_rawgeti(L, 1, i);
		int fd = luaL_checkinteger(L, -1);
		FD_SET(fd, &fds);
		lua_pop(L, 1);
		if (fd > nfd) nfd = fd;
	}
	fd_set *rfds = to_read ? &fds : NULL;
	fd_set *wfds = to_read ? NULL : &fds;
	int retval = select(nfd+1, rfds, wfds, NULL, timeout < 0 ? NULL : &tv);
	if (retval == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	if (retval > 0)
	{
		for(i=1, j=1; i<=n; i++)
		{
			lua_rawgeti(L, 1, i);
			if(FD_ISSET(lua_tointeger(L, -1), &fds))
				lua_rawseti(L, -2, j++);
			else
				lua_pop(L, 1);
		}	
	}
	return 1;
}

// fd.readable(fds[, timeout=-1])
DEF_METHOD(readable, L)
{
	return lfd_select(L, 1);
}

// fd.writable(fds[, timeout=-1])
DEF_METHOD(writable, L)
{
	return lfd_select(L, 0);
}

#define REG_METHOD(Method) {#Method, FUNCNAME(Method)}
static struct luaL_Reg fdlib[] = {
	REG_METHOD(open),
	REG_METHOD(creat),
	REG_METHOD(pipe),
	REG_METHOD(close),
	REG_METHOD(lseek),
	REG_METHOD(truncate),
	REG_METHOD(pathconf),
	REG_METHOD(stat),
	REG_METHOD(sync),
	REG_METHOD(chmod),
	REG_METHOD(chown),
	REG_METHOD(read),
	REG_METHOD(write),
	REG_METHOD(pread),
	REG_METHOD(pwrite),
#ifdef HAVE_SENDFILE
	REG_METHOD(sendfile),
#endif
	REG_METHOD(dup),
	REG_METHOD(dup2),
	REG_METHOD(fileno),
	REG_METHOD(fcntl),
	REG_METHOD(flock),
	REG_METHOD(lockf),
	REG_METHOD(readable),
	REG_METHOD(writable),
	{NULL, NULL}
};

static void lfd_AddMacro(lua_State *L, int t);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_fd(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, fdlib);
#else
	luaL_register(L, "posix_fd", fdlib);
#endif
	lfd_AddMacro(L, lua_gettop(L));
	return 1;
}

void lfd_AddMacro(lua_State *L, int t)
{
	SET_CONSTANT(L, t, STDIN_FILENO);
	SET_CONSTANT(L, t, STDOUT_FILENO);
	SET_CONSTANT(L, t, STDERR_FILENO);
	// for open
	SET_CONSTANT(L, t, O_RDONLY);
	SET_CONSTANT(L, t, O_WRONLY);
	SET_CONSTANT(L, t, O_RDWR);
	SET_CONSTANT(L, t, O_APPEND);
#ifdef O_ASYNC
	SET_CONSTANT(L, t, O_ASYNC);
#endif
	SET_CONSTANT(L, t, O_CREAT);
#ifdef O_DIRECT
	SET_CONSTANT(L, t, O_DIRECT);
#endif
#ifdef O_DIRECTORY
	SET_CONSTANT(L, t, O_DIRECTORY);
#endif
	SET_CONSTANT(L, t, O_EXCL);
#ifdef O_LARGEFILE
	SET_CONSTANT(L, t, O_LARGEFILE);
#endif
#ifdef O_NOATIME
	SET_CONSTANT(L, t, O_NOATIME);
#endif
	SET_CONSTANT(L, t, O_NOCTTY);
#ifdef O_NOFOLLOW
	SET_CONSTANT(L, t, O_NOFOLLOW);
#endif
	SET_CONSTANT(L, t, O_NONBLOCK);
	SET_CONSTANT(L, t, O_NDELAY);
	SET_CONSTANT(L, t, O_SYNC);
	SET_CONSTANT(L, t, O_TRUNC);
	// for lseek
	SET_CONSTANT(L, t, SEEK_SET);
	SET_CONSTANT(L, t, SEEK_CUR);
	SET_CONSTANT(L, t, SEEK_END);
	// for fcntl
	SET_CONSTANT(L, t, F_DUPFD);
	SET_CONSTANT(L, t, F_GETFD);
	SET_CONSTANT(L, t, F_SETFD);
	SET_CONSTANT(L, t, F_GETFL);
	SET_CONSTANT(L, t, F_SETFL);
	SET_CONSTANT(L, t, F_GETOWN);
	SET_CONSTANT(L, t, F_SETOWN);
#ifdef F_GETSIG
	SET_CONSTANT(L, t, F_GETSIG);
#endif
#ifdef F_SETSIG
	SET_CONSTANT(L, t, F_SETSIG);
#endif
#ifdef F_GETLEASE
	SET_CONSTANT(L, t, F_GETLEASE);
#endif
#ifdef F_SETLEASE
	SET_CONSTANT(L, t, F_SETLEASE);
#endif
#ifdef F_NOTIFY
	SET_CONSTANT(L, t, F_NOTIFY);
#endif
	SET_CONSTANT(L, t, FD_CLOEXEC);
	// for flock
	SET_CONSTANT(L, t, F_GETLK);
	SET_CONSTANT(L, t, F_SETLK);
	SET_CONSTANT(L, t, F_SETLKW);
	
	SET_CONSTANT(L, t, F_RDLCK);
	SET_CONSTANT(L, t, F_WRLCK);
	SET_CONSTANT(L, t, F_UNLCK);
	// for lockf
	SET_CONSTANT(L, t, F_LOCK);
	SET_CONSTANT(L, t, F_TLOCK);
	SET_CONSTANT(L, t, F_ULOCK);
	SET_CONSTANT(L, t, F_TEST);
}