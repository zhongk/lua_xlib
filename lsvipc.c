/**************************************************************************
local svipc = require('svipc')

svipc.IPC_*
svipc.ftok(path[, proj_id='/'])

svipc.shmget(key, size[, ipcflg=IPC_CREAT, mode=0660]) return userdata shm
shm:stat()
shm:info()
shm:remove()
shm:size()
shm:attach([readonly=false])
shm:detach()
shm:read(length)
shm:write(buffer)
shm:tell()
shm:seek(offset)

svipc.msgget(key[, ipcflg=IPC_CREAT, mode=0660]) return userdata msg
msg:stat()
msg:info()
msg:remove()
msg:receive([msgtyp=0, nowait=false])
msg:send(message[, msgtyp=1, nowait=false])

svipc.semget(key[, nsems=1, ipcflg=IPC_CREAT, mode=0660]) return userdata sem
sem:stat()
sem:info()
sem:remove()
sem:add_op(sem_op[, sem_num=1])
sem:done([nowait=false])
sem:getpid([sem_num=1])
sem:getncnt([sem_num=1])
sem:getzcnt([sem_num=1])
sem:getall()
sem:setall(val)
sem:getval([sem_num=1])
sem:setval(val[, sem_num=1])
**************************************************************************/

#define LUA_SYSV_SHMNAME "sysv.ipc.SharedMemory"
#define LUA_SYSV_SEMNAME "sysv.ipc.Semaphore"
#define LUA_SYSV_MSGNAME "sysv.ipc.MessageQueue"
#define MSGMAX  (64*1024)

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include "lua_util.h"

typedef struct
{
	int shmid;
	int readonly;
	size_t size, pos;
	void *addr;
} shm_obj;

typedef struct
{
	int semid;
	int nsems;
	int nops;
	unsigned short *vals;
	struct sembuf *sops;
} sem_obj;

typedef struct
{
	int msqid;
	char *msgbuf;
} msg_obj;

#if !defined(_AIX) && !defined(__hpux) && !defined(__sun)
struct msgbuf {
	long mtype;		/* message type, must be > 0 */
	char mtext[1];	/* message data */
};
#endif

// svipc.ftok(path[, proj_id='/'])
static int lipc_ftok(lua_State *L)
{
	const char *path = luaL_checkstring(L, 1);
	const char *proj_id = luaL_optstring(L, 2, "/");
	lua_pushinteger(L, ftok(path, proj_id[0]));
	return 1;
}

// svipc.shmget(key, size[, ipcflg=IPC_CREAT, mode=0660])
static int lipc_shmget(lua_State *L)
{
	key_t key = (key_t)luaL_checkinteger(L, 1);
	size_t size = (size_t)luaL_checkinteger(L, 2);
	int ipcflg = luaL_optinteger(L, 3, IPC_CREAT);
	mode_t mode = luaL_optinteger(L, 4, 0660);
	
	int shmid = shmget(key, size, ipcflg|mode);
	if (shmid == -1)
		return report_errno(L, LRET_NIL);
	struct shmid_ds buf;
	shmctl(shmid, IPC_STAT, &buf);
	
	shm_obj *shmobj = (shm_obj*)lua_newuserdata(L, sizeof(shm_obj));
	memset(shmobj, 0, sizeof(shm_obj));
	shmobj->shmid = shmid;
	shmobj->size = buf.shm_segsz;
	luaL_getmetatable(L, LUA_SYSV_SHMNAME);
	lua_setmetatable(L, -2);
	lua_pushinteger(L, shmid);
	return 2;
}

static int push_ipc_perm(lua_State *L, struct ipc_perm *perm)
{
	lua_newtable(L);
#ifdef __linux
	lua_pushinteger(L, perm->__key); lua_setfield(L, -2, "key");
	lua_pushinteger(L, perm->__seq); lua_setfield(L, -2, "seq");
#else
	lua_pushinteger(L, perm->key);  lua_setfield(L, -2, "key");
	lua_pushinteger(L, perm->seq);  lua_setfield(L, -2, "seq");
#endif
	lua_pushinteger(L, perm->uid);  lua_setfield(L, -2, "uid");
	lua_pushinteger(L, perm->gid);  lua_setfield(L, -2, "gid");
	lua_pushinteger(L, perm->cuid); lua_setfield(L, -2, "cuid");
	lua_pushinteger(L, perm->cgid); lua_setfield(L, -2, "cgid");
	lua_pushinteger(L, perm->mode); lua_setfield(L, -2, "mode");
	return 1;
}

// shm:stat()
static int lshm_stat(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	struct shmid_ds buf;
	if (shmctl(shm->shmid, IPC_STAT, &buf) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, buf.shm_segsz);  lua_setfield(L, -2, "shm_segsz");
	lua_pushinteger(L, buf.shm_atime);  lua_setfield(L, -2, "shm_atime");
	lua_pushinteger(L, buf.shm_dtime);  lua_setfield(L, -2, "shm_dtime");
	lua_pushinteger(L, buf.shm_ctime);  lua_setfield(L, -2, "shm_ctime");
	lua_pushinteger(L, buf.shm_cpid);   lua_setfield(L, -2, "shm_cpid");
	lua_pushinteger(L, buf.shm_lpid);   lua_setfield(L, -2, "shm_lpid");
	lua_pushinteger(L, buf.shm_nattch); lua_setfield(L, -2, "shm_nattch");
	push_ipc_perm(L, &buf.shm_perm);    lua_setfield(L, -2, "shm_perm");
	return 1;
}

#ifdef IPC_INFO
// shm:info()
static int lshm_info(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	struct shminfo info;
	if (shmctl(shm->shmid, IPC_INFO, (struct shmid_ds *)&info) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, info.shmmax); lua_setfield(L, -2, "shmmax");
	lua_pushinteger(L, info.shmmin); lua_setfield(L, -2, "shmmin");
	lua_pushinteger(L, info.shmmni); lua_setfield(L, -2, "shmmni");
	lua_pushinteger(L, info.shmseg); lua_setfield(L, -2, "shmseg");
	lua_pushinteger(L, info.shmall); lua_setfield(L, -2, "shmall");
	return 1;	
}
#endif

// shm:remove()
static int lshm_remove(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	if (shmctl(shm->shmid, IPC_RMID, 0) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// shm:size()
static int lshm_size(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	lua_pushinteger(L, shm->size);
	return 1;
}

// shm:attach([readonly=false])
static int lshm_attach(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	int readonly = lua_toboolean(L, 2);
	if (shm->addr) return report_error(L, LRET_BOOLEAN, EEXIST);
	void *shmaddr = shmat(shm->shmid, NULL, readonly ? SHM_RDONLY : 0);
	if (shmaddr == (void*)-1)
		return report_errno(L, LRET_BOOLEAN);
	shm->addr = shmaddr;
	shm->readonly = readonly;
	lua_pushboolean(L, 1);
	return 1;
}

// shm:detach()
static int lshm_detach(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	if (shm->addr && shmdt(shm->addr)==-1)
		return report_errno(L, LRET_BOOLEAN);
	shm->addr = NULL;
	lua_pushboolean(L, 1);
	return 1;
}

// shm:read(length)
static int lshm_read(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
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
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	if (!shm->addr) return report_error(L, LRET_NIL, EBADF);
	if (shm->readonly) return report_error(L, LRET_NEGATIVE, EACCES);
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
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	lua_pushinteger(L, shm->pos);
	return 1;	
}

// shm:seek(offset)
static int lshm_seek(lua_State *L)
{
	shm_obj *shm = (shm_obj *)luaL_checkudata(L, 1, LUA_SYSV_SHMNAME);
	long pos = luaL_checkinteger(L, 2);
	if (labs(pos) >= shm->size) return report_error(L, LRET_NEGATIVE, EFAULT);
	shm->pos = pos < 0 ? shm->size+pos : pos;
	lua_pushinteger(L, shm->pos);
	return 1;
}

// svipc.msgget(key[, ipcflg=IPC_CREAT, mode=0660]) return userdata msg
static int lipc_msgget(lua_State *L)
{
	key_t key = (key_t)luaL_checkinteger(L, 1);
	int ipcflg = luaL_optinteger(L, 2, IPC_CREAT);
	mode_t mode = luaL_optinteger(L, 3, 0660);
	
	int msqid = msgget(key, ipcflg|mode);
	if (msqid == -1)
		return report_errno(L, LRET_NIL);
	
	msg_obj *msgobj = (msg_obj*)lua_newuserdata(L, sizeof(msg_obj));
	memset(msgobj, 0, sizeof(msg_obj));
	msgobj->msqid = msqid;
	msgobj->msgbuf = (char *)malloc(MSGMAX + sizeof(long));
	luaL_getmetatable(L, LUA_SYSV_MSGNAME);
	lua_setmetatable(L, -2);
	lua_pushinteger(L, msqid);
	return 2;
}

static int lmsg_gc(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	free(msg->msgbuf);
	msg->msqid = -1;
	return 0;
}

// msg:stat()
static int lmsg_stat(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	struct msqid_ds buf;
	if (msgctl(msg->msqid, IPC_STAT, &buf) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, buf.msg_stime);  lua_setfield(L, -2, "msg_stime");
	lua_pushinteger(L, buf.msg_rtime);  lua_setfield(L, -2, "msg_rtime");
	lua_pushinteger(L, buf.msg_ctime);  lua_setfield(L, -2, "msg_ctime");
	lua_pushinteger(L, buf.msg_cbytes); lua_setfield(L, -2, "msg_cbytes");
	lua_pushinteger(L, buf.msg_qnum);   lua_setfield(L, -2, "msg_qnum");
	lua_pushinteger(L, buf.msg_qbytes); lua_setfield(L, -2, "msg_qbytes");
	lua_pushinteger(L, buf.msg_lspid);  lua_setfield(L, -2, "msg_lspid");
	lua_pushinteger(L, buf.msg_lrpid);  lua_setfield(L, -2, "msg_lrpid");
	push_ipc_perm(L, &buf.msg_perm);    lua_setfield(L, -2, "msg_perm");
	return 1;
}

#ifdef IPC_INFO
// msg:info()
static int lmsg_info(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	struct msginfo info;
	if (msgctl(msg->msqid, IPC_INFO, (struct msqid_ds *)&info) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, info.msgpool); lua_setfield(L, -2, "msgpool");
	lua_pushinteger(L, info.msgmap);  lua_setfield(L, -2, "msgmap");
	lua_pushinteger(L, info.msgmax);  lua_setfield(L, -2, "msgmax");
	lua_pushinteger(L, info.msgmnb);  lua_setfield(L, -2, "msgmnb");
	lua_pushinteger(L, info.msgmni);  lua_setfield(L, -2, "msgmni");
	lua_pushinteger(L, info.msgssz);  lua_setfield(L, -2, "msgssz");
	lua_pushinteger(L, info.msgtql);  lua_setfield(L, -2, "msgtql");
	lua_pushinteger(L, info.msgseg);  lua_setfield(L, -2, "msgseg");
	return 1;	
}
#endif

// msg:remove()
static int lmsg_remove(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	if (msgctl(msg->msqid, IPC_RMID, 0) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// msg:receive([msgtype=0, nowait=false])
static int lmsg_receive(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	long msgtype = luaL_optinteger(L, 2, 0);
	int nowait = lua_toboolean(L, 3);
	
	struct msgbuf *msgp = (struct msgbuf *)msg->msgbuf;
	ssize_t msglen = msgrcv(msg->msqid, msgp, MSGMAX, msgtype, nowait ? IPC_NOWAIT : 0);
	if (msglen == -1)
	{
		if (errno==EAGAIN || errno==ENOMSG || errno==EINTR)
		{
			return report_errno(L, LRET_EMPTYSTR);
		}
		return report_errno(L, LRET_NIL);
	}
	lua_pushlstring(L, msgp->mtext, msglen);
	lua_pushinteger(L, msgp->mtype);
	return 2;
}

// msg:send(message[, msgtype=1, nowait=false])
static int lmsg_send(lua_State *L)
{
	msg_obj *msg = (msg_obj *)luaL_checkudata(L, 1, LUA_SYSV_MSGNAME);
	size_t msglen;
	const char *msgbuf = luaL_checklstring(L, 2, &msglen);
	long msgtype = luaL_optinteger(L, 3, 1);
	luaL_argcheck(L, msgtype>0, 2, "message type must be > 0");
	int nowait = lua_toboolean(L, 4);
	if (msglen > MSGMAX) return report_error(L, LRET_BOOLEAN, E2BIG);
	
	struct msgbuf *msgp = (struct msgbuf *)msg->msgbuf;
	msgp->mtype = msgtype;
	memcpy(msgp->mtext, msgbuf, msglen);
	if (msgsnd(msg->msqid, msgp, msglen, nowait ? IPC_NOWAIT : 0) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// svipc.semget(key[, nsems=1, ipcflg=IPC_CREAT, mode=0660]) return userdata sem
static int lipc_semget(lua_State *L)
{
	key_t key = (key_t)luaL_checkinteger(L, 1);
	int nsems = luaL_optinteger(L, 2, 1);
	int ipcflg = luaL_optinteger(L, 3, IPC_CREAT);
	mode_t mode = luaL_optinteger(L, 4, 0660);
	
	int semid = semget(key, nsems, ipcflg|mode);
	if (semid == -1)
		return report_errno(L, LRET_NIL);
	struct semid_ds buf;
	semctl(semid, 0, IPC_STAT, &buf);
	
	sem_obj *semobj = (sem_obj*)lua_newuserdata(L, sizeof(sem_obj));
	memset(semobj, 0, sizeof(sem_obj));
	semobj->semid = semid;
	semobj->nsems = buf.sem_nsems;
	semobj->vals = (unsigned short *)calloc(semobj->nsems, sizeof(unsigned short));
	semobj->sops = (struct sembuf *)calloc(semobj->nsems*3, sizeof(struct sembuf));
	luaL_getmetatable(L, LUA_SYSV_SEMNAME);
	lua_setmetatable(L, -2);
	lua_pushinteger(L, semid);
	return 2;
}

static int lsem_gc(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	free(sem->vals);
	free(sem->sops);
	sem->semid = -1;
	return 0;
}

// sem:stat()
static int lsem_stat(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	struct semid_ds buf;
	if (semctl(sem->semid, 0, IPC_STAT, &buf) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, buf.sem_otime);  lua_setfield(L, -2, "sem_otime");
	lua_pushinteger(L, buf.sem_ctime);  lua_setfield(L, -2, "sem_ctime");
	lua_pushinteger(L, buf.sem_nsems);  lua_setfield(L, -2, "sem_nsems");
	push_ipc_perm(L, &buf.sem_perm);    lua_setfield(L, -2, "sem_perm");
	return 1;
}

#ifdef IPC_INFO
// sem:info()
static int lsem_info(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	struct seminfo info;
	if (semctl(sem->semid, 0, IPC_INFO, &info) == -1)
		return report_errno(L, LRET_NIL);
	
	lua_newtable(L);
	lua_pushinteger(L, info.semmap); lua_setfield(L, -2, "semmap");
	lua_pushinteger(L, info.semmni); lua_setfield(L, -2, "semmni");
	lua_pushinteger(L, info.semmns); lua_setfield(L, -2, "semmns");
	lua_pushinteger(L, info.semmnu); lua_setfield(L, -2, "semmnu");
	lua_pushinteger(L, info.semmsl); lua_setfield(L, -2, "semmsl");
	lua_pushinteger(L, info.semopm); lua_setfield(L, -2, "semopm");
	lua_pushinteger(L, info.semume); lua_setfield(L, -2, "semume");
	lua_pushinteger(L, info.semusz); lua_setfield(L, -2, "semusz");
	lua_pushinteger(L, info.semvmx); lua_setfield(L, -2, "semvmx");
	lua_pushinteger(L, info.semaem); lua_setfield(L, -2, "semaem");
	return 1;	
}
#endif

// sem:remove()
static int lsem_remove(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	if (semctl(sem->semid, 0, IPC_RMID, 0) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sem:add_op(sem_op[, sem_num=1])
static int lsem_addop(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	short sem_op = (unsigned short)luaL_checkinteger(L, 2);
	int sem_num = luaL_optinteger(L, 3, 1);
	if (sem_num<=0 || sem_num>sem->nsems) return report_error(L, LRET_NEGATIVE, EINVAL);
	if (sem->nops >= sem->nsems*3) return report_error(L, LRET_NEGATIVE, ERANGE);
	sem->sops[sem->nops].sem_num = sem_num-1;
	sem->sops[sem->nops].sem_op = sem_op;
	sem->sops[sem->nops].sem_flg = sem_op ? SEM_UNDO : 0;
	sem->nops++;
	lua_pushboolean(L, 1);
	return 1;
}

// sem:done([nowait=false])
static int lsem_done(lua_State *L)
{
	int i = 0;
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	int nowait = lua_toboolean(L, 2);
	if (nowait)
	{
		for(i=0; i<sem->nops; i++)
			sem->sops[i].sem_flg |= IPC_NOWAIT;
	}
	int nops = sem->nops;
	sem->nops = 0;
	if (semop(sem->semid, sem->sops, nops) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

static int lsem_ctlget(lua_State *L, int cmd)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	int sem_num = luaL_optinteger(L, 2, 1);
	if (sem_num<=0 || sem_num>sem->nsems) return report_error(L, LRET_NEGATIVE, EINVAL);
	int result = semctl(sem->semid, sem_num-1, cmd, 0);
	if (result == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, result);
	return 1;
}

// sem:getpid([sem_num=1])
static int lsem_getpid(lua_State *L)
{
	return lsem_ctlget(L, GETPID);
}

// sem:getncnt([sem_num=1])
static int lsem_getncnt(lua_State *L)
{
	return lsem_ctlget(L, GETNCNT);
}

// sem:getzcnt([sem_num=1])
static int lsem_getzcnt(lua_State *L)
{
	return lsem_ctlget(L, GETZCNT);
}

// sem:getall()
static int lsem_getall(lua_State *L)
{
	int i = 0;
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	if (semctl(sem->semid, 0, GETALL, sem->vals) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_newtable(L);
	for(i=0; i<sem->nsems; i++)
	{
		lua_pushinteger(L, sem->vals[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}

// sem:setall(val)
static int lsem_setall(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	int i, val = luaL_checkinteger(L, 2);
	if (val < 0) return report_error(L, LRET_NEGATIVE, EINVAL);
	for (i=0; i<sem->nsems; i++)
		sem->vals[i] = val;
	if (semctl(sem->semid, 0, SETALL, sem->vals) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sem:getval([sem_num=1])
static int lsem_getval(lua_State *L)
{
	return lsem_ctlget(L, GETVAL);
}

// sem:setval(val[, sem_num=1])
static int lsem_setval(lua_State *L)
{
	sem_obj *sem = (sem_obj *)luaL_checkudata(L, 1, LUA_SYSV_SEMNAME);
	int val = luaL_checkinteger(L, 2);
	int sem_num = luaL_optinteger(L, 3, 1);
	if (sem_num<=0 || sem_num>sem->nsems) return report_error(L, LRET_NEGATIVE, EINVAL);
	if (semctl(sem->semid, sem_num-1, SETVAL, val) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

static struct luaL_Reg ipclib[] = {
	{"ftok", lipc_ftok},
	{"shmget", lipc_shmget},
	{"semget", lipc_semget},
	{"msgget", lipc_msgget},
	{NULL, NULL}
};
static struct luaL_Reg shm_methods[] = {
	{"__gc", lshm_detach},
	{"stat", lshm_stat},
#ifdef IPC_INFO
	{"info", lshm_info},
#endif
	{"remove", lshm_remove},
	{"attach", lshm_attach},
	{"detach", lshm_detach},
	{"size", lshm_size},
	{"read", lshm_read},
	{"write", lshm_write},
	{"tell", lshm_tell},
	{"seek", lshm_seek},
	{NULL, NULL}
};
static struct luaL_Reg sem_methods[] = {
	{"__gc", lsem_gc},
	{"stat", lsem_stat},
#ifdef IPC_INFO
	{"info", lsem_info},
#endif
	{"remove", lsem_remove},
	{"add_op", lsem_addop},
	{"done", lsem_done},
	{"getpid", lsem_getpid},
	{"getncnt", lsem_getncnt},
	{"getzcnt", lsem_getzcnt},
	{"getall", lsem_getall},
	{"setall", lsem_setall},
	{"getval", lsem_getval},
	{"setval", lsem_setval},
	{NULL, NULL}
};
static struct luaL_Reg msg_methods[] = {
	{"__gc", lmsg_gc},
	{"stat", lmsg_stat},
#ifdef IPC_INFO
	{"info", lmsg_info},
#endif
	{"remove", lmsg_remove},
	{"receive", lmsg_receive},
	{"send", lmsg_send},
	{NULL, NULL}
};

static void lipc_AddMacro(lua_State *L, int t);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_svipc(lua_State *L)
{
	create_metatable(L, LUA_SYSV_SHMNAME, shm_methods);
	create_metatable(L, LUA_SYSV_SEMNAME, sem_methods);
	create_metatable(L, LUA_SYSV_MSGNAME, msg_methods);
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, ipclib);
#else
	luaL_register(L, "svipc", ipclib);
#endif
	lipc_AddMacro(L, lua_gettop(L));
	return 1;
}

void lipc_AddMacro(lua_State *L, int t)
{
	// for svipc.shmget/svipc.semget/svipc.msgget
	SET_CONSTANT(L, t, IPC_CREAT);
	SET_CONSTANT(L, t, IPC_EXCL);
	SET_CONSTANT(L, t, IPC_PRIVATE);
}