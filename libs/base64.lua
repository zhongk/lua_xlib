local BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local BASE64ENC = { [-1]=string.byte('=') }
for i=1,64 do BASE64ENC[i-1] = string.byte(BASE64,i) end
local BASE64DEC = {}
for i=1,64 do BASE64DEC[string.byte(BASE64,i)] = i-1 end
base64 = {}

function base64.encode(s)
	local len = #s
	local result = {}
	for i=1, len, 3 do
		local i1, i2, i3 = s:byte(i, i+2)
		local o1, o2, o3, o4
		o1 = i1 >> 2
		o2 = (i1&0x03) << 4
		o3, o4 = -1, -1
		if i2 then
			o2 = o2 | (i2>>4)
			o3 = (i2&0x0f) << 2
			if i3 then
				o3 = o3 | (i3>>6)
				o4 = i3 & 0x3f
			end
		end
		table.insert(result, string.char(BASE64ENC[o1], BASE64ENC[o2], BASE64ENC[o3], BASE64ENC[o4]))
	end
	return table.concat(result)
end

function base64.decode(s)
	local len = #s
	local result = {}
	for i=1, len, 4 do
		local n1, n2, n3, n4 = s:byte(i, i+3)
		local i1, i2, i3, i4 = BASE64DEC[n1], BASE64DEC[n2], BASE64DEC[n3], BASE64DEC[n4]
		local o1, o2, o3
		o1 = (i1<<2) | (i2>>4)
		if i3 then
			o2 = (i2<<4) | (i3>>2)
			o2 = o2 & 0xff
			if i4 then
				o3 = (i3<<6) | i4
				o3 = o3 & 0xff
			end
		end
		table.insert(result, string.char(table.unpack{o1, o2, o3}))
	end
	return table.concat(result)
end

local BASE16 = "0123456789abcdef"
local BASE16ENC = {}
for i=1,16 do BASE16ENC[i-1] = string.byte(BASE16,i) end
local BASE16DEC = {}
for i=1,16 do BASE16DEC[string.byte(BASE16,i)] = i-1 end
for i=11,16 do BASE16DEC[string.byte(BASE16,i)-32] = i-1 end
base16 = {}

function base16.encode(s)
	local len = #s
	local result = {}
	for i=1, len do
		local b = s:byte(i)
		local o1, o2 = (b>>4), (b&0x0f)
		table.insert(result, string.char(BASE16ENC[o1], BASE16ENC[o2]))
	end
	return table.concat(result)
end

function base16.decode(s)
	local len = #s
	local result = {}
	for i=1, len, 2 do
		local n1, n2 = s:byte(i, i+1)
		local i1, i2 = BASE16DEC[n1], BASE16DEC[n2]
		local o = (i1<<4) | i2
		table.insert(result, string.char(o))
	end
	return table.concat(result)
end

return base64
