local rapidxml = require('rapidxml')
require('util')

local doc = rapidxml.new('root')
doc:declaration{version='1.0', encoding='utf-8'}
local root = doc:root()
local node_a = root:append('tag_first', 'test normal data node #&</tag_first>')
root:append(rapidxml.node_type.comment, 'test child nodes')
local node_b = root:append('tag_second')
node_b:append('tag_second_1', 'test child node 1'):attr('node', 1)
node_b:append('tag_second_2', 'test child node 2')
node_b:append('tag_second_3'):attr{node=3, value='nodata'}
node_b:append('tag_second_4')
local node_c = root:append('tag_third')
node_c:append(rapidxml.node_type.cdata):value('test cdata node #&</tag_third>')
print(doc)

local xml = doc:dump()
local out_doc = rapidxml.parse(xml, {rapidxml.node_type.declaration,})
print('xml declaration:', repr(out_doc:declaration()))
local out_root = out_doc:root()
local out_a = out_root:child('tag_first')
print('node '..out_a:name()..':', out_a:value())
local out_b = out_a:next()
print('node '..out_b:name()..':', 'child:'..#out_b)
for child in out_b:children() do
	print('', 'node '..child:name()..':', string.format('value:%s, attr:%s', repr(child:value()), repr(child:attr())))
end
local out_c = out_a:next('tag_third')
assert(out_c:child():type()==rapidxml.node_type.cdata)
print('node '..out_c:name()..':', out_c:child():value())
