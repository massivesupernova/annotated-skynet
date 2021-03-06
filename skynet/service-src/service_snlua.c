#include "skynet.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct snlua {
	lua_State * L;
	struct skynet_context * ctx;
};

// LUA_CACHELIB may defined in patched lua for shared proto
#ifdef LUA_CACHELIB

#define codecache luaopen_cache

#else

static int
cleardummy(lua_State *L) {
  return 0;
}

static int 
codecache(lua_State *L) {
	luaL_Reg l[] = {
		{ "clear", cleardummy },
		{ "mode", cleardummy },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	lua_getglobal(L, "loadfile");
	lua_setfield(L, -2, "loadfile");
	return 1;
}

#endif

static int 
traceback (lua_State *L) {
	const char *msg = lua_tostring(L, 1);
	if (msg)
		luaL_traceback(L, L, msg, 1);
	else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

static void
_report_launcher_error(struct skynet_context *ctx) {
	// sizeof "ERROR" == 5
	skynet_sendname(ctx, 0, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

static const char *
optstring(struct skynet_context *ctx, const char *key, const char * str) {
	const char * ret = skynet_command(ctx, "GETENV", key);
	if (ret == NULL) {
		return str;
	}
	return ret;
}

static int
_init(struct snlua *l, struct skynet_context *ctx, const char * args, size_t sz) {
	lua_State *L = l->L;
	l->ctx = ctx;

  // stop gc
  lua_gc(L, LUA_GCSTOP, 0);

  // set `registry_table.LUA_NOENV = true` and pop up the boolean value out of stack
	lua_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
	lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");

  // open all libs for this Lua State
	luaL_openlibs(L);

  // set `registry_table.skynet_context = ctx` and pop up the light userdatd out of stack
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "skynet_context");

  // set the module "skynet.codecache" loaded: `package.loaded["skynet.codecache"] = codecache(L)`
	luaL_requiref(L, "skynet.codecache", codecache , 0);
	lua_pop(L,1); // and pop up the copy of the module out of the stack

  // skynet env:
  // root = "./"
  // thread = 8
  // logger = nil
  // logpath = "."
  // harbor = 1
  // address = "127.0.0.1:2526"
  // master = "127.0.0.1:2013"
  // start = "main"  -- main script
  // bootstrap = "snlua bootstrap" -- The service for bootstrap
  // standalone = "0.0.0.0:2013"
  // luaservice = root.."service/?.lua;"..root.."test/?.lua;"..root.."examples/?.lua"
  // lualoader = "lualib/loader.lua"
  // -- preload = "./examples/preload.lua" -- run preload.lua before every lua service run
  // snax = root.."examples/?.lua;"..root.."test/?.lua"
  // -- snax_interface_g = "snax_g"
  // cpath = root.."cservice/?.so"
  // -- daemon = "./skynet.pid"


  // set global variable (LUA_PATH = "./lualib/?.lua;./lualib/?/init.lua") by default
	const char *path = optstring(ctx, "lua_path","./lualib/?.lua;./lualib/?/init.lua");
	lua_pushstring(L, path);
	lua_setglobal(L, "LUA_PATH");

  // set global variable (LUA_CPATH = "./luaclib/?.so") by default
	const char *cpath = optstring(ctx, "lua_cpath","./luaclib/?.so");
	lua_pushstring(L, cpath);
	lua_setglobal(L, "LUA_CPATH");

  // set global variable (LUA_SERVICE = "./service/?.lua") by default
	const char *service = optstring(ctx, "luaservice", "./service/?.lua");
	lua_pushstring(L, service);
	lua_setglobal(L, "LUA_SERVICE");

  // set global variable (LUA_PRELOAD = "./examples/preload.lua") for example
	const char *preload = skynet_command(ctx, "GETENV", "preload");
	lua_pushstring(L, preload);
	lua_setglobal(L, "LUA_PRELOAD");

  // put function `traceback` into the stack  
	lua_pushcfunction(L, traceback);
	assert(lua_gettop(L) == 1);

  // load "./lualib/loader.lua" and compile it to the top of the stack
	const char * loader = optstring(ctx, "lualoader", "./lualib/loader.lua");
	int r = luaL_loadfile(L,loader);
	if (r != LUA_OK) {
		skynet_error(ctx, "Can't load %s : %s", loader, lua_tostring(L, -1));
		_report_launcher_error(ctx);
		return 1;
	}

  // push the string of args ("bootstrap" for example) in to the stack
	lua_pushlstring(L, args, sz);

  // call the loader with the args, with 1 argument, no result, 
  // and the message handler function is at index of 1 (traceback)
	r = lua_pcall(L,1,0,1);
	if (r != LUA_OK) {
		skynet_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
		_report_launcher_error(ctx);
		return 1;
	}

  // clear the stack
	lua_settop(L,0);

  // restart the gc
	lua_gc(L, LUA_GCRESTART, 0);

	return 0;
}

static int
_launch(struct skynet_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz) {
	assert(type == 0 && session == 0);
	struct snlua *l = ud;
  
  // clear this service's callback function
	skynet_callback(context, NULL, NULL);

  // do snlua service's work
  int err = _init(l, context, msg, sz);
	if (err) {
		skynet_command(context, "EXIT", NULL);
	}

	return 0;
}

int
snlua_init(struct snlua *l, struct skynet_context *ctx, const char * args) {
  // alloc a string 'tmp' to store the args (such as "bootstrap")
  // this string is represented the lua service needed to launch
	int sz = strlen(args);
	char * tmp = skynet_malloc(sz);
	memcpy(tmp, args, sz);
  
  // set ctx->cb to _launch and set ctx->cb_ud to the object of snlua `l`
  // set callback function to be called when destination received the message
	skynet_callback(ctx, l , _launch);

  // call "REG" function: cmd_reg(ctx, NULL)
  // the ctx->handle is convert to string and stored at ctx->result
  // the returned string is ctx->result
	const char * self = skynet_command(ctx, "REG", NULL);
	uint32_t handle_id = strtoul(self+1, NULL, 16);

  // send message of `tmp` (with size of `sz`, such as "bootstrap") to handle_id
  // 1. if this handle_id is consider as remote handle (high 8-bit is not 0 and not equal to HARBOR)
  // then push this message to the message queue of REMOTE->queue
  // 2. else push this message into the service's message queue (service ctx->queue)
  
	// it must be first message
	skynet_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY,0, tmp, sz);
	return 0;
}

struct snlua *
snlua_create(void) {
  // create snlua and new a Lua State with it
	struct snlua * l = skynet_malloc(sizeof(*l));
	memset(l,0,sizeof(*l));
	l->L = lua_newstate(skynet_lalloc, NULL);
	return l;
}

void
snlua_release(struct snlua *l) {
	lua_close(l->L);
	skynet_free(l);
}

void
snlua_signal(struct snlua *l, int signal) {
	skynet_error(l->ctx, "recv a signal %d", signal);
#ifdef lua_checksig
	// If our lua support signal (modified lua version by skynet), trigger it.
	skynet_sig_L = l->L;
#endif
}
