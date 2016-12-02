/**************************************************************************
local sys = require('posix.sys')

sys.getpid()
sys.getppid()
sys.getsid([pid])
sys.setsid()
sys.getpgid([pid])
sys.setpgid([pid,] pgid)
sys.getpgrp()
sys.setpgrp()

sys.execlp(file, arg, ...)
sys.daemon([nochdir=1, noclose=0])
sys.kill(pid[, signo=SIGTERM])
sys.fork()
sys._exit([status=0])
sys.wait()
sys.waitpid(pid[, options=WNOHANG])
sys.W*(status)

sys.alarm(seconds)
sys.pause()
sys.signal(signo, function handler(signo))

sys.sleep(seconds)
sys.usleep(usec)
sys.nanosleep(float seconds)
sys.gettimeofday()

sys.uname()
sys.getlogin()
sys.gethostid()
sys.ctermid()
sys.getenv(name)
sys.setenv(name, value[, overwrite=1])
sys.unsetenv(name)
sys.pathconf(path, optname)
sys.sysconf(optname)
sys.getpagesize()
sys.getrusage([who=RUSAGE_SELF])
sys.getrlimit(resource)
sys.setrlimit(resource, softlimit[, hardlimit=-1])
sys.crypt(key[, salt=key])
sys.iconv(inbytes, fromcode[, tocode])
**************************************************************************/
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <libgen.h>
#include <signal.h>
#include "lua_util.h"

#define FUNCNAME(Func) lsys_##Func
#define DEF_METHOD(Method, L) static int FUNCNAME(Method)(lua_State *L)

// sys.getpid()
DEF_METHOD(getpid, L)
{
	lua_pushinteger(L, getpid());
	return 1;
}

// sys.getppid()
DEF_METHOD(getppid, L)
{
	lua_pushinteger(L, getppid());
	return 1;
}

#if defined(_XOPEN_SOURCE) && defined(_XOPEN_SOURCE_EXTENDED)
// sys.getsid([pid])
DEF_METHOD(getsid, L)
{
	pid_t pid = (pid_t)luaL_optinteger(L, 1, 0);
	pid_t sid = getsid(pid);
	if (sid == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, sid);
	return 1;
}

// sys.getpgid([pid])
DEF_METHOD(getpgid, L)
{
	pid_t pid = (pid_t)luaL_optinteger(L, 1, 0);
	pid_t pgid = getpgid(pid);
	if (pgid == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, pgid);
	return 1;
}
#endif

// sys.setsid()
DEF_METHOD(setsid, L)
{
	if (setsid() == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.setpgid([pid,] pgid)
DEF_METHOD(setpgid, L)
{
	pid_t pid = 0, pgid;
	if (lua_gettop(L) == 1)
		pgid = (pid_t)luaL_checkinteger(L, 1);
	else{
		pid = (pid_t)luaL_checkinteger(L, 1);
		pgid = (pid_t)luaL_checkinteger(L, 2);
	}
	if (setpgid(pid, pgid) == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.getpgrp()
DEF_METHOD(getpgrp, L)
{
	pid_t pgrp = getpgrp();
	if (pgrp == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, pgrp);
	return 1;
}

// sys.setpgrp()
DEF_METHOD(setpgrp, L)
{
	if (setpgrp() == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.alarm(seconds)
DEF_METHOD(alarm, L)
{
	unsigned int seconds = luaL_checkinteger(L, 1);
	lua_pushinteger(L, alarm(seconds));
	return 1;
}

// sys.pause()
DEF_METHOD(pause, L)
{
	pause();
	return 0;
}

// sys.sleep(seconds)
DEF_METHOD(sleep, L)
{
	unsigned int seconds = luaL_checkinteger(L, 1);
	lua_pushinteger(L, sleep(seconds));
	return 1;
}

// sys.usleep(usec)
DEF_METHOD(usleep, L)
{
	unsigned long usec = luaL_checkinteger(L, 1);
	if (usleep(usec) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.nanosleep(float seconds)
DEF_METHOD(nanosleep, L)
{
	double iptr, seconds = (double)luaL_checknumber(L, 1);
	struct timespec req, rem;
	req.tv_nsec = (long)(modf(seconds, &iptr)*1000000000);
	req.tv_sec = (time_t)iptr;
	if (nanosleep(&req, &rem) == -1)
	{
		if (errno != EINTR)
			return report_errno(L, LRET_NIL);
		double rem_time = rem.tv_sec + (double)rem.tv_nsec/1000000000;
		lua_pushnumber(L, rem_time);
	}else
		lua_pushnumber(L, 0);
	return 1;
}

// sys.gettimeofday()
DEF_METHOD(gettimeofday, L)
{
	struct timeval now;
	if (gettimeofday(&now, NULL) == -1)
		return report_errno(L, LRET_NIL);
	double now_time = now.tv_sec + (double)now.tv_usec/1000000;
	lua_pushnumber(L, now_time);
	return 1;
}

// sys.uname()
DEF_METHOD(uname, L)
{
	struct utsname nbuf;
	if (uname(&nbuf) == -1)
		return report_errno(L, LRET_NIL);
	lua_newtable(L);
	lua_pushstring(L, nbuf.sysname);    lua_setfield(L, -2, "sysname");
	lua_pushstring(L, nbuf.nodename);   lua_setfield(L, -2, "nodename");
	lua_pushstring(L, nbuf.release);    lua_setfield(L, -2, "release");
	lua_pushstring(L, nbuf.version);    lua_setfield(L, -2, "version");
	lua_pushstring(L, nbuf.machine);    lua_setfield(L, -2, "machine");
#ifdef _GNU_SOURCE
	lua_pushstring(L, nbuf.domainname); lua_setfield(L, -2, "domainname");
#endif
	return 1;
}

// sys.getlogin()
DEF_METHOD(getlogin, L)
{
	lua_pushstring(L, getlogin());
	return 1;
}

// sys.gethostid()
DEF_METHOD(gethostid, L)
{
	lua_pushinteger(L, gethostid());
	return 1;
}

// sys.ctermid()
DEF_METHOD(ctermid, L)
{
	lua_pushstring(L, ctermid(NULL));
	return 1;
}

#ifdef HAVE_CRYPT
extern char *crypt(const char *key, const char *salt);
// sys.crypt(key[, salt])
DEF_METHOD(crypt, L)
{
	const char *key = luaL_checkstring(L, 1);
	const char *salt = luaL_optstring(L, 2, key);
	luaL_argcheck(L, strlen(salt)>1, 2, "salt length must be >1");
	char *res = crypt(key, salt);
	if (!res)
		return report_errno(L, LRET_NIL);
	lua_pushstring(L, res);
	return 1;
}
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
// sys.iconv(inbytes, fromcode[, tocode])
DEF_METHOD(iconv, L)
{
	size_t inbytes;
	const char* inbuf = luaL_checklstring(L, 1, &inbytes);
	const char* fromcode = luaL_checkstring(L, 2);
	const char* tocode = luaL_optstring(L, 3, fromcode);
	
	iconv_t ic = iconv_open(tocode, fromcode);
	if (ic == (iconv_t)-1) return report_errno(L, LRET_NIL);
	
	char outbuf[4096];
	size_t ileft = inbytes, nresult = 0;
	while(ileft > 0)
	{
		char *inp = (char *)inbuf + inbytes - ileft, *outp = outbuf;
		size_t oleft = sizeof(outbuf);
		if (iconv(ic, &inp, &ileft, &outp, &oleft) == (size_t)-1 && errno != E2BIG)
		{
			if (nresult) lua_pop(L, nresult);
			int nret = report_errno(L, LRET_NIL);
			iconv_close(ic);
			return nret;
		}
		lua_pushlstring(L, outbuf, outp - outbuf);
		nresult++;
	}
	
	iconv_close(ic);
	lua_concat(L, nresult);
	return 1;
}
#endif

// sys.getenv(name)
DEF_METHOD(getenv, L)
{
	const char *name = luaL_checkstring(L, 1);
	char *value = getenv(name);
	if (value)
		lua_pushstring(L, value);
	else
		lua_pushnil(L);
	return 1;
}

#ifdef __sun__

static int setenv(const char *name, const char *value, int overwrite)
{
	if (!overwrite)
	{
		char *ret = getenv(name);
		if (ret && *ret) return 0;
	}
	char *env = (char *)malloc(strlen(name)+strlen(value)+2);
	sprintf(env, "%s=%s", name, value);
	int error = putenv(env);
	if (error)
	{
		free(env);
		errno = error;
		return -1;
	}
	return 0;
}
#define unsetenv(name) setenv(name, "", 1)

#endif

// sys.setenv(name, value[, overwrite=1])
DEF_METHOD(setenv, L)
{
	const char *name = luaL_checkstring(L, 1);
	const char *value = luaL_checkstring(L, 2);
	int overwrite = luaL_optinteger(L, 3, 1);
	if (setenv(name, value, overwrite) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.unsetenv(name)
DEF_METHOD(unsetenv, L)
{
	const char *name = luaL_checkstring(L, 1);
	if (unsetenv(name) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.pathconf(path, optname)
DEF_METHOD(pathconf, L)
{
	const char *path = luaL_checkstring(L, 1);
	int name = luaL_checkinteger(L, 2);
	errno = 0;
	long value = pathconf(path, name);
	if (value==-1 && errno!=0)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, value);
	return 1;
}

// sys.sysconf(optname)
DEF_METHOD(sysconf, L)
{
	int name = luaL_checkinteger(L, 1);
	errno = 0;
	long value = sysconf(name);
	if (value==-1 && errno!=0)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, value);
	return 1;
}

// sys.getpagesize()
DEF_METHOD(getpagesize, L)
{
	lua_pushinteger(L, getpagesize());
	return 1;
}

// sys.getrusage([who=RUSAGE_SELF])
DEF_METHOD(getrusage, L)
{
	int who = luaL_optinteger(L, 1, RUSAGE_SELF);
	struct rusage usage;
	if (getrusage(who, &usage) == -1)
		return report_errno(L, LRET_NIL);
	double ru_utime = usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec/1000000;
	double ru_stime = usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec/1000000;
	lua_newtable(L);
	lua_pushnumber(L, ru_utime);           lua_setfield(L, -2, "ru_utime");
	lua_pushnumber(L, ru_stime);           lua_setfield(L, -2, "ru_stime");
	lua_pushinteger(L, usage.ru_maxrss);   lua_setfield(L, -2, "ru_maxrss");
	lua_pushinteger(L, usage.ru_ixrss);    lua_setfield(L, -2, "ru_ixrss");
	lua_pushinteger(L, usage.ru_idrss);    lua_setfield(L, -2, "ru_idrss");
	lua_pushinteger(L, usage.ru_isrss);    lua_setfield(L, -2, "ru_isrss");
	lua_pushinteger(L, usage.ru_minflt);   lua_setfield(L, -2, "ru_minflt");
	lua_pushinteger(L, usage.ru_majflt);   lua_setfield(L, -2, "ru_majflt");
	lua_pushinteger(L, usage.ru_nswap);    lua_setfield(L, -2, "ru_nswap");
	lua_pushinteger(L, usage.ru_inblock);  lua_setfield(L, -2, "ru_inblock");
	lua_pushinteger(L, usage.ru_oublock);  lua_setfield(L, -2, "ru_oublock");
	lua_pushinteger(L, usage.ru_msgsnd);   lua_setfield(L, -2, "ru_msgsnd");
	lua_pushinteger(L, usage.ru_msgrcv);   lua_setfield(L, -2, "ru_msgrcv");
	lua_pushinteger(L, usage.ru_nsignals); lua_setfield(L, -2, "ru_nsignals");
	lua_pushinteger(L, usage.ru_nvcsw);    lua_setfield(L, -2, "ru_nvcsw");
	lua_pushinteger(L, usage.ru_nivcsw);   lua_setfield(L, -2, "ru_nivcsw");
	return 1;
}

// sys.getrlimit(resource)
DEF_METHOD(getrlimit, L)
{
	int resource = luaL_checkinteger(L, 1);
	struct rlimit rlim;
	if (getrlimit(resource, &rlim) == -1)
		return report_errno(L, LRET_NIL);
	lua_pushinteger(L, rlim.rlim_cur);
	lua_pushinteger(L, rlim.rlim_max);
	return 2;
}

// sys.setrlimit(resource, softlimit[, hardlimit=-1])
DEF_METHOD(setrlimit, L)
{
	int resource = luaL_checkinteger(L, 1);
	int softlimit = luaL_optinteger(L, 2, -1);
	int hardlimit = luaL_optinteger(L, 3, -1);
	struct rlimit rlim;
	if (getrlimit(resource, &rlim) == -1)
		return report_errno(L, LRET_BOOLEAN);
	if (softlimit >= 0) rlim.rlim_cur = softlimit;
	if (hardlimit >= 0) rlim.rlim_max = hardlimit;
	if (setrlimit(resource, &rlim) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.execlp(file, arg, ...)
DEF_METHOD(execlp, L)
{
	char *arg[256];
	const char *file = luaL_checkstring(L, 1);
	arg[0] = basename((char *)file);
	int index, narg = lua_gettop(L);
	luaL_argcheck(L, narg<256, narg, "too many args");
	for(index=1; index<narg; index++)
	{
		arg[index] = (char *)luaL_checkstring(L, index+1);
	}
	arg[narg] = NULL;
	if (execvp(file, arg) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

#if defined(_AIX) || defined(__hpux) || defined(__sun)
static int daemon(int nochdir, int noclose)
{
	int fd;

	switch (fork()) {
	case -1:
		return (-1);
	case 0:
		break;
	default:
		_exit(0);
	}

	if (setsid() == -1)
		return (-1);

	if (!nochdir)
		chdir("/");

	if (!noclose) {
		int fd = open("/dev/null", O_RDWR);
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close (fd);
	}
	
	return (0);
}
#endif

// sys.daemon([nochdir, noclose])
DEF_METHOD(daemon, L)
{
	int nochdir = luaL_optinteger(L, 1, 1);
	int noclose = luaL_optinteger(L, 2, 0);
	if (daemon(nochdir, noclose) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.kill(pid[, signo])
DEF_METHOD(kill, L)
{
	pid_t pid = luaL_checkinteger(L, 1);
	int signo = luaL_optinteger(L, 2, SIGTERM);
	if (kill(pid, signo) == -1)
		return report_errno(L, LRET_BOOLEAN);
	lua_pushboolean(L, 1);
	return 1;
}

// sys.fork()
DEF_METHOD(fork, L)
{
	pid_t pid = fork();
	if (pid == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, pid);
	return 1;
}

// sys._exit([status=0])
DEF_METHOD(_exit, L)
{
	int status = luaL_optinteger(L, 1, 0);
	_exit(status);
	return 0;
}

// sys.wait()
DEF_METHOD(wait, L)
{
	int status = 0;
	pid_t pid = wait(&status);
	if (pid == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, pid);
	lua_pushinteger(L, status);
	return 2;
}

// sys.waitpid(pid[, options])
DEF_METHOD(waitpid, L)
{
	pid_t pid = (pid_t)luaL_checkinteger(L, 1);
	int options = luaL_optinteger(L, 2, WNOHANG);
	int status = 0;
	pid_t rpid = waitpid(pid, &status, options);
	if (rpid == -1)
		return report_errno(L, LRET_NEGATIVE);
	lua_pushinteger(L, rpid);
	lua_pushinteger(L, status);
	return 2;
}

#define DECL_WSTATFUNC(L, Method, bool_return) \
	static int lsys_##Method(lua_State *L) \
	{ \
		int status = luaL_checkinteger(L, 1); \
		if (bool_return) \
			lua_pushboolean(L, Method(status)); \
		else \
			lua_pushinteger(L, Method(status)); \
		return 1; \
	}
DECL_WSTATFUNC(L, WIFEXITED,    1)
DECL_WSTATFUNC(L, WEXITSTATUS,  0)
DECL_WSTATFUNC(L, WIFSIGNALED,  1)
DECL_WSTATFUNC(L, WTERMSIG,     0)
DECL_WSTATFUNC(L, WCOREDUMP,    1)
DECL_WSTATFUNC(L, WIFSTOPPED,   1)
DECL_WSTATFUNC(L, WSTOPSIG,     0)
DECL_WSTATFUNC(L, WIFCONTINUED, 1)

#define SIGMAX  32
static void sig_handler_init(lua_State *L);
static void sig_handler(int signo);
static int sig_handler_wrap(lua_State *L, int signo, int func_index);
// sys.signal(signo, handler)
DEF_METHOD(signal, L)
{
	int signo = luaL_checkinteger(L, 1);
	luaL_argcheck(L, (signo>0 && signo<SIGMAX && signo!=SIGKILL && signo!=SIGSTOP), 1, "invalid signal number");
	if (lua_isnumber(L, 2))
	{
		long handler = lua_tointeger(L, 2);
		luaL_argcheck(L, handler==(long)SIG_IGN || handler==(long)SIG_DFL, 2, "invalid signal handler");
		sig_handler_wrap(L, signo, 0);
		signal(signo, (sig_t)handler);
		lua_pushboolean(L, 1);
		return 1;
	}
	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (sig_handler_wrap(L, signo, 2))
	{
		if (signal(signo, sig_handler) == SIG_ERR)
			return report_errno(L, LRET_BOOLEAN);
	}
	lua_pushboolean(L, 1);
	return 1;
}

#define REG_METHOD(Method) {#Method, FUNCNAME(Method)}
static struct luaL_Reg syslib[] = {
	REG_METHOD(sleep),
	REG_METHOD(usleep),
	REG_METHOD(nanosleep),
	REG_METHOD(gettimeofday),
#ifdef HAVE_CRYPT
	REG_METHOD(crypt),
#endif
#ifdef HAVE_ICONV
	REG_METHOD(iconv),
#endif
	REG_METHOD(uname),
	REG_METHOD(getlogin),
	REG_METHOD(gethostid),
	REG_METHOD(ctermid),
	REG_METHOD(getenv),
	REG_METHOD(setenv),
	REG_METHOD(unsetenv),
	REG_METHOD(pathconf),
	REG_METHOD(sysconf),
	REG_METHOD(getpagesize),
	REG_METHOD(getrusage),
	REG_METHOD(getrlimit),
	REG_METHOD(setrlimit),
	REG_METHOD(alarm),
	REG_METHOD(pause),
	REG_METHOD(signal),
	REG_METHOD(getpid),
	REG_METHOD(getppid),
#if defined(_XOPEN_SOURCE) && defined(_XOPEN_SOURCE_EXTENDED)
	REG_METHOD(getsid),
	REG_METHOD(getpgid),
#endif
	REG_METHOD(setsid),
	REG_METHOD(setpgid),
	REG_METHOD(getpgrp),
	REG_METHOD(setpgrp),
	REG_METHOD(execlp),
	REG_METHOD(daemon),
	REG_METHOD(kill),
	REG_METHOD(fork),
	REG_METHOD(_exit),
	REG_METHOD(wait),
	REG_METHOD(waitpid),
	REG_METHOD(WIFEXITED),
	REG_METHOD(WEXITSTATUS),
	REG_METHOD(WIFSIGNALED),
	REG_METHOD(WTERMSIG),
	REG_METHOD(WCOREDUMP),
	REG_METHOD(WIFSTOPPED),
	REG_METHOD(WSTOPSIG),
	REG_METHOD(WIFCONTINUED),
	{NULL, NULL}
};

static void lsys_AddMacro(lua_State *L, int t);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_sys(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, syslib);
#else
	luaL_register(L, "posix_sys", syslib);
#endif
	lsys_AddMacro(L, lua_gettop(L));
	sig_handler_init(L);
	return 1;
}

void lsys_AddMacro(lua_State *L, int t)
{
	// for pathconf
	SET_CONSTANT(L, t, _PC_LINK_MAX);
	SET_CONSTANT(L, t, _PC_MAX_CANON);
	SET_CONSTANT(L, t, _PC_MAX_INPUT);
	SET_CONSTANT(L, t, _PC_NAME_MAX);
	SET_CONSTANT(L, t, _PC_PATH_MAX);
	SET_CONSTANT(L, t, _PC_PIPE_BUF);
	SET_CONSTANT(L, t, _PC_CHOWN_RESTRICTED);
	SET_CONSTANT(L, t, _PC_NO_TRUNC);
	SET_CONSTANT(L, t, _PC_VDISABLE);
	// for sysconf
	SET_CONSTANT(L, t, _SC_ARG_MAX);
	SET_CONSTANT(L, t, _SC_CHILD_MAX);
	SET_CONSTANT(L, t, _SC_HOST_NAME_MAX);
	SET_CONSTANT(L, t, _SC_LOGIN_NAME_MAX);
	SET_CONSTANT(L, t, _SC_CLK_TCK);
	SET_CONSTANT(L, t, _SC_OPEN_MAX);
	SET_CONSTANT(L, t, _SC_PAGESIZE);
	SET_CONSTANT(L, t, _SC_RE_DUP_MAX);
	SET_CONSTANT(L, t, _SC_STREAM_MAX);
#ifdef SYMLOOP_MAX
	SET_CONSTANT(L, t, SYMLOOP_MAX);
#endif
	SET_CONSTANT(L, t, _SC_TTY_NAME_MAX);
	SET_CONSTANT(L, t, _SC_TZNAME_MAX);
	SET_CONSTANT(L, t, _SC_VERSION);
	SET_CONSTANT(L, t, _SC_BC_BASE_MAX);
	SET_CONSTANT(L, t, _SC_BC_DIM_MAX);
	SET_CONSTANT(L, t, _SC_BC_SCALE_MAX);
	SET_CONSTANT(L, t, _SC_BC_STRING_MAX);
	SET_CONSTANT(L, t, _SC_COLL_WEIGHTS_MAX);
	SET_CONSTANT(L, t, _SC_EXPR_NEST_MAX);
	SET_CONSTANT(L, t, _SC_LINE_MAX);
	SET_CONSTANT(L, t, _SC_RE_DUP_MAX);
	SET_CONSTANT(L, t, _SC_2_VERSION);
	SET_CONSTANT(L, t, _SC_2_C_DEV);
	SET_CONSTANT(L, t, _SC_2_FORT_DEV);
	SET_CONSTANT(L, t, _SC_2_FORT_RUN);
	SET_CONSTANT(L, t, _SC_2_LOCALEDEF);
	SET_CONSTANT(L, t, _SC_2_SW_DEV);
	SET_CONSTANT(L, t, _SC_PHYS_PAGES);
	SET_CONSTANT(L, t, _SC_AVPHYS_PAGES);
	// for getrusage
	SET_CONSTANT(L, t, RUSAGE_SELF);
	SET_CONSTANT(L, t, RUSAGE_CHILDREN);
	// for getrlimit/setrlimit
	SET_CONSTANT(L, t, RLIMIT_AS);
	SET_CONSTANT(L, t, RLIMIT_CORE);
	SET_CONSTANT(L, t, RLIMIT_CPU);
	SET_CONSTANT(L, t, RLIMIT_DATA);
	SET_CONSTANT(L, t, RLIMIT_FSIZE);
#ifdef RLIMIT_LOCKS
	SET_CONSTANT(L, t, RLIMIT_LOCKS);
#endif
#ifdef RLIMIT_MEMLOCK
	SET_CONSTANT(L, t, RLIMIT_MEMLOCK);
#endif
#ifdef RLIMIT_MSGQUEUE
	SET_CONSTANT(L, t, RLIMIT_MSGQUEUE);
#endif
#ifdef RLIMIT_NICE
	SET_CONSTANT(L, t, RLIMIT_NICE);
#endif
	SET_CONSTANT(L, t, RLIMIT_NOFILE);
#ifdef RLIMIT_NPROC
	SET_CONSTANT(L, t, RLIMIT_NPROC);
#endif
#ifdef RLIMIT_RSS
	SET_CONSTANT(L, t, RLIMIT_RSS);
#endif
#ifdef RLIMIT_RTPRIO
	SET_CONSTANT(L, t, RLIMIT_RTPRIO);
#endif
#ifdef RLIMIT_SIGPENDING
	SET_CONSTANT(L, t, RLIMIT_SIGPENDING);
#endif
	SET_CONSTANT(L, t, RLIMIT_STACK);
	SET_CONSTANT(L, t, RLIMIT_OFILE);
	// for waitpid
	SET_CONSTANT(L, t, WNOHANG);
	SET_CONSTANT(L, t, WUNTRACED);
	SET_CONSTANT(L, t, WCONTINUED);
	// for signal
	SET_CONSTANT(L, t, SIG_DFL);
	SET_CONSTANT(L, t, SIG_IGN);

	SET_CONSTANT(L, t, SIGHUP);
	SET_CONSTANT(L, t, SIGINT);
	SET_CONSTANT(L, t, SIGQUIT);
	SET_CONSTANT(L, t, SIGILL);
	SET_CONSTANT(L, t, SIGTRAP);
	SET_CONSTANT(L, t, SIGABRT);
	SET_CONSTANT(L, t, SIGBUS);
	SET_CONSTANT(L, t, SIGFPE);
	SET_CONSTANT(L, t, SIGUSR1);
	SET_CONSTANT(L, t, SIGSEGV);
	SET_CONSTANT(L, t, SIGUSR2);
	SET_CONSTANT(L, t, SIGPIPE);
	SET_CONSTANT(L, t, SIGALRM);
	SET_CONSTANT(L, t, SIGTERM);
	SET_CONSTANT(L, t, SIGCLD);
	SET_CONSTANT(L, t, SIGCHLD);
	SET_CONSTANT(L, t, SIGCONT);
	SET_CONSTANT(L, t, SIGTSTP);
	SET_CONSTANT(L, t, SIGTTIN);
	SET_CONSTANT(L, t, SIGTTOU);
	SET_CONSTANT(L, t, SIGURG);
	SET_CONSTANT(L, t, SIGXCPU);
	SET_CONSTANT(L, t, SIGXFSZ);
	SET_CONSTANT(L, t, SIGVTALRM);
	SET_CONSTANT(L, t, SIGPROF);
	SET_CONSTANT(L, t, SIGWINCH);
	SET_CONSTANT(L, t, SIGPOLL);
	SET_CONSTANT(L, t, SIGIO);
	SET_CONSTANT(L, t, SIGPWR);
	SET_CONSTANT(L, t, SIGSYS);
}

static lua_State *_sigL;
static int _sig_handler[SIGMAX];

void sig_handler_init(lua_State *L)
{
	int i = 0;
	_sigL = L;
	for(i=0; i<SIGMAX; i++)
	{
		_sig_handler[i] = LUA_REFNIL;
	}
}

void sig_handler(int signo)
{
	sigset_t mask, oldmask;
	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, &oldmask);

	int sig_ref = _sig_handler[signo];
	if (sig_ref == LUA_REFNIL) return;
	lua_rawgeti(_sigL, LUA_REGISTRYINDEX, sig_ref);
	if (lua_isfunction(_sigL, -1))
	{
		lua_pushinteger(_sigL, signo);
		lua_pcall(_sigL, 1, 0, 0);
	}else
	{
		lua_pop(_sigL, 1);
	}
	
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

int sig_handler_wrap(lua_State *L, int signo, int func_index)
{
	assert(_sigL == L);
	int old_ref = _sig_handler[signo];
	if (old_ref != LUA_REFNIL)
		luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
	if (func_index == 0)
	{
		_sig_handler[signo] = LUA_REFNIL;
		return 0;
	}
	lua_pushvalue(L, func_index);
	_sig_handler[signo] = luaL_ref(L, LUA_REGISTRYINDEX);
	return (old_ref == LUA_REFNIL);
}