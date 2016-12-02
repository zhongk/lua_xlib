local socket = require('socket')
local sys = require('posix.sys')
local zlog = require('zlog')
require('util')

function get_args()
	local opts, args = getopt(arg, '', {'--local-address=', '--remote-address=', '--timeout='})
	local addr = assert(opts['--local-address'], 'must have option --local-address')
	local ip, port = assert(string.match(addr, '^([^:]+):(%d+)$'))
	local_address = { ip, tonumber(port) }
	addr = assert(opts['--remote-address'], 'must have option --remote-address')
	ip, port = assert(string.match(addr, '^([^:]+):(%d+)$'))
	remote_address = { ip, tonumber(port) }
	timeout = tonumber(opts['--timeout'])
	timeout = (timeout and timeout>0) and timeout or -1
end
xpcall(get_args,
	function(errmsg)
		print(errmsg)
		print('usage: lua '..arg[0]..' --local-address=local-ip:port --remote-address=remote-ip:port [--timeout=-1]')
		print('example:\nlua '..arg[0]..' --local-address=*:6666 --remote-address=192.168.17.234:5672 --timeout=30')
		os.exit()
	end)

local server = socket.socket()
assert(server:bind(local_address))
assert(server:listen())
sys.signal(sys.SIGCHLD, sys.SIG_IGN)

print('process running to background, pid save into tcp_proxy.pid')
sys.daemon()
local f_pid = io.open('tcp_proxy.pid', 'w')
if f_pid then
	f_pid:write(sys.getpid())
	f_pid:close()
end

zlog.init('zlog.cfg', 'tcp_proxy')
zlog.info('tcp_proxy process start ...')

function do_proxy(c)
	local s = socket.socket()
	local ok, err = s:connect(remote_address)
	if not ok then
		zlog.error('connect remote address failed: '..err)
		c:close()
		return -1
	end
	zlog.info('connect to '..table.concat(s:getpeername(), ':'))
	local proxy = { [c:fileno()]={reader=c, writer=s}, [s:fileno()]={reader=s, writer=c} }
	
	local poll, transfer = socket.PollSelector(), true
	poll:register(c:fileno(), socket.POLLIN)
	poll:register(s:fileno(), socket.POLLIN)
	while transfer do
		transfer = false
		for fd,revent in poll:select(timeout) do
			local buffer = proxy[fd].reader:recv(4096)
			if not buffer or buffer=='' then
				transfer = false
				break
			end
			proxy[fd].writer:send(buffer)
			transfer = true
			zlog.debug(string.format('%s -> %s: %d bytes',
			                  table.concat(proxy[fd].reader:getpeername(), ':'),
			                  table.concat(proxy[fd].writer:getpeername(), ':'),
			                  #buffer))
		end
	end
	
	zlog.info('session end')
	c:close()
	s:close()
	return 0
end

while true do
	local client, addr = server:accept()
	if client then
		local pid, err = sys.fork()
		if pid < 0 then
			zlog.error('fork new session failed: '..err)
		elseif pid == 0 then
			server:close()
			sys._exit(do_proxy(client))
		else
			zlog.info(string.format('session [%d] begin: accept socket %s', pid, table.concat(addr,':')))
		end
		client:close()
	end
end
