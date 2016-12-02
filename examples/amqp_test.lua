require('util')
local amqp = require('rabbitmq')
local method_name = {}
for k,v in pairs(amqp.method) do method_name[v] = k end

local conn = assert(amqp.connect{host='127.0.0.1', port=5672, heartbeat=10, timeout=2.0,
             client_properties={capabilities={['connection.blocked']=true, ['consumer_cancel_notify']=true}}})
assert(conn:good())
print('server_properties:', repr(conn:get_server_properties()))

local ch = assert(conn:channel())
local ex, q = 'myexchange', 'myqueue'
assert(ch:exchange_declare(ex,'topic'))
assert(ch:queue_declare(q))
assert(ch:queue_bind(q, ex, 'test.#'))

--ch:confirm_select()
local messge, routing_key = 'message sample 1', 'test'
print(string.format("publish : message='%s', routing_key='%s'", messge, routing_key))
ch:basic_publish(messge, ex, routing_key, {mandatory=1, properties={message_id='message1', headers={test='test'}}})

if ch:basic_get(q, {no_ack=1}) > 0 then
	while true do
		local m = conn:drain_events(1.0)
		print('get '..method_name[m.method]..' :', repr(m))
		if m.method==amqp.method.GetOK then break end
	end
end

messge, routing_key = 'message sample 2', 'test.2'
print(string.format("publish : message='%s', routing_key='%s'", messge, routing_key))
ch:basic_publish(messge, ex, 'test.1', {mandatory=1, properties={message_id='message2', headers={test='test2'}}})
messge, routing_key = 'message sample 3', 'test3'
print(string.format("publish : message='%s', routing_key='%s'", messge, routing_key))
ch:basic_publish(messge, ex, routing_key, {mandatory=1, properties={message_id='message3', headers={test='test3'}}})
messge, routing_key = 'message sample 4', 'test.X'
print(string.format("publish : message='%s', routing_key='%s'", messge, routing_key))
ch:basic_publish(messge, ex, routing_key, {mandatory=1, properties={message_id='message4', headers={test='test4'}}})

local consumer_tag = ch:basic_consume(q)
print('consumer_tag:', consumer_tag)
while true do
	local m = conn:drain_events(0.2)
	if not m then break end
	print('get '..method_name[m.method]..' :', repr(m))
	if m.method==amqp.method.Deliver then
		ch:basic_ack(m.delivery_tag)
	elseif m.method==amqp.method.Empty then
		break
	end
end

ch:exchange_delete(ex)
ch:queue_delete(q)
conn:close()
