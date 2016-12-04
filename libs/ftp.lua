local socket = require("socket")
--[[
	local ftp = assert( FtpConnect{host, port=21, user, password, passive=true, timeout=30, verbose=false} )
	return {
		function close(),                        --command: QUIT
		function pwd(),                          --command: PWD
		function chdir(newdir),                  --command: CWD
		function mkdir(newdir),                  --command: MKD
		function rmdir(dir),                     --command: RMD
		function get(remote_file[, local_file]), --command: RETR
		function put(local_file[, remote_file]), --command: STOR
		function rename(oldfile, newfile),       --command: RNFR...RNTO
		function remove(file),                   --command: DELE
		function size(file),                     --command: SIZE
		function time(file),                     --command: MDTM
		function list([pattern='*']),            --command: LIST
		function pasv_mode(['on'|'off'])         --command: PASV
		}
]]

function FtpConnect(login)
	local fcmd = socket.socket()
	local reply_code, reply_message
	local PRELIM, COMPLETE, CONTINUE, TRANSIENT, ERROR = 1, 2, 3, 4, 5
	local verbose = login.verbose
	local passive = login.passive
	local timeout = login.timeout or 30
	if passive==nil then passive = true end

	local function send_cmd(cmd)
		fcmd:send(cmd.."\r\n")
		if verbose then print(string.find(cmd, "PASS ")==1 and "> PASS ****" or ("> "..cmd)) end
	end
	local function get_reply()
		while true do
			local line, err = fcmd:readline(timeout)
			if err then
				reply_code, reply_message = -1, err
				return reply_code
			end
			if verbose then io.write(line) end
			if line:byte(4)==32 then
				reply_code, reply_message = string.match(line, "^(%d%d%d) ([^\r\n]+)")
				if reply_code then
					reply_code = tonumber(reply_code)
					return (reply_code//100)
				end
			end
		end
	end

	local ret, err = fcmd:connect(login.host, login.port or 21)
	if not ret then return nil, err end
	if get_reply() ~= COMPLETE then return nil, reply_message end
	send_cmd("USER "..login.user)
	ret = get_reply()
	if ret ~= COMPLETE then
		if ret == CONTINUE then
			send_cmd("PASS "..login.password)
			ret = get_reply()
		end
		if ret ~= COMPLETE then return nil, reply_message end
	end
	send_cmd("TYPE I")
	get_reply()

	local function close()
		send_cmd("QUIT")
		get_reply()
		fcmd:close()
	end

	local function getpwd()
		send_cmd("PWD")
		if get_reply() ~= COMPLETE then return nil, reply_message end
		return reply_message:match('"(.+)"')
	end
	
	local function chdir(newdir)
		send_cmd("CWD "..newdir)
		if get_reply() ~= COMPLETE then return false, reply_message end
		return true
	end

	local function mkdir(newdir)
		send_cmd("MKD "..newdir)
		if get_reply() ~= COMPLETE then return false, reply_message end
		return true
	end

	local function rmdir(dir)
		send_cmd("RMD "..dir)
		if get_reply() ~= COMPLETE then return false, reply_message end
		return true
	end

	local function rename(oldfile, newfile)
		send_cmd("RNFR "..oldfile)
		if get_reply() ~= CONTINUE then return false, reply_message end
		send_cmd("RNTO "..newfile)
		if get_reply() ~= COMPLETE then return false, reply_message end
		return true
	end

	local function remove(file)
		send_cmd("DELE "..file)
		if get_reply() ~= COMPLETE then return false, reply_message end
		return true
	end
	
	local function filesize(file)
		send_cmd("SIZE "..file)
		if get_reply() ~= COMPLETE then return -1, reply_message end
		return tonumber(reply_message)
	end

	local function filetime(file)
		send_cmd("MDTM "..file)
		if get_reply() ~= COMPLETE then return nil, reply_message end
	--	return reply_message
		local t = {}
		t.year, t.month, t.day, t.hour, t.min, t.sec = 
			string.match(reply_message, "^(%d%d%d%d)(%d%d)(%d%d)(%d%d)(%d%d)(%d%d)")
		return os.time(t) + 8*3600
	end

	local function pasvconn()
		send_cmd("PASV")
		if get_reply() ~= COMPLETE then return nil, reply_message end
		local n1, n2, n3, n4, p1, p2 = string.match(reply_message, "%((%d+),(%d+),(%d+),(%d+),(%d+),(%d+)%)")
		local fdata = socket.socket()
		local ok, err = fdata:connect(fcmd:getpeername()[1], p1*256+p2)
		if not ok then
			fdata:close()
			return nil, err
		end
		return fdata
	end
	
	local function send_port()
		local fsvr = socket.socket()
		local ok, err = fsvr:bind(0)
		if not ok then fsvr:close(); return nil, err end
		ok, err = fsvr:listen()
		if not ok then fsvr:close(); return nil, err end
		
		local host = fcmd:getsockname()[1]
		local port = fsvr:getsockname()[2]
		local arg = string.format("%s,%d,%d", host, port//256, port%256)
		send_cmd("PORT "..string.gsub(arg, "%.", ","))
		if get_reply() ~= COMPLETE then
			fsvr:close()
			return nil, reply_message
		end
		return fsvr
	end
	
	local function portconn(fsvr)
		if fsvr:poll(socket.POLLIN, timeout) <= 0 then
			fsvr:close()
			return nil, "wait port connection time out"
		end
		local fdata = fsvr:accept()
		fsvr:close()
		return fdata
	end

	local function data_init()
		if passive then
			return pasvconn()
		else
			return send_port()
		end
	end
	
	local function data_conn(fdata)
		if passive then
			return fdata
		else
			return portconn(fdata)
		end
	end
	
	local function parse_file(line)
		local token = {}
		string.gsub(line, "(%S+)", function(n) table.insert(token, n) end)
		local type, last = token[1]:sub(1, 1), #token
		local time_token = {}
		for i = 6, ((type=='l') and last-3 or last-1) do table.insert(time_token, token[i]) end
		return {
			context = line,
			type = type,
			mode = token[1]:sub(2),
			user = token[3],
			group = token[4],
			size = token[5],
			time = table.concat(time_token, ' '),
			name = (type=='l') and token[last-2] or token[last],
			link = (type=='l') and token[last] or nil
			}
	end
	
	local function list(pattern)
		local fdata, err = data_init()
		if not fdata then return nil, err end
		send_cmd(pattern and ("LIST "..pattern) or "LIST")
		fdata, err = data_conn(fdata)
		if get_reply() ~= PRELIM then
			if fdata then fdata:close() end
			return nil, reply_message
		end

		local list_file = {}
		while true do
			local line, err = fdata:readline(timeout)
			if line and #line>20 then
				line = string.gsub(line, "[\r\n]", "")
				table.insert(list_file, parse_file(line))
			end
			if err then break end
		end
		fdata:close()
		get_reply()
		return list_file
	end

	local function getfile(remote_file, local_file)
		local_file = local_file or string.match(remote_file, "([^/]*)$")
		local f, err = io.open(local_file, "wb")
		if not f then return false, err end

		local fdata, err = data_init()
		if not fdata then return false, err end
		send_cmd("RETR "..remote_file)
		fdata, err = data_conn(fdata)
		if get_reply() ~= PRELIM then
			if fdata then fdata:close() end
			f:close()
			return false, reply_message
		end
	
		while true do
			local data = fdata:recv(4096)
			if not data then break end
			f:write(data)
		end
		fdata:close()
		local write_size = f:seek()
		f:close()
		get_reply()
		return true, write_size
	end

	local function putfile(local_file, remote_file)
		remote_file = remote_file or string.match(local_file, "([^/]*)$")
		local f, err = io.open(local_file, "rb")
		if not f then return false, err end

		local fdata, err = data_init()
		if not fdata then return false, err end
		send_cmd("STOR "..remote_file)
		fdata, err = data_conn(fdata)
		if get_reply() ~= PRELIM then
			if fdata then fdata:close() end
			f:close()
			return false, reply_message
		end
	
		while true do
			local data = f:read(4096)
			if not data then break end
			fdata:send(data)
		end
		fdata:close()
		f:close()
		get_reply()
		return true
	end

	local function pasv_mode(flag)
		if flag=="on" then
			passive = true
		elseif flag=="off" then
			passive = false
		end
		return passive
	end
	
	local methods =
	{
		close = close,
		pwd = getpwd,
		chdir = chdir,
		mkdir = mkdir,
		rmdir = rmdir,
		get = getfile,
		put = putfile,
		rename = rename,
		remove = remove,
		size = filesize,
		time = filetime,
		list = list,
		pasv_mode = pasv_mode
	}
	return methods
end
