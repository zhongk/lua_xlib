#include <string.h>
#include <stdlib.h>
#include "mini-gmp.h"
#include "lua_util.h"

#define LUA_BIGINT "gmp.bigint"
#define gmp_free(p) ((*gmp_free_func) ((p), 0))

static void (*gmp_free_func) (void *, size_t);

static int bn_new(lua_State *L)
{
	mpz_t *bn = NULL;
	if (lua_type(L, 1) == LUA_TNUMBER)
	{
		bn = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
		mpz_init_set_si(*bn, lua_tointeger(L, 1));
	}else
	{
		const char *s = luaL_checkstring(L, 1);
		bn = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
		if (mpz_init_set_str(*bn, s, 0) == -1)
		{
			mpz_clear(*bn);
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_pushstring(L, "invalid character in string");
			return 2;
		}
	}
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_free(lua_State *L)
{
	mpz_t *bn = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	mpz_clear(*bn);
	return 0;
}

static int bn_tostring(lua_State *L)
{
	mpz_t *bn = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	char *output = mpz_get_str(NULL, 10, *bn);
	lua_pushstring(L, output);
	gmp_free(output);
	return 1;
}

static int bn_hex(lua_State *L)
{
	mpz_t *bn = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	char *output = mpz_get_str(NULL, 16, *bn);
	lua_pushstring(L, output);
	gmp_free(output);
	return 1;
}

static int bn_len(lua_State *L)
{
	mpz_t *bn = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	lua_pushinteger(L, mpz_size(*bn)*sizeof(mp_limb_t));
	return 1;
}

static int nb_check_arg(lua_State *L, int index, mpz_t *bn, mpz_t **b)
{
	if (lua_type(L, index) == LUA_TNUMBER)
	{
		mpz_init_set_si(*bn, lua_tointeger(L, index));
		*b = bn;
		return 1;
	}
	*b = (mpz_t *)luaL_checkudata(L, index, LUA_BIGINT);
	return 0;
}

static int bn_op(lua_State *L, void (*op)(mpz_t, const mpz_t, const mpz_t))
{
	mpz_t *a, *b, an, bn;
	int af = nb_check_arg(L, 1, &an, &a);
	int bf = nb_check_arg(L, 2, &bn, &b);
	
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init(*r);
	op(*r, *a, *b);
	if (af) mpz_clear(an);
	if (bf) mpz_clear(bn);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_op_assign(lua_State *L, void (*op_assign)(mpz_t, const mpz_t, const mpz_t))
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	mpz_t *b, bn;
	int ret = nb_check_arg(L, 2, &bn, &b);
	
	op_assign(*a, *a, *b);
	if (ret) mpz_clear(bn);
	lua_pushboolean(L, 1);
	return 1;
}

static int bn_add(lua_State *L)
{	/* r = a + b */
	return bn_op(L, mpz_add);
}

static int bn_addx(lua_State *L)
{	/* a += b */
	return bn_op_assign(L, mpz_add);
}

static int bn_sub(lua_State *L)
{	/* r = a - b */
	return bn_op(L, mpz_sub);
}

static int bn_subx(lua_State *L)
{	/* a -= b */
	return bn_op_assign(L, mpz_sub);
}

static int bn_mul(lua_State *L)
{	/* r = a * b */
	return bn_op(L, mpz_mul);
}

static int bn_mulx(lua_State *L)
{	/* a *= b */
	return bn_op_assign(L, mpz_mul);
}

static int bn_div(lua_State *L)
{	/* r = a / b */
	return bn_op(L, mpz_fdiv_q);
}

static int bn_divx(lua_State *L)
{	/* a /= b */
	return bn_op_assign(L, mpz_fdiv_q);
}

static int bn_mod(lua_State *L)
{	/* r = a % b */
	return bn_op(L, mpz_fdiv_r);
}

static int bn_and(lua_State *L)
{	/* r = a and b */
	return bn_op(L, mpz_and);
}

static int bn_or(lua_State *L)
{	/* r = a or b */
	return bn_op(L, mpz_ior);
}

static int bn_xor(lua_State *L)
{	/* r = a xor b */
	return bn_op(L, mpz_xor);
}

static int bn_pow(lua_State *L)
{	/* r = a ^ p */
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	unsigned b = (unsigned)luaL_checkinteger(L, 2);
	
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init(*r);
	mpz_pow_ui(*r, *a, b);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_op_shift(lua_State *L, void (*op_shift)(mpz_t, const mpz_t, mp_bitcnt_t))
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	unsigned b = (unsigned)luaL_checkinteger(L, 2);
	
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init(*r);
	op_shift(*r, *a, b);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_lshift(lua_State *L)
{	// r = a << n
	return bn_op_shift(L, mpz_mul_2exp);
}

static int bn_rshift(lua_State *L)
{	// r = a >> n
	return bn_op_shift(L, mpz_fdiv_q_2exp);
}

static int bn_unm(lua_State *L)
{	/* r = -a */
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init(*r);
	mpz_neg(*r, *a);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_abs(lua_State *L)
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init(*r);
	mpz_abs(*r, *a);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_getbit(lua_State *L)
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	int n = (int)luaL_checkinteger(L, 2);
	lua_pushinteger(L, mpz_tstbit(*a, n));
	return 1;
}

static int bn_setbit(lua_State *L)
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	int n = (int)luaL_checkinteger(L, 2);
	int bit = (int)luaL_optinteger(L, 3, 1);
	if (bit)
		mpz_setbit(*a, n);
	else
		mpz_clrbit(*a, n);
	return 0;
}

static int bn_numbits(lua_State *L)
{
	mpz_t *bn = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	lua_pushinteger(L, mpz_sizeinbase(*bn, 2));
	return 1;
}

static int bn_copy(lua_State *L)
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	mpz_t *r = (mpz_t *)lua_newuserdata(L, sizeof(mpz_t));
	mpz_init_set(*r, *a);
	luaL_getmetatable(L, LUA_BIGINT);
	lua_setmetatable(L, -2);
	return 1;
}

static int bn_cmp(lua_State *L)
{
	mpz_t *a = (mpz_t *)luaL_checkudata(L, 1, LUA_BIGINT);
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		lua_pushinteger(L, mpz_cmp_si(*a, lua_tointeger(L, 2)));
		return 1;
	}
	mpz_t *b = (mpz_t *)luaL_checkudata(L, 2, LUA_BIGINT);
	lua_pushinteger(L, mpz_cmp(*a, *b));
	return 1;
}

#define bn_op_cmp(L, op) \
	do{ \
		mpz_t *a, *b, an, bn; \
		int af = nb_check_arg(L, 1, &an, &a); \
		int bf = nb_check_arg(L, 2, &bn, &b); \
		lua_pushboolean(L, mpz_cmp(*a, *b) op 0); \
		if (af) mpz_clear(an); \
		if (bf) mpz_clear(bn); \
	}while(0)

static int bn_eq(lua_State *L)
{	/* a == b */
	bn_op_cmp(L, ==);
	return 1;
}

static int bn_lt(lua_State *L)
{	/* a < b */
	bn_op_cmp(L, <);
	return 1;
}

static int bn_le(lua_State *L)
{	/* a <= b */
	bn_op_cmp(L, <=);
	return 1;
}

static const luaL_Reg bn_lib[] = {
	{"__gc", bn_free},
	{"__call", bn_new},
	{"__tostring", bn_tostring},
	{"__len", bn_len},
	{"__add", bn_add},
	{"__sub", bn_sub},
	{"__mul", bn_mul},
	{"__div", bn_div},
	{"__idiv", bn_div},
	{"__mod", bn_mod},
	{"__pow", bn_pow},
	{"__unm", bn_unm},
	{"__eq", bn_eq},
	{"__lt", bn_lt},
	{"__le", bn_le},
	{"hexdump", bn_hex},
	{"copy", bn_copy},
	{"cmp", bn_cmp},
	{"abs", bn_abs},
	{"add", bn_addx},
	{"sub", bn_subx},
	{"mul", bn_mulx},
	{"div", bn_divx},
	{"__band", bn_and},
	{"__bor", bn_or},
	{"__bxor", bn_xor},
	{"__shl", bn_lshift},
	{"__shr", bn_rshift},
	{"get_bit", bn_getbit},
	{"set_bit", bn_setbit},
	{"num_bits", bn_numbits},
	{NULL, NULL}
};

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_bigint(lua_State *L)
{
	create_metatable(L, LUA_BIGINT, bn_lib);
	lua_pushcfunction(L, bn_new);
#if LUA_VERSION_NUM < 502
	lua_pushvalue(L, -1);
	lua_setglobal(L, "bigint");
#endif
	mp_get_memory_functions(NULL, NULL, &gmp_free_func);
	return 1;
}
