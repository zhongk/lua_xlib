/*****************************************************************************
local python = require('python')

python.dofile(filename)
python.exec(code[, globals[, locals]])
python.eval(code[, globals[, locals]]) return py_obj
python.import(name) return module py_obj
python.object([type='None', value[, arg]]) return py_obj
# type in {'None','bool','int','float','str','string','bytes','tuple','list','set','dict'}
# other type can use python.eval('${typename}') to get type py_obj

python.callable(obj) return bool
python.call(obj[, {args...}])
python.getitem(obj, key) return obj[key]
python.repr(obj) return string repr
python.type(obj) return type name
python.iter(obj) return iterator py_obj
python.dir(obj) return {attr_names...}
python.tolua(obj) return lua object

py_obj:__eq(o)           # py_obj.__eq__(o)
py_obj:__lt(o)           # py_obj.__lt__(o)
py_obj:__le(o)           # py_obj.__le__(o)
py_obj:__add(o)          # py_obj.__add__(o)
py_obj:__sub(o)          # py_obj.__sub__(o)
py_obj:__mul(o)          # py_obj.__mul__(o)
py_obj:__div(o)          # py_obj.__truediv__(o)
py_obj:__mod(o)          # py_obj.__mod__(o)
py_obj:__pow(o)          # py_obj.__pow__(o)
py_obj:__unm()           # py_obj.__neg__(o)

# if lua version >= 5.3
py_obj:__idiv(o)         # py_obj.__floordiv__(o)
py_obj:__band(o)         # py_obj.__and__(o)
py_obj:__bor(o)          # py_obj.__or__(o)
py_obj:__bxor(o)         # py_obj.__xor__(o)
py_obj:__bnot()          # py_obj.__invert__()
py_obj:__shl(o)          # py_obj.__lshift__(o)
py_obj:__shr(o)          # py_obj.__rshift__(o)
# endif

py_obj:__gc()
py_obj:__len()
py_obj:__tostring()
py_obj:__call(args...)  # = python.call(py_obj, {args...})
py_obj:__index(attr) return attr py_obj
# if type(attr) is int:
#   return py_obj[attr]
# elif attr.startwith('.'):
#   return py_obj[attr[1:]]
# else:
#   return getattr(py_obj, attr)

py_obj:_type()       # = python.type(py_obj)
py_obj:_iter()       # = python.iter(py_obj)
py_obj:_dir()        # = python.dir(py_obj)
py_obj:tolua()       # = python.tolua(py_obj)
*****************************************************************************/
#include <Python.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#if LUA_VERSION_NUM <= 501
	#define lua_rawlen lua_objlen
	#define luaL_newlib(L, funcs) luaL_register(L, "python", funcs)
	#define luaL_setfuncs(L, funcs, flag) luaL_openlib(L, NULL, funcs, flag)
#endif

#if PY_MAJOR_VERSION >= 3
	#define PyString_FromString PyUnicode_DecodeFSDefault
	#define PyString_FromStringAndSize PyUnicode_DecodeFSDefaultAndSize
	#define PyString_Decode PyUnicode_Decode
	#define PyString_Check PyUnicode_Check
	
	#define py2lua_str(L, o) \
		do { \
			PyObject *s = PyUnicode_EncodeFSDefault(o); \
			lua_pushlstring(L, PyBytes_AS_STRING(s), PyBytes_GET_SIZE(s)); \
			Py_DECREF(s); \
		}while(0)
#else
	#define py2lua_str(L, o) \
		lua_pushlstring(L, PyString_AS_STRING(o), PyString_GET_SIZE(o))
#endif

static int py2lua_type(lua_State *L, PyObject *o);
static PyObject* lua2py_type(lua_State *L, int index, int type, void*);
static PyObject* lua2py_list(lua_State *L, int index, int type);
static PyObject* lua2py_dict(lua_State *L, int index, int enable_intkey);
static PyObject* lua2py_any(lua_State *L, int index);

#define PYTHON_OBJECT "python.object"

typedef struct{ PyObject *obj; } py_object;

static int py_seterror(lua_State *L, PyObject *exc, PyObject *val)
{
	lua_pushfstring(L, "%s: ", ((PyTypeObject *)exc)->tp_name);
	PyObject *repr = PyObject_Str(val);
	py2lua_str(L, repr);
	Py_DECREF(repr);
	lua_concat(L, 2);
	return 1;
}

static int py_ReportException(lua_State *L, const char *errmsg, int raise_error)
{
	lua_pushnil(L);
	if (PyErr_Occurred())
	{
		PyObject *exc, *val, *tb;
		PyErr_Fetch(&exc, &val, &tb);
		py_seterror(L, exc, val);
	}else
		lua_pushstring(L, errmsg);
	if (raise_error)
		lua_error(L);
	return 2;
}

static int py_newobj(lua_State *L, PyObject *o)
{
	py_object* py = (py_object*)lua_newuserdata(L, sizeof(py_object));
	lua_pushvalue(L, -1);
	py->obj = o;
	luaL_getmetatable(L, PYTHON_OBJECT);
	lua_setmetatable(L, -2);
	return 1;
}

static PyObject* lua_checkdict(lua_State *L, int index);

static int py_run(lua_State *L, int eval)
{
	const char *code = luaL_checkstring(L, 1);
	PyObject *globals = lua_checkdict(L, 2);
	PyObject *locals = lua_checkdict(L, 3);

	if (!globals)
	{
		PyObject *module = PyImport_AddModule("__main__");
		if (!module)
			return py_ReportException(L, "Can't get __main__ module", !eval);
		globals = PyModule_GetDict(module);
		if (!globals)
			return py_ReportException(L, "Can't get __main__.__dict__", !eval);
		Py_INCREF(globals);
	}
	
	PyObject *obj = PyRun_StringFlags(code, eval ? Py_eval_input : Py_single_input, globals, locals, NULL);
	Py_XDECREF(globals);
	Py_XDECREF(locals);
	if (!obj)
		return py_ReportException(L, "Can't run code string", !eval);
	if (!eval) Py_XDECREF(obj);
	return eval ? py_newobj(L, obj) : 0;
}

// python.exec(code[, globals[, locals]])
static int py_exec(lua_State *L)
{
	return py_run(L, 0);
}

// python.eval(code[, globals[, locals]])
static int py_eval(lua_State *L)
{
	return py_run(L, 1);
}

// python.dofile(filename)
static int py_dofile(lua_State *L)
{
	const char *filename = luaL_checkstring(L, 1);
	FILE *fp = fopen(filename, "r");
	luaL_argcheck(L, fp, 1, strerror(errno));
	if (PyRun_SimpleFileEx(fp, filename, 1) != 0)
		luaL_error(L, "Failed to execute python file");
	return 0;
}

// python.import(name) return py_obj
static int py_import(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	PyObject *module = PyImport_ImportModule(name);
	if (!module)
	{
		char errmsg[256];
		sprintf(errmsg, "Can't import module '%s'", name);
		return py_ReportException(L, errmsg, 0);
	}
	return py_newobj(L, module);
}

// python.object(type, value[, arg])
static int py_obj(lua_State *L)
{
	static const char *const typenames[] =
		{"None", "int", "float", "str", "string", "bytes", "bool", "tuple", "list", "set", "dict", NULL};
	static const int const types[] =
		{'0', 'i', 'f', 's', 's', 'y', 'b', 'T', 'L', 'S', 'D', 0};
	int n = luaL_checkoption(L, 1, "None", typenames);
	int type = types[n];
	void *arg = NULL;
	switch(type)
	{
	case 's' :
		arg = (void *)luaL_optstring(L, 3, NULL); // get encoding
		break;
	case 'i' : {
		long base = luaL_optinteger(L, 3, 0);
		luaL_argcheck(L, base==0 || (2<=base && base<=36), 3, "Valid base are 0 and 2-36.");
		arg = (void *)base;
		}break;
	case 'b' :
		luaL_checktype(L, 2, LUA_TBOOLEAN);
		break;
	}
	PyObject *obj = lua2py_type(L, 2, type, arg);
	if (obj) py_newobj(L, obj);
	return 1;
}

// python.type(obj) return type name
static int py_type(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyTypeObject *type = (PyTypeObject *)PyObject_Type(o->obj);
	if (!type)
		return py_ReportException(L, "Can't get object type", 0);
	lua_pushstring(L, type->tp_name);
	Py_DECREF(type);
	return 1;
}

// python.callable(obj)
static int py_callable(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	lua_pushboolean(L, PyCallable_Check(o->obj));
	return 1;
}

static int pycall_result(lua_State *L, PyObject *result)
{
	if (result == Py_NotImplemented)
	{
		Py_DECREF(result);
		luaL_error(L, "NotImplemented");
	}else if (result == Py_None)
	{
		Py_DECREF(result);
		return 0;
	}else if (PyBool_Check(result))
	{
		lua_pushboolean(L, (result == Py_True) ? 1 : 0);
		Py_DECREF(result);
	}else
		py_newobj(L, result);
	return 1;
}

// python.call(obj[, {args...})
static int py_call(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	luaL_argcheck(L, PyCallable_Check(o->obj), 1, "Not callable python object");
	PyObject *args = lua2py_list(L, 2, 'T');
	PyObject *kwargs = lua2py_dict(L, 2, 0);
	PyObject *result = PyObject_Call(o->obj, args, kwargs);
	Py_XDECREF(args);
	Py_XDECREF(kwargs);
	if (!result)
		return py_ReportException(L, "Failed to call python function", 1);
	return pycall_result(L, result);
}

static int py_next(lua_State *L)
{
	PyObject *iter = (PyObject *)lua_touserdata(L, lua_upvalueindex(1));
	PyObject *item = PyIter_Next(iter);
	if (item) return py_newobj(L, item);
	Py_DECREF(iter);
	return 0;
}

// python.iter(obj)
static int py_iter(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *iter = PyObject_GetIter(o->obj);
	if (!iter)
		return py_ReportException(L, "Can't get object iterator", 1);
	lua_pushlightuserdata(L, (void *)iter);
	lua_pushcclosure(L, py_next, 1);
	return 1;
}

static int pyobj_getitem(lua_State *L, PyObject *obj, PyObject *key)
{
	PyObject *item = PyObject_GetItem(obj, key);
	Py_DECREF(key);
	if (!item)
		return py_ReportException(L, "Failed to call getitem(obj)", 0);
	return py_newobj(L, item);
}

// python.getitem(obj, key)
static int py_getitem(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *key = lua2py_any(L, 2);
	return pyobj_getitem(L, o->obj, key);
}

// python.dir(obj)
static int py_dir(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *dir = PyObject_Dir(o->obj);
	if (!dir)
		return py_ReportException(L, "Failed to call dir(obj)", 1);
	py2lua_type(L, dir);
	Py_DECREF(dir);
	return 1;
}

// python.repr(obj)
static int py_repr(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *repr = PyObject_Repr(o->obj);
	if (!repr)
		return py_ReportException(L, "Failed to call repr(obj)", 1);
	py2lua_str(L, repr);
	Py_DECREF(repr);
	return 1;
}

// python.tolua(obj)
static int py_tolua(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	if (!py2lua_type(L, o->obj))
		lua_pushvalue(L, 1);
	return 1;
}

// py_obj:__gc()
static int pyobj_gc(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	Py_XDECREF(o->obj);
	return 0;
}

// py_obj:__len()
static int pyobj_len(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	int len = (int)PyObject_Length(o->obj);
	if (len == -1)
		return py_ReportException(L, "Failed to call len(obj)", 1);
	lua_pushinteger(L, len);
	return 1;
}

// py_obj:__tostring()
static int pyobj_tostring(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *repr = PyObject_Str(o->obj);
	if (repr)
	{
		py2lua_str(L, repr);
		Py_DECREF(repr);
	}else
	{
		if (PyErr_Occurred()) PyErr_Print();
		lua_pushfstring(L, "<userdata %s : %p>", PYTHON_OBJECT, o->obj);
	}
	return 1;
}

// py_obj:__call(args...)
static int pyobj_call(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	luaL_argcheck(L, PyCallable_Check(o->obj), 1, "Not callable python object");
	int i, narg = lua_gettop(L);
	PyObject *args = PyTuple_New(narg - 1);
	for(i=1; i<narg; i++)
	{
		PyObject *item = lua2py_any(L, i+1);
		PyTuple_SetItem(args, i-1, item);
	}
	PyObject *result = PyObject_CallObject(o->obj, args);
	Py_DECREF(args);
	if (!result)
		return py_ReportException(L, "Failed to call python function", 1);
	return pycall_result(L, result);
}

// py_obj:__index(attr)
static int pyobj_index(lua_State *L)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	if (lua_isnumber(L, 2))
	{
		PyObject *key = lua2py_type(L, 2, 'i', NULL);
		return pyobj_getitem(L, o->obj, key);
	}
	
	const char *name = luaL_checkstring(L, 2);
	if (name[0] == '.')
	{
		PyObject *key = PyString_FromString(name+1);
		return pyobj_getitem(L, o->obj, key);
	}
	
	luaL_getmetatable(L, PYTHON_OBJECT);
	lua_getfield(L, -1, name);
	if (lua_iscfunction(L, -1))
	{
		lua_remove(L, -2);
		return 1;
	}
	lua_pop(L, 2);

	PyObject *attr = PyObject_GetAttrString(o->obj, name);
	if (!attr)
	{
		char errmsg[256];
		sprintf(errmsg, "Can't get attribute '%s' from object", name);
		return py_ReportException(L, errmsg, 1);
	}
	return py_newobj(L, attr);
}

static PyObject *lua2py_opval(lua_State *L, int index, PyObject *obj);

static int pyobj_op(lua_State *L, const char *op_name, int oparg)
{
	py_object *o = (py_object *)luaL_checkudata(L, 1, PYTHON_OBJECT);
	PyObject *obj = NULL, *args = NULL;
	if (oparg)
	{
		PyObject *value = lua2py_opval(L, 2, o->obj);
		args = PyTuple_New(1);
		PyTuple_SetItem(args, 0, value);
		if (PyLong_Check(o->obj) && PyFloat_Check(value))
			obj = PyFloat_FromDouble(PyLong_AsDouble(o->obj));
	}
	PyObject *op = PyObject_GetAttrString(obj ? obj : o->obj, op_name);
	Py_XDECREF(obj);
	if (!op)
	{
		Py_XDECREF(args);
		char errmsg[256];
		sprintf(errmsg, "No operator attribute '%s' in object", op_name);
		return py_ReportException(L, errmsg, 1);
	}
	PyObject *result = PyObject_CallObject(op, args);
	Py_XDECREF(args);
	if (!result)
	{
		char errmsg[256];
		sprintf(errmsg, "Invalid argument for object's attribute '%s'", op_name);
		return py_ReportException(L, errmsg, 1);
	}
	return pycall_result(L, result);
}

#define DEF_LUAOP_FUNC(L, lua_op, py_op, oparg) \
	static int pyobj##lua_op(lua_State *L) { return pyobj_op(L, #py_op, oparg); }

DEF_LUAOP_FUNC(L, __eq,   __eq__,       1)
DEF_LUAOP_FUNC(L, __lt,   __lt__,       1)
DEF_LUAOP_FUNC(L, __le,   __le__,       1)
DEF_LUAOP_FUNC(L, __add,  __add__,      1)
DEF_LUAOP_FUNC(L, __sub,  __sub__,      1)
DEF_LUAOP_FUNC(L, __mul,  __mul__,      1)
DEF_LUAOP_FUNC(L, __div,  __truediv__,  1)
DEF_LUAOP_FUNC(L, __mod,  __mod__,      1)
DEF_LUAOP_FUNC(L, __pow,  __pow__,      1)
DEF_LUAOP_FUNC(L, __unm,  __neg__,      0)
#if LUA_VERSION_NUM >= 503
DEF_LUAOP_FUNC(L, __idiv, __floordiv__, 1)
DEF_LUAOP_FUNC(L, __band, __and__,      1)
DEF_LUAOP_FUNC(L, __bor,  __or__,       1)
DEF_LUAOP_FUNC(L, __bxor, __xor__,      1)
DEF_LUAOP_FUNC(L, __bnot, __invert__,   0)
DEF_LUAOP_FUNC(L, __shl,  __lshift__,   1)
DEF_LUAOP_FUNC(L, __shr,  __rshift__,   1)
#endif

static int create_metatable(lua_State *L, const char *name, const luaL_Reg *methods)
{
	if (!luaL_newmetatable(L, name))
		return -1;
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, methods, 0);
	lua_pop(L, 1);
	return 0;
}

static struct luaL_Reg py_libs[] = {
	{"dofile", py_dofile},
	{"exec", py_exec},
	{"eval", py_eval},
	{"import", py_import},
	{"object", py_obj},
	{"type", py_type},
	{"callable", py_callable},
	{"call", py_call},
	{"iter", py_iter},
	{"getitem", py_getitem},
	{"dir", py_dir},
	{"repr", py_repr},
	{"tolua", py_tolua},
	{NULL, NULL}
};

#define REG_LUAOP_FUNC(op)  {#op, pyobj##op}
static struct luaL_Reg pyobj_libs[] = {
	REG_LUAOP_FUNC(__eq),
	REG_LUAOP_FUNC(__lt),
	REG_LUAOP_FUNC(__le),
	REG_LUAOP_FUNC(__add),
	REG_LUAOP_FUNC(__sub),
	REG_LUAOP_FUNC(__mul),
	REG_LUAOP_FUNC(__div),
	REG_LUAOP_FUNC(__mod),
	REG_LUAOP_FUNC(__pow),
	REG_LUAOP_FUNC(__unm),
#if LUA_VERSION_NUM >= 503
	REG_LUAOP_FUNC(__idiv),
	REG_LUAOP_FUNC(__band),
	REG_LUAOP_FUNC(__bor),
	REG_LUAOP_FUNC(__bxor),
	REG_LUAOP_FUNC(__bnot),
	REG_LUAOP_FUNC(__shl),
	REG_LUAOP_FUNC(__shr),
#endif

	{"__gc", pyobj_gc},
	{"__len", pyobj_len},
	{"__call", pyobj_call},
	{"__index", pyobj_index},
	{"__tostring", pyobj_tostring},
	{"_type", py_type},
	{"_iter", py_iter},
	{"_dir", py_dir},
	{"tolua", py_tolua},
	{NULL, NULL}
};

#ifdef __cplusplus
exetrn "C"
#endif
int luaopen_python(lua_State *L)
{
	Py_Initialize();
	create_metatable(L, PYTHON_OBJECT, pyobj_libs);
	luaL_newlib(L, py_libs);
	return 1;
}

PyObject* lua2py_type(lua_State *L, int index, int type, void *arg)
{
	PyObject *obj = NULL;
	int ltype = lua_type(L, index);
	switch(type)
	{
	case '0': // None
		Py_INCREF(Py_None);
		obj = Py_None;
		break;
	case 'i': // int
		if (ltype == LUA_TSTRING)
		{
			char *v = (char *)lua_tostring(L, index);
			obj = PyLong_FromString(v, NULL, (long)arg);
		}else
		{
			long v = luaL_checkinteger(L, index);
			obj = PyLong_FromLong(v);
		}
		break;
	case 'f': { // float
		double v = luaL_checknumber(L, index);
		obj = PyFloat_FromDouble(v);
		}break;
	case 's': { // str
		size_t len = 0;
		const char *v = luaL_checklstring(L, index, &len);
		obj = (!arg) ?
			PyString_FromStringAndSize(v, (Py_ssize_t)len) :
			PyString_Decode(v, (Py_ssize_t)len, (const char *)arg, "strict");
		}break;
	case 'y': { // bytes
		size_t len = 0;
		const char *v = luaL_checklstring(L, index, &len);
		obj = PyBytes_FromStringAndSize(v, (Py_ssize_t)len);
		}break;
	case 'b': { // bool
		long v = lua_toboolean(L, index);
		obj = PyBool_FromLong(v);
		}break;
	case 'T': // tuple
	case 'L': // list
	case 'S': // set
		obj = lua2py_list(L, index, type);
		break;
	case 'D': // dict
		obj = lua2py_dict(L, index, 1);
		break;
	default :
		luaL_error(L, "Unknown python type for conversion");
	}
	if (!obj)
		py_ReportException(L, "Failed to convert lua type to python object", 1);
	return obj;
}

PyObject *lua2py_any(lua_State *L, int index)
{
	PyObject *obj = NULL;
	switch(lua_type(L, index))
	{
	case LUA_TNIL :
	case LUA_TNONE :
		obj = lua2py_type(L, index, '0', NULL);
		break;
	case LUA_TSTRING :
		obj = lua2py_type(L, index, 's', NULL);
		break;
	case LUA_TBOOLEAN :
		obj = lua2py_type(L, index, 'b', NULL);
		break;
	case LUA_TNUMBER : {
	#if LUA_VERSION_NUM >= 503
		obj = (lua_isinteger(L, index)) ?
		      PyLong_FromLong(lua_tointeger(L, index)) :
		      PyFloat_FromDouble(lua_tonumber(L, index));
	#else
		double v = lua_tonumber(L, index);
		obj = (v != (long)v) ? PyFloat_FromDouble(v) : PyLong_FromDouble(v);
	#endif
		}break;
	case LUA_TTABLE : {
		int n = lua_rawlen(L, index);
		obj = lua2py_type(L, index, n ? 'L' : 'D', NULL);
		}break;
	case LUA_TUSERDATA : {
		py_object *o = (py_object *)luaL_checkudata(L, index, PYTHON_OBJECT);
		Py_INCREF(o->obj);
		obj = o->obj;
		}break;
	default:
		luaL_argcheck(L, 0, index, "Unsupport python type conversion");
	}
	return obj;
}

PyObject* lua2py_list(lua_State *L, int index, int type)
{
	PyObject *obj;
	int i, n = 0;
	if (!lua_isnoneornil(L, index))
	{
		luaL_checktype(L, index, LUA_TTABLE);
		n = lua_rawlen(L, index);
	}
	switch(type)
	{
	case 'T' : obj = PyTuple_New(n); break;
	case 'L' : obj = PyList_New(n); break;
	case 'S' : obj = PySet_New(NULL); break;
	}
	if (!obj) return NULL;
	
	for(i=0; i<n; i++)
	{
		lua_rawgeti(L, index, i+1);
		PyObject *item = lua2py_any(L, lua_gettop(L));
		switch(type)
		{
		case 'T' : PyTuple_SetItem(obj, i, item); break;
		case 'L' : PyList_SetItem(obj, i, item); break;
		case 'S' : PySet_Add(obj, item); Py_DECREF(item); break;
		}
		lua_pop(L, 1);
	}
	return obj;
}

PyObject* lua2py_dict(lua_State *L, int index, int enable_intkey)
{
	if (lua_isnoneornil(L, index))
		return PyDict_New();
	luaL_checktype(L, index, LUA_TTABLE);
	PyObject *obj = PyDict_New();
	if (!obj) return NULL;
	
	lua_pushnil(L);
	while(lua_next(L, index) != 0)
	{
		int intkey = lua_isnumber(L,-2);
		if (!intkey || enable_intkey)
		{
			PyObject *key = lua2py_type(L, -2, intkey ? 'i' : 's', NULL);
			PyObject *value = lua2py_any(L, lua_gettop(L));
			PyDict_SetItem(obj, key, value);
			Py_DECREF(key);
			Py_DECREF(value);
		}
		lua_pop(L, 1);
	}
	return obj;
}

PyObject *lua2py_opval(lua_State *L, int index, PyObject *obj)
{
	switch(lua_type(L, index))
	{
	case LUA_TSTRING :
		return lua2py_type(L, index, PyString_Check(obj) ? 's' : 'y', NULL);
	case LUA_TTABLE :
		if (PyTuple_Check(obj))
			return lua2py_list(L, index, 'T');
		else if (PyList_Check(obj))
			return lua2py_list(L, index, 'L');
		else if (PySet_Check(obj))
			return lua2py_list(L, index, 'S');
		else if (PyDict_Check(obj))
			return lua2py_dict(L, index, 1);
	}
	return lua2py_any(L, index);
}

PyObject* lua_checkdict(lua_State *L, int index)
{
	PyObject *obj = NULL;
	switch(lua_type(L, index))
	{
	case LUA_TNIL :
	case LUA_TNONE :
		break;
	case LUA_TTABLE :
		obj = lua2py_type(L, index, 'D', NULL);
		break;
	default : {
		py_object *v = (py_object *)luaL_checkudata(L, index, PYTHON_OBJECT);
		luaL_argcheck(L, PyDict_Check(v->obj), index, "Not table or python dict object");
		Py_INCREF(v->obj);
		obj = v->obj;
		}
	}
	return obj;
}

static int py2lua_list(lua_State *L, PyObject *o);
static int py2lua_dict(lua_State *L, PyObject *o);

int py2lua_type(lua_State *L, PyObject *o)
{
	if (o == Py_None)
	{
		lua_pushnil(L);
	}else if (PyBool_Check(o))
	{
		lua_pushboolean(L, o==Py_True ? 1 : 0);
	}else if (PyString_Check(o))
	{
		py2lua_str(L, o);
	}else if (PyBytes_Check(o))
	{
		lua_pushlstring(L, PyBytes_AS_STRING(o), PyBytes_GET_SIZE(o));
#if PY_MAJOR_VERSION < 3
	}else if (PyInt_Check(o))
	{
		lua_pushinteger(L, PyInt_AsLong(o));
#endif
	}else if (PyLong_Check(o))
	{
		int overflow = 0;
		long v = PyLong_AsLongAndOverflow(o, &overflow);
		if (!overflow)
			lua_pushinteger(L, v);
		else
			lua_pushnumber(L, PyLong_AsDouble(o));
	}else if (PyFloat_Check(o))
	{
		lua_pushnumber(L, PyFloat_AsDouble(o));
	}else if (PyTuple_Check(o) || PyList_Check(o) || PySet_Check(o))
	{
		py2lua_list(L, o);
	}else if (PyDict_Check(o))
	{
		py2lua_dict(L, o);
	}else
	{
		return 0;
	}
	PyErr_Clear();
	return 1;
}

int py2lua_list(lua_State *L, PyObject *o)
{
	lua_newtable(L);
	PyObject *item, *iter = PyObject_GetIter(o);
	int n = 0;
	while((item = PyIter_Next(iter)) != NULL)
	{
		if (!py2lua_type(L, item))
		{
			Py_INCREF(item);
			py_newobj(L, item);
		}
		lua_rawseti(L, -2, ++n);
		Py_DECREF(item);
	}
	Py_DECREF(iter);
	return 1;
}

int py2lua_dict(lua_State *L, PyObject *o)
{
	lua_newtable(L);
	PyObject *key, *value;
	Py_ssize_t pos = 0;
	while (PyDict_Next(o, &pos, &key, &value))
	{
		if (PyTuple_Check(key))
			luaL_error(L, "Can't convert tuple key in dict");
		py2lua_type(L, key);
		if (!py2lua_type(L, value))
		{
			Py_INCREF(value);
			py_newobj(L, value);
		}
		lua_settable(L, -3);
	}
	return 1;
}
