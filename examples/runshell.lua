local sys = require('posix.sys')
local fd = require('posix.fd')
require('util')

function runshell(cmd, timeout)
	local READ, WRITE = 1, 2
	timeout = timeout or -1
	local pout, perr = fd.pipe(), fd.pipe()
	local pid, err = sys.fork()
	if pid<0 then
		fd.close(pout[READ]); fd.close(perr[READ])
		fd.close(pout[WRITE]); fd.close(perr[WRITE])
		assert(pid>=0, err)
	elseif pid==0 then
		fd.close(pout[READ]); fd.close(perr[READ])
		fd.close(fd.STDOUT_FILENO); fd.dup2(pout[WRITE], fd.STDOUT_FILENO)
		fd.close(fd.STDERR_FILENO); fd.dup2(perr[WRITE], fd.STDERR_FILENO)
		sys.execlp('sh', '-c', cmd)
	end
	
	fd.close(pout[WRITE]); fd.close(perr[WRITE])
	local outbuf, errbuf = {}, {}
	local now, closed = os.time(), false
	local endtime = timeout<0 and 0 or (now + math.ceil(timeout))
	local stdout, stderr = pout[READ], perr[READ]
	while (timeout<0 or now<endtime) and not closed do
		local rfds = fd.readable({stdout, stderr}, endtime-now)
		if (not rfds) or #rfds==0 then break end
		for _, f in ipairs(rfds) do
			local msg = fd.read(f, 4096)
			if not msg or #msg==0 then
				closed = true
			else
				table.insert(f==stdout and outbuf or errbuf, msg)
			end
		end
		now = os.time()
	end
	fd.close(pout[READ]); fd.close(perr[READ])

	local rpid, status = sys.waitpid(pid, sys.WNOHANG)
	if rpid==0 then
		sys.kill(pid)
		rpid, status = sys.waitpid(pid, 0)
	end
	return sys.WIFEXITED(status) and sys.WEXITSTATUS(status) or -status, table.concat(outbuf), table.concat(errbuf)
end

local cmd, timeout
xpcall(
	function()
		local opts, args = getopt(arg, 'c:t:')
		cmd, timeout = opts['-c'], tonumber(opts['-t']) or -1
		assert(cmd and cmd~='', 'must have option -c')
	end,
	function(err)
		print(err)
		print(string.format('usage: lua %s -c command [-t timeout]', arg[0]))
		os.exit()
	end)

local status, stdout, stderr = runshell(cmd, timeout)
print(string.format("run shell '%s', set timeout=%d", cmd, timeout))
print('return status:', status)
print('stdout:', stdout)
print('stderr:', stderr)
