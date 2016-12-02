require('util')
local otl_oracle_connect = require("otldb.oracle")

local db = assert(otl_oracle_connect("username/password@tnsname"))

db:direct_exec("create table test_tab( x varchar(50), y number(10) primary key)")

local stmt = db:cursor()
assert(stmt:execute("select 1, 'hello world', null, to_clob('this is a clob column'), sysdate from dual"))
local ret = assert(stmt:fetchone())
assert(ret[1] == 1)
assert(ret[2] == 'hello world')
assert(stmt:is_null(ret[3]))
assert(ret[3] == stmt.null)
print('clob:', repr(ret[4]))
print('sysdate:', repr(ret[5]))

stmt:set_batch_size(50)
assert(stmt:execute("insert into test_tab(x,y) values(:x<char[50]>,:y<int>)"))
stmt:set_batch_error_mode(true)
print('\nstatement type:', stmt:get_stream_type())
for n=0,30 do
	local x = 'record-'..n
	local y = (n>20 and n<25) and n-10 or n
	if n==15 then y = nil end
	assert(stmt:bind_values{x, y})
	print('insert ['..n..']:', repr{x, y})
end
local ok, emsg, ecode = stmt:flush()
print('get_rpc() return:', stmt:get_rpc())
if not ok then
	print('oracle error message:', emsg)
	if ecode==stmt.code_batch_errors then
		print('batch_errors:')
		for _,e in ipairs(stmt:get_batch_errors()) do print('', repr(e)) end
	end
end
db:commit()

assert(stmt:execute("select * from test_tab where y > :min<int> and y < :max<int>"))
print('\nstatement type:', stmt:get_stream_type())
assert(stmt:bind_names({min=15, max=30}))
local desc = stmt:describe_select()
for i,f in ipairs(desc) do
	print('column ['..i..']:', repr(desc[i]))
end

local n = 0
for row in stmt:rows() do
	print('fetch ['..n..']:', repr(row))
	n = n + 1
end
stmt:close()

print('\ndelete rows:', db:direct_exec("delete from test_tab"))
db:commit()

assert(db:direct_exec("drop table test_tab"))

local chunk = db:cursor()
chunk:set_batch_size(20)
assert(chunk:execute("begin :b<int,out> := :a<int,inout>*3; :c<int,out> := :a + 15; :a := :b + :c; end;"))
print('\nchunk type:', chunk:get_stream_type())
in_vars, out_vars = {}, {}

for i=1,30 do table.insert(in_vars, {a=i}) end
for _,in_var in ipairs(in_vars) do
	assert(chunk:bind_names(in_var))
	while not chunk:eof() do table.insert(out_vars, chunk:outvars()) end
end
chunk:flush()
while not chunk:eof() do table.insert(out_vars, chunk:outvars()) end

for i=1,#in_vars do
	print('chunk ['..i..']:', 'in='..repr(in_vars[i])..', out='..repr(out_vars[i]))
end
chunk:close()

db:close()
