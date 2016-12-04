local socket = require('socket')

local http_client = {}
http_client.__index = http_client

function HttpConnection(host, port, timeout)
	port, timeout = port or 80, timeout or 30
	local self = { host=host, port=port, timeout=timeout, closed=true }
	setmetatable(self, http_client)
	return self
end

function http_client:request(method, url, body, headers)
	headers = headers or {}
	local skip_header = {}
	for k, v in pairs(headers) do
		skip_header[string.lower(k)] = true
	end		
	local ok, reason = pcall(
		function()
			if self.closed then
				assert(self:connect())
			end
			assert(self:_putrequest(method, url, skip_header))
			for header, value in pairs(headers) do
				assert(self:_putheader(header, value))
			end
			assert(self:_endheaders(body))
		end)
	if not ok then self:close() end
	return ok, reason
end

function http_client:getresponse()
	local response, content_length, line, reason = { headers={} }, 0
	repeat
		line, reason = self.socket:readline(self.timeout)
		if not line or line=='' then
			self:close()
			return nil, reason
		end
		if not response.status then
			response.version, response.status, response.reason = string.match(line, '^(%S+)%s+(%d+)%s+(.-)\r\n$')
		else
			local header, value = string.match(line, '(.-)%s*:%s*(.+)\r\n')
			if header then
				response.headers[header] = value
				if header:lower()=='content-length' then
					content_length = tonumber(value)
				end
			end
		end
	until line=='\r\n'
	
	reason = nil
	if content_length > 0 then
		local body = {}
		while content_length > 0 do
			if self.socket:poll(self.timeout) <= 0 then
				reason = 'time out'
				self:close()
				break
			end
			local data, errmsg = self.socket:recv(content_length<4096 and content_length or 4096)
			if not data or data=='' then
				reason = errmsg
				self:close()
				break
			end
			table.insert(body, data)
			content_length = content_length - #data
		end
		response.body = table.concat(body)
	end
	return response, reason
end

function http_client:connect()
	if self.closed then
		self.socket = socket.socket()
		local ok, reason = self.socket:connect(self.host, self.port)
		if not ok then return false, reason end
		self.closed = false
	end
	return true
end

function http_client:close()
	if not self.closed then
		self.socket:close()
		self.closed = true
	end
end

function http_client:_putrequest(method, url, skip_header)
	url = (url and #url>0) and url or '/'
	self.socket:send(string.format('%s %s HTTP/1.1\r\n', string.upper(method), url))
	if not skip_header['host'] then
		self:_putheader('Host', string.format('%s:%d', self.host, self.port))
	end
	if not skip_header['accept'] then
		self:_putheader('Accept', '*/*')
	end
	return true
end

function http_client:_putheader(header, value)
	if type(value)=='number' then value = tostring(value) end
	local ret, reason = self.socket:send(string.format('%s: %s\r\n', header, value))
	return ret>0, reason
end

function http_client:_endheaders(body)
	if body and #body>0 then
		self:_putheader('Content-Length', #body)
		self.socket:send('\r\n')
		local ret, reason = self.socket:send(body)
		if ret < 0 then return false, reason end
	else
		self.socket:send('\r\n')
	end
	return true
end
