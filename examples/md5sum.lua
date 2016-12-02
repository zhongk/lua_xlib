require('digest')
require('base64')

if not arg[1] then
	print(string.format('usage: lua %s filename', arg[0]))
	os.exit()
end
local md5 = get_digest('md5')
local text = assert(io.open(arg[1], 'r')):read('*a')
local md5sum = base16.encode(md5(text))
print(md5sum)
