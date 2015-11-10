#include "skynet.h"
#include "skynet_env.h"
#include "spinlock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct skynet_env {
	struct spinlock lock;
	lua_State *L;
};

static struct skynet_env *E = NULL;

const char * 
skynet_getenv(const char *key) {
	SPIN_LOCK(E)

	lua_State *L = E->L;
	
	lua_getglobal(L, key);
	const char * result = lua_tostring(L, -1);
	lua_pop(L, 1);

	SPIN_UNLOCK(E)

	return result;
}

// 1. create a global variable to store the value
// 2. this global variable is belong to the Lua State in skynet global envrionment (E->L)
void 
skynet_setenv(const char *key, const char *value) {
	SPIN_LOCK(E)
      
	lua_State *L = E->L;

  // get global variable (with the name of string in the key) and push its value into the stack
	lua_getglobal(L, key);

  // first there is not this global variable exist (the top of the stack is nil)
	assert(lua_isnil(L, -1));

  // pop the nil out of the stack
	lua_pop(L,1);

  // push the value to the stack
	lua_pushstring(L,value);

  // create a new global varible with the name of key and set the top element as its value
  // and the top element is puped out of the stack
	lua_setglobal(L,key);

	SPIN_UNLOCK(E)
}

void
skynet_env_init() {
  // alloc the global skynet_env of E and initialize it
  // E has two fields: a spinlock and a lus state
	E = skynet_malloc(sizeof(*E)); // alloc the structure
	SPIN_INIT(E)                   // init spinlock
	E->L = luaL_newstate();        // new a lua state
}
