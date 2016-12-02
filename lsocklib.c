/**************************************************************************
local socket = require('socket')

socket.AF_*
socket.SOCK_*
socket.IPPROTO_*
socket.MSG_*
socket.SOL_*
socket.IP_*
socket.IPTOS_*
socket.IPV6_*
socket.TCP_*
socket.SHUT_*
socket.POLL_*

socket.socket([family=AF_INET[, type=SOCK_STREAM[, proto=0]]]) return sockobj
socket.socketpair([family[, type[, proto]]]) return sockobj[2]
socket.PollSelector() return poller object

socket.gethostname() return hostname
socket.gethostbyname(host_name[, family=AF_INET]) return ip_addr
socket.gethostbyaddr(ip_addr) return host_name
socket.getprotobyname(name) return protocol number
socket.getprotobynumber(proto) return protocol name
socket.getservbyname(name) return service port
socket.getservbyport(port) return service name
socket.ntohl(x)
socket.ntohs(x)
socket.htonl(x)
socket.htons(x)
socket.address(hostname, port) return {hostname, port}

sockobj:accept() return sockobj, address(ip_addr, port)
sockobj:bind(port[, ip_addr='']) or sockobj:bind(address(ip_addr, port)) or sockobj:bind(unix_path)
sockobj:close()
sockobj:connect(hostname, port) or sockobj:connect(address(ip_addr, port)) or sockobj:connect(unix_path)
sockobj:fileno()
sockobj:getpeername() return address(ip_addr, port) or unix_path
sockobj:getsockname() return address(ip_addr, port) or unix_path
sockobj:getsockopt(level, optname) return optvalue
sockobj:info() return table{fileno,family,type,proto}
sockobj:isblocking() return bool
sockobj:listen([backlog=50])
sockobj:readline([size=-1,] timeout)
sockobj:recv(bufsize[, flags=0])
sockobj:recvfrom(bufsize[, flags=0]) return bytes, address(ip_addr, port)
sockobj:recvmsg(bufsize[, ancbufsize=0[, flags=0]])
	return bytes, ancdata(cmsg_level, cmsg_type, cmsg_data), msg_flags, address(ip_addr, port)
sockobj:send(bytes[, flags=0])
sockobj:sendto(bytes, address(ip_addr, port)) or sockobj:sendto(bytes, flags, address(ip_addr, port))
sockobj:sendmsg(bytes, [ancdata(cmsg_level, cmsg_type, cmsg_data)[, flags=0[, address(ip_addr, port)]]])
sockobj:setblocking(flag)
sockobj:setsockopt(level, optname, value)
sockobj:shutdown(how)
sockobj:poll([events=POLLIN,] timeout) return int(-1:error, 0:timeout, >0:revents)

poller:register(sockfd, events)
poller:unregister(sockfd)
poller:modify(sockfd, events)
poller:select([timeout=-1]) return iterator(sockfd, revents)
poller:clear()
**************************************************************************/
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#ifdef USE_EPOLL
#include <sys/epoll.h>
#ifndef FD_SETSIZE
#define FD_SETSIZE 4096
#endif
#endif
#include <signal.h>
#include "lua_util.h"

#define LUASOCKET_NAME "socket"
#ifndef USE_EPOLL
#define LUAPOLLER_NAME "selector.poll"
#else
#define LUAPOLLER_NAME "selector.epoll"
#endif

#ifdef __hpux
#undef  socklen_t
#define socklen_t	int
#endif
#ifndef SOL_TCP
#define SOL_TCP     IPPROTO_TCP
#endif
#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)0xffffffff)
#endif
#define MAX_BUFSIZE 65536

typedef union
{
	struct sockaddr sa;
	struct sockaddr_in in;
	struct sockaddr_un un;
	struct sockaddr_in6 in6;
} sock_addr_t;

typedef struct
{
	int sockfd;
	int family;
	int type;
	int proto;
} sockobj;

typedef struct
{
	int len, size;
#ifndef USE_EPOLL
	struct pollfd *fds;
#else
	int epfd;
	struct epoll_event *events;
#endif
} pollobj;

#define lsock_newobj(L, Fd, Family, Type, Proto) \
	do { \
		sockobj* o = (sockobj*)lua_newuserdata(L, sizeof(sockobj)); \
		lua_pushvalue(L, -1); \
		o->sockfd = Fd; \
		o->family = (Family); \
		o->type = (Type); \
		o->proto = (Proto); \
		luaL_getmetatable(L, LUASOCKET_NAME); \
		lua_setmetatable(L, -2); \
	} while(0)
		
// socket.socket(family=AF_INET, type=SOCK_STREAM, proto=0)
static int lsock_socket(lua_State *L)
{
	int family = luaL_optinteger(L, 1, AF_INET);
	luaL_argcheck(L, (family==AF_INET || family==AF_INET6 || family==AF_UNIX), 1, "family in AF_INET/AF_INET6/AF_UNIX");
	int type = luaL_optinteger(L, 2, SOCK_STREAM);
	int proto = luaL_optinteger(L, 3, 0);
	int fd = socket(family, type, proto);
	if (fd == -1)
		return report_errmsg(L, LRET_NIL, errno, "socket()");
	
	lsock_newobj(L, fd, family, type, proto);
	return 1;
}

// socket.socketpair(family=AF_INET, type=SOCK_STREAM, proto=0)
static int lsock_socketpair(lua_State *L)
{
	int fds[2];
	int family = luaL_optinteger(L, 1, AF_INET);
	luaL_argcheck(L, (family==AF_INET || family==AF_UNIX), 1, "family in AF_INET/AF_UNIX");
	int type = luaL_optinteger(L, 2, SOCK_STREAM);
	int proto = luaL_optinteger(L, 3, 0);
	if (socketpair(family, type, proto, fds) == -1)
		return report_errmsg(L, LRET_NIL, errno, "socketpair()");
	
	lua_newtable(L);
	lsock_newobj(L, fds[0], family, type, proto);
	lua_rawseti(L, -2, 1);
	lsock_newobj(L, fds[1], family, type, proto);
	lua_rawseti(L, -2, 2);
	return 1;
}

// socket.gethostname()
static int lsock_gethostname(lua_State *L)
{
	char hostname[HOST_NAME_MAX];
	if (gethostname(hostname, sizeof(hostname)) == -1)
		return report_errmsg(L, LRET_NIL, errno, "gethostname()");
	lua_pushstring(L, hostname);
	return 1;
}

// socket.gethostbyname(host_name[, family=AF_INET])
static int lsock_gethostbyname(lua_State *L)
{
	const char *hostname = luaL_checkstring(L, 1);
	int family = luaL_optinteger(L, 2, AF_INET);
	luaL_argcheck(L, (family==AF_INET || family==AF_INET6), 2, "family in AF_INET/AF_INET6");
	
	struct addrinfo hint, *addrinfo;
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = family;
	hint.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(hostname, NULL, &hint, &addrinfo);
	if (ret != 0)
		return report_errmsg(L, LRET_NIL, 0, gai_strerror(ret));
	
	char ipaddr[INET6_ADDRSTRLEN];
	void *p_addr = (family==AF_INET) ?
		(void *)&(((struct sockaddr_in *)(addrinfo->ai_addr))->sin_addr) :
		(void *)&(((struct sockaddr_in6 *)(addrinfo->ai_addr))->sin6_addr);
	lua_pushstring(L, inet_ntop(family, p_addr, ipaddr, sizeof(ipaddr)));
	freeaddrinfo(addrinfo);
	return 1;
}

// socket.gethostbyaddr(ip_addr)
static int lsock_gethostbyaddr(lua_State *L)
{
	const char *ipaddr = luaL_checkstring(L, 1);
	int af = (inet_addr(ipaddr)!=INADDR_NONE) ? AF_INET : AF_INET6;
	
	char addr[sizeof(struct in6_addr)];
	if (inet_pton(af, ipaddr, addr) <= 0)
		return report_errmsg(L, LRET_NIL, errno, "inet_pton() : illegal IP address string");
	struct hostent *lphost = gethostbyaddr(addr, (af==AF_INET ? sizeof(struct in_addr) : sizeof(struct in6_addr)), af);
	if (!lphost)
		return report_errmsg(L, LRET_NIL, 0, hstrerror(h_errno));
	
	lua_pushstring(L, lphost->h_name);
	return 1;
}

// socket.getprotobyname(name)
static int lsock_getprotobyname(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	struct protoent *pproto = getprotobyname(name);
	if (!pproto)
		return report_errmsg(L, LRET_NEGATIVE, errno, "getprotobyname()");
	lua_pushinteger(L, pproto->p_proto);
	return 1;
}

// socket.getprotobynumber(proto)
static int lsock_getprotobynumber(lua_State *L)
{
	int proto = luaL_checkinteger(L, 1);
	struct protoent *pproto = getprotobynumber(proto);
	if (!pproto)
		return report_errmsg(L, LRET_NEGATIVE, errno, "getprotobynumber()");
	lua_pushstring(L, pproto->p_name);
	return 1;
}

// socket.getservbyname(name)
static int lsock_getservbyname(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	struct servent *pserv = getservbyname(name, NULL);
	if (!pserv)
		return report_errmsg(L, LRET_NEGATIVE, errno, "getservbyname()");
	lua_pushinteger(L, pserv->s_port);
	lua_pushstring(L, pserv->s_proto);
	return 2;
}

// socket.getservbyport(port)
static int lsock_getservbyport(lua_State *L)
{
	int port = luaL_checkinteger(L, 1);
	struct servent *pserv = getservbyport(port, NULL);
	if (!pserv)
		return report_errmsg(L, LRET_NEGATIVE, errno, "getservbyport()");
	lua_pushstring(L, pserv->s_name);
	lua_pushstring(L, pserv->s_proto);
	return 2;
}

// socket.ntohl(x)
static int lsock_ntohl(lua_State *L)
{
	uint32_t x = (uint32_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, ntohl(x));
	return 1;
}

// socket.ntohs(x)
static int lsock_ntohs(lua_State *L)
{
	uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, ntohs(x));
	return 1;
}

// socket.htonl(x)
static int lsock_htonl(lua_State *L)
{
	uint32_t x = (uint32_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, htonl(x));
	return 1;
}

// socket.htons(x)
static int lsock_htons(lua_State *L)
{
	uint16_t x = (uint16_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, htons(x));
	return 1;
}


// socket.address(hostname, port)
static int lsock_address(lua_State *L)
{
	const char *hostname = luaL_optstring(L, 1, "");
	int port = luaL_checkinteger(L, 2);
	lua_newtable(L);
	lua_pushstring(L, hostname);
	lua_rawseti(L, -2, 1);
	lua_pushinteger(L, port);
	lua_rawseti(L, -2, 2);
	return 1;
}

static int gen_sockaddr(sock_addr_t *address, int family, const char *addr, int port)
{
	if (family == AF_INET)
	{
		address->in.sin_family = AF_INET;
		if (!addr || !*addr || (addr[0]=='*' && addr[1]=='\0'))
			address->in.sin_addr.s_addr = htonl(INADDR_ANY);
		else
		{
			address->in.sin_addr.s_addr = inet_addr(addr);
			if (address->in.sin_addr.s_addr == INADDR_NONE)
			{
				struct hostent* lphost = gethostbyname(addr);
				if (lphost != NULL)
					address->in.sin_addr.s_addr = ((struct in_addr*)lphost->h_addr)->s_addr;
				else
				{
					errno = EINVAL;
					return 0;
				}
			}
		}
		address->in.sin_port = htons(port);
		return sizeof(address->in);
	}else
	if (family == AF_INET6)
	{
		address->in6.sin6_family = AF_INET6;
		if (!addr || !*addr)
			address->in6.sin6_addr = in6addr_any;
		else
		{
			if (inet_pton(AF_INET6, addr, &address->in6.sin6_addr) <= 0)
			{
				struct addrinfo hint, *addrinfo;
				memset(&hint, 0, sizeof(hint));
				hint.ai_family = family;
				hint.ai_socktype = SOCK_STREAM;
				int ret = getaddrinfo(addr, NULL, &hint, &addrinfo);
				if (ret == 0)
				{
				//	char ipaddr[INET6_ADDRSTRLEN];
					address->in6.sin6_addr = ((struct sockaddr_in6 *)(addrinfo->ai_addr))->sin6_addr;
					freeaddrinfo(addrinfo);
				}else
				{
					errno = EINVAL;
					return 0;
				}
			}
		}
		address->in6.sin6_port = htons(port);
		return sizeof(address->in6);
	}else
	if (family == AF_UNIX)
	{
		address->un.sun_family = AF_UNIX;
		strcpy(address->un.sun_path, addr);
		return sizeof(address->un);
	}
	
	return -1;
}

static int lsock_pushaddr(lua_State* L, const sock_addr_t *address)
{
	if (address->sa.sa_family == AF_INET)
	{
		lua_newtable(L);
		lua_pushstring(L, inet_ntoa(address->in.sin_addr));
		lua_rawseti(L, -2, 1);
		lua_pushinteger(L, ntohs(address->in.sin_port));
		lua_rawseti(L, -2, 2);
	}else
	if (address->sa.sa_family == AF_INET6)
	{
		char ipaddr[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &address->in6.sin6_addr, ipaddr, sizeof(ipaddr));
		lua_newtable(L);
		lua_pushstring(L, ipaddr);
		lua_rawseti(L, -2, 1);
		lua_pushinteger(L, ntohs(address->in6.sin6_port));
		lua_rawseti(L, -2, 2);
	}else
	if (address->sa.sa_family == AF_UNIX)
	{
		lua_pushstring(L, address->un.sun_path);
	}else
		luaL_error(L, "unsupport socket family %d", address->sa.sa_family);
	return 1;
}

static sockobj *lsock_checkobj(lua_State *L, int index, int check_fd)
{
	sockobj *sobj = (sockobj *)luaL_checkudata(L, index, LUASOCKET_NAME);
	luaL_argcheck(L, sobj != NULL, index, "userdata socket expected");
	if (check_fd)
		luaL_argcheck(L, sobj->sockfd > 0, index, "socket closed");
	return sobj;
}
	
// sockobj:accept()
static int lsock_accept(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	sock_addr_t address;
	socklen_t address_len = sizeof(address);
	int client = accept(sobj->sockfd, (struct sockaddr*)&address, &address_len);
	if (client == -1)
		return report_errmsg(L, LRET_NIL, errno, "accept()");
	
	lsock_newobj(L, client, sobj->family, sobj->type, sobj->proto);
	lsock_pushaddr(L, &address);
	return 2;
}

// sockobj:bind(port[, ip_addr])
static int lsock_bind(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	sock_addr_t address;
	socklen_t address_len = -1;
	if (sobj->family == AF_INET || sobj->family == AF_INET6)
	{
		const char *addr;
		int port;
		if (!lua_istable(L, 2))
		{
			port = luaL_checkinteger(L, 2);
			addr = luaL_optstring(L, 3, "");
		}else
		{
			lua_rawgeti(L, 2, 1);
			addr = luaL_optstring(L, -1, "");
			lua_pop(L, 1);
			lua_rawgeti(L, 2, 2);
			port = luaL_checkinteger(L, -1);
			lua_pop(L, 1);
		}
		address_len = (socklen_t)gen_sockaddr(&address, sobj->family, addr, port);
	}else
	if (sobj->family == AF_UNIX)
	{
		const char *path = luaL_checkstring(L, 3);
		address_len = (socklen_t)gen_sockaddr(&address, AF_UNIX, path, 0);
	}
	if (address_len <= 0)
		return report_errmsg(L, LRET_BOOLEAN, 0, "bind() : unknown address or unsupport socket family");
	if (bind(sobj->sockfd, (struct sockaddr*)&address, address_len) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "bind()");
	
	lua_pushboolean(L, 1);
	return 1;
}

// sockobj:close()
static int lsock_close(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 0);
	if (sobj->sockfd > 0)
	{
		close(sobj->sockfd);
		sobj->sockfd = -1;
	}
	return 0;
}

// sockobj:closed()
static int lsock_closed(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 0);
	lua_pushboolean(L, (sobj->sockfd == -1));
	return 1;
}

// sockobj:connect(hostname, port)
static int lsock_connect(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	sock_addr_t address;
	socklen_t address_len = 0;
	if (sobj->family == AF_INET || sobj->family == AF_INET6)
	{
		const char *addr;
		int port;
		if (!lua_istable(L, 2))
		{
			addr = luaL_checkstring(L, 2);
			port = luaL_checkinteger(L, 3);
		}else
		{
			lua_rawgeti(L, 2, 1);
			addr = luaL_checkstring(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, 2, 2);
			port = luaL_checkinteger(L, -1);
			lua_pop(L, 1);
		}
		address_len = (socklen_t)gen_sockaddr(&address, sobj->family, addr, port);
	}else
	if (sobj->family == AF_UNIX)
	{
		const char *path = luaL_checkstring(L, 2);
		address_len = (socklen_t)gen_sockaddr(&address, AF_UNIX, path, 0);
	}
	if (address_len <= 0)
		return report_errmsg(L, LRET_BOOLEAN, 0, "bind() : unknown address or unsupport socket family");
	if (connect(sobj->sockfd, (struct sockaddr*)&address, address_len) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "connect()");
	
	lua_pushboolean(L, 1);
	return 1;
}

// sockobj:fileno()
static int lsock_fileno(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 0);
	lua_pushinteger(L, sobj->sockfd);
	return 1;
}

// sockobj:info() return table{fileno,family,type,proto}
static int lsock_info(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 0);
	
	lua_newtable(L);
	lua_pushinteger(L, sobj->sockfd);
	lua_setfield(L, -2, "fileno");
	lua_pushinteger(L, sobj->family);
	lua_setfield(L, -2, "family");
	lua_pushinteger(L, sobj->type);
	lua_setfield(L, -2, "type");
	lua_pushinteger(L, sobj->proto);
	lua_setfield(L, -2, "proto");
	return 1;
}

// sockobj:getpeername()
static int lsock_getpeername(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	sock_addr_t name;
	socklen_t namelen = sizeof(name);
	if (getpeername(sobj->sockfd, (struct sockaddr*)&name, &namelen) == -1)
		return report_errmsg(L, LRET_NIL, errno, "getpeername()");
	
	lsock_pushaddr(L, &name);
	return 1;
}

// sockobj:getsockname()
static int lsock_getsockname(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	sock_addr_t name;
	socklen_t namelen = sizeof(name);
	if (getsockname(sobj->sockfd, (struct sockaddr*)&name, &namelen) == -1)
		return report_errmsg(L, LRET_NIL, errno, "getsockname()");
	
	lsock_pushaddr(L, &name);
	return 1;
}

// sockobj:isblocking()
static int lsock_isblocking(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	
	int flags = fcntl(sobj->sockfd, F_GETFL);
	int nonblock = flags & O_NONBLOCK;
	lua_pushboolean(L, nonblock ? 0 : 1);
	return 1;
}

// sockobj:listen([backlog=50])
static int lsock_listen(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int backlog = luaL_optinteger(L, 2, 50);

	if (listen(sobj->sockfd, backlog) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "listen()");
	
	lua_pushboolean(L, 1);
	return 1;
}

// sockobj:poll([events=POLLIN,] timeout)
static int lsock_poll(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int events = POLLIN, timeout_index = 2;
	if (!lua_isnoneornil(L, 3))
	{
		events = luaL_checkinteger(L, 2);
		timeout_index++;
	}
	double to = (double)luaL_checknumber(L, timeout_index);
	int timeout = (to < 0.0) ? -1 : (int)(to*1000);
	
	struct pollfd fds = { sobj->sockfd, events, 0 };
	int ret = poll(&fds, 1, timeout);
	if (ret < 0)
		return report_errmsg(L, LRET_NEGATIVE, errno, "poll()");
	
	lua_pushinteger(L, ret ? fds.revents : 0);
	return 1;
}

static int time_add(struct timeval *endtime, double to)
{
	int timeout = (to < 0.0) ? -1 : (int)(to*1000);
	if (timeout > 0)
	{
		endtime->tv_sec += timeout/1000;
		endtime->tv_usec += timeout%1000*1000000;
		if (endtime->tv_usec > 1000000)
		{
			++endtime->tv_sec;
			endtime->tv_usec %= 1000000;
		}
	}
	return timeout;
}
static int time_left(const struct timeval *endtime)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	int left = ((endtime->tv_sec-now.tv_sec)*1000000 + (endtime->tv_usec-now.tv_usec))/1000;
	return (left <= 0 ? 0 : left);
}

// sockobj:readline([size=-1,] timeout)
static int lsock_readline(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int size = -1, timeout_index = 2;
	if (!lua_isnoneornil(L, 3))
	{
		size = luaL_checkinteger(L, 2);
		timeout_index++;
	}
	double to = (double)luaL_checknumber(L, timeout_index);
	struct timeval endtime;
	gettimeofday(&endtime, NULL);
	int timeout = time_add(&endtime, to);
	
	int ret = 0, nresult = 0;
	struct pollfd fds = { sobj->sockfd, POLLIN, 0 };
	while(1)
	{
		if (timeout >= 0)
		{
			fds.revents = 0;
			ret = poll(&fds, 1, timeout);
			if (ret <= 0) break;
		}
		
		char buffer[2048];
		int nread = size < sizeof(buffer) ? size : sizeof(buffer);
		int len = recv(sobj->sockfd, buffer, nread, MSG_PEEK);
		if (len <= 0)
		{
		retry:
			if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)
			{
				if (timeout > 0) timeout = time_left(&endtime);
				continue;
			}
			ret = len ? -1 : -2;
			break;
		}
		
		for(nread = 0; nread < len; nread++)
		{
			if (buffer[nread]=='\n')
			{
				++nread;
				break;
			}
		}
		len = recv(sobj->sockfd, buffer, nread, 0);
		if (len <= 0) goto retry;
		lua_pushlstring(L, buffer, len);
		nresult++;
		
		if (size > 0) size -= len;
		if (buffer[len-1]=='\n' || size==0) break;
		if (timeout > 0) timeout = time_left(&endtime);
	}
	
	if (!nresult)
	{
		switch(ret)
		{
		case 0  :
			return report_errmsg(L, LRET_EMPTYSTR, EAGAIN, "poll()");
		case -1 :
			return report_errmsg(L, LRET_NIL, errno, "recv()");
		case -2 :
			return report_errmsg(L, LRET_NIL, 0, "recv() : connection closed by peer");
		}
	}
	lua_concat(L, nresult);
	return 1;
}

// sockobj:recv(bufsize[, flags=0])
static int lsock_recv(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	size_t bufsize = (size_t)luaL_checkinteger(L, 2);
	luaL_argcheck(L, bufsize<=MAX_BUFSIZE, 2, "value has exceed limit 65536");
	int flags = luaL_optinteger(L, 3, 0);
	
	char buffer[MAX_BUFSIZE];
	int len = recv(sobj->sockfd, buffer, bufsize, flags);
	if (len == 0)
		return report_errmsg(L, LRET_NIL, 0, "recv() : connection closed by peer");
	if (len < 0)
	{
		if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)
		{
			return report_errmsg(L, LRET_EMPTYSTR, errno, "recv()");
		}
		return report_errmsg(L, LRET_NIL, errno, "recv()");
	}
	
	lua_pushlstring(L, buffer, len);
	return 1;
}

// sockobj:recvfrom(bufsize[, flags=0])
static int lsock_recvfrom(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	size_t bufsize = (size_t)luaL_checkinteger(L, 2);
	luaL_argcheck(L, bufsize<=MAX_BUFSIZE, 2, "value has exceed limit 65536");
	int flags = luaL_optinteger(L, 3, 0);
	
	char bytes[MAX_BUFSIZE];
	sock_addr_t address;
	socklen_t address_len = sizeof(address);
	int len = recvfrom(sobj->sockfd, bytes, bufsize, flags, (struct sockaddr*)&address, &address_len);
	if (len == 0)
		return report_errmsg(L, LRET_NIL, 0, "recvfrom() : connection closed by peer");
	if (len < 0)
	{
		if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)
		{
			return report_errmsg(L, LRET_EMPTYSTR, errno, "recvfrom()");
		}
		return report_errmsg(L, LRET_NIL, errno, "recvfrom()");
	}
	
	lua_pushlstring(L, bytes, len);
	lsock_pushaddr(L, &address);
	return 2;
}

// sockobj:send(bytes[, flags=0])
static int lsock_send(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	size_t size = 0;
	const char *bytes = luaL_checklstring(L, 2, &size);
	int flags = luaL_optinteger(L, 3, 0);

	int len = send(sobj->sockfd, bytes, size, flags);
	if (len < 0)
	{
		if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)
		{
			lua_pushinteger(L, 0);
			lua_pushstring(L, strerror(errno));
			lua_pushinteger(L, errno);
			return 3;
		}
		return report_errmsg(L, LRET_NEGATIVE, errno, "send()");
	}

	lua_pushinteger(L, len);
	return 1;
}

// sockobj:sendto(bytes, [flags=0,] address(ip_addr, port))
static int lsock_sendto(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	size_t size = 0;
	const char *bytes = luaL_checklstring(L, 2, &size);
	int flags = 0, addr_index = 3;
	if (lua_isnumber(L, 3))
	{
		flags = lua_tointeger(L, 3);
		addr_index++;
	}
	luaL_argcheck(L, lua_istable(L, addr_index), addr_index, "table{addr,port} expected");
	lua_rawgeti(L, addr_index, 1);
	const char *addr = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	lua_rawgeti(L, addr_index, 2);
	int port = luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	
	sock_addr_t address;
	socklen_t address_len = (socklen_t)gen_sockaddr(&address, sobj->family, addr, port);
	if (address_len <= 0)
		return report_errmsg(L, LRET_NEGATIVE, 0, "bind() : unknown address or unsupport socket family");
	int len = sendto(sobj->sockfd, bytes, size, flags, (struct sockaddr *)&address, address_len);
	if (len < 0)
	{
		if (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)
		{
			lua_pushinteger(L, 0);
			lua_pushstring(L, strerror(errno));
			lua_pushinteger(L, errno);
			return 3;
		}
		return report_errmsg(L, LRET_NEGATIVE, errno, "sendto()");
	}

	lua_pushinteger(L, len);
	return 1;
}

// sockobj:setblocking(flag)
static int lsock_setblocking(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	luaL_argcheck(L, lua_isboolean(L, 2), 2, "boolean expected");
	int blocking = lua_toboolean(L, 2);
	
	int flags = fcntl(sobj->sockfd, F_GETFL);
	int nonblock = flags & O_NONBLOCK;
	if ((nonblock && !blocking) || (!nonblock && blocking)) return 0;
	if (!blocking)
		fcntl(sobj->sockfd, F_SETFL, flags | O_NONBLOCK);
	else
		fcntl(sobj->sockfd, F_SETFL, flags & ~O_NONBLOCK);
	return 0;
}

// sockobj:shutdown(how)
static int lsock_shutdown(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int how = luaL_checkinteger(L, 2);
	luaL_argcheck(L, how>=0 && how<=2, 2, "value in SHUT_RD/SHUT_WR/SHUT_RDWR");
	
	if (shutdown(sobj->sockfd, how) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "shutdown()");
	
	lua_pushboolean(L, 1);
	return 1;
}

// socket.PollSelector()
static int lsock_PollSelector(lua_State *L)
{
	int size = luaL_optinteger(L, 1, FD_SETSIZE);
	pollobj* poller = (pollobj*)lua_newuserdata(L, sizeof(pollobj));
	lua_pushvalue(L, -1);
	poller->len = 0;
	poller->size = 64;
#ifndef USE_EPOLL
	poller->fds = (struct pollfd *)calloc(poller->size, sizeof(struct pollfd));
#else
	poller->epfd = epoll_create(size);
	if (poller->epfd == -1)
		return report_errmsg(L, LRET_NIL, errno, "epoll_create()");
	poller->events = (struct epoll_event *)calloc(poller->size, sizeof(struct epoll_event));
#endif
	luaL_getmetatable(L, LUAPOLLER_NAME);
	lua_setmetatable(L, -2);
	return 1;
}

static pollobj *lpoll_checkobj(lua_State *L, int index)
{
	pollobj *poller = (pollobj *)luaL_checkudata(L, index, LUAPOLLER_NAME);
	luaL_argcheck(L, poller != NULL, index, "userdata PollSelector expected");
	return poller;
}

static int lpoll_gc(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
#ifndef USE_EPOLL
	free(poller->fds);
#else
	free(poller->events);
	close(poller->epfd);
#endif
	memset(poller, 0, sizeof(pollobj));
	return 0;
}

// poller:register(sockfd, events)
static int lpoll_register(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
	int fd = luaL_checkinteger(L, 2);
	int events = luaL_checkinteger(L, 3);

#ifndef USE_EPOLL
	int i = 0;
	for(i=0; i<poller->len; i++)
	{
		if (poller->fds[i].fd == fd)
		{
			poller->fds[i].events = events;
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	
	if (i == poller->size)
	{
		struct pollfd *newfds = (struct pollfd *)realloc(poller->fds, (poller->size*2)*sizeof(struct pollfd));
		if (!newfds) return report_errmsg(L, LRET_BOOLEAN, errno, "realloc()");
		poller->fds = newfds;
		poller->size *= 2;
	}
	
	poller->fds[i].fd = fd;
	poller->fds[i].events = events;
	poller->fds[i].revents = 0;
	poller->len++;
#else
	if (poller->len == poller->size)
	{
		struct epoll_event *evts = (struct epoll_event *)realloc(poller->events, (poller->size*2)*sizeof(struct epoll_event));
		if (!evts) return report_errmsg(L, LRET_BOOLEAN, errno, "realloc()");
		poller->events = evts;
		poller->size *= 2;
	}
	
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;
	if (epoll_ctl(poller->epfd, EPOLL_CTL_ADD, fd, &event) == -1)
	{
		if (errno!=EEXIST || epoll_ctl(poller->epfd, EPOLL_CTL_MOD, fd, &event)==-1)
			return report_errmsg(L, LRET_BOOLEAN, errno, "epoll_ctl()");
	}else
		poller->len++;
#endif
	lua_pushboolean(L, 1);
	return 1;
}

// poller:unregister(sockfd)
static int lpoll_unregister(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
	int fd = luaL_checkinteger(L, 2);
	
#ifndef USE_EPOLL	
	int i = 0;
	for(i=0; i<poller->len; i++)
	{
		if (poller->fds[i].fd == fd)
			break;
	}
	if (i == poller->len)
		return report_errmsg(L, LRET_BOOLEAN, 0, "file descriptor never registered");
	
	if (i < poller->len-1)
		poller->fds[i] = poller->fds[poller->len-1];
	memset(&poller->fds[poller->len-1], 0, sizeof(struct pollfd));
#else
	struct epoll_event event;
	if (epoll_ctl(poller->epfd, EPOLL_CTL_DEL, fd, &event) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "epoll_ctl()");
#endif
	poller->len--;
	lua_pushboolean(L, 1);
	return 1;
}

// poller:modify(sockfd, events)
static int lpoll_modify(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
	int fd = luaL_checkinteger(L, 2);
	int events = luaL_checkinteger(L, 3);
	
#ifndef USE_EPOLL
	int i = 0;
	for(i=0; i<poller->len; i++)
	{
		if (poller->fds[i].fd == fd)
		{
			poller->fds[i].events = events;
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	return report_errmsg(L, LRET_BOOLEAN, 0, "file descriptor never registered");
#else
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;
	if (epoll_ctl(poller->epfd, EPOLL_CTL_MOD, fd, &event) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "epoll_ctl()");
	lua_pushboolean(L, 1);
	return 1;
#endif
}

static int lpoll_nextevents(lua_State *L)
{
	pollobj *poller = (pollobj *)lua_touserdata(L, lua_upvalueindex(1));
	int i = (int)lua_tointeger(L, lua_upvalueindex(2));
#ifndef USE_EPOLL
	for(; i < poller->len; i++)
	{
		if (poller->fds[i].revents != 0)
		{
			lua_pushinteger(L, i+1);
			lua_replace(L, lua_upvalueindex(2));
			lua_pushinteger(L, poller->fds[i].fd);
			lua_pushinteger(L, poller->fds[i].revents);
			return 2;
		}
	}
#else
	int nfds = (int)lua_tointeger(L, lua_upvalueindex(3));
	for(; i < nfds; i++)
	{
		lua_pushinteger(L, i+1);
		lua_replace(L, lua_upvalueindex(2));
		lua_pushinteger(L, poller->events[i].data.fd);
		lua_pushinteger(L, poller->events[i].events);
		return 2;
	}
#endif
	return 0;
}

// poller:select([timeout=-1])
static int lpoll_select(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
	lua_Number tot = luaL_optnumber(L, 2, -1);
	int timeout = (tot<0) ? -1 : (tot>0) ? (int)(tot*1000) : 0;

#ifndef USE_EPOLL
	int i = 0;
	for(i=0; i<poller->len; i++)
		poller->fds[i].revents = 0;
	int ret = poll(poller->fds, poller->len, timeout);
	if (ret == -1)
		return report_errmsg(L, LRET_NIL, errno, "poll()");
#else
	int ret = epoll_wait(poller->epfd, poller->events, poller->size, timeout);
	if (ret == -1)
		return report_errmsg(L, LRET_NIL, errno, "epoll_wait()");
#endif

#ifdef POLLER_SELECT_RETURN_TABLE
	lua_newtable(L);
	if (ret > 0)
	{
	#ifndef USE_EPOLL
		int rindex = 1;
		for (i=0; i<poller->len; i++)
		{
			if (poller->fds[i].revents != 0)
			{
				lua_newtable(L);
				lua_pushinteger(L, poller->fds[i].fd);
				lua_rawseti(L, -2, 1);
				lua_pushinteger(L, poller->fds[i].revents);
				lua_rawseti(L, -2, 2);
				lua_rawseti(L, -2, rindex++);
			}
		}
	#else
		int i = 0;
		for (i=0; i<ret; i++)
		{
			lua_newtable(L);
			lua_pushinteger(L, poller->events[i].data.fd);
			lua_rawseti(L, -2, 1);
			lua_pushinteger(L, poller->events[i].revents);
			lua_rawseti(L, -2, 2);
			lua_rawseti(L, -2, i+1);
		}
	#endif
	}
#else
	lua_pushlightuserdata(L, (void *)poller);
	lua_pushinteger(L, 0);
	#ifndef USE_EPOLL
	lua_pushcclosure(L, lpoll_nextevents, 2);
	#else
	lua_pushinteger(L, ret);
	lua_pushcclosure(L, lpoll_nextevents, 3);
	#endif
#endif
	return 1;
}

// poller:clear()
static int lpoll_clear(lua_State *L)
{
	pollobj *poller = lpoll_checkobj(L, 1);
	poller->len = 0;
#ifndef USE_EPOLL
	memset(poller->fds, 0, poller->size * sizeof(struct pollfd));
#else
	close(poller->epfd);
	poller->epfd = epoll_create(FD_SETSIZE);
	memset(poller->events, 0, poller->size * sizeof(struct epoll_event));
#endif
	return 0;
}

static int lsock_sendmsg(lua_State *L);
static int lsock_recvmsg(lua_State *L);
static int lsock_getsockopt(lua_State *L);
static int lsock_setsockopt(lua_State *L);
static void lsock_AddMacro(lua_State *L, int t);

static struct luaL_Reg socketlib[] = {
	{"socket", lsock_socket},
	{"socketpair", lsock_socketpair},
	{"PollSelector", lsock_PollSelector},
	{"gethostname", lsock_gethostname},
	{"gethostbyname", lsock_gethostbyname},
	{"gethostbyaddr", lsock_gethostbyaddr},
	{"getprotobyname", lsock_getprotobyname},
	{"getprotobynumber", lsock_getprotobynumber},
	{"getservbyname", lsock_getservbyname},
	{"getservbyport", lsock_getservbyport},
	{"ntohl", lsock_ntohl},
	{"ntohs", lsock_ntohs},
	{"htonl", lsock_htonl},
	{"htons", lsock_htons},
	{"address", lsock_address},
	{NULL, NULL}
};
static struct luaL_Reg socket_methods[] = {
	{"__gc", lsock_close},
	{"close", lsock_close},
	{"closed", lsock_closed},
	{"shutdown", lsock_shutdown},
	{"bind", lsock_bind},
	{"connect", lsock_connect},
	{"listen", lsock_listen},
	{"accept", lsock_accept},
	{"fileno", lsock_fileno},
	{"info", lsock_info},
	{"setblocking", lsock_setblocking},
	{"isblocking", lsock_isblocking},
	{"poll", lsock_poll},
	{"readline", lsock_readline},
	{"recv", lsock_recv},
	{"recvfrom", lsock_recvfrom},
	{"recvmsg", lsock_recvmsg},
	{"send", lsock_send},
	{"sendto", lsock_sendto},
	{"sendmsg", lsock_sendmsg},
	{"getpeername", lsock_getpeername},
	{"getsockname", lsock_getsockname},
	{"setsockopt", lsock_setsockopt},
	{"getsockopt", lsock_getsockopt},
	{NULL, NULL}
};
static struct luaL_Reg poller_methods[] = {
	{"__gc", lpoll_gc},
	{"register", lpoll_register},
	{"unregister", lpoll_unregister},
	{"modify", lpoll_modify},
	{"select", lpoll_select},
	{"clear", lpoll_clear},
	{NULL, NULL}
};

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_socket(lua_State *L)
{
	create_metatable(L, LUASOCKET_NAME, socket_methods);
	create_metatable(L, LUAPOLLER_NAME, poller_methods);
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, socketlib);
//	lua_pushvalue(L, -1);
//	lua_setglobal(L, "socket");
#else
	luaL_register(L, "socket", socketlib);
#endif
	lsock_AddMacro(L, lua_gettop(L));
	
	signal(SIGPIPE, SIG_IGN);
	return 1;
}

#define OPTVAL_BYTE    1
#define OPTVAL_BOOL    2
#define OPTVAL_INT     3
#define OPTVAL_TIMEVAL 4
#define OPTVAL_LINGER  5
#define OPTVAL_MCAST   6
static struct sockopt_t
{
	int level, optname, valtype;
} sockopts[] = 
{
	{ SOL_SOCKET, SO_DEBUG,      OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_ACCEPTCONN, OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_REUSEADDR,  OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_KEEPALIVE,  OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_DONTROUTE,  OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_BROADCAST,  OPTVAL_BOOL    },
	{ SOL_SOCKET, SO_LINGER,     OPTVAL_LINGER  },
	{ SOL_SOCKET, SO_OOBINLINE,  OPTVAL_BOOL    },
#ifdef SO_DONTLINGER
	{ SOL_SOCKET, SO_DONTLINGER, OPTVAL_BOOL    },
#endif
	{ SOL_SOCKET, SO_SNDBUF,     OPTVAL_INT     },
	{ SOL_SOCKET, SO_RCVBUF,     OPTVAL_INT     },
	{ SOL_SOCKET, SO_SNDLOWAT,   OPTVAL_INT     },
	{ SOL_SOCKET, SO_RCVLOWAT,   OPTVAL_INT     },
	{ SOL_SOCKET, SO_SNDTIMEO,   OPTVAL_TIMEVAL },
	{ SOL_SOCKET, SO_RCVTIMEO,   OPTVAL_TIMEVAL },
	{ SOL_SOCKET, SO_ERROR,      OPTVAL_INT     },
	{ SOL_SOCKET, SO_TYPE,       OPTVAL_INT     },
#ifdef TCP_NODELAY
	{ SOL_TCP,    TCP_NODELAY,   OPTVAL_BOOL    },
#endif
#ifdef TCP_MAXSEG
	{ SOL_TCP,    TCP_MAXSEG,    OPTVAL_INT     },
#endif
	{ SOL_IP,     IP_HDRINCL,    OPTVAL_BOOL    },
	{ SOL_IP,     IP_TOS,        OPTVAL_BYTE    },
	{ SOL_IP,     IP_TTL,        OPTVAL_INT     },
/*	{ SOL_IP,   IP_MULTICAST_IF,      OPTVAL_MCAST },
	{ SOL_IP,   IP_MULTICAST_TTL,     OPTVAL_INT   },
	{ SOL_IP,   IP_MULTICAST_LOOP,    OPTVAL_BOOL  },
	{ SOL_IP,   IP_ADD_MEMBERSHIP,    OPTVAL_MCAST },
	{ SOL_IP,   IP_DROP_MEMBERSHIP,   OPTVAL_MCAST },*/
	{ SOL_IPV6, IPV6_HOPLIMIT,        OPTVAL_INT   },
	{ SOL_IPV6, IPV6_UNICAST_HOPS,    OPTVAL_INT   },
/*	{ SOL_IPV6, IPV6_MULTICAST_IF,    OPTVAL_INT   },
	{ SOL_IPV6, IPV6_MULTICAST_HOPS,  OPTVAL_INT   },
	{ SOL_IPV6, IPV6_MULTICAST_LOOP,  OPTVAL_BOOL  },
	{ SOL_IPV6, IPV6_ADD_MEMBERSHIP,  OPTVAL_MCAST },
	{ SOL_IPV6, IPV6_DROP_MEMBERSHIP, OPTVAL_MCAST },*/
	{ 0, 0, 0 }
};
static struct sockopt_t *get_sockopt(int level, int optname)
{
	int i = 0;
	for(i = 0; sockopts[i].level != 0; i++)
	{
		if (level==sockopts[i].level && optname==sockopts[i].optname)
			return &sockopts[i];
	}
	return NULL;
}

union optval_t
{
	u_char b;
	int i;
	struct timeval tv;
	struct linger lg;
};

// sockobj:getsockopt(level, optname)
int lsock_getsockopt(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int level = luaL_checkinteger(L, 2);
	int optname = luaL_checkinteger(L, 3);
	struct sockopt_t *opt = get_sockopt(level, optname);
	if (!opt) return report_errmsg(L, LRET_NIL, 0, "getsockopt() : unsupport level or optname");
	
	union optval_t optval;
	socklen_t optlen = sizeof(optval);
	if (getsockopt(sobj->sockfd, level, optname, (void *)&optval, &optlen) == -1)
		return report_errmsg(L, LRET_NIL, errno, "getsockopt()");
	switch(opt->valtype)
	{
	case OPTVAL_BYTE :
		lua_pushinteger(L, optval.b);
		return 1;
	case OPTVAL_BOOL :
		lua_pushboolean(L, optval.i);
		return 1;
	case OPTVAL_INT :
		lua_pushinteger(L, optval.i);
		return 1;
	case OPTVAL_TIMEVAL :
#ifdef __CYGWIN__
		lua_pushnumber(L, (double)optval.i/1000);
#else
		lua_pushnumber(L, optval.tv.tv_sec + (double)optval.tv.tv_usec/1000000);
#endif
		return 1;
	case OPTVAL_LINGER :
		lua_pushinteger(L, optval.lg.l_linger);
		lua_pushinteger(L, optval.lg.l_onoff);
		return 2;
	}
	return report_errmsg(L, LRET_NIL, 0, "getsockopt() : can not analyse output");
}

// sockobj:setsockopt(level, optname, value)
int lsock_setsockopt(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int level = luaL_checkinteger(L, 2);
	int optname = luaL_checkinteger(L, 3);
	struct sockopt_t *opt = get_sockopt(level, optname);
	if (!opt) return report_errmsg(L, LRET_BOOLEAN, 0, "setsockopt() : unsupport level or optname");
	lua_Number value;
	if (opt->valtype == OPTVAL_BOOL)
	{
		luaL_argcheck(L, lua_isboolean(L, 4), 4, "boolean expected");
		value = (lua_Number)lua_toboolean(L, 4);
	}else
		value = luaL_checknumber(L, 4);
	
	union optval_t optval;
	socklen_t optlen = 0;
	switch(opt->valtype)
	{
	case OPTVAL_BYTE :
		optval.b = ((int)value) & 0x0ff;
		optlen = sizeof(optval.b);
		break;
	case OPTVAL_BOOL :
	case OPTVAL_INT :
		optval.i = (int)value;
		optlen = sizeof(optval.i);
		break;
	case OPTVAL_TIMEVAL :
#ifdef __CYGWIN__
		optval.i = (int)(value * 1000.0f);
		optlen = sizeof(optval.i);
#else
		optval.tv.tv_sec = (int)value;
		optval.tv.tv_usec = (int)(fmod(value, 1.0f)*1000000);
		optlen = sizeof(optval.tv);
#endif
		break;
	case OPTVAL_LINGER :
		optval.lg.l_onoff = 1;
		optval.lg.l_linger = (int)value;
		break;
	}

	if (setsockopt(sobj->sockfd, level, optname, (const void *)&optval, optlen) == -1)
		return report_errmsg(L, LRET_BOOLEAN, errno, "getsockopt()");
	lua_pushboolean(L, 1);
	return 1;
}

// sockobj:sendmsg(data, [ancdata(cmsg_level, cmsg_type, cmsg_data)[, flags=0[, address(ip_addr, port)]]])
int lsock_sendmsg(lua_State *L)
{
	const char *data = NULL, *ancdata = NULL, *addr = NULL;
	size_t data_len = 0, ancdata_len = 0;
	int cmsg_level = 0, cmsg_type = 0, port = 0, flags = 0;
	
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	data = luaL_checklstring(L, 2, &data_len);
	if (lua_istable(L, 3))
	{
		lua_rawgeti(L, 3, 1);
		cmsg_level = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		lua_rawgeti(L, 3, 2);
		cmsg_type = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		lua_rawgeti(L, 3, 3);
		ancdata = luaL_checklstring(L, -1, &ancdata_len);
		lua_pop(L, 1);
	}else
		luaL_argcheck(L, lua_isnoneornil(L, 3), 3, "table ancdata(cmsg_level, cmsg_type, cmsg_data) expected");
	flags = luaL_optinteger(L, 4, 0);
	if (lua_istable(L, 5))
	{
		lua_rawgeti(L, 5, 1);
		addr = luaL_checkstring(L, -1);
		lua_pop(L, 1);
		lua_rawgeti(L, 5, 2);
		port = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
	}
	
	struct msghdr msg;
	struct iovec iov[1];
	char *cmsgbuf = NULL;
	sock_addr_t address;
	memset((char *)&msg, 0, sizeof(msg));
	if (addr)
	{
		msg.msg_namelen = gen_sockaddr(&address, sobj->family, addr, port);
		msg.msg_name = (void *)&address;
	}
	iov->iov_base = (void *)data;
	iov->iov_len = data_len;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	if (ancdata)
	{
		cmsgbuf = (char *)malloc(CMSG_SPACE(ancdata_len));
		msg.msg_control = (void *)cmsgbuf;
		msg.msg_controllen = CMSG_LEN(ancdata_len);
		struct cmsghdr *cmptr = CMSG_FIRSTHDR(&msg);
		cmptr->cmsg_len = CMSG_LEN(ancdata_len);
		cmptr->cmsg_level = cmsg_level;
		cmptr->cmsg_type = cmsg_type;
		memcpy(CMSG_DATA(cmptr), ancdata, ancdata_len);
	}
	int ret = sendmsg(sobj->sockfd, &msg, flags);
	if (cmsgbuf) free(cmsgbuf);
	if (ret == -1)
		return report_errmsg(L, LRET_NEGATIVE, errno, "sendmsg()");
	
	lua_pushinteger(L, ret);
	return 1;
}

// sockobj:recvmsg(bufsize[, ancbufsize=0[, flags=0]])
int lsock_recvmsg(lua_State *L)
{
	sockobj *sobj = lsock_checkobj(L, 1, 1);
	int bufsize = (size_t)luaL_checkinteger(L, 2);
	int ancbufsize = (size_t)luaL_optinteger(L, 3, 0);
	int flags = luaL_optinteger(L, 4, 0);
	
	struct msghdr msg;
	struct iovec iov[1];
	char *cmsgbuf = NULL;
	char *data = (char *)malloc(bufsize+1);
	sock_addr_t address;
	memset((char *)&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&address;
	msg.msg_namelen = sizeof(address);
	iov->iov_base = (void *)data;
	iov->iov_len = bufsize;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	if (ancbufsize > 0)
	{
		cmsgbuf = (char *)malloc(CMSG_SPACE(ancbufsize));
		msg.msg_control = (void *)cmsgbuf;
		msg.msg_controllen = CMSG_LEN(ancbufsize);
	}
	int ret = recvmsg(sobj->sockfd, &msg, flags);
	if (ret <= 0)
	{
		int error = errno;
		free(data);
		if (cmsgbuf) free(cmsgbuf);
		return report_errmsg(L, LRET_NIL, ret ? error : EPIPE, "recvmsg()");
	}
	
	lua_pushlstring(L, (char *)iov->iov_base, iov->iov_len);
	if (ancbufsize > 0)
	{
		lua_newtable(L);
		struct cmsghdr *cmptr = CMSG_FIRSTHDR(&msg);
		lua_pushinteger(L, cmptr->cmsg_level);
		lua_rawseti(L, -2, 1);
		lua_pushinteger(L, cmptr->cmsg_type);
		lua_rawseti(L, -2, 2);
		lua_pushlstring(L, (char *)CMSG_DATA(cmptr), cmptr->cmsg_len - CMSG_LEN(0));
		lua_rawseti(L, -2, 1);
	}else
		lua_pushnil(L);
	lua_pushinteger(L, msg.msg_flags);
	lsock_pushaddr(L, &address);
	free(data);
	if (cmsgbuf) free(cmsgbuf);
	return 4;
}

void lsock_AddMacro(lua_State *L, int t)
{
	// for socket.socket
	SET_CONSTANT(L, t, AF_UNIX);
	SET_CONSTANT(L, t, AF_INET);
	SET_CONSTANT(L, t, AF_INET6);
	
	SET_CONSTANT(L, t, SOCK_STREAM);
	SET_CONSTANT(L, t, SOCK_DGRAM);
	SET_CONSTANT(L, t, SOCK_RAW);
	SET_CONSTANT(L, t, SOCK_RDM);
	SET_CONSTANT(L, t, SOCK_SEQPACKET);
	
	SET_CONSTANT(L, t, IPPROTO_IP);
	SET_CONSTANT(L, t, IPPROTO_HOPOPTS);
	SET_CONSTANT(L, t, IPPROTO_ICMP);
	SET_CONSTANT(L, t, IPPROTO_IGMP);
	SET_CONSTANT(L, t, IPPROTO_IPIP);
	SET_CONSTANT(L, t, IPPROTO_TCP);
	SET_CONSTANT(L, t, IPPROTO_EGP);
	SET_CONSTANT(L, t, IPPROTO_PUP);
	SET_CONSTANT(L, t, IPPROTO_UDP);
	SET_CONSTANT(L, t, IPPROTO_IDP);
	SET_CONSTANT(L, t, IPPROTO_RAW);
	SET_CONSTANT(L, t, IPPROTO_IPV6);
	SET_CONSTANT(L, t, IPPROTO_ROUTING);
	SET_CONSTANT(L, t, IPPROTO_FRAGMENT);
	SET_CONSTANT(L, t, IPPROTO_ESP);
	SET_CONSTANT(L, t, IPPROTO_AH);
	SET_CONSTANT(L, t, IPPROTO_ICMPV6);
	SET_CONSTANT(L, t, IPPROTO_NONE);
	SET_CONSTANT(L, t, IPPROTO_DSTOPTS);
	// for socket:recv/send
	SET_CONSTANT(L, t, MSG_OOB);
	SET_CONSTANT(L, t, MSG_PEEK);
	SET_CONSTANT(L, t, MSG_DONTROUTE);
	SET_CONSTANT(L, t, MSG_WAITALL);
	SET_CONSTANT(L, t, MSG_DONTWAIT);
	SET_CONSTANT(L, t, MSG_TRUNC);
	SET_CONSTANT(L, t, MSG_CTRUNC);
#ifdef MSG_NOSIGNAL
	SET_CONSTANT(L, t, MSG_NOSIGNAL);
#endif
	// for socket:getsockopt/setsockopt
	SET_CONSTANT(L, t, SOL_SOCKET);
	SET_CONSTANT(L, t, SOL_IP);
	SET_CONSTANT(L, t, SOL_IPV6);
#ifdef SOL_TCP
	SET_CONSTANT(L, t, SOL_TCP);
#endif

	SET_CONSTANT(L, t, SO_DEBUG);
	SET_CONSTANT(L, t, SO_ACCEPTCONN);
	SET_CONSTANT(L, t, SO_REUSEADDR);
	SET_CONSTANT(L, t, SO_KEEPALIVE);
	SET_CONSTANT(L, t, SO_DONTROUTE);
	SET_CONSTANT(L, t, SO_BROADCAST);
	SET_CONSTANT(L, t, SO_LINGER);
	SET_CONSTANT(L, t, SO_OOBINLINE);
#ifdef SO_DONTLINGER
	SET_CONSTANT(L, t, SO_DONTLINGER);
#endif
	SET_CONSTANT(L, t, SO_SNDBUF);
	SET_CONSTANT(L, t, SO_RCVBUF);
	SET_CONSTANT(L, t, SO_SNDLOWAT);
	SET_CONSTANT(L, t, SO_RCVLOWAT);
	SET_CONSTANT(L, t, SO_SNDTIMEO);
	SET_CONSTANT(L, t, SO_RCVTIMEO);
	SET_CONSTANT(L, t, SO_ERROR);
	SET_CONSTANT(L, t, SO_TYPE);

	SET_CONSTANT(L, t, IP_OPTIONS);
	SET_CONSTANT(L, t, IP_HDRINCL);
	SET_CONSTANT(L, t, IP_TOS);
	SET_CONSTANT(L, t, IP_TTL);
	SET_CONSTANT(L, t, IP_MULTICAST_IF);
	SET_CONSTANT(L, t, IP_MULTICAST_TTL);
	SET_CONSTANT(L, t, IP_MULTICAST_LOOP);
	SET_CONSTANT(L, t, IP_ADD_MEMBERSHIP);
	SET_CONSTANT(L, t, IP_DROP_MEMBERSHIP);

	SET_CONSTANT(L, t, IPV6_HOPLIMIT);
	SET_CONSTANT(L, t, IPV6_UNICAST_HOPS);
	SET_CONSTANT(L, t, IPV6_MULTICAST_IF);
	SET_CONSTANT(L, t, IPV6_MULTICAST_HOPS);
	SET_CONSTANT(L, t, IPV6_MULTICAST_LOOP);
	SET_CONSTANT(L, t, IPV6_ADD_MEMBERSHIP);
	SET_CONSTANT(L, t, IPV6_DROP_MEMBERSHIP);
	
#ifdef IPTOS_LOWDELAY
	SET_CONSTANT(L, t, IPTOS_LOWDELAY);
#endif
#ifdef IPTOS_MINCOST
	SET_CONSTANT(L, t, IPTOS_MINCOST);
#endif
#ifdef IPTOS_RELIABILITY
	SET_CONSTANT(L, t, IPTOS_RELIABILITY);
#endif
#ifdef IPTOS_THROUGHPUT
	SET_CONSTANT(L, t, IPTOS_THROUGHPUT);
#endif

#ifdef TCP_NODELAY
	SET_CONSTANT(L, t, TCP_NODELAY);
#endif
#ifdef TCP_MAXSEG
	SET_CONSTANT(L, t, TCP_MAXSEG);
#endif
	// for socket:shutdown
	SET_CONSTANT(L, t, SHUT_RD);
	SET_CONSTANT(L, t, SHUT_WR);
	SET_CONSTANT(L, t, SHUT_RDWR);
	// for socket:poll or PollSelector
	SET_CONSTANT(L, t, POLLIN);
	SET_CONSTANT(L, t, POLLPRI);
	SET_CONSTANT(L, t, POLLOUT);
	SET_CONSTANT(L, t, POLLERR);
	SET_CONSTANT(L, t, POLLHUP);
	SET_CONSTANT(L, t, POLLNVAL);
#ifdef USE_EPOLL
	SET_CONSTANT(L, t, EPOLLET);
	SET_CONSTANT(L, t, EPOLLIN);
	SET_CONSTANT(L, t, EPOLLPRI);
	SET_CONSTANT(L, t, EPOLLOUT);
	SET_CONSTANT(L, t, EPOLLERR);
	SET_CONSTANT(L, t, EPOLLHUP);
#endif
}