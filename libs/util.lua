function tuple(...)
	local self = {}
	local _tuple = {
		__index = {...},
		__class = 'tuple',
		__newindex = function(t, k, v)
			error("'tuple' does not support item assignment", 2)
		end
	}
	setmetatable(self, _tuple)
	return self
end

------------------------------------------------------------------------------------

local _list = { __class = 'list' }

function list(...)
	local self = { _items={...} }
	setmetatable(self, _list)
	return self
end

function _list:append(value)
	table.insert(self._items, value)
end

function _list:clear()
	self._items = {}
end

function _list:items()
	local pos = 0
	return function()
		pos = pos+1
		return self._items[pos]
	end
end

function _list:slice(start, stop)
	if not stop then
		start, stop = 1, start
	end
	if start < 0 then start = #self._items + 1 + start end
	if stop < 0 then stop = #self._items + 1 + stop end
	
	local object = list()
	if start<=#self._items and stop>0 then
		if start < 1 then start = 1 end
		if stop > #self._items then stop = #self._items end
		for i = start, stop do object:append(self._items[i]) end
	end
	return object
end

function _list:copy()
	return self:slice(-1)
end

function _list:count(value)
	local n = 0
	for _,v in ipairs(self._items) do
		if v==value then n=n+1 end
	end
	return n
end

function _list:extend(object)
	assert(getmetatable(object) == _list)
	local m, n = #self.items, #object.items
	for i = 1, n do
		self.items[m+i] = object.items[i]
	end
end

function _list:index(value, start, stop)
	start = start or 0
	stop = stop or #self._items
	for i = start, stop do
		if self._items[i]==value then return i end
	end
	return nil
end

function _list:insert(index, value)
	table.insert(self._items, index, value)
end

function _list:pop(index)
	local v = self._items[index or #self._items]
	table.remove(self._items, index)
	return v
end

function _list:remove(value)
	local n = #self._items
	for i = n, 1, -1 do
		if self._items[i]==value then table.remove(self._items, i) end
	end
end

function _list:reverse()
	local n = #self._items
	for i = 1, n/2 do
		self._items[i], self._items[n+1-i] = self._items[n+1-i], self._items[i]
	end
end

function _list:sort(compare)
	table.sort(self._items, compare)
end

function _list:__len()
	return #self._items
end

function _list:__add(object)
	return self:extend(object)
end

function _list:__mul(times)
	local l = list()
	for i = 1, times do l:extend(self) end
	return l
end

function _list:__eq(object)
	assert(getmetatable(object) == _list)
	if #self._items==#object.items then
		for i = 1, #self._items do
			if self._items[i]~=object.items[i] then return false end
		end
		return true
	end
	return false
end

function _list:__lt(object)
	assert(getmetatable(object) == _list)
	local n = math.min(#self._items, #object.items)
	for i = 1, n do
		if self._items[i]~=object.items[i] then
			return (self._items[i] < object.items[i])
		end
	end
	return (#self._items < #object.items)
end

function _list:__index(index)
	if type(index)~='number' then return _list[index] end
	if index < 0 then index = #self._items + 1 + index end
	return self._items[index]
end

function _list:__newindex(index, value)
	assert(type(index) == 'number')
	local n = #self._items
	assert(-n<=index and index~=0 and index<=n)
	if index < 0 then index = n + index + 1 end
	if value ~= nil then
		self._items[index] = value
	else
		error('value can not set to nil')
	end
end

------------------------------------------------------------------------------------

local _set = { __class = 'set' }

function set(...)
	local self = { _hash={}, n=0 }
	setmetatable(self, _set)
	for _,v in ipairs({...}) do
		_set.add(self, v)
	end
	return self
end

function _set:add(value)
	if not self._hash[value] then
		self._hash[value] = true
		self.n = self.n + 1
	end
end

function _set:clear()
	self._hash, self.n = {}, 0
end

function _set:__len()
	return self.n
end

function _set:items()
	local k
	return function()
		k = next(self._hash, k)
		return k
	end
end

function _set:find(value)
	return (self._hash[value] or false)
end

function _set:pop()
	v = next(self._hash)
	if v then
		self._hash[v] = nil
		self.n = self.n - 1
	end
	return v
end

function _set:remove(value)
	if self._hash[value] then
		self._hash[value] = nil
		self.n = self.n - 1
	end
end

function _set:difference(object)
	assert(getmetatable(object) == _set)
	local t = set()
	for k in self:items() do
		if not object._hash[k] then t:add(k) end
	end
	return t
end

function _set:symmetric_difference(object)
	assert(getmetatable(object) == _set)
	local t = set()
	for k in self:items() do
		if not object._hash[k] then t:add(k) end
	end
	for k in object:items() do
		if not self._hash[k] then t:add(k) end
	end
	return t
end

function _set:intersection(object)
	assert(getmetatable(object) == _set)
	local t = set()
	for k in self:items() do
		if object._hash[k] then t:add(k) end
	end
	return t
end

function _set:union(object)
	assert(getmetatable(object) == _set)
	local t = set()
	for k in self:items() do t:add(k) end
	for k in object:items() do t:add(k) end
	return t
end

function _set:__add(object)
	return self:union(object)
end

function _set:__mul(object)
	return self:intersection(object)
end

function _set:__sub(object)
	return self:difference(object)
end

function _set:__pow(object)
	return self:symmetric_difference(object)
end

function _set:__bor(object)
	return self:union(object)
end

function _set:__band(object)
	return self:intersection(object)
end

local function _set_compare(self, object, flag)
	if ((flag=='eq' and not (self.n==object.n)) or
	    (flag=='le' and not (self.n<=object.n)) or
	    (flag=='lt' and not (self.n<object.n))) then
	   return false
	end
	for k in self:items() do
		if not object._hash[k] then return false end
	end
	return true
end

function _set:__eq(object)
	assert(getmetatable(object) == _set)
	return _set_compare(self, object, 'eq')
end

function _set:__lt(object)
	assert(getmetatable(object) == _set)
	return _set_compare(self, object, 'lt')
end

function _set:__le(object)
	assert(getmetatable(object) == _set)
	return _set_compare(self, object, 'le')
end

function _set:__index(key)
	return _set[key]
end

------------------------------------------------------------------------------------

if _VERSION < 'Lua 5.2' then
	table.pack = function(...) return arg end
	table.unpack = unpack
end

function dostring(code, error_handler)
	local call = type(err)=='function' and xpcall or pcall
	return call(function()
		local f = assert((loadstring or load)(code))
		return f()
	end, error_handler)
end

function eval(code)
	local f = assert((loadstring or load)('return table.pack('..code..')'))
	return table.unpack(f())
end

local escape_character = {
	['\\'] = '\\\\', ['\''] = '\\\'', ['\"'] = '\\\"', ['\a'] = '\\a', ['\b'] = '\\b',
	['\f'] = '\\f',  ['\n'] = '\\n',  ['\r'] = '\\r',  ['\t'] = '\\t', ['\v'] = '\\v'
	}
local function repr_string(s)
	local pattern = '[\\\'\"%c]'
	local v = string.gsub(s, pattern, function(c)
		return escape_character[c] or string.format('\\%03d', string.byte(c))
	end)
	return '"'..v..'"'
end

local function repr_table(t)
	local _t, n = {}, #t
	for i=1,n do table.insert(_t, repr(t[i])) end
	for k,v in pairs(t) do
		if type(k)~='number' or (k<1 or k>n) then
			key = type(k)=='number' and ('['..tostring(k)..']')
			   or string.find(k,"^[%a_][%w_]*$") and k or ('["'..k..'"]')
			table.insert(_t, key..'='..repr(v))
		end
	end
	return '{'..table.concat(_t, ',')..'}'
end

function repr(obj)
	if type(obj)=='table' then
		return repr_table(obj)
	elseif type(obj)=='string' then
		return repr_string(obj)
	elseif type(obj)=='number' or obj==true or not obj then
		return tostring(obj)
	else
		error(type(obj)..' type is unsupport for repr()')
	end
end

------------------------------------------------------------------------------------

function basename(path)
	return string.match(path, "([^/]*)$")
end

function dirname(path)
	local n = string.find(path, "(/+[^/]*)$")
	if not n then return "." end
	return n==1 and "/" or string.gsub(string.sub(path, 1, n-1), "/+", "/")
end

-- reference to python's getopt module
function getopt(args, shortopts, longopts)
	longopts = longopts or {}
	local short_opts, long_opts = {}, {}
	string.gsub(shortopts, '(%a)(:?)', function(o,a) short_opts[o]=(a==':') end)
	for _,opt in ipairs(longopts) do
		string.gsub(opt, '(%a[^=]*)(=?)', function(o,a) long_opts[o]=(a=='=') end)
	end
	
	local opts, out_args, i = {}, {}, 1
	while i <= #args do
		local argv = args[i]
		if argv:byte(1)==string.byte('-') then
			local o = argv:sub(2, 2)
			if o~='-' then
				local a = short_opts[o]
				if a==nil then error('option -'..o..' not recognized') end
				if #argv > 2 then
					if not a then error('option -'..o..' not recognized') end
					opts[argv:sub(1,2)] = argv:sub(3)
				else
					if not a then
						opts[argv] = ''
					else
						i = i + 1
						local optval = args[i]
						if optval:byte(1)==string.byte('-') then error('option -'..o..' not recognized') end
						opts[argv] = optval
					end
				end
			else
				local _, n, o, q = string.find(argv, '^([^=]+)(=?)', 3)
				local a = o and long_opts[o]
				if a==nil then error('option --'..(o or argv)..' not recognized') end
				if q=='=' then
					if not a then error('option --'..o..'  must not have an argument') end
					opts[argv:sub(1,n-1)] = argv:sub(n+1)
				else
					if a then error('option --'..o..'  requires argument') end
					opts[argv] = ''
				end
			end
		else
			table.insert(out_args, argv)
		end
		i = i + 1
	end
	return opts, out_args
end

--copy and modify from http://lua-users.org/wiki/LuaXml, LOW PERFORMANCE AND NOT ENTIRELY CORRECT!!!
function simple_xmlparse(xml)
	local stack = {}
	local doc = {}
	local ni, c, tag, attr, empty, pi
	local i, j = 1, 1

	local getAttributes = function(str)
		local arg
		string.gsub(str, "([%a_][%w_-]*)=([\"'])(.-)%2", function (w, _, a)
			arg = arg or {}
		--	table.insert(arg, w)
			arg[w] = a
		end)
		return arg
	end
	
	local entityMap  = { ["lt"]="<", ["gt"]=">", ["amp"]="&", ["quot"]='"', ["apos"]="'" }
	local entitySwap = function(orig,n,s) return entityMap[s] or n=="#" and string.char('0'..s) or orig end
	local unescape = function(str) return string.gsub(str, '(&(#?)([%d%a]+);)', entitySwap) end
	
	xml = string.gsub(xml, "<!%-%-(.-)%-%->", "")
	ni, j, pi = string.find(xml, "^%s*<%?xml(.-)%?>%s*", i)
	if ni then
		doc.decl = getAttributes(pi)
		i = j+1
	end
	
	local top = doc
	table.insert(stack, top)
	while true do
		ni, j, c, tag, attr, empty = string.find(xml, "<(%/?)([%a][%w:_-]*)(.-)(%/?)>", i)
		if not ni then break end
		local text = string.sub(xml, i, ni-1)
		if not string.find(text, "^%s*$") then
			table.insert(top, string.match(text, "^%s*<!%[CDATA%[(.-)%]%]>") or unescape(text))
		end
		
		if empty == "/" then  -- empty element tag
			table.insert(top, {tag=tag, attr=getAttributes(attr)})
		elseif c == "" then   -- start tag
			local el = {tag=tag, attr=getAttributes(attr)}
			if top==doc then
				if doc.root then error("already has a root '"..doc.root.tag.."' element") end
				doc.root = el
			else
				table.insert(top, el)
			end
			top = el
			table.insert(stack, top)   -- new level
		else  -- end tag
			local toclose = table.remove(stack)  -- remove top
			top = stack[#stack]
			if #stack < 1 then
				error("nothing to close with "..tag)
			end
			if toclose.tag ~= tag then
				error("trying to close "..toclose.tag.." with "..tag)
			end
		end
		i = j+1
	end
	if #stack > 1 then
		error("unclosed "..stack[#stack].tag)
	end
	return doc
end
