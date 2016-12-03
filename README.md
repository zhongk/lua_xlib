
# lua_xlib
Multiple libraries for Lua, including the interface of POSIX functions, SOCKET, Database Access, Python Adapter, etc. And provide some useful tools written in Lua.

##Building

Command to build lua_xlib on Linux only is:<br>
**`make`**<br>
You need modify `Makefile` to building on other platforms.<br>


##Modules

<div>
<table border="0">
<tr><th>module</th><th>source</th><th>description</th></tr>
<tr><td>posix.fs</td><td>lfslib.c</td><td>Posix filesystem functions: file, path, directory...</td></tr>
<tr><td>posix.fd</td><td>lfdlib.c</td><td>Posix functions associated with file descriptor.</td></tr>
<tr><td>posix.sys</td><td>lsyslib.c</td><td>Posix system-call functions: process, signal, timer...</td></tr>
<tr><td>posix.ipc</td><td>lipclib.c</td><td>Posix IPC functions: shared memory,  message queue, named semaphore.</td></tr>
<tr><td>posix.user</td><td>luserlib.c</td><td>Posix user functions: user, group, privilege.</td></tr>
<tr><td>posix.errno</td><td>lerrno.c</td><td>Posix errno functions: errno, strerror, perror.</td></tr>
<tr><td>svipc</td><td>lsvipc.c</td><td>SYSV IPC interface: shared memory, message queue, semaphores.</td></tr>
<tr><td>socket</td><td>lsocklib.c</td><td>TCP/IP networking interface.</td></tr>
<tr><td>python</td><td>lua_py.c</td><td>Python-Lua bridge that allows Lua to call Python methods.</td></tr>
<tr><td>bigint</td><td>lbnlib.c</td><td>Big Integer requires GMP. homepage: https://gmplib.org/</td></tr>
<tr><td>zlog</td><td>lzlog.c</td><td>Log interface requires zlog. homepage: http://hardysimpson.github.io/zlog/</td></tr>
<tr><td>rabbitmq</td><td>lamqplib.c</td><td>AMQP client requires rabbitmq-c. homepage: https://github.com/alanxz/rabbitmq-c</td></tr>
<tr><td>rapidxml</td><td>rapidxml.cpp</td><td>XML-parser requires rapidxml. homepage: http://rapidxml.sourceforge.net/</td></tr>
<tr><td>otldb.odbc</td><td>luaotl_odbc.cpp</td><td>ODBC interface requires OTL4.0. homepage: http://otl.sourceforge.net/</td></tr>
<tr><td>otldb.oracle</td><td>luaotl_oracle.cpp</td><td>Oracle interface requires OTL4.0.</td></tr>
<tr><td>util</td><td>libs/util.lua</td><td>Some utility functions.</td></tr>
<tr><td>base64</td><td>libs/base64.lua</td><td>Encoding and decoding for base64 and base16. requires lua-5.3</td></tr>
<tr><td>digest</td><td>libs/digest.lua</td><td>Some secure hash algorithms: MD5, SHA1, SHA224, SHA256. requires lua-5.3</td></tr>
</table>
</div>


##How to

You just only add the path of libs directory to environment variable `LUA_PATH` and `LUA_CPATH`.<br>


##Examples

	amqp_test.lua   - test rabbitmq module.
	otldb_test.lua  - test otldb.oracle module.
	py_test.lua     - test python module.
	xml_test.lua    - test rapidxml module.
	pi.lua          - calculting N digits of PI for testing bigint module.
	md5sum.lua      - print MD5 checksum of file for testing digest module.
	runshell.lua    - similar to os.execute, add timeout argument and return output.
	tcp_proxy.lua   - a simple tcp proxy for testing socket and zlog module.

