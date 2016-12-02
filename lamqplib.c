/*************************************************************************************************************************
local amqp = require('rabbitmq')

amqp.method = {Empty, GetOk, Deliver, Return, Ack, Nack, Cancel, Blocked, Unblocked}
amqp.sasl = {PLAIN, EXTERNAL}
amqp.properties = {...}
amqp.connect({host='localhost', port=5672, user='guest', password='guest', vhost='/', sasl_method=amqp.sasl.PLAIN,
              channel_max=0, frame_max=131072, heartbeat=0, [timeout=-1,] [ssl={},] client_properties={}})
	return userdata connection

connection:get_channel_max()
connection:get_frame_max()
connection:get_heartbeat()
connection:get_server_properties()
connection:get_client_properties()
connection:close()
connection:closed()
connection:good()
connection:connect()
connection:drain_events([timeout])
	return message={body, properties, method, ...}
connection:channel([channel_id])
	return userdata channel

channel:ident()
channel:close()
channel:good()
channel:flow(boolean active)
channel:exchange_declare(exchange, exch_type, {passive=0, durable=0, auto_delete=0, internal=0, arguments={}})
channel:exchange_delete(exchange, {if_unused=0})
channel:exchange_bind(dest_exch, source_exch, routing_key, arguments={})
channel:exchange_unbind(dest_exch, source_exch, routing_key, arguments={})
channel:queue_declare(queue='', {passive=0, durable=0, exclusive=0, auto_delete=0, arguments={}})
channel:queue_delete(queue, {if_unused=0, if_empty=0})
channel:queue_bind(queue, exchange, routing_key, arguments={})
channel:queue_unbind(queue, exchange, routing_key, arguments={})
channel:queue_purge(queue)
channel:basic_qos(prefetch_size=0, prefetch_count=0, {global=0})
channel:basic_publish(msg, exchange, routing_key, {properties={..., headers={}}, mandatory=0, immediate=0})
channel:basic_consume(queue, {no_local=0, no_ack=0, exclusive=0, arguments={}})
channel:basic_cancel(consumer_tag)
channel:basic_recover({requeue=0})
channel:basic_get(queue, {no_ack=0})
channel:basic_ack(delivery_tag, {multiple=0})
channel:basic_nack(delivery_tag, {multiple=0, requeue=0})
channel:basic_reject(delivery_tag, {requeue=0})
channel:tx_select()
channel:tx_commit()
channel:tx_rollback()
channel:confirm_select()
*************************************************************************************************************************/
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#ifdef AMQP_WITH_SSL
#include <amqp_ssl_socket.h>
#endif
#ifdef __cplusplus
	#include <lua.hpp>
#else
	#include <lua.h>
	#include <lualib.h>
	#include <lauxlib.h>
#endif

#if LUA_VERSION_NUM <= 501
	#define lua_rawlen lua_objlen
	#define luaL_setfuncs(L, funcs, flag) luaL_openlib(L, NULL, funcs, flag)
#endif

extern int amqp_queue_frame(amqp_connection_state_t state, amqp_frame_t *frame);

#define LUA_AMQP_CONNECTION	"AMQP.Connection"
#define LUA_AMQP_CHANNEL	"AMQP.Channel"

#define BIT_TEST(bits, n)  (*((u_char *)(bits) + (n)/8) & (1 << ((n)%8)))
#define BIT_ZERO(bits, n)  (*((u_char *)(bits) + (n)/8) &= ~(1 << ((n)%8)))
#define BIT_SET(bits, n)   (*((u_char *)(bits) + (n)/8) |= (1 << ((n)%8)))

typedef struct
{
	const char *host, *user, *password, *vhost;
	int port, sasl_method, channel_max, frame_max, heartbeat;
	double timeout;
#ifdef AMQP_WITH_SSL
	struct{
		int in_use;
		const char *cacert, *cert, *key;
		amqp_boolean_t verify_peer, verify_hostname;
	} ssl;
#endif
	amqp_table_t client_properties;
	amqp_pool_t pool;
} login_info_t;

typedef struct
{
	int status;
	time_t timestamp;
	amqp_connection_state_t state;
	amqp_pool_t pool;
	login_info_t login_info;
	u_char channels[8*1024];
} connection_t;

typedef struct
{
	int connection;
	int channel_id;
	time_t timestamp;
} channel_t;

static int cn_create(lua_State *L);
static int cn_destory(lua_State *L);
static int cn_get_channel_max(lua_State *L);
#if AMQP_VERSION >= 0x070000
static int cn_get_frame_max(lua_State *L);
static int cn_get_heartbeat(lua_State *L);
static int cn_get_server_properties(lua_State *L);
static int cn_get_client_properties(lua_State *L);
#endif
static int cn_close(lua_State *L);
static int cn_closed(lua_State *L);
static int cn_good(lua_State *L);
static int cn_connect(lua_State *L);
static int cn_drain(lua_State *L);
static int cn_channel(lua_State *L);
static int ch_ident(lua_State *L);
static int ch_free(lua_State *L);
static int ch_close(lua_State *L);
static int ch_good(lua_State *L);
static int ch_flow(lua_State *L);
static int ch_exchange_declare(lua_State *L);
static int ch_exchange_delete(lua_State *L);
static int ch_exchange_bind(lua_State *L);
static int ch_exchange_unbind(lua_State *L);
static int ch_queue_declare(lua_State *L);
static int ch_queue_delete(lua_State *L);
static int ch_queue_bind(lua_State *L);
static int ch_queue_unbind(lua_State *L);
static int ch_queue_purge(lua_State *L);
static int ch_basic_qos(lua_State *L);
static int ch_basic_publish(lua_State *L);
static int ch_basic_consume(lua_State *L);
static int ch_basic_cancel(lua_State *L);
static int ch_basic_recover(lua_State *L);
static int ch_basic_get(lua_State *L);
static int ch_basic_ack(lua_State *L);
static int ch_basic_nack(lua_State *L);
static int ch_basic_reject(lua_State *L);
static int ch_tx_select(lua_State *L);
static int ch_tx_commit(lua_State *L);
static int ch_tx_rollback(lua_State *L);
static int ch_confirm_select(lua_State *L);

static int create_metatable(lua_State *L, const char *name, const luaL_Reg *methods)
{
	if (!luaL_newmetatable(L, name))
		return 0;

	/* define methods */
	luaL_setfuncs(L, methods, 0);

	/* define metamethods */
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);

	lua_pop(L, 1);
	return 1;
}

static struct luaL_Reg amqplib[] = {
	{"connect", cn_create},
	{NULL, NULL}
};

static struct luaL_Reg connection_libs[] = {
	{"__gc", cn_destory},
	{"close", cn_close},
	{"closed", cn_closed},
	{"good", cn_good},
	{"connect", cn_connect},
	{"drain_events", cn_drain},
	{"channel", cn_channel},
	{"get_channel_max", cn_get_channel_max},
#if AMQP_VERSION >= 0x070000
	{"get_frame_max", cn_get_frame_max},
	{"get_heartbeat", cn_get_heartbeat},
	{"get_server_properties", cn_get_server_properties},
	{"get_client_properties", cn_get_client_properties},
#endif
	{NULL, NULL}
};

static struct luaL_Reg channel_libs[] = {
	{"ident", ch_ident},
	{"__gc", ch_free},
	{"close", ch_close},
	{"good", ch_good},
	{"flow", ch_flow},
	{"exchange_declare", ch_exchange_declare},
	{"exchange_delete", ch_exchange_delete},
	{"exchange_bind", ch_exchange_bind},
	{"exchange_unbind", ch_exchange_unbind},
	{"queue_declare", ch_queue_declare},
	{"queue_delete", ch_queue_delete},
	{"queue_bind", ch_queue_bind},
	{"queue_unbind", ch_queue_unbind},
	{"queue_purge", ch_queue_purge},
	{"basic_qos", ch_basic_qos},
	{"basic_publish", ch_basic_publish},
	{"basic_consume", ch_basic_consume},
	{"basic_cancel", ch_basic_cancel},
	{"basic_recover", ch_basic_recover},
	{"basic_get", ch_basic_get},
	{"basic_ack", ch_basic_ack},
	{"basic_nack", ch_basic_nack},
	{"basic_reject", ch_basic_reject},
	{"tx_select", ch_tx_select},
	{"tx_commit", ch_tx_commit},
	{"tx_rollback", ch_tx_rollback},
	{"confirm_select", ch_confirm_select},
	{NULL, NULL}
};

static struct properties_desc_t
{
	const char *name, *domain, *desc;
} _properties_desc[] =
{
	{"content_type", "shortstr", "MIME content type"},
	{"content_encoding", "shortstr", "MIME content encoding"},
	{"delivery_mode", "octet", "non-persistent(1) or persistent(2)"},
	{"priority", "octet", "message priority, 0 to 9"},
	{"correlation_id", "shortstr", "application correlation identifier"},
	{"reply_to", "shortstr", "address to reply to"},
	{"expiration", "shortstr", "message expiration specification"},
	{"message_id", "shortstr", "application message identifier"},
	{"timestamp", "timestamp", "message timestamp"},
	{"type", "shortstr", "message type name"},
	{"user_id", "shortstr", "creating user id"},
	{"app_id", "shortstr", "creating application id"},
	{"headers", "table", "message header field table"},
	{NULL, NULL, NULL}
};

#ifdef __cplusplus
extern "C"
#endif
int luaopen_rabbitmq(lua_State *L)
{
	create_metatable(L, LUA_AMQP_CONNECTION, connection_libs);
	create_metatable(L, LUA_AMQP_CHANNEL, channel_libs);
#if LUA_VERSION_NUM > 501
	luaL_newlib(L, amqplib);
#else
	luaL_register(L, "rabbitmq", amqplib);
#endif

	lua_newtable(L);
	lua_pushinteger(L, AMQP_SASL_METHOD_PLAIN); lua_setfield(L, -2, "PLAIN");
#if AMQP_VERSION >= 0x070000
	lua_pushinteger(L, AMQP_SASL_METHOD_EXTERNAL); lua_setfield(L, -2, "EXTERNAL");
#endif
	lua_setfield(L, -2, "sasl");
	
	lua_newtable(L);
	lua_pushinteger(L, AMQP_BASIC_GET_EMPTY_METHOD); lua_setfield(L, -2, "Empty");
	lua_pushinteger(L, AMQP_BASIC_RETURN_METHOD); lua_setfield(L, -2, "Return");
	lua_pushinteger(L, AMQP_BASIC_GET_OK_METHOD); lua_setfield(L, -2, "GetOK");
	lua_pushinteger(L, AMQP_BASIC_DELIVER_METHOD); lua_setfield(L, -2, "Deliver");
	lua_pushinteger(L, AMQP_BASIC_CANCEL_METHOD); lua_setfield(L, -2, "Cancel");
	lua_pushinteger(L, AMQP_BASIC_ACK_METHOD); lua_setfield(L, -2, "Ack");
	lua_pushinteger(L, AMQP_BASIC_NACK_METHOD); lua_setfield(L, -2, "Nack");
	lua_pushinteger(L, AMQP_CONNECTION_BLOCKED_METHOD); lua_setfield(L, -2, "Blocked");
	lua_pushinteger(L, AMQP_CONNECTION_UNBLOCKED_METHOD); lua_setfield(L, -2, "Unblocked");
	lua_setfield(L, -2, "method");
	
	lua_newtable(L);
	struct properties_desc_t *property;
	for(property = _properties_desc; property->name; property++)
	{
		lua_newtable(L);
		lua_pushstring(L, property->name); lua_setfield(L, -2, "name");
		lua_pushstring(L, property->domain); lua_setfield(L, -2, "domain");
		lua_pushstring(L, property->desc); lua_setfield(L, -2, "desc");
		lua_setfield(L, -2, property->name);
	}
	lua_setfield(L, -2, "properties");
	
	return 1;
}

static int report_amqp_error(lua_State *L, int f, amqp_rpc_reply_t r);
static int report_os_error(lua_State *L, int f, int x);
static amqp_rpc_reply_t establish_connection(lua_State *L, connection_t *conn);
static void close_connection(connection_t *conn);
static amqp_rpc_reply_t drain_event(lua_State *L, connection_t* conn, amqp_frame_t *frame, amqp_message_t *message, struct timeval *timeout);
static void decode_frame(lua_State *L, int t, amqp_frame_t *frame);
static void decode_properties(lua_State *L, amqp_basic_properties_t *properties);
static void decode_table(lua_State *L, amqp_table_t *table);
static void decode_array(lua_State *L, amqp_array_t *array);
static int  decode_field(lua_State *L, amqp_field_value_t *field);
static void encode_properties(lua_State *L, int t, amqp_basic_properties_t *properties, amqp_pool_t *pool);
static void encode_table(lua_State *L, int t, amqp_table_t *table, amqp_pool_t *pool);
static void encode_array(lua_State *L, int t, amqp_array_t *array, amqp_pool_t *pool);
static int  encode_field(lua_State *L, int t, amqp_field_value_t *field, amqp_pool_t *pool);
static amqp_bytes_t encode_bytes(const char *s, size_t l, amqp_pool_t *pool);

static void check_rpc_reply(connection_t *conn, channel_t *ch, amqp_rpc_reply_t reply);
#define check_status(conn, ch, res) \
	do { \
		if (res == AMQP_STATUS_CONNECTION_CLOSED || \
			res == AMQP_STATUS_SOCKET_ERROR || \
			res == AMQP_STATUS_SSL_ERROR) \
		{ \
			memset(conn->channels, 0, sizeof(conn->channels)); \
			conn->channels[0] |= 0x01; \
			conn->status = -1; \
			if (ch) ch->channel_id = -1; \
		} \
	}while(0)
#define assert_rpc_reply(L, f, reply, conn, ch) \
	do { \
		if (reply.reply_type != AMQP_RESPONSE_NORMAL) \
		{ \
			check_rpc_reply(conn, ch, reply); \
			return report_amqp_error(L, f, reply); \
		} \
	}while(0)
#define assert_status(L, f, res, conn, ch) \
	do { \
		if (res) { \
			check_status(conn, ch, res); \
			return report_os_error(L, f, res); \
		} \
	}while(0)

static int check_table(lua_State *L, int t)
{
	if (lua_isnoneornil(L, t)) return 0;
	luaL_checktype(L, t, LUA_TTABLE);
	return 1;
}
static const char *get_field_s(lua_State *L, int t, const char *name, const char *defval)
{
	lua_getfield(L, t, name);
	const char *s = luaL_optstring(L, -1, defval);
	lua_pop(L, 1);
	return s;
}
static long get_field_i(lua_State *L, int t, const char *name, long defval)
{
	lua_getfield(L, t, name);
	long i = luaL_optinteger(L, -1, defval);
	lua_pop(L, 1);
	return i;
}

#define check_connection(L, conn) \
	do{ \
		if (!(conn)->state || (conn)->status <= 0) \
		{ \
			lua_pushnil(L); \
			lua_pushstring(L, "connection disconnect or abnormal status"); \
			return 2; \
		} \
	}while(0)
#define check_channel(L, ch, conn) \
	do{ \
		check_connection(L, (conn)); \
		if ((ch)->timestamp<(conn)->timestamp || (ch)->channel_id<=0 || BIT_TEST((conn)->channels, (ch)->channel_id)==0) \
		{ \
			lua_pushnil(L); \
			lua_pushstring(L, "channel has closed"); \
			return 2; \
		} \
	}while(0)

// amqp.connect({host='localhost', port=5672, user='guest', password='guest', vhost='/', sasl_method=amqp.sasl.PLAIN,
//               channel_max=0, frame_max=131072, heartbeat=0, ssl={}, client_properties={}})
int cn_create(lua_State *L)
{
	connection_t *conn = (connection_t *)lua_newuserdata(L, sizeof(connection_t));
	memset(conn, 0, sizeof(connection_t));
	init_amqp_pool(&conn->login_info.pool, 4096);
	conn->login_info.client_properties = amqp_empty_table;
	if (check_table(L, 1))
	{
		conn->login_info.host = (const char *)encode_bytes(get_field_s(L, 1, "host", "localhost"), 0, &conn->login_info.pool).bytes;
		conn->login_info.user = (const char *)encode_bytes(get_field_s(L, 1, "user", "guest"), 0, &conn->login_info.pool).bytes;
		conn->login_info.password = (const char *)encode_bytes(get_field_s(L, 1, "password", "guest"), 0, &conn->login_info.pool).bytes;
		conn->login_info.vhost = (const char *)encode_bytes(get_field_s(L, 1, "vhost", "/"), 0, &conn->login_info.pool).bytes;
		conn->login_info.port = get_field_i(L, 1, "port", 5672);
		conn->login_info.sasl_method = get_field_i(L, 1, "sasl_method", AMQP_SASL_METHOD_PLAIN);
		conn->login_info.channel_max = get_field_i(L, 1, "channel_max", 0);
		conn->login_info.frame_max = get_field_i(L, 1, "frame_max", 131072);
		conn->login_info.heartbeat = get_field_i(L, 1, "heartbeat", 0);
		lua_getfield(L, 1, "timeout");
		conn->login_info.timeout = (double)luaL_optnumber(L, -1, -1);
		lua_pop(L, 1);
		lua_getfield(L, 1, "client_properties");
		if (lua_istable(L, -1))
		{
			encode_table(L, lua_gettop(L), &conn->login_info.client_properties, &conn->login_info.pool);
		}
		lua_pop(L, 1);
		lua_getfield(L, 1, "ssl");
		if (lua_istable(L, -1))
		{
		#ifdef AMQP_WITH_SSL
			conn->login_info.ssl.in_use = 1;
			const char *cacert, *cert, *key;
			lua_getfield(L, -1, "cacert"); cacert = luaL_optstring(L, -1, NULL); lua_pop(L, 1);
			lua_getfield(L, -1, "cert"); cert = luaL_optstring(L, -1, NULL); lua_pop(L, 1);
			lua_getfield(L, -1, "key"); key = luaL_optstring(L, -1, cacert); lua_pop(L, 1);
			if (cacert)
				conn->login_info.ssl.cacert = (const char *)encode_bytes(cacert, 0, &conn->login_info.pool).bytes;
			if (cert)
				conn->login_info.ssl.cert = (const char *)encode_bytes(cert, 0, &conn->login_info.pool).bytes;
			if (key)
				conn->login_info.ssl.key = (const char *)encode_bytes(key, 0, &conn->login_info.pool).bytes;
			lua_getfield(L, -1, "verify_peer");
			conn->login_info.ssl.verify_peer = luaL_optinteger(L, -1, 0);
			lua_pop(L, 1);
			lua_getfield(L, -1, "verify_hostname");
			conn->login_info.ssl.verify_hostname = luaL_optinteger(L, -1, 0);
			lua_pop(L, 1);
		#else
			luaL_error(L, "unsupport ssl");
		#endif
		}
		lua_pop(L, 1);
	}else
	{
		conn->login_info.host = "localhost";
		conn->login_info.user = "guest";
		conn->login_info.password = "guest";
		conn->login_info.vhost = "/";
		conn->login_info.port = 5672;
		conn->login_info.sasl_method = AMQP_SASL_METHOD_PLAIN;
		conn->login_info.channel_max = 0;
		conn->login_info.frame_max = 131072;
		conn->login_info.heartbeat = 0;
		conn->login_info.timeout = -1;
	}
	
	amqp_rpc_reply_t reply = establish_connection(L, conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL)
	{
		empty_amqp_pool(&conn->login_info.pool);
		lua_pop(L, 1);
		return report_amqp_error(L, -1, reply);
	}
	luaL_getmetatable(L, LUA_AMQP_CONNECTION);
	lua_setmetatable(L, -2);
	return 1;
}

// connection:__gc()
int cn_destory(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	close_connection(conn);
	empty_amqp_pool(&conn->login_info.pool);
	return 0;
}

int cn_get_channel_max(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, conn->state, 1, "connection disconnect");
	lua_pushinteger(L, amqp_get_channel_max(conn->state));
	return 1;
}

#if AMQP_VERSION >= 0x070000

int cn_get_frame_max(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, conn->state, 1, "connection disconnect");
	lua_pushinteger(L, amqp_get_frame_max(conn->state));
	return 1;
}

int cn_get_heartbeat(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, conn->state, 1, "connection disconnect");
	lua_pushinteger(L, amqp_get_heartbeat(conn->state));
	return 1;
}

int cn_get_server_properties(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, conn->state, 1, "connection disconnect");
	amqp_table_t *properties = amqp_get_server_properties(conn->state);
	decode_table(L, properties);
	return 1;
}

int cn_get_client_properties(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, conn->state, 1, "connection disconnect");
	amqp_table_t *properties = amqp_get_client_properties(conn->state);
	decode_table(L, properties);
	return 1;
}

#endif

// connection:close()
int cn_close(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	close_connection(conn);
	return 0;
}

// connection:closed()
int cn_closed(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	lua_pushboolean(L, (conn->state==NULL));
	return 1;
}

// connection:good()
int cn_good(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	lua_pushboolean(L, (conn->state && conn->status>0));
	return 1;
}

// connection:connect()
int cn_connect(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	luaL_argcheck(L, !conn->state, 1, "already connect");
	amqp_rpc_reply_t reply = establish_connection(L, conn);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL)
	{
		return report_amqp_error(L, 0, reply);
	}
	lua_pushboolean(L, 1);
	return 1;
}

// connection:drain_events([timeout])
int cn_drain(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	check_connection(L, conn);
	double timeout = (double)luaL_optnumber(L, 2, -1), iptr;
	
	amqp_frame_t frame;
	amqp_message_t message;
	struct timeval tv = { (long)timeout, (long)(modf(timeout, &iptr)*1000000) };
	amqp_rpc_reply_t reply = drain_event(L, conn, &frame, &message, timeout < 0 ? NULL : &tv);
	amqp_maybe_release_buffers(conn->state);
	if (reply.reply_type == AMQP_RESPONSE_NORMAL)
	{
		lua_newtable(L);
		decode_frame(L, lua_gettop(L), &frame);
		if (frame.payload.method.id == AMQP_BASIC_DELIVER_METHOD ||
			frame.payload.method.id == AMQP_BASIC_RETURN_METHOD ||
			frame.payload.method.id == AMQP_BASIC_GET_OK_METHOD)
		{
			decode_properties(L, &message.properties);
			lua_setfield(L, -2, "properties");
			lua_pushlstring(L, (const char*)message.body.bytes, message.body.len);
			lua_setfield(L, -2, "body");
			amqp_destroy_message(&message);
		}
		return 1;
	}else
	if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && reply.library_error == AMQP_STATUS_TIMEOUT)
	{
		lua_newtable(L);
		lua_pushinteger(L, AMQP_BASIC_GET_EMPTY_METHOD);
		lua_setfield(L, -2, "method");
		return 1;
	}
	
	channel_t ch = {0, frame.channel, 0};
	check_rpc_reply(conn, &ch, reply);
	return report_amqp_error(L, -1, reply);
}

// connection:channel([channel_id])
int cn_channel(lua_State *L)
{
	connection_t *conn = (connection_t *)luaL_checkudata(L, 1, LUA_AMQP_CONNECTION);
	check_connection(L, conn);
	int channel_id = luaL_optinteger(L, 2, 0);
	int channel_max = amqp_get_channel_max(conn->state);
	char errmsg[64];
	sprintf(errmsg, "channel id in (0, %d]", channel_max);
	luaL_argcheck(L, channel_id>=0 && (channel_max==0 || channel_id<channel_max), 2, errmsg);
	if (!channel_id)
	{
		u_short n, i;
		for(n=0; conn->channels[n]==0xff; n++);
		for(i=0; i<8 && (conn->channels[n] & (1<<i)); i++);
		channel_id = 8*n + i;
	}
	if (!BIT_TEST(conn->channels, channel_id))
	{
		if (!amqp_channel_open(conn->state, channel_id))
		{
			amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
			return report_amqp_error(L, -1, reply);
		}
		BIT_SET(conn->channels, channel_id);
	}
	
	channel_t *ch = (channel_t *)lua_newuserdata(L, sizeof(channel_t));
	luaL_getmetatable(L, LUA_AMQP_CHANNEL);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, 1);
	ch->connection = luaL_ref(L, LUA_REGISTRYINDEX);
	ch->channel_id = channel_id;
	ch->timestamp = time(NULL);
	return 1;
}

static connection_t *ch_getconn(lua_State *L, channel_t *ch)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, ch->connection);
	connection_t *conn = (connection_t *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return conn;
}

// channel:ident()
int ch_ident(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	lua_pushinteger(L, ch->channel_id);
	return 1;
}

// channel:__gc()
int ch_free(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	luaL_unref(L, LUA_REGISTRYINDEX, ch->connection);
	return 0;
}

// channel:close()
int ch_close(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	if (ch->channel_id > 0)
	{
		if (conn->state && conn->status>0 && BIT_TEST(conn->channels, ch->channel_id))
		{
			amqp_channel_close(conn->state, ch->channel_id, AMQP_REPLY_SUCCESS);
			BIT_ZERO(conn->channels, ch->channel_id);
		}
		ch->channel_id = -1;
	}
	return 0;
}

// channel:good()
int ch_good(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);
	if (ch->channel_id > 0)
	{
		if (!conn->state || conn->status<0 || BIT_TEST(conn->channels, ch->channel_id)==0)
			ch->channel_id = -1;
	}
	lua_pushboolean(L, ch->channel_id > 0);
	return 1;
}

// channel:flow(active)
int ch_flow(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	amqp_boolean_t active = (amqp_boolean_t)lua_toboolean(L, 2);
	
	amqp_channel_flow(conn->state, ch->channel_id, active);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:exchange_declare(exchange, exch_type, {passive=0, durable=0, auto_delete=0, internal=0, arguments={}})
int ch_exchange_declare(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *exchange = luaL_checkstring(L, 2);
	static const char *types[] = {"direct", "fanout", "topic", "headers", NULL};
	int type = luaL_checkoption(L, 3, NULL, types);
	amqp_boolean_t passive = 0, durable = 0, auto_delete = 0, internal = 0;
	amqp_table_t arguments = amqp_empty_table;
	if (check_table(L, 4))
	{
		passive = get_field_i(L, 4, "passive", 0);
		durable = get_field_i(L, 4, "durable", 0);
		auto_delete = get_field_i(L, 4, "auto_delete", 0);
		internal = get_field_i(L, 4, "internal", 0);
		lua_getfield(L, 4, "arguments");
		if (lua_istable(L, -1))
			encode_table(L, lua_gettop(L), &arguments, &conn->pool);
		lua_pop(L, 1);
	}
	
	amqp_exchange_declare(conn->state, ch->channel_id, amqp_cstring_bytes(exchange),
	                      amqp_cstring_bytes(types[type]), passive, durable,
#if AMQP_VERSION >= 0x060000
	                      auto_delete, internal,
#endif
	                      arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:exchange_delete(exchange, {if_unused=0})
int ch_exchange_delete(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *exchange = luaL_checkstring(L, 2);
	amqp_boolean_t if_unused = 0;
	if (check_table(L, 3))
	{
		if_unused = get_field_i(L, 3, "if_unused", 0);
	}
	
	amqp_exchange_delete(conn->state, ch->channel_id, amqp_cstring_bytes(exchange), if_unused);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:exchange_bind(dest_exch, source_exch, routing_key, arguments={})
int ch_exchange_bind(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *dest_exch = luaL_checkstring(L, 2);
	const char *source_exch = luaL_checkstring(L, 3);
	const char *routing_key = luaL_checkstring(L, 4);
	amqp_table_t arguments = amqp_empty_table;
	if (lua_istable(L, 5))
		encode_table(L, 5, &arguments, &conn->pool);

	amqp_exchange_bind(conn->state, ch->channel_id, amqp_cstring_bytes(dest_exch), 
	             amqp_cstring_bytes(source_exch), amqp_cstring_bytes(routing_key), arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:exchange_unbind(dest_exch, source_exch, routing_key, arguments={})
int ch_exchange_unbind(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *dest_exch = luaL_checkstring(L, 2);
	const char *source_exch = luaL_checkstring(L, 3);
	const char *routing_key = luaL_checkstring(L, 4);
	amqp_table_t arguments = amqp_empty_table;
	if (lua_istable(L, 5))
		encode_table(L, 5, &arguments, &conn->pool);

	amqp_exchange_unbind(conn->state, ch->channel_id, amqp_cstring_bytes(dest_exch), 
	             amqp_cstring_bytes(source_exch), amqp_cstring_bytes(routing_key), arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:queue_declare(queue='', {passive=0, durable=0, exclusive=0, auto_delete=0, arguments={}})
int ch_queue_declare(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_optstring(L, 2, "");
	amqp_boolean_t passive = 0, durable = 0, auto_delete = 0, exclusive = 0;
	amqp_table_t arguments = amqp_empty_table;
	if (check_table(L, 3))
	{
		passive = get_field_i(L, 3, "passive", 0);
		durable = get_field_i(L, 3, "durable", 0);
		auto_delete = get_field_i(L, 3, "auto_delete", 0);
		exclusive = get_field_i(L, 3, "exclusive", 0);
		lua_getfield(L, 3, "arguments");
		if (lua_istable(L, -1))
			encode_table(L, lua_gettop(L), &arguments, &conn->pool);
		lua_pop(L, 1);
	}
	
	amqp_queue_declare_ok_t *r =
		amqp_queue_declare(conn->state, ch->channel_id, amqp_cstring_bytes(queue),
		                   passive, durable, exclusive, auto_delete, arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_newtable(L);
	lua_pushlstring(L, (const char *)r->queue.bytes, r->queue.len); lua_setfield(L, -2, "queue");
	lua_pushinteger(L, r->message_count); lua_setfield(L, -2, "message_count");
	lua_pushinteger(L, r->consumer_count); lua_setfield(L, -2, "consumer_count");
	return 1;
}

// channel:queue_delete(queue, {if_unused=0, if_empty=0})
int ch_queue_delete(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	amqp_boolean_t if_unused = 0, if_empty = 0;
	if (check_table(L, 3))
	{
		if_unused = get_field_i(L, 3, "if_unused", 0);
		if_empty = get_field_i(L, 3, "if_empty", 0);
	}
	
	amqp_queue_delete(conn->state, ch->channel_id, amqp_cstring_bytes(queue), if_unused, if_empty);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:queue_bind(queue, exchange, routing_key, arguments={})
int ch_queue_bind(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	const char *exchange = luaL_checkstring(L, 3);
	const char *routing_key = luaL_checkstring(L, 4);
	amqp_table_t arguments = amqp_empty_table;
	if (lua_istable(L, 5))
		encode_table(L, 5, &arguments, &conn->pool);

	amqp_queue_bind(conn->state, ch->channel_id, amqp_cstring_bytes(queue), 
	             amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key), arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:queue_unbind(queue, exchange, routing_key, arguments={})
int ch_queue_unbind(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	const char *exchange = luaL_checkstring(L, 3);
	const char *routing_key = luaL_checkstring(L, 4);
	amqp_table_t arguments = amqp_empty_table;
	if (lua_istable(L, 5))
		encode_table(L, 5, &arguments, &conn->pool);

	amqp_queue_unbind(conn->state, ch->channel_id, amqp_cstring_bytes(queue), 
	             amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key), arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:queue_purge(queue)
int ch_queue_purge(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	
	amqp_queue_purge(conn->state, ch->channel_id, amqp_cstring_bytes(queue));
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_qos(prefetch_size=0, prefetch_count=0, {global=0})
int ch_basic_qos(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	uint16_t prefetch_size = (uint16_t)luaL_checkinteger(L, 2);
	uint16_t prefetch_count = (uint16_t)luaL_checkinteger(L, 3);
	amqp_boolean_t global = 0;
	if (check_table(L, 4))
	{
		global = get_field_i(L, 4, "global", 0);
	}
	
	amqp_basic_qos(conn->state, ch->channel_id, prefetch_size, prefetch_count, global);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_publish(msg, exchange, routing_key, {properties={..., headers={}}, mandatory=0, immediate=0})
int ch_basic_publish(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	size_t len;
	const char *msg = luaL_checklstring(L, 2, &len);
	const char *exchange = luaL_checkstring(L, 3);
	const char *routing_key = luaL_checkstring(L, 4);
	amqp_boolean_t mandatory = 0, immediate = 0;
	amqp_basic_properties_t properties;
	memset(&properties, 0, sizeof(amqp_basic_properties_t));
	if (check_table(L, 5))
	{
		mandatory = get_field_i(L, 5, "mandatory", 0);
		immediate = get_field_i(L, 5, "immediate", 0);
		lua_getfield(L, 5, "properties");
		if (lua_istable(L, -1))
			encode_properties(L, lua_gettop(L), &properties, &conn->pool);
		lua_pop(L, 1);
	}
	
	amqp_bytes_t body = { len, (void *)msg };
	int status = amqp_basic_publish(conn->state, ch->channel_id, amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key),
	                                mandatory, immediate, (properties._flags == 0) ? NULL : &properties, body);
	empty_amqp_pool(&conn->pool);
	assert_status(L, 0, status, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_consume(queue, {no_local=0, no_ack=0, exclusive=0, arguments={}})
int ch_basic_consume(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	amqp_boolean_t no_local = 0, no_ack = 0, exclusive = 0;
	amqp_table_t arguments = amqp_empty_table;
	if (check_table(L, 3))
	{
		no_local = get_field_i(L, 3, "no_local", 0);
		no_ack = get_field_i(L, 3, "no_ack", 0);
		exclusive = get_field_i(L, 3, "exclusive", 0);
		lua_getfield(L, 3, "arguments");
		if (lua_istable(L, -1))
			encode_table(L, lua_gettop(L), &arguments, &conn->pool);
		lua_pop(L, 1);
	}

	amqp_basic_consume_ok_t *r =
		amqp_basic_consume(conn->state, ch->channel_id, amqp_cstring_bytes(queue),
		                   amqp_empty_bytes, no_local, no_ack, exclusive, arguments);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	empty_amqp_pool(&conn->pool);
	assert_rpc_reply(L, -1, reply, conn, ch);
	lua_pushlstring(L, (const char*)r->consumer_tag.bytes, r->consumer_tag.len);
	return 1;
}

// channel:basic_cancel(consumer_tag)
int ch_basic_cancel(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *consumer_tag = luaL_checkstring(L, 2);

	amqp_basic_cancel(conn->state, ch->channel_id, amqp_cstring_bytes(consumer_tag));
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, -1, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_recover({requeue=0})
int ch_basic_recover(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	amqp_boolean_t requeue = 0;
	if (check_table(L, 2))
	{
		requeue = get_field_i(L, 2, "requeue", 0);
	}

	amqp_basic_recover(conn->state, ch->channel_id, requeue);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_get(queue, {no_ack=0})
int ch_basic_get(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	const char *queue = luaL_checkstring(L, 2);
	amqp_boolean_t no_ack = 0;
	if (check_table(L, 3))
	{
		no_ack = get_field_i(L, 3, "no_ack", 0);
	}

	amqp_rpc_reply_t r = amqp_basic_get(conn->state, ch->channel_id, amqp_cstring_bytes(queue), no_ack);
	assert_rpc_reply(L, -1, r, conn, ch);
	switch(r.reply.id)
	{
	case AMQP_BASIC_GET_EMPTY_METHOD :
		amqp_maybe_release_buffers(conn->state);
		lua_pushinteger(L, 0);
		break;
	case AMQP_BASIC_GET_OK_METHOD : {
		amqp_frame_t frame;
		frame.frame_type = AMQP_FRAME_METHOD;
		frame.channel = ch->channel_id;
		frame.payload.method = r.reply;
		amqp_queue_frame(conn->state, &frame);
		amqp_basic_get_ok_t *get_ok = (amqp_basic_get_ok_t *)r.reply.decoded;
		lua_pushinteger(L, get_ok->message_count + 1);
		}
	}
	return 1;
}

// channel:basic_ack(delivery_tag, {multiple=0})
int ch_basic_ack(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	uint64_t delivery_tag = (uint64_t)luaL_checkinteger(L, 2);
	amqp_boolean_t multiple = 0;
	if (check_table(L, 3))
	{
		multiple = get_field_i(L, 3, "multiple", 0);
	}

	int status = amqp_basic_ack(conn->state, ch->channel_id, delivery_tag, multiple);
	assert_status(L, 0, status, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_nack(delivery_tag, {multiple=0, requeue=0})
int ch_basic_nack(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	uint64_t delivery_tag = (uint64_t)luaL_checkinteger(L, 2);
	amqp_boolean_t multiple = 0, requeue = 0;
	if (check_table(L, 3))
	{
		multiple = get_field_i(L, 3, "multiple", 0);
		requeue = get_field_i(L, 3, "requeue", 0);
	}

	int status = amqp_basic_nack(conn->state, ch->channel_id, delivery_tag, multiple, requeue);
	assert_status(L, 0, status, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:basic_reject(delivery_tag, {requeue=0})
int ch_basic_reject(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	uint64_t delivery_tag = (uint64_t)luaL_checkinteger(L, 2);
	amqp_boolean_t requeue = 0;
	if (check_table(L, 3))
	{
		requeue = get_field_i(L, 3, "requeue", 0);
	}

	int status = amqp_basic_reject(conn->state, ch->channel_id, delivery_tag, requeue);
	assert_status(L, 0, status, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:tx_select()
int ch_tx_select(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	amqp_tx_select(conn->state, ch->channel_id);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:tx_commit()
int ch_tx_commit(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	amqp_tx_commit(conn->state, ch->channel_id);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:tx_rollback()
int ch_tx_rollback(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	amqp_tx_rollback(conn->state, ch->channel_id);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

// channel:confirm_select()
int ch_confirm_select(lua_State *L)
{
	channel_t *ch = (channel_t *)luaL_checkudata(L, 1, LUA_AMQP_CHANNEL);
	connection_t *conn = ch_getconn(L, ch);	
	check_channel(L, ch, conn);
	amqp_confirm_select(conn->state, ch->channel_id);
	amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn->state);
	assert_rpc_reply(L, 0, reply, conn, ch);
	lua_pushboolean(L, 1);
	return 1;
}

int report_amqp_error(lua_State *L, int f, amqp_rpc_reply_t r)
{
	if (r.reply_type == AMQP_RESPONSE_NORMAL) return 0;
	switch(f)
	{
	case -1 : lua_pushnil(L); break;
	case 0  : lua_pushboolean(L, 0); break;
	default : lua_pushinteger(L, -1); break;
	}
	switch (r.reply_type)
	{
	case AMQP_RESPONSE_NONE :
		lua_pushstring(L, "missing RPC reply type"); break;
	case AMQP_RESPONSE_LIBRARY_EXCEPTION :
		lua_pushfstring(L, "library error:%d, message:%s", r.library_error, amqp_error_string2(r.library_error)); break;
	case AMQP_RESPONSE_SERVER_EXCEPTION :
		switch (r.reply.id)
		{
		case AMQP_CONNECTION_CLOSE_METHOD: {
			amqp_connection_close_t *m = (amqp_connection_close_t *)r.reply.decoded;
			lua_pushfstring(L, "server connection error: %d, message: %s", m->reply_code, (char *)m->reply_text.bytes);
			return 2;
			}
		case AMQP_CHANNEL_CLOSE_METHOD: {
			amqp_channel_close_t *m = (amqp_channel_close_t *)r.reply.decoded;
			lua_pushfstring(L, "server channel error: %d, message: %s", m->reply_code, (char *)m->reply_text.bytes);
			return 2;
			}
		}
	default :
		lua_pushfstring(L, "unknown server error, method id 0x%08X", r.reply.id);
	}
	return 2;
}

int report_os_error(lua_State *L, int f, int x)
{
	if (!x) return 0;
	amqp_rpc_reply_t r;
	r.reply_type = AMQP_RESPONSE_LIBRARY_EXCEPTION;
	r.library_error = x;
	return report_amqp_error(L, f, r);
}

void check_rpc_reply(connection_t *conn, channel_t *ch, amqp_rpc_reply_t r)
{
	if (conn->status > 0)
	{
		switch(r.reply_type)
		{
		case AMQP_RESPONSE_LIBRARY_EXCEPTION :
			check_status(conn, ch, r.library_error);
			break;
		case AMQP_RESPONSE_SERVER_EXCEPTION :
			switch(r.reply.id)
			{
			case AMQP_CONNECTION_CLOSE_METHOD: {
				amqp_connection_close_ok_t reply;
				amqp_send_method(conn->state, ch->channel_id, AMQP_CONNECTION_CLOSE_OK_METHOD, &reply);
				memset(conn->channels, 0, sizeof(conn->channels));
				conn->channels[0] |= 0x01;
				conn->status = -1;
				if (ch) ch->channel_id = -1;
				break;
				}
			case AMQP_CHANNEL_CLOSE_METHOD: {
				amqp_channel_close_ok_t reply;
				amqp_send_method(conn->state, ch->channel_id, AMQP_CHANNEL_CLOSE_OK_METHOD, &reply);
				BIT_ZERO(conn->channels, ch->channel_id);
				ch->channel_id = -1;
				break;
				}
			}
		}
	}
}

amqp_rpc_reply_t establish_connection(lua_State *L, connection_t *conn)
{
	amqp_rpc_reply_t reply;
	conn->state = amqp_new_connection();
	amqp_socket_t *socket;
#ifdef AMQP_WITH_SSL
	if (conn->login_info.ssl.in_use)
	{
		socket = amqp_ssl_socket_new(conn->state);
		if (conn->login_info.ssl.cacert)
			amqp_ssl_socket_set_cacert(socket, conn->login_info.ssl.cacert);
		if (conn->login_info.ssl.cert)
			amqp_ssl_socket_set_key(socket, conn->login_info.ssl.cert, conn->login_info.ssl.key);
	#if AMQP_VERSION >= 0x080000
		amqp_ssl_socket_set_verify_peer(socket, conn->login_info.ssl.verify_peer);
		amqp_ssl_socket_set_verify_hostname(socket, conn->login_info.ssl.verify_hostname);
	#else
		amqp_ssl_socket_set_verify(socket, conn->login_info.ssl.verify_peer);
	#endif
	}else
#endif
	socket = amqp_tcp_socket_new(conn->state);
	int status = 0;
	if (conn->login_info.timeout < 0)
	{
		status = amqp_socket_open(socket, conn->login_info.host, conn->login_info.port);
	}else
	{
		double iptr;
		struct timeval timeout = { (long)conn->login_info.timeout, (long)(modf(conn->login_info.timeout, &iptr)*1000000) };
		status = amqp_socket_open_noblock(socket, conn->login_info.host, conn->login_info.port, &timeout);
	}
	if (status)
	{
		reply.reply_type = AMQP_RESPONSE_LIBRARY_EXCEPTION;
		reply.library_error = status;
		goto error_login;
	}
	reply = amqp_login_with_properties(conn->state, conn->login_info.vhost, conn->login_info.channel_max,
	                                   conn->login_info.frame_max, conn->login_info.heartbeat, &conn->login_info.client_properties,
	                                   conn->login_info.sasl_method, conn->login_info.user, conn->login_info.password);
	if (reply.reply_type != AMQP_RESPONSE_NORMAL) goto error_login;
	memset(conn->channels, 0, sizeof(conn->channels));
	conn->channels[0] |= 0x01;
	init_amqp_pool(&conn->pool, 4096);
	conn->status = 1;
	conn->timestamp = time(NULL);
	return reply;

error_login:
	amqp_destroy_connection(conn->state);
	conn->state = NULL;
	return reply;
}

void close_connection(connection_t *conn)
{
	if (conn->state)
	{
		if (conn->status > 0)
		{
			amqp_connection_close(conn->state, AMQP_REPLY_SUCCESS);
		}
		amqp_maybe_release_buffers(conn->state);
		amqp_destroy_connection(conn->state);
		empty_amqp_pool(&conn->pool);
		conn->state = NULL;
		conn->status = 0;
	}
}

amqp_rpc_reply_t drain_event(lua_State *L, connection_t* conn, amqp_frame_t *frame, amqp_message_t *message, struct timeval *timeout)
{
	amqp_rpc_reply_t r;
	memset(&r, 0, sizeof(amqp_rpc_reply_t));
	do{
		int status = amqp_simple_wait_frame_noblock(conn->state, frame, timeout);
		if (status)
		{
			r.reply_type = AMQP_RESPONSE_LIBRARY_EXCEPTION;
			r.library_error = status;
			return r;
		}
		
		switch(frame->payload.method.id)
		{
		case AMQP_CONNECTION_BLOCKED_METHOD :
		case AMQP_CONNECTION_UNBLOCKED_METHOD :
		case AMQP_BASIC_CANCEL_METHOD :
		case AMQP_BASIC_ACK_METHOD :
		case AMQP_BASIC_NACK_METHOD :
			r.reply_type = AMQP_RESPONSE_NORMAL;
			r.reply = frame->payload.method;
			break;
		case AMQP_BASIC_DELIVER_METHOD :
		case AMQP_BASIC_RETURN_METHOD :
		case AMQP_BASIC_GET_OK_METHOD :
			return amqp_read_message(conn->state, frame->channel, message, 0);
		case AMQP_CHANNEL_CLOSE_METHOD:
		case AMQP_CONNECTION_CLOSE_METHOD:
			r.reply_type = AMQP_RESPONSE_SERVER_EXCEPTION;
			r.reply = frame->payload.method;
			break;
		}
	}while(r.reply_type == 0);
	return r;
}

void decode_frame(lua_State *L, int t, amqp_frame_t *frame)
{
	#define decode_frame_field_s(name) \
		do{ \
			lua_pushlstring(L, r->name.bytes, r->name.len); \
			lua_setfield(L, t, #name); \
		}while(0)
	#define decode_frame_field_i(name) \
		do{ \
			lua_pushinteger(L, r->name); \
			lua_setfield(L, t, #name); \
		}while(0)
	
	lua_pushinteger(L, frame->payload.method.id);
	lua_setfield(L, t, "method");
	lua_pushinteger(L, frame->channel);
	lua_setfield(L, t, "channel");
	switch(frame->payload.method.id)
	{
	case AMQP_BASIC_ACK_METHOD :
	case AMQP_BASIC_NACK_METHOD : {
		amqp_basic_ack_t * r = (amqp_basic_ack_t *)frame->payload.method.decoded;
		decode_frame_field_i(delivery_tag);
		decode_frame_field_i(multiple);
		break;
		}
	case AMQP_BASIC_CANCEL_METHOD : {
		amqp_basic_cancel_t * r = (amqp_basic_cancel_t *)frame->payload.method.decoded;
		decode_frame_field_s(consumer_tag);
		break;
		}
	case AMQP_CONNECTION_BLOCKED_METHOD : {
		amqp_connection_blocked_t * r = (amqp_connection_blocked_t *)frame->payload.method.decoded;
		decode_frame_field_s(reason);
		break;
		}
	case AMQP_BASIC_DELIVER_METHOD : {
		amqp_basic_deliver_t * r = (amqp_basic_deliver_t *)frame->payload.method.decoded;
		decode_frame_field_s(exchange);
		decode_frame_field_s(routing_key);
		decode_frame_field_i(delivery_tag);
		decode_frame_field_i(redelivered);
		decode_frame_field_s(consumer_tag);
		break;
		}
	case AMQP_BASIC_GET_OK_METHOD : {
		amqp_basic_get_ok_t * r = (amqp_basic_get_ok_t *)frame->payload.method.decoded;
		decode_frame_field_s(exchange);
		decode_frame_field_s(routing_key);
		decode_frame_field_i(delivery_tag);
		decode_frame_field_i(redelivered);
		decode_frame_field_i(message_count);
		break;
		}
	case AMQP_BASIC_RETURN_METHOD : {
		amqp_basic_return_t * r = (amqp_basic_return_t *)frame->payload.method.decoded;
		decode_frame_field_s(exchange);
		decode_frame_field_s(routing_key);
		decode_frame_field_i(reply_code);
		decode_frame_field_s(reply_text);
		break;
		}
	}
}

void decode_properties(lua_State *L, amqp_basic_properties_t *properties)
{
	lua_newtable(L);
	
	#define decode_header_field_s(field, flag) \
		do { \
			if (properties->_flags & flag) { \
				lua_pushlstring(L, (const char *)properties->field.bytes, properties->field.len); \
				lua_setfield(L, -2, #field); \
			} \
		}while(0)
	#define decode_header_field_i(field, flag) \
		do { \
			if (properties->_flags & flag) { \
				lua_pushinteger(L, properties->field); \
				lua_setfield(L, -2, #field); \
			} \
		}while(0)

	decode_header_field_s(content_type, AMQP_BASIC_CONTENT_TYPE_FLAG);
	decode_header_field_s(content_encoding, AMQP_BASIC_CONTENT_ENCODING_FLAG);
	decode_header_field_s(type, AMQP_BASIC_TYPE_FLAG);
	decode_header_field_i(timestamp, AMQP_BASIC_TIMESTAMP_FLAG);
	decode_header_field_i(delivery_mode, AMQP_BASIC_DELIVERY_MODE_FLAG);
	decode_header_field_i(priority, AMQP_BASIC_PRIORITY_FLAG);
	decode_header_field_s(expiration, AMQP_BASIC_EXPIRATION_FLAG);
	decode_header_field_s(user_id, AMQP_BASIC_USER_ID_FLAG);
	decode_header_field_s(app_id, AMQP_BASIC_APP_ID_FLAG);
	decode_header_field_s(message_id, AMQP_BASIC_MESSAGE_ID_FLAG);
	decode_header_field_s(reply_to, AMQP_BASIC_REPLY_TO_FLAG);
	decode_header_field_s(correlation_id, AMQP_BASIC_CORRELATION_ID_FLAG);
	decode_header_field_s(cluster_id, AMQP_BASIC_CLUSTER_ID_FLAG);
	
	if ((properties->_flags&AMQP_BASIC_HEADERS_FLAG) && properties->headers.num_entries > 0)
	{
		decode_table(L, &properties->headers);
		lua_setfield(L, -2, "headers");
	}
}

void decode_table(lua_State *L, amqp_table_t *table)
{
	int i;
	lua_newtable(L);
	for(i = 0; i < table->num_entries; i++)
	{
		amqp_table_entry_t *entry = &table->entries[i];
		lua_pushlstring(L, (const char *)entry->key.bytes, entry->key.len);
		if (decode_field(L, &entry->value))
			lua_settable(L, -3);
		else
			lua_pop(L, 1);
	}
}

void decode_array(lua_State *L, amqp_array_t *array)
{
	int i, n = 1;
	lua_newtable(L);
	for(i = 0; i < array->num_entries; i++)
	{
		amqp_field_value_t *entry = &array->entries[i];
		if (decode_field(L, entry))
			lua_rawseti(L, -2, n++);
	}
}

int decode_field(lua_State *L, amqp_field_value_t *entry)
{
	switch (entry->kind)
	{
	case AMQP_FIELD_KIND_BOOLEAN:
		lua_pushboolean(L, entry->value.boolean); break;
	case AMQP_FIELD_KIND_I8:
		lua_pushinteger(L, entry->value.i8); break;
	case AMQP_FIELD_KIND_U8:
		lua_pushinteger(L, entry->value.u8); break;
	case AMQP_FIELD_KIND_I16:
		lua_pushinteger(L, entry->value.i16); break;
	case AMQP_FIELD_KIND_U16:
		lua_pushinteger(L, entry->value.u16); break;
	case AMQP_FIELD_KIND_I32:
		lua_pushinteger(L, entry->value.i32); break;
	case AMQP_FIELD_KIND_U32:
		lua_pushinteger(L, entry->value.u32); break;
	case AMQP_FIELD_KIND_I64:
		lua_pushinteger(L, entry->value.i64); break;
	case AMQP_FIELD_KIND_U64:
		lua_pushinteger(L, entry->value.u64); break;
	case AMQP_FIELD_KIND_F32:
		lua_pushnumber(L, entry->value.f32); break;
	case AMQP_FIELD_KIND_F64:
		lua_pushnumber(L, entry->value.f64); break;
	case AMQP_FIELD_KIND_BYTES:
	case AMQP_FIELD_KIND_UTF8:
		lua_pushlstring(L, (const char *)entry->value.bytes.bytes, entry->value.bytes.len); break;
	case AMQP_FIELD_KIND_TABLE:
		decode_table(L, &entry->value.table); break;
	case AMQP_FIELD_KIND_ARRAY:
		decode_array(L, &entry->value.array); break;
	default : return 0;
	}
	return 1;
}

amqp_bytes_t encode_bytes(const char *s, size_t l, amqp_pool_t *pool)
{
	if (!l) l = strlen(s);
	amqp_bytes_t value;
	value.bytes = (void *)amqp_pool_alloc(pool, l+1);
	memcpy(value.bytes, s, l+1);
	value.len = l;
	return value;
}

void encode_properties(lua_State *L, int t, amqp_basic_properties_t *properties, amqp_pool_t *pool)
{
	#define encode_header_field_s(field, flag) \
		do { \
			lua_getfield(L, t, #field); \
			if (lua_isstring(L, -1)) { \
				properties->field = amqp_cstring_bytes(lua_tostring(L, -1)); \
				properties->_flags |= flag; \
			} \
			lua_pop(L, 1); \
		}while(0)
	#define encode_header_field_i(field, flag) \
		do { \
			lua_getfield(L, t, #field); \
			if (lua_isnumber(L, -1)) { \
				properties->field = lua_tointeger(L, -1); \
				properties->_flags |= flag; \
			} \
			lua_pop(L, 1); \
		}while(0)
	
	properties->_flags = 0;
	encode_header_field_s(content_type, AMQP_BASIC_CONTENT_TYPE_FLAG);
	encode_header_field_s(content_encoding, AMQP_BASIC_CONTENT_ENCODING_FLAG);
	encode_header_field_s(type, AMQP_BASIC_TYPE_FLAG);
	encode_header_field_i(timestamp, AMQP_BASIC_TIMESTAMP_FLAG);
	encode_header_field_i(delivery_mode, AMQP_BASIC_DELIVERY_MODE_FLAG);
	encode_header_field_i(priority, AMQP_BASIC_PRIORITY_FLAG);
	encode_header_field_s(expiration, AMQP_BASIC_EXPIRATION_FLAG);
	encode_header_field_s(user_id, AMQP_BASIC_USER_ID_FLAG);
	encode_header_field_s(app_id, AMQP_BASIC_APP_ID_FLAG);
	encode_header_field_s(message_id, AMQP_BASIC_MESSAGE_ID_FLAG);
	encode_header_field_s(reply_to, AMQP_BASIC_REPLY_TO_FLAG);
	encode_header_field_s(correlation_id, AMQP_BASIC_CORRELATION_ID_FLAG);
	encode_header_field_s(cluster_id, AMQP_BASIC_CLUSTER_ID_FLAG);
	
	lua_getfield(L, t, "headers");
	if (lua_istable(L, -1))
	{
		int h = lua_gettop(L);
		encode_table(L, h, &properties->headers, pool);
		if (properties->headers.num_entries > 0)
			properties->_flags |= AMQP_BASIC_HEADERS_FLAG;
	}
	lua_pop(L, 1);
}

void encode_table(lua_State *L, int t, amqp_table_t *table, amqp_pool_t *pool)
{
	int n = 0;
	lua_pushnil(L);
	while(lua_next(L, t) != 0)
	{
		lua_pop(L, 1);
		n++;
	}
	
	table->num_entries = 0;
	table->entries = (amqp_table_entry_t *)amqp_pool_alloc(pool, sizeof(amqp_table_entry_t) * n);
	lua_pushnil(L);
	while(lua_next(L, t) != 0)
	{
		amqp_table_entry_t *entry = &table->entries[table->num_entries];
		size_t len;
		const char *key = lua_tolstring(L, -2, &len);
		entry->key = encode_bytes(key, len, pool);
		if (encode_field(L, lua_gettop(L), &entry->value, pool))
			table->num_entries++;
		lua_pop(L, 1);
	}
}

void encode_array(lua_State *L, int t, amqp_array_t *array, amqp_pool_t *pool)
{
	int i, n = lua_rawlen(L, t);
	array->num_entries = 0;
	array->entries = (amqp_field_value_t *)amqp_pool_alloc(pool, sizeof(amqp_field_value_t) * n);
	for(i = 1; i <= n; i++)
	{
		lua_rawgeti(L, t, i);
		if (encode_field(L, lua_gettop(L), &array->entries[array->num_entries], pool))
			array->num_entries++;
		lua_pop(L, 1);
	}
}

static int checkout_integer(lua_State *L, int t, long *l, double *f)
{
#if LUA_VERSION_NUM >= 503
	if (lua_isinteger(L, t))
	{
		*l = (long)lua_tointeger(L, t);
		return 1;
	}
	*f = (double)lua_tonumber(L, t);
	return 0;
#else
	*f = (double)lua_tonumber(L, t);
	*l = (long)(*f);
	return ((*f) == (*l));
#endif
}

int encode_field(lua_State *L, int t, amqp_field_value_t *field, amqp_pool_t *pool)
{
	long i;
	double f;
	size_t l;
	const char *s;
	
	switch(lua_type(L, t))
	{
	case LUA_TBOOLEAN :
		field->kind = AMQP_FIELD_KIND_BOOLEAN;
		field->value.boolean = lua_toboolean(L, t);
		break;
	case LUA_TNUMBER :
		if (checkout_integer(L, t, &i, &f))
		{
			if (INT_MIN <= i && i <= INT_MAX)
			{
				field->kind = AMQP_FIELD_KIND_I32;
				field->value.i32 = (int32_t)i;
			}else
			{
				field->kind = AMQP_FIELD_KIND_I64;
				field->value.i64 = (int64_t)i;
			}
		}else
		{
			field->kind = AMQP_FIELD_KIND_F64;
			field->value.f64 = f;
		}
		break;
	case LUA_TSTRING :
		s = lua_tolstring(L, t, &l);
		field->kind = AMQP_FIELD_KIND_UTF8;
		field->value.bytes = encode_bytes(s, l, pool);
		break;
	case LUA_TTABLE :
		if (lua_rawlen(L, t) == 0)
		{
			field->kind = AMQP_FIELD_KIND_TABLE;
			encode_table(L, t, &field->value.table, pool);
		}else
		{
			field->kind = AMQP_FIELD_KIND_ARRAY;
			encode_array(L, t, &field->value.array, pool);
		}
		break;
	case LUA_TNIL :
	default :
		field->kind = AMQP_FIELD_KIND_VOID;
		return 0;
	}
	return 1;
}
