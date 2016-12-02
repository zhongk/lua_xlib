/* LUA API  (in rapidxml.so):
    local rapidxml = require('rapidxml')
    rapidxml.node_type            -- {element, data, cdata, comment, declaration, doctype, pi}
    
	rapidxml.new(string name) return userdata xmldoc
	rapidxml.parse(string data[, {parse_types}]) return userdata xmldoc
	xmldoc:root() return userdata xmlnode
	xmldoc:dump([no_indent = false]) return string
	xmldoc:declaration{[version='1.0',][encoding='utf-8',][standalone]}  -- to set declaration OR
	xmldoc:declaration() return table xml declaration

	xmlnode:__len() return count of child nodes
	xmlnode:isroot() return boolean
	xmlnode:name() return string name
	xmlnode:type() return node_type
	xmlnode:attr()                -- get all attributes
	xmlnode:attr(string key)      -- get key attribute
	xmlnode:attr(key, value)      -- set key attribute
	xmlnode:attr{...}             -- set attributes
	xmlnode:delattr(string key)   -- remove attributed
	xmlnode:value()               -- get value
	xmlnode:value(string text)    -- set value
	xmlnode:prev([string name]) return userdata prev xmlnode
	xmlnode:next([string name]) return userdata next xmlnode
	xmlnode:child([string name]) return userdata child xmlnode
	xmlnode:children([string name]) return iterator of userdata child xmlnode
	-- Appends a new child node(node_type is element) OR
	xmlnode:append(string name[, string value]) return userdata child xmlnode
	-- Appends a new child node(node_type in {data, cdata, comment, doctype, pi})
	xmlnode:append(int node_type[, string content]) return userdata child xmlnode
	-- Prepends a new child node, arguments like xmlnode:append
	xmlnode:prepend(...) return userdata child xmlnode
	-- Inserts a new sibling node at this node position, arguments like xmlnode:append
	xmlnode:insert(...) return userdata sibling xmlnode
	xmlnode:remove()
*/
#include <rapidxml.hpp>
#include <rapidxml_print.hpp>
#include <lua.hpp>

#define LUA_XMLDOC	"rapidxml.document"
#define LUA_XMLNODE	"rapidxml.node"

typedef rapidxml::xml_document<> xmldoc_t;
typedef rapidxml::xml_node<> xmlnode_t;
typedef rapidxml::xml_attribute<> xmlattr_t;

typedef struct
{
	xmldoc_t *doc;
	int ref_xml;
} xmldoc_data;
typedef struct
{
	xmldoc_t *doc;
	xmlnode_t *node;
} xmlnode_data;

static int xml_new(lua_State *L);
static int xml_parse(lua_State *L);
static int doc_gc(lua_State *L);
static int doc_dump(lua_State *L);
static int doc_decl(lua_State *L);
static int doc_root(lua_State *L);
static int node_isroot(lua_State *L);
static int node_name(lua_State *L);
static int node_type(lua_State *L);
static int node_len(lua_State *L);
static int node_attr(lua_State *L);
static int node_delattr(lua_State *L);
static int node_text(lua_State *L);
static int node_prev(lua_State *L);
static int node_next(lua_State *L);
static int node_child(lua_State *L);
static int node_children(lua_State *L);
static int node_append(lua_State *L);
static int node_prepend(lua_State *L);
static int node_insert(lua_State *L);
static int node_remove(lua_State *L);

static int report_error(lua_State *L, const char *fmt, ...)
{
	va_list ap;
	lua_pushnil(L);
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	return 2;
}

static int create_metatable(lua_State *L, const char *name, const luaL_Reg *methods)
{
	if (!luaL_newmetatable(L, name))
		return 0;

	/* define methods */
#if LUA_VERSION_NUM > 501
	luaL_setfuncs(L, methods, 0);
#else
	luaL_openlib(L, NULL, methods, 0);
#endif

	/* define metamethods */
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);

	lua_pop(L, 1);
	return 1;
}

static struct luaL_Reg xml_libs[] = {
	{"new", xml_new},
	{"parse", xml_parse},
	{NULL, NULL}
};
static struct luaL_Reg xmldoc_libs[] = {
	{"__gc", doc_gc},
	{"__tostring", doc_dump},
	{"dump", doc_dump},
	{"root", doc_root},
	{"declaration", doc_decl},
	{NULL, NULL}
};
static struct luaL_Reg xmlnode_libs[] = {
	{"__len", node_len},
	{"isroot", node_isroot},
	{"name", node_name},
	{"type", node_type},
	{"value", node_text},
	{"attr", node_attr},
	{"delattr", node_delattr},
	{"prev", node_prev},
	{"next", node_next},
	{"child", node_child},
	{"children", node_children},
	{"prepend", node_prepend},
	{"append", node_append},
	{"insert", node_insert},
	{"remove", node_remove},
	{NULL, NULL}
};

extern "C"
int luaopen_rapidxml(lua_State *L)
{
	create_metatable(L, LUA_XMLNODE, xmlnode_libs);

#if LUA_VERSION_NUM > 501
	luaL_newlib(L, xml_libs);
#else
	luaL_register(L, "rapidxml", xml_libs);
#endif
	lua_pushvalue(L, -1);
	create_metatable(L, LUA_XMLDOC, xmldoc_libs);
	lua_setmetatable(L, -2);
	
	lua_newtable(L);
	lua_pushinteger(L, rapidxml::node_element);
	lua_setfield(L, -2, "element");
	lua_pushinteger(L, rapidxml::node_data);
	lua_setfield(L, -2, "data");
	lua_pushinteger(L, rapidxml::node_cdata);
	lua_setfield(L, -2, "cdata");
	lua_pushinteger(L, rapidxml::node_comment);
	lua_setfield(L, -2, "comment");
	lua_pushinteger(L, rapidxml::node_declaration);
	lua_setfield(L, -2, "declaration");
	lua_pushinteger(L, rapidxml::node_doctype);
	lua_setfield(L, -2, "doctype");
	lua_pushinteger(L, rapidxml::node_pi);
	lua_setfield(L, -2, "pi");
	lua_setfield(L, -2, "node_type");
	
	return 1;
}

static int new_xmldoc(lua_State *L, xmldoc_t *doc, int ref = -1)
{
	xmldoc_data *pnew = (xmldoc_data *)lua_newuserdata(L, sizeof(xmldoc_data));
	pnew->doc = doc;
	pnew->ref_xml = ref;
	luaL_getmetatable(L, LUA_XMLDOC);
	lua_setmetatable(L, -2);
	return 1;
}
static int new_xmlnode(lua_State *L, xmldoc_t *doc, xmlnode_t *node)
{
	xmlnode_data *pnew = (xmlnode_data *)lua_newuserdata(L, sizeof(xmlnode_data));
	pnew->doc = doc;
	pnew->node = node;
	luaL_getmetatable(L, LUA_XMLNODE);
	lua_setmetatable(L, -2);
	return 1;
}

int xml_new(lua_State *L)
{
	size_t len;
	const char *name = luaL_checklstring(L, 1, &len);
	xmldoc_t *doc = new xmldoc_t();
	doc->append_node(doc->allocate_node(rapidxml::node_element, doc->allocate_string(name)));
	return new_xmldoc(L, doc);
}

inline void xmldoc_parse(xmldoc_t *doc, char *xml, int flags)
{
//	const int P0 = rapidxml::parse_non_destructive;
	const int P0 = 0;
	const int P1 = rapidxml::parse_declaration_node;
	const int P2 = rapidxml::parse_comment_nodes;
	const int P3 = rapidxml::parse_pi_nodes;
	const int P4 = rapidxml::parse_doctype_node;
	switch(flags)
	{
	#define case_ParseFlags(Flags) \
		case (Flags) : doc->parse<P0 | Flags>(xml); break
		case_ParseFlags(P1);
		case_ParseFlags(P2);
		case_ParseFlags(P3);
		case_ParseFlags(P4);
		case_ParseFlags(P1 | P2);
		case_ParseFlags(P1 | P3);
		case_ParseFlags(P1 | P4);
		case_ParseFlags(P2 | P3);
		case_ParseFlags(P2 | P4);
		case_ParseFlags(P3 | P4);
		case_ParseFlags(P1 | P2 | P3);
		case_ParseFlags(P1 | P2 | P4);
		case_ParseFlags(P1 | P3 | P4);
		case_ParseFlags(P2 | P3 | P4);
		case_ParseFlags(P1 | P2 | P3 | P4);
		default : doc->parse<P0>(xml);
	}
}

int xml_parse(lua_State *L)
{
	char *xml = (char *)luaL_checkstring(L, 1);
	xmldoc_t *doc = new xmldoc_t();
	int flags = 0;
	if (lua_istable(L, 2))
	{
	#if LUA_VERSION_NUM > 501
		int n = lua_rawlen(L, 2);
	#else
		int n = lua_objlen(L, 2);
	#endif
		for(int i=1; i<=n; i++)
		{
			lua_rawgeti(L, 2, i);
			switch(luaL_checkinteger(L, -1))
			{
			case rapidxml::node_declaration :
				flags |= rapidxml::parse_declaration_node;
				break;
			case rapidxml::node_comment :
				flags |= rapidxml::parse_comment_nodes;
				break;
			case rapidxml::node_pi :
				flags |= rapidxml::parse_pi_nodes;
				break;
			case rapidxml::node_doctype :
				flags |= rapidxml::parse_doctype_node;
				break;
			}
		}
	}
	try{
		xmldoc_parse(doc, xml, flags);
	}catch(rapidxml::parse_error &e)
	{
		int ret = report_error(L, e.what());
		delete doc;
		return ret;
	}
	lua_pushvalue(L, 1);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return new_xmldoc(L, doc, ref);
}

int doc_gc(lua_State *L)
{
	xmldoc_data *dp = (xmldoc_data *)luaL_checkudata(L, 1, LUA_XMLDOC);
	dp->doc->clear();
	delete dp->doc;
	if (dp->ref_xml != -1) luaL_unref(L, LUA_REGISTRYINDEX, dp->ref_xml);
	dp->doc = NULL;
	dp->ref_xml = -1;
	return 0;
}

int doc_dump(lua_State *L)
{
	xmldoc_data *dp = (xmldoc_data *)luaL_checkudata(L, 1, LUA_XMLDOC);
	int no_indent = lua_toboolean(L, 2);
	std::string xml;
	rapidxml::print(std::back_inserter(xml), *dp->doc, no_indent ? rapidxml::print_no_indenting : 0);
	lua_pushlstring(L, xml.c_str(), xml.length());
	return 1;
}

int doc_decl(lua_State *L)
{
	xmldoc_data *dp = (xmldoc_data *)luaL_checkudata(L, 1, LUA_XMLDOC);
	xmlnode_t *decl = NULL;
	for(xmlnode_t *node = dp->doc->first_node(); node; node = node->next_sibling())
	{
		if (node->type() == rapidxml::node_declaration)
			decl = node;
	}

	if (lua_istable(L, 2))
	{	// set declaration attributes
		if (decl) dp->doc->remove_node(decl);
		decl = dp->doc->allocate_node(rapidxml::node_declaration);
	
	#define decl_getattr(attr, defval) \
		do{ \
			lua_getfield(L, 2, attr); \
			const char *s = luaL_optstring(L, -1, defval); \
			if (s) \
			{ \
				const char *name = dp->doc->allocate_string(attr); \
				const char *value = dp->doc->allocate_string(s); \
				decl->append_attribute(dp->doc->allocate_attribute(name, value)); \
			} \
			lua_pop(L, 1); \
		}while(0)
			
		decl_getattr("version", "1.0");
		decl_getattr("encoding", "utf-8");
		decl_getattr("standalone", NULL);
		dp->doc->prepend_node(decl);
		lua_pushboolean(L, 1);
		return 1;
	}
	
	// get declaration attributes
	if (!decl) return 0;
	xmlattr_t *attr = decl->first_attribute();
	if (!attr) return 0;
	lua_newtable(L);
	for(; attr; attr = attr->next_attribute())
	{
		lua_pushlstring(L, attr->name(), attr->name_size());
		lua_pushlstring(L, attr->value(), attr->value_size());
		lua_settable(L, -3);
	}
	return 1;
}

int doc_root(lua_State *L)
{
	xmldoc_data *dp = (xmldoc_data *)luaL_checkudata(L, 1, LUA_XMLDOC);
	xmlnode_t *root = dp->doc->first_node();
	while(root->type() != rapidxml::node_element)
		root = root->next_sibling();
	return new_xmlnode(L, dp->doc, root);
}

int node_isroot(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	lua_pushboolean(L, np->node->parent()==np->doc ? 1 : 0);
	return 1;
}

int node_name(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	lua_pushlstring(L, np->node->name(), np->node->name_size());
	return 1;
}

int node_type(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	lua_pushinteger(L, np->node->type());
	return 1;
}

int node_len(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	int nchild = 0;
	xmlnode_t *node = np->node->first_node();
	while(node)
	{
		nchild++;
		node = node->next_sibling();
	}
	lua_pushinteger(L, nchild);
	return 1;
}

int node_attr(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	if (lua_istable(L, 2))
	{	// set attributes
		lua_pushnil(L);
		while(lua_next(L, 2) != 0)
		{
			if (lua_type(L, -2) == LUA_TSTRING)
			{
				const char *name = lua_tostring(L, -2);
				const char *value = np->doc->allocate_string(lua_tostring(L, -1));
				xmlattr_t *attr = np->node->first_attribute(name);
				if (!attr)
				{
					attr = np->doc->allocate_attribute(np->doc->allocate_string(name), value);
					np->node->append_attribute(attr);
				}else
					attr->value(value);
			}
			lua_pop(L, 1);
		}
		lua_pushboolean(L, 1);
		return 1;
	}
	
	const char *name = luaL_optstring(L, 2, NULL);
	const char *value = luaL_optstring(L, 3, NULL);
	if (!name)
	{ 	// get attributes
		xmlattr_t *attr = np->node->first_attribute();
		if (!attr) return 0;
		int i = 0;
		lua_newtable(L);
		for(; attr; attr = attr->next_attribute())
		{
			lua_pushlstring(L, attr->name(), attr->name_size());
			lua_pushlstring(L, attr->value(), attr->value_size());
			lua_settable(L, -3);
		}
	}else
	if (!value)
	{ 	// get attribute with name
		xmlattr_t *attr = np->node->first_attribute(name);
		if (!attr) return 0;
		lua_pushlstring(L, attr->value(), attr->value_size());
	}else
	{	// get attributes
		xmlattr_t *attr = np->node->first_attribute(name);
		const char *_value = np->doc->allocate_string(value);
		if (!attr)
		{
			attr = np->doc->allocate_attribute(np->doc->allocate_string(name), _value);
			np->node->append_attribute(attr);
		}else
			attr->value(_value);
		lua_pushboolean(L, 1);
	}
	
	return 1;
}

static int node_delattr(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	const char *name = luaL_checkstring(L, 2);
	xmlattr_t *attr = np->node->first_attribute(name);
	if (attr)
	{
		np->node->remove_attribute(attr);
		lua_pushboolean(L, 1);
	}else
		lua_pushboolean(L, 0);
	return 1;
}

int node_text(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	size_t len;
	const char *text = luaL_optlstring(L, 2, NULL, &len);
	if (!text)
	{
		if (np->node->value_size()>0)
			lua_pushlstring(L, np->node->value(), np->node->value_size());
		else
			lua_pushnil(L);
	}else
	{
		np->node->value(np->doc->allocate_string(text));
		lua_pushboolean(L, 1);
	}
	return 1;
}

int node_prev(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	const char *name = luaL_optstring(L, 2, NULL);
	xmlnode_t *prev = np->node->previous_sibling(name);
	if (!prev) return 0;
	return new_xmlnode(L, np->doc, prev);
}

int node_next(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	const char *name = luaL_optstring(L, 2, NULL);
	xmlnode_t *next = np->node->next_sibling(name);
	if (!next) return 0;
	return new_xmlnode(L, np->doc, next);
}

int node_child(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	const char *name = luaL_optstring(L, 2, NULL);
	xmlnode_t *child = np->node->first_node(name);
	if (!child) return 0;
	return new_xmlnode(L, np->doc, child);
}

static int node_nextchild(lua_State *L)
{
	xmlnode_data *np1 = (xmlnode_data *)lua_touserdata(L, 1);
	xmlnode_data *np2 = (xmlnode_data *)lua_touserdata(L, 2);
	const char *name = (const char *)luaL_optstring(L, lua_upvalueindex(1), NULL);
	
	xmlnode_t *node = NULL;
	if (!np2)
		node = np1->node->first_node(name);
	else
		node = np2->node->next_sibling(name);
	if (!node) return 0;
	return new_xmlnode(L, np1->doc, node);
}

int node_children(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	lua_pushvalue(L, 2);
	lua_pushcclosure(L, node_nextchild, 1);
	lua_pushlightuserdata(L, np);
	lua_pushnil(L);
	return 3;
}

static xmlnode_t *make_node(lua_State *L, int arg, xmldoc_t *doc)
{
	xmlnode_t *node = NULL;
	if (lua_isnumber(L, arg))
	{	// append(node_type, content)
		rapidxml::node_type type = (rapidxml::node_type)
		luaL_checkinteger(L, arg);
		luaL_argcheck(L,
			(type==rapidxml::node_comment || type==rapidxml::node_pi ||
			 type==rapidxml::node_data || type==rapidxml::node_cdata),
			2, "node_type only in {data, cdata, comment, doctype, pi}");
		const char *content = luaL_optstring(L, arg+1, NULL);
		char *value = content ? doc->allocate_string(content) : NULL;
		if (type==rapidxml::node_pi)
		{
			luaL_argcheck(L, value, arg+1, "There's not content when node_type is pi");
			node = doc->allocate_node(type, value);
		}else
		{
			node = doc->allocate_node(type, NULL);
			if (value) node->value(value);
		}
	}else
	{	// append(name[, value])
		const char *name = luaL_checkstring(L, arg);
		const char *text = luaL_optstring(L, arg+1, NULL);
		char *value = text ? doc->allocate_string(text) : NULL;
		node = doc->allocate_node(rapidxml::node_element, doc->allocate_string(name), value);
	}
	return node;
}

int node_append(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	xmlnode_t *node = make_node(L, 2, np->doc);
	np->node->append_node(node);
	return new_xmlnode(L, np->doc, node);
}

int node_prepend(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	xmlnode_t *node = make_node(L, 2, np->doc);
	np->node->prepend_node(node);
	return new_xmlnode(L, np->doc, node);
}

int node_insert(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	xmlnode_t *parent = np->node->parent();
	luaL_argcheck(L, (parent && parent!=np->doc), 1, "Can't insert new sibling node");
	xmlnode_t *node = make_node(L, 2, np->doc);
	parent->insert_node(np->node, node);
	return new_xmlnode(L, np->doc, node);
}

int node_remove(lua_State *L)
{
	xmlnode_data *np = (xmlnode_data *)luaL_checkudata(L, 1, LUA_XMLNODE);
	xmlnode_t *parent = np->node->parent();
	if (parent)
	{
		parent->remove_node(np->node);
		lua_pushboolean(L, 1);
	}else
		lua_pushboolean(L, 0);
	return 1;
}