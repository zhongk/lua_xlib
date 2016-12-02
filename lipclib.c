/**************************************************************************
local ipc = require('posix.ipc')

ipc.shm_open(name[, oflags=O_RDWR|O_CREAT, mode=0660]) return fd
ipc.shm_unlink(name)
ipc.mmap(fd[, prot=PROT_READ|PROT_WRITE, flags=MAP_SHARED, offset=0, length=0]) return userdata shm
shm:size()
shm:close()
shm:read(length)
shm:write(buffer)
shm:tell()
shm:seek(offset)

ipc.sem_open(name[, oflags=O_RDWR|O_CREAT, mode=0660, initval=1]) return userdata sem
ipc.sem_unlink()
sem:close()
sem:getvalue()
sem:post()
sem:wait([timeout=-1])
sem:trywait()

ipc.mq_open(name[, oflags=O_RDWR|O_CREAT, mode=0660, maxmsg=0, msgsize=0]) return userdata mq
ipc.mq_unlink(name)
mq:close()
mq:getattr() return {mq_flags,mq_maxmsg,mq_msgsize,mq_curmsgs}
mq:setblocking(bool)
mq:receive([timeout=-1]) return message, priority
mq:send(message[, priority=0, timeout=-1])
**************************************************************************/

#define LUA_POSIX_SHMNAME "posix.ipc.SharedMemory"
#define LUA_POSIX_SEMNAME "posix.ipc.Semaphore"
#define LUA_POSIX_MQNAME  "posix.ipc.MessageQueue"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include "lua_util.h"

typedef struct
{
	int fd;
	void *addr;
	size_t size, pos;
} shm_obj;

typedef struct
{
	sem_t *sem;
} sem_obj;

typedef struct
{
	mqd_t mqdes;
	int nonblock;
	size_t msgsize;
	char *msgbuf;
} mq_obj;

// ipc.shm_open(name[, oflags=O_RDWR|O_CREAT, mode=0660])
static int lshm_open(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	int oflags = luaL_optinteger(L, 2, O_RDWR|O_CREAT);
	mode_t mode = luaL_optinteger(L, 3, 0660);
	int fd = shm_open(name, oflags, mode);
	if (fd == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, fd);
	return 1;
}

// ipc.shm_unlink(name)
static int lshm_unlink(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	if (shm_unlink(name) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// ipc.mmap(fd[, prot=PROT_READ|PROT_WRITE, flags=MAP_SHARED, offset=0, length=0])
static int lshm_mmap(lua_State *L)
{
	int fd = luaL_checkinteger(L, 1);
	int prot = luaL_optinteger(L, 2, PROT_READ|PROT_WRITE);
	int flags = luaL_optinteger(L, 3, MAP_SHARED);
	off_t offset = (off_t)luaL_optinteger(L, 4, 0);
	size_t length = (size_t)luaL_optinteger(L, 5, 0);
	if (!length)
	{
		struct stat sb;
		if (fstat(fd, &sb) == -1)
			return report_errno(L, LRET_NIL);
		length = sb.st_size;
	}
	
	void *addr = mmap(NULL, length, prot, flags, fd, offset);
	if (addr == MAP_FAILED)
		return report_errno(L, LRET_NIL);
	
	shm_obj *shm = (shm_obj*)lua_newuserdata(L, sizeof(shm_obj));
	shm->fd = fd;
	shm->addr = addr;
	shm->size = length;
	shm->pos = 0;
	luaL_getmetatable(L, LUA_POSIX_SHMNAME);
	lua_setmetatable(L, -2);
	return 1;
}

// shm:size()
static int lshm_size(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	lua_pushinteger(L, shm->size);
	return 1;
}

// shm:close()
static int lshm_close(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	if (munmap(shm->addr, shm->size) == -1)
		return report_errno(L, LRET_BOOLEAN);
	memset(shm, 0, sizeof(shm_obj));
	shm->fd = -1;
	lua_pushboolean(L, 1);
	return 1;
}

// shm:read(length)
static int lshm_read(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	if (!shm->addr) return report_error(L, LRET_NIL, EBADF);
	size_t len = luaL_checkinteger(L, 2);
	if (shm->size < len + shm->pos)
		len = shm->size - shm->pos;
	lua_pushlstring(L, (char *)shm->addr + shm->pos, len);
	shm->pos += len;
	return 1;
}

// shm:write(buffer)
static int lshm_write(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	if (!shm->addr) return report_error(L, LRET_NIL, EBADF);
	size_t len = 0;
	const char *buffer = luaL_checklstring(L, 2, &len);
	if (shm->size < len + shm->pos)
		return report_error(L, LRET_NEGATIVE, EFAULT);
	memcpy((char *)shm->addr + shm->pos, buffer, len);
	shm->pos += len;
	lua_pushinteger(L, shm->pos);
	return 1;
}

// shm:tell()
static int lshm_tell(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	lua_pushinteger(L, shm->pos);
	return 1;	
}

// shm:seek(position)
static int lshm_seek(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_POSIX_SHMNAME);
	long pos = luaL_checkinteger(L, 2);
	if (labs(pos) >= shm->size) return report_error(L, LRET_NEGATIVE, EFAULT);
	shm->pos = pos < 0 ? shm->size+pos : pos;
	lua_pushinteger(L, shm->pos);
	return 1;
}

// ipc.sem_open(name[, oflags=O_RDWR|O_CREAT, mode=0660, initval=1])
static int lsem_open(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	int oflags = luaL_optinteger(L, 2, O_RDWR|O_CREAT);
	mode_t mode = luaL_optinteger(L, 3, 0660);
	unsigned initval = luaL_optinteger(L, 4, 1);
	
	sem_t *sem = sem_open(name, oflags, mode, initval);
	if (sem == SEM_FAILED)
		return report_errno(L, LRET_NIL);
	
	sem_obj *semobj = (sem_obj*)lua_newuserdata(L, sizeof(sem_obj));
	semobj->sem = sem;
	luaL_getmetatable(L, LUA_POSIX_SEMNAME);
	lua_setmetatable(L, -2);
	return 1;
}

// ipc.sem_unlink(name)
static int lsem_unlink(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	if (sem_unlink(name) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sem:close()
static int lsem_close(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_POSIX_SEMNAME);
	if (sem_close(sem->sem))
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	sem->sem = NULL;
	return 1;
}

// sem:getvalue()
static int lsem_getvalue(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_POSIX_SEMNAME);
	int value = 0;
	if (sem_getvalue(sem->sem, &value) == -1)
		return report_errno(L, LRET_NIL);
	lua_pushinteger(L, value);
	return 1;
}

// sem:post()
static int lsem_post(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_POSIX_SEMNAME);
	if (sem_post(sem->sem) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

static void expire_time(double timeout, struct timespec *ts)
{
	double iptr;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ts->tv_nsec = tv.tv_usec*1000 + (long)(modf(timeout, &iptr)*1000000000);
	ts->tv_sec = tv.tv_sec + (time_t)iptr;
	if (ts->tv_nsec > 1000000000)
	{
		++ts->tv_sec;
		ts->tv_nsec %= 1000000000;
	}
}

// sem:wait([timeout=-1])
static int lsem_wait(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_POSIX_SEMNAME);
	double timeout = (double)luaL_optnumber(L, 2, -1);
	int result = 0;
	if (timeout < 0)
	{
		result = sem_wait(sem->sem);
	}else
	{
		struct timespec abstime;
		expire_time(timeout, &abstime);
		result = sem_timedwait(sem->sem, &abstime);
	}
	if (result == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sem:trywait()
static int lsem_trywait(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_POSIX_SEMNAME);
	if (sem_trywait(sem->sem) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// ipc.mq_open(name[, oflags=O_RDWR|O_CREAT, mode=0660, maxmsg=0, msgsize=0])
static int lmq_open(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	int oflags = luaL_optinteger(L, 2, O_RDWR|O_CREAT);
	mode_t mode = luaL_optinteger(L, 3, 0660);
	long maxmsg = luaL_optinteger(L, 4, 0);
	long msgsize = luaL_optinteger(L, 5, 0);
	struct mq_attr attr;
	if (msgsize || maxmsg)
	{
		memset(&attr, 0, sizeof(attr));
		attr.mq_flags = oflags & O_NONBLOCK;
		attr.mq_msgsize = msgsize ? msgsize : 8192;
		attr.mq_maxmsg = maxmsg ? maxmsg : 10;
	}
	
	mqd_t mqdes = mq_open(name, oflags, mode, (msgsize || maxmsg) ? &attr : NULL);
	if (mqdes == (mqd_t)-1)
		return report_errno(L, LRET_NIL);
	mq_getattr(mqdes, &attr);
	
	mq_obj *mq = (mq_obj*)lua_newuserdata(L, sizeof(mq_obj));
	mq->mqdes = mqdes;
	mq->nonblock = attr.mq_flags ? 1 : 0;
	mq->msgbuf = (char *)malloc(attr.mq_msgsize+1);
	mq->msgsize = attr.mq_msgsize;
	luaL_getmetatable(L, LUA_POSIX_MQNAME);
	lua_setmetatable(L, -2);
	return 1;
}

// ipc.mq_unlink(name)
static int lmq_unlink(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	if (mq_unlink(name) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// mq:close()
static int lmq_close(lua_State *L)
{
	mq_obj *mq = (mq_obj *)luaL_checkudata(L, 1, LUA_POSIX_MQNAME);
	if (mq->mqdes != (mqd_t)-1)
	{
		if (mq_close(mq->mqdes) == -1)
			return report_errno(L, LRET_BOOLEAN);
		mq->mqdes = -1;
		free(mq->msgbuf);
		mq->msgbuf = NULL;
	}
	lua_pushboolean(L, 1);
	return 1;
}

// mq:getattr()
static int lmq_getattr(lua_State *L)
{
	mq_obj *mq = (mq_obj *)luaL_checkudata(L, 1, LUA_POSIX_MQNAME);
	struct mq_attr attr;
	if (mq_getattr(mq->mqdes, &attr) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, attr.mq_flags);   lua_setfield(L, -2, "mq_flags");
	lua_pushinteger(L, attr.mq_maxmsg);  lua_setfield(L, -2, "mq_maxmsg");
	lua_pushinteger(L, attr.mq_msgsize); lua_setfield(L, -2, "mq_msgsize");
	lua_pushinteger(L, attr.mq_curmsgs); lua_setfield(L, -2, "mq_curmsgs");
	return 1;
}

// mq:setblocking(bool)
static int lmq_setblocking(lua_State *L)
{
	mq_obj *mq = (mq_obj *)luaL_checkudata(L, 1, LUA_POSIX_MQNAME);
	int nonblock = !lua_toboolean(L, 2);
	if (nonblock != mq->nonblock)
	{
		struct mq_attr attr;
		mq_getattr(mq->mqdes, &attr);
		attr.mq_flags = nonblock ? O_NONBLOCK : 0;
		if (mq_setattr(mq->mqdes, &attr, NULL) == -1)
			return report_errno(L, LRET_BOOLEAN);
		mq->nonblock = nonblock;
	}
	lua_pushboolean(L, 1);
	return 1;
}

// mq:receive([timeout=-1]) return message, priority
static int lmq_receive(lua_State *L)
{
	mq_obj *mq = (mq_obj *)luaL_checkudata(L, 1, LUA_POSIX_MQNAME);
	double timeout = (double)luaL_optnumber(L, 2, -1);
	unsigned msg_prio = 0;
	ssize_t msglen = 0;
	if (timeout < 0)
	{
		msglen = mq_receive(mq->mqdes, mq->msgbuf, mq->msgsize, &msg_prio);
	}else
	{
		struct timespec abstime;
		expire_time(timeout, &abstime);
		msglen = mq_timedreceive(mq->mqdes, mq->msgbuf, mq->msgsize, &msg_prio, &abstime);
	}
	
	if (msglen == -1)
	{
		if (errno==EAGAIN || errno==EINTR || errno==ETIMEDOUT)
		{
			return report_errno(L, LRET_EMPTYSTR);
		}
		return report_errno(L, LRET_NIL);
	}
	lua_pushlstring(L, mq->msgbuf, msglen);
	lua_pushinteger(L, msg_prio);
	return 2;
}

// mq:send(message[, priority=0, timeout=-1])
static int lmq_send(lua_State *L)
{
	mq_obj *mq = (mq_obj *)luaL_checkudata(L, 1, LUA_POSIX_MQNAME);
	size_t msglen;
	const char *msgbuf = luaL_checklstring(L, 2, &msglen);
	unsigned msg_prio = (unsigned)luaL_optinteger(L, 3, 0);
	double timeout = (double)luaL_optnumber(L, 4, -1);
	int result = 0;
	if (timeout < 0)
	{
		result = mq_send(mq->mqdes, msgbuf, msglen, msg_prio);
	}else
	{
		struct timespec abstime;
		expire_time(timeout, &abstime);
		result = mq_timedsend(mq->mqdes, msgbuf, msglen, msg_prio, &abstime);
	}
	
	if (result == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

static struct luaL_Reg ipclib[] = {
	{"shm_open", lshm_open},
	{"shm_unlink", lshm_unlink},
	{"mmap", lshm_mmap},
	{"sem_open", lsem_open},
	{"sem_unlink", lsem_unlink},
	{"mq_open", lmq_open},
	{"mq_unlink", lmq_unlink},
	{NULL, NULL}
};
static struct luaL_Reg shm_methods[] = {
	{"size", lshm_size},
	{"close", lshm_close},
	{"read", lshm_read},
	{"write", lshm_write},
	{"tell", lshm_tell},
	{"seek", lshm_seek},
	{NULL, NULL}
};
static struct luaL_Reg sem_methods[] = {
	{"close", lsem_close},
	{"getvalue", lsem_getvalue},
	{"post", lsem_post},
	{"wait", lsem_wait},
	{"trywait", lsem_trywait},
	{NULL, NULL}
};
static struct luaL_Reg mq_methods[] = {
	{"close", lmq_close},
	{"getattr", lmq_getattr},
	{"setblocking", lmq_setblocking},
	{"receive", lmq_receive},
	{"send", lmq_send},
	{NULL, NULL}
};

static void lipc_AddMacro(lua_State *L, int t);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_ipc(lua_State *L)
{
	create_metatable(L, LUA_POSIX_SHMNAME, shm_methods);
	create_metatable(L, LUA_POSIX_SEMNAME, sem_methods);
	create_metatable(L, LUA_POSIX_MQNAME, mq_methods);
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, ipclib);
#else
	luaL_register(L, "posix_ipc", ipclib);
#endif
	lipc_AddMacro(L, lua_gettop(L));
	return 1;
}

void lipc_AddMacro(lua_State *L, int t)
{
	// for ipc.mmap
	SET_CONSTANT(L, t, PROT_READ);
	SET_CONSTANT(L, t, PROT_WRITE);
	SET_CONSTANT(L, t, PROT_EXEC);
	SET_CONSTANT(L, t, PROT_NONE);

	SET_CONSTANT(L, t, MAP_SHARED);
	SET_CONSTANT(L, t, MAP_PRIVATE);
#ifdef MAP_LOCKED
	SET_CONSTANT(L, t, MAP_LOCKED);
#endif
#ifdef MAP_ANONYMOUS
	SET_CONSTANT(L, t, MAP_ANONYMOUS);
#endif
#ifdef MAP_32BIT
	SET_CONSTANT(L, t, MAP_32BIT);
#endif
}