local function b32(x) return (x & 0xffffffff) end

local function decode32(b, n, big_endian)
	local s = big_endian and {24,16,8,0} or {0,8,16,24}
	local byte = function(i) return (string.byte(b, n+i-1) << s[i]) end
	return (byte(1) | byte(2) | byte(3) | byte(4))
end
local function encode32(b, big_endian)
	b = b32(b)
	local s = big_endian and {24,16,8,0} or {0,8,16,24}
	local byte = function(i) return (0x0ff & (b >> s[i])) end
	return string.char(byte(1), byte(2), byte(3), byte(4))
end

local function padding(msglen, big_endian)
	local left = msglen & 0x3f
	local padn = (left < 56) and (56 - left) or (120 - left)
	local size = msglen << 3
	local high, low = (size >> 32), (size & 0xffffffff)
	
	local pad = big_endian
	        and { string.char(0x80), string.rep('\0',padn-1), encode32(high, big_endian), encode32(low, big_endian) }
	         or { string.char(0x80), string.rep('\0',padn-1), encode32(low, big_endian), encode32(high, big_endian) }
	return table.concat(pad)
end

local function md5_process(context, chunk)
	local r = {
	[0]=7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
		5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
		4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
		6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
		}
	local k = { -- k[i] = math.floor(math.abs(math.sin(i + 1)) * (2^32))
	[0]=0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
		}
	local H, w = context, {}
	for i=0,15 do w[i] = decode32(chunk, i*4+1, false) end
	local a, b, c, d = H[1], H[2], H[3], H[4]
	
	for i = 0, 63 do
		local f, g
	    if i < 16 then
			f = (b & c) | ((~b) & d)
			g = i
	    elseif i < 32 then
			f = (d & b) | ((~d) & c)
			g = (5*i + 1) & 0x0f
	    elseif i < 48 then
			f = b ~ c ~ d
			g = (3*i + 5) & 0x0f
	    else
			f = c ~ (b | (~d))
			g = (7*i) & 0x0f
	    end
	    d, c, b, a = c, b, b32(bit32.lrotate(b32(a + f + k[i] + w[g]), r[i]) + b), d
	end

	H[1], H[2], H[3], H[4] = H[1]+a, H[2]+b, H[3]+c, H[4]+d
end

local function sha1_process(context, chunk)
	local H, w = context, {}
	for i = 0, 79 do
		if i < 16 then
			w[i] = decode32(chunk, i*4+1, true)
		else
			w[i] = bit32.lrotate((w[i-3] ~ w[i-8] ~ w[i-14] ~ w[i-16]), 1)
		end
	end
	local a, b, c, d, e = H[1], H[2], H[3], H[4], H[5]
	
	for i = 0, 79 do
		local f, k
	    if i < 20 then
			f = (b & c) | ((~b) & d)
			k = 0x5a827999
	    elseif i < 40 then
			f = b ~ c ~ d
			k = 0x6ed9eba1
	    elseif i < 60 then
			f = (b & c) | (b & d) | (c & d)
			k = 0x8f1bbcdc
	    else
			f = b ~ c ~ d
			k = 0xca62c1d6
	    end
	    local temp = b32(bit32.lrotate(a, 5) + f + e + k + w[i])
	    e, d, c, b, a = d, c, bit32.lrotate(b, 30), a, temp
	end
	
	H[1], H[2], H[3], H[4], H[5] = H[1]+a, H[2]+b, H[3]+c, H[4]+d, H[5]+e
end

local function sha256_process(context, chunk)
	local k = {
	[0]=0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2    
		}
	local H, w = context, {}
	for i = 0, 64 do
		if i < 16 then
			w[i] = decode32(chunk, i*4+1, true)
		else
			local s0 = bit32.rrotate(w[i-15], 7) ~ bit32.rrotate(w[i-15], 18) ~ (w[i-15] >> 3)
			local s1 = bit32.rrotate(w[i-2], 17) ~ bit32.rrotate(w[i-2], 19) ~ (w[i-2] >> 10)
			w[i] = b32(w[i-16] + s0 + w[i-7] + s1)
		end
	end
	local a, b, c, d, e, f, g, h = H[1], H[2], H[3], H[4], H[5], H[6], H[7], H[8]
	
	for i = 0, 63 do
		local s0 = bit32.rrotate(a, 2) ~ bit32.rrotate(a, 13) ~ bit32.rrotate(a, 22)
		local maj = (a & b) ~ (a & c) ~ (b & c)
		local t2 = s0 + maj
		local s1 = bit32.rrotate(e, 6) ~ bit32.rrotate(e, 11) ~ bit32.rrotate(e, 25)
		local ch = (e & f) ~ ((~e) & g)
		local t1 = h + s1 + ch + k[i] + w[i]
		h, g, f, e, d, c, b, a = g, f, e, b32(d+t1), c, b, a, b32(t1+t2)
	end
	
	H[1], H[2], H[3], H[4], H[5], H[6], H[7], H[8] = H[1]+a, H[2]+b, H[3]+c, H[4]+d, H[5]+e, H[6]+f, H[7]+g, H[8]+h
end

local Digest = {
	['md5'] = {
		context = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 },
		process = md5_process,
		big_endian = false,
		outw = 4
		},
	['sha1'] = {
		context = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 },
		process = sha1_process,
		big_endian = true,
		outw = 5
		},
	['sha224'] = {
		context = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 },
		process = sha256_process,
		big_endian = true,
		outw = 7
		},
	['sha256'] = {
		context = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 },
		process = sha256_process,
		big_endian = true,
		outw = 8
		},
	}

function get_digest(name)
	local digest = Digest[string.lower(name)]
	if not digest then
		local names = {}
		for n,_ in pairs(Digest) do table.insert(names, n) end
		table.sort(names)
		return nil, 'unknown digest algorithm name, only ' .. table.concat(names, ',')
	end
	
	return function(message)
		local context, result = {}, {}
		table.move(digest.context, 1, #digest.context, 1, context)
		
		local pos, left = 1, string.len(message)
		while left >= 64 do
			digest.process(context, string.sub(message, pos, pos+63))
			pos, left = pos + 64, left - 64
		end
		local final = (left > 0 and string.sub(message, pos) or '') .. padding(string.len(message), digest.big_endian)
		for i = 1, #final, 64 do digest.process(context, string.sub(final, i, i+63)) end
		
		for i = 1, digest.outw do table.insert(result, encode32(context[i], digest.big_endian)) end
		return table.concat(result)
	end
end
