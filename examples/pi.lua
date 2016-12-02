local bigint = require('bigint')
local precision = tonumber(arg[1]) or 100
print('precision = '..precision)

local base, sum, n, d = bigint('1'..string.rep(0, precision+2)), bigint(0), 0
repeat
	d = (base*256/(10*n+1) - base*64/(10*n+3) - base*32/(4*n+1) - base/(4*n+3) 
	   - base*4/(10*n+5) - base*4/(10*n+7) + base/(10*n+9))/(bigint(-1024)^n)
	sum, n = sum+d, n+1
until d:abs() < 64

local pi = tostring(sum/64)
io.write('3.')
for i = 2, precision+1, 5 do
	io.write(pi:sub(i,i+4), ' ')
	if (i+3)%50 == 0 then io.write('\n  ') end
end
