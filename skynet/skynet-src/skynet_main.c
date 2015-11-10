#include "skynet.h"

#include "skynet_imp.h"
#include "skynet_env.h"
#include "skynet_server.h"
#include "luashrtbl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>

// optint, optstring:
// 1. if skynet environment has this key then return the related value
// 2. otherwise return the given `opt` instead

static int 
optint(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp,"%d",opt);
		skynet_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

/*
static int
optboolean(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		skynet_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str,"true")==0;
}
*/

static const char *
optstring(const char *key,const char * opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		if (opt) {
			skynet_setenv(key, opt);
			opt = skynet_getenv(key);
		}
		return opt;
	}
	return str;
}

static void
_init_env(lua_State *L) {

  // ----------------------------------------- Index
  // b. and a nil value pushed on the stack     (-1)
  // a. first the result table is on the stack  (-2)
	lua_pushnil(L);  /* first key */

  // int lua_next (lua_State *L, int idx):
  // 1. `idx` (-2) is the index of the table in the stack
  // 2. pop up the top element (nil for 1st time or the key of table) in the stack
  // 3. and then push a key and value pair of the table into the stack (if the table traverse finished and loop end)
  while (lua_next(L, -2) != 0) {

    // ----------------------------------------- Index
    // c. the value                               (-1)
    // b. the key                                 (-2)
    // a. first the result table is on the stack  (-3)
    int keyt = lua_type(L, -2); // get key type
		if (keyt != LUA_TSTRING) {
			fprintf(stderr, "Invalid config table\n");
			exit(1);
		}

    // get the config name string
		const char * key = lua_tostring(L,-2);

    // get the value of this config name
    // and set this config to skynet environment
		if (lua_type(L,-1) == LUA_TBOOLEAN) {
			int b = lua_toboolean(L,-1);
			skynet_setenv(key,b ? "true" : "false" );
		} else {
			const char * value = lua_tostring(L,-1);
			if (value == NULL) {
				fprintf(stderr, "Invalid config table key = %s\n", key);
				exit(1);
			}

      // defined in skynet_env.c
      // 1. create a global variable to store the config value
      // 2. this global variable is belong to the Lua State in skynet global envrionment (E->L) 
			skynet_setenv(key,value);
		}

    // pop 1 element out of the stack
		lua_pop(L,1);

    // ----------------------------------------- Index
    // b. the key                                 (-1)
    // a. first the result table is on the stack  (-2)
	}

  // pop 1 element out of the stack
	lua_pop(L,1);
  
  // ----------------------------------------- Index
  // a. first the result table is on the stack  (-1)
}

// set SIGPIPE signal's handler function to SIG_IGN (ignore this signal)
// background story: if a socket is closed by client, then call write twice to this socket on server, 
// the second call will generate SIGPIPE signal, this signal terminate process by default
int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

/**[Load Config]
the string of load_config is a lua chunk.
lua chunk will be handled as an anonymous vararg function.

this lua chunk executes following actions:
1. config_name get config file name from vararg
2. open config file through config_name
3. read entrie file content as lua code
4. define a function to get os envrionment variable's value
5. repalce existing envrionment variable to its value in code
   the envrionment variable pattern `%$([%w_%d]+)`: 
   start with `$` and follow one or more letter/underscore/digit
6. close file and define a `result` table
7. load the code that originally from config file, and return a compiled anonymous function
8. the `result` table is passed to intianlize the first upvalue in this anonymous function
9. execute this anonymous function with no arguments (assert will return all its arguments when assert true)

code = string.gsub(code, "%$([%w_%d]+)", getenv):
1. each match "%$([%w_%d]+)" in code, the function getenv will be called a time
2. each time the parameter passed into getenv is the string match "[%w_%d]+", i.e. the pattern in the parentheses
3. if getenv return not false and nil, the return string will be used to replace original matched string in code
4. finally, gsub return entire modified string

assert(load(code, "=(load)", "t", result))():
1. load the chunk in code that originally from config file
2. the "=(load)" is the name of the chunk for debug and error handling
3. the "t" says that the code in chunk is plain text not precompiled binary code
4. the `result` is used to initialise the first upvalue in the chunk, 
   if this chunk has more upvalues then these extra upvalues will be initialized to 'nil`.
5. finally the chunk is loaded and return a compiled anonymous function
6. then the anonymous function is called with no arguments.
 */
static const char * load_config = "\
	local config_name = ...\
	local f = assert(io.open(config_name))\
	local code = assert(f:read \'*a\')\
	local function getenv(name) return assert(os.getenv(name), \'os.getenv() failed: \' .. name) end\
	code = string.gsub(code, \'%$([%w_%d]+)\', getenv)\
	f:close()\
	local result = {}\
	assert(load(code,\'=(load)\',\'t\',result))()\
	return result\
";

/**[Example Config File]
************************
root = "./"
thread = 8
logger = nil
logpath = "."
harbor = 1
address = "127.0.0.1:2526"
master = "127.0.0.1:2013"
start = "main"  -- main script
bootstrap = "snlua bootstrap" -- The service for bootstrap
standalone = "0.0.0.0:2013"
luaservice = root.."service/?.lua;"..root.."test/?.lua;"..root.."examples/?.lua"
lualoader = "lualib/loader.lua"
-- preload = "./examples/preload.lua" -- run preload.lua before every lua service run
snax = root.."examples/?.lua;"..root.."test/?.lua"
-- snax_interface_g = "snax_g"
cpath = root.."cservice/?.so"
-- daemon = "./skynet.pid"
************************
 */

int
main(int argc, char *argv[]) {
  // get config file name string from argv[1]
	const char * config_file = NULL ;
	if (argc > 1) {
		config_file = argv[1];
	} else {
		fprintf(stderr, "Need a config file. Please read skynet wiki : https://github.com/cloudwu/skynet/wiki/Config\n"
			"usage: skynet configfilename\n");
		return 1;
	}
  
  // only useful for modified version of lua, check ENABLE_SHORT_STRING_TABLE
	luaS_initshr();
  
  // defined in skynet_server.c 
  // 1. init the global skynet node of G_NODE 
  // 2. set current thread's specific data to THREAD_MAIN
	skynet_globalinit();

  // defined in skynet_env.c
  // 1. alloc the global skynet_env of E and initialize it
  // 2. E has a spinlock and a lua state
	skynet_env_init();

  // defined in this file: ignore SIGPIPE signal
	sigign();

	struct skynet_config config;

  // create a new Lua State and open all libs for this State
	struct lua_State *L = lua_newstate(skynet_lalloc, NULL);
	luaL_openlibs(L);	// link lua lib

  // load config lua code and push the result function to the stack
	int err = luaL_loadstring(L, load_config);
	assert(err == LUA_OK);

  // push the config file name string to the stack
	lua_pushstring(L, config_file);
  
  // run the config lua code with 1 argument (config_file) and 1 result (the result table)
  // the result is pushed on the top of the stack
	err = lua_pcall(L, 1, 1, 0);
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L);
		return 1;
	}

  // traverse the result table and parse each config in element 
  // and set config values into global variables (belong to the Lua State in skynet global environment E->L)
	_init_env(L);
  
  // get configs in the skynet environment or the default config in the 2nd argument
  // and set configs to the config structure
	config.thread =  optint("thread",8);
	config.module_path = optstring("cpath","./cservice/?.so");
	config.harbor = optint("harbor", 1);
	config.bootstrap = optstring("bootstrap","snlua bootstrap");
	config.daemon = optstring("daemon", NULL);
	config.logger = optstring("logger", NULL);
	config.logservice = optstring("logservice", "logger");

  // close the Lua State that was used to run lua config code
	lua_close(L);

  // defined in skynet_start.c:
  // start skynet with the configs in the structure
	skynet_start(&config);

  // definded in skynet_server.c: 
  // delete created thread specific data key stored in G_NODE.handle_key
	skynet_globalexit();

  // only useful for modified version of lua, check ENABLE_SHORT_STRING_TABLE
	luaS_exitshr();

	return 0;
}
