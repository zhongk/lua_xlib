/**************************************************************************
local errno = require('posix.errno')

errno.E*
errno.errno()
errno.perror([prompt])
errno.strerror(errnum)
**************************************************************************/
#include <string.h>
#include <errno.h>
#include "lua_util.h"

static int l_errno(lua_State *L)
{
	lua_pushinteger(L, errno);
	return 1;
}

static int l_perror(lua_State *L)
{
	const char *s = luaL_optstring(L, 1, NULL);
	perror(s);
	return 0;
}

static int l_strerror(lua_State *L)
{
	int errnum = luaL_optinteger(L, 1, errno);
	lua_pushstring(L, strerror(errnum));
	return 1;
}

static struct luaL_Reg errnolib[] = {
	{"errno", l_errno},
	{"perror", l_perror},
	{"strerror", l_strerror},
	{NULL, NULL}
};

static void lerrno_AddMacro(lua_State *L, int t);

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_posix_errno(lua_State *L)
{
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, errnolib);
#else
	luaL_register(L, "posix_errno", errnolib);
#endif
	lerrno_AddMacro(L, lua_gettop(L));
	return 1;
}

void lerrno_AddMacro(lua_State *L, int t)
{
	SET_CONSTANT(L, t, E2BIG);
	SET_CONSTANT(L, t, EACCES);
	SET_CONSTANT(L, t, EADDRINUSE);
	SET_CONSTANT(L, t, EADDRNOTAVAIL);
	SET_CONSTANT(L, t, EADV);
	SET_CONSTANT(L, t, EAFNOSUPPORT);
	SET_CONSTANT(L, t, EAGAIN);
	SET_CONSTANT(L, t, EALREADY);
	SET_CONSTANT(L, t, EBADE);
	SET_CONSTANT(L, t, EBADF);
	SET_CONSTANT(L, t, EBADFD);
	SET_CONSTANT(L, t, EBADR);
	SET_CONSTANT(L, t, EBADMSG);
	SET_CONSTANT(L, t, EBADRQC);
	SET_CONSTANT(L, t, EBADSLT);
	SET_CONSTANT(L, t, EBFONT);
	SET_CONSTANT(L, t, EBUSY);
	SET_CONSTANT(L, t, ECANCELED);
	SET_CONSTANT(L, t, ECHILD);
	SET_CONSTANT(L, t, ECHRNG);
	SET_CONSTANT(L, t, ECOMM);
	SET_CONSTANT(L, t, ECONNABORTED);
	SET_CONSTANT(L, t, ECONNREFUSED);
	SET_CONSTANT(L, t, ECONNRESET);
	SET_CONSTANT(L, t, EDEADLK);
	SET_CONSTANT(L, t, EDEADLOCK);
	SET_CONSTANT(L, t, EDESTADDRREQ);
	SET_CONSTANT(L, t, EDOM);
	SET_CONSTANT(L, t, EDOTDOT);
	SET_CONSTANT(L, t, EDQUOT);
	SET_CONSTANT(L, t, EEXIST);
	SET_CONSTANT(L, t, EFAULT);
	SET_CONSTANT(L, t, EFBIG);
	SET_CONSTANT(L, t, EHOSTDOWN);
	SET_CONSTANT(L, t, EHOSTUNREACH);
	SET_CONSTANT(L, t, EIDRM);
	SET_CONSTANT(L, t, EILSEQ);
	SET_CONSTANT(L, t, EINPROGRESS);
	SET_CONSTANT(L, t, EINTR);
	SET_CONSTANT(L, t, EINVAL);
	SET_CONSTANT(L, t, EIO);
	SET_CONSTANT(L, t, EISCONN);
	SET_CONSTANT(L, t, EISDIR);
#ifdef EISNAM
	SET_CONSTANT(L, t, EISNAM);
#endif
	SET_CONSTANT(L, t, EL2HLT);
	SET_CONSTANT(L, t, EL2NSYNC);
	SET_CONSTANT(L, t, EL3HLT);
	SET_CONSTANT(L, t, EL3RST);
	SET_CONSTANT(L, t, ELIBACC);
	SET_CONSTANT(L, t, ELIBBAD);
	SET_CONSTANT(L, t, ELIBEXEC);
	SET_CONSTANT(L, t, ELIBMAX);
	SET_CONSTANT(L, t, ELIBSCN);
	SET_CONSTANT(L, t, ELNRNG);
	SET_CONSTANT(L, t, ELOOP);
	SET_CONSTANT(L, t, EMFILE);
	SET_CONSTANT(L, t, EMLINK);
	SET_CONSTANT(L, t, EMSGSIZE);
	SET_CONSTANT(L, t, EMULTIHOP);
	SET_CONSTANT(L, t, ENAMETOOLONG);
#ifdef ENAVAIL
	SET_CONSTANT(L, t, ENAVAIL);
#endif
	SET_CONSTANT(L, t, ENETDOWN);
	SET_CONSTANT(L, t, ENETRESET);
	SET_CONSTANT(L, t, ENETUNREACH);
	SET_CONSTANT(L, t, ENFILE);
	SET_CONSTANT(L, t, ENOANO);
	SET_CONSTANT(L, t, ENOBUFS);
	SET_CONSTANT(L, t, ENOCSI);
	SET_CONSTANT(L, t, ENODATA);
	SET_CONSTANT(L, t, ENODEV);
	SET_CONSTANT(L, t, ENOENT);
	SET_CONSTANT(L, t, ENOEXEC);
#ifdef ENOKEY
	SET_CONSTANT(L, t, ENOKEY);
#endif
	SET_CONSTANT(L, t, ENOLCK);
	SET_CONSTANT(L, t, ENOLINK);
	SET_CONSTANT(L, t, ENOMEM);
	SET_CONSTANT(L, t, ENOMSG);
	SET_CONSTANT(L, t, ENONET);
	SET_CONSTANT(L, t, ENOPKG);
	SET_CONSTANT(L, t, ENOPROTOOPT);
	SET_CONSTANT(L, t, ENOSPC);
	SET_CONSTANT(L, t, ENOSR);
	SET_CONSTANT(L, t, ENOSTR);
	SET_CONSTANT(L, t, ENOSYS);
	SET_CONSTANT(L, t, ENOTBLK);
	SET_CONSTANT(L, t, ENOTCONN);
	SET_CONSTANT(L, t, ENOTDIR);
	SET_CONSTANT(L, t, ENOTEMPTY);
#ifdef ENOTNAM
	SET_CONSTANT(L, t, ENOTNAM);
#endif
	SET_CONSTANT(L, t, ENOTSOCK);
	SET_CONSTANT(L, t, ENOTSUP);
	SET_CONSTANT(L, t, ENOTTY);
	SET_CONSTANT(L, t, ENOTUNIQ);
	SET_CONSTANT(L, t, ENXIO);
	SET_CONSTANT(L, t, EOPNOTSUPP);
	SET_CONSTANT(L, t, EOVERFLOW);
	SET_CONSTANT(L, t, EPERM);
	SET_CONSTANT(L, t, EPFNOSUPPORT);
	SET_CONSTANT(L, t, EPIPE);
	SET_CONSTANT(L, t, EPROTO);
	SET_CONSTANT(L, t, EPROTONOSUPPORT);
	SET_CONSTANT(L, t, EPROTOTYPE);
	SET_CONSTANT(L, t, ERANGE);
	SET_CONSTANT(L, t, EREMCHG);
	SET_CONSTANT(L, t, EREMOTE);
#ifdef EREMOTEIO
	SET_CONSTANT(L, t, EREMOTEIO);
#endif
#ifdef ERESTART
	SET_CONSTANT(L, t, ERESTART);
#endif
	SET_CONSTANT(L, t, EROFS);
	SET_CONSTANT(L, t, ESHUTDOWN);
	SET_CONSTANT(L, t, ESPIPE);
	SET_CONSTANT(L, t, ESOCKTNOSUPPORT);
	SET_CONSTANT(L, t, ESRCH);
	SET_CONSTANT(L, t, ESRMNT);
	SET_CONSTANT(L, t, ESTALE);
	SET_CONSTANT(L, t, ESTRPIPE);
	SET_CONSTANT(L, t, ETIME);
	SET_CONSTANT(L, t, ETIMEDOUT);
	SET_CONSTANT(L, t, ETOOMANYREFS);
	SET_CONSTANT(L, t, ETXTBSY);
#ifdef EUCLEAN
	SET_CONSTANT(L, t, EUCLEAN);
#endif
	SET_CONSTANT(L, t, EUNATCH);
	SET_CONSTANT(L, t, EUSERS);
	SET_CONSTANT(L, t, EWOULDBLOCK);
	SET_CONSTANT(L, t, EXDEV);
	SET_CONSTANT(L, t, EXFULL);
}