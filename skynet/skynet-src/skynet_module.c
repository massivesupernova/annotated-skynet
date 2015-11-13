#include "skynet.h"

#include "skynet_module.h"
#include "spinlock.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_MODULE_TYPE 32

struct modules {
	int count;
	struct spinlock lock;
	const char * path;
	struct skynet_module m[MAX_MODULE_TYPE];
};

static struct modules * M = NULL;

static void *
_try_open(struct modules *m, const char * name) {
	const char *l;

  // default path is "./cservice/?.so"
	const char * path = m->path;
	size_t path_size = strlen(path);
	size_t name_size = strlen(name);

	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		memset(tmp,0,sz);
		while (*path == ';') path++;
		if (*path == '\0') break;

    // current is first non-';' char 
    // find the position of next ';', 
    // if it is not found consider the end of the string as this position
		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);

    // copy the module name to replace the '?' in the path
    int len = l - path;
		int i;
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C service path\n");
			exit(1);
		}

    // <dlfcn.h> void* dlopen(const char* filename, int flag);
    // 1. loads the dynamic library file named by the null-terminated string `filename`
    // and return an opaque "handle" for the dynamic library.
    // 2. if `filename` is NULL, then the returned handle is for the main program.
    // 3. if the library has dependencies on other shared libraries,
    // then these are also automatically loaded by the dynamic linker using the same rules.
    // 4. RTLD_NOW: all undefined symbols in the library are resolved before dlopen returns
    // 5. RTLD_GLOBAL: the symbols defined by this library will be available for 
    // symbol resolution of subsequently loaded libraries.
    
    // load dynamic library file for example "./cservice/name.so"
    // and return the handle, if dynamic library loaded success then end the loop
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

static struct skynet_module * 
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) {
		if (strcmp(M->m[i].name,name)==0) {
			return &M->m[i];
		}
	}
	return NULL;
}

static int
_open_sym(struct skynet_module *mod) {
	size_t name_size = strlen(mod->name);
	char tmp[name_size + 9]; // create/init/release/signal , longest name is release (7)
	memcpy(tmp, mod->name, name_size);
	strcpy(tmp+name_size, "_create");
	mod->create = dlsym(mod->module, tmp);
	strcpy(tmp+name_size, "_init");
	mod->init = dlsym(mod->module, tmp);
	strcpy(tmp+name_size, "_release");
	mod->release = dlsym(mod->module, tmp);
	strcpy(tmp+name_size, "_signal");
	mod->signal = dlsym(mod->module, tmp);

	return mod->init == NULL;
}

struct skynet_module * 
skynet_module_query(const char * name) {

  // query skynet module from M->m[i] by compare M->m[i].name with name
  // if found return this skynet module
	struct skynet_module * result = _query(name);
	if (result)
		return result;

	SPIN_LOCK(M)

	result = _query(name); // double check

  // if the module (such as "logger" and "snlua") is not found, 
  // then load dynamic library and append new module to M and return this new module
	if (result == NULL && M->count < MAX_MODULE_TYPE) {
		int index = M->count;
    
    // try to load dymaic library of c service 
    // located in "./cservice/<name>.so" (default setting)
		void * dl = _try_open(M,name);
		if (dl) {
      // if dynamic library load success, then:
      // 1. load synbols (<name>_create _init _release _signal) to M->m[index]
      // 2. M->m[index].name keep the name of module
      // 3. M->m[index].module keep the dynamic library handle
      // 4. add up the total moudle count
      // 5. return the new module
			M->m[index].name = name;
			M->m[index].module = dl;
			if (_open_sym(&M->m[index]) == 0) {
				M->m[index].name = skynet_strdup(name);
				M->count ++;
				result = &M->m[index];
			}
		}
	}

	SPIN_UNLOCK(M)

	return result;
}

void 
skynet_module_insert(struct skynet_module *mod) {
	SPIN_LOCK(M)

	struct skynet_module * m = _query(mod->name);
	assert(m == NULL && M->count < MAX_MODULE_TYPE);
	int index = M->count;
	M->m[index] = *mod;
	++M->count;

	SPIN_UNLOCK(M)
}

void * 
skynet_module_instance_create(struct skynet_module *m) {
	if (m->create) {
		return m->create();
	} else {
		return (void *)(intptr_t)(~0);
	}
}

int
skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm);
}

void 
skynet_module_instance_release(struct skynet_module *m, void *inst) {
	if (m->release) {
		m->release(inst);
	}
}

void
skynet_module_instance_signal(struct skynet_module *m, void *inst, int signal) {
	if (m->signal) {
		m->signal(inst, signal);
	}
}

void 
skynet_module_init(const char *path) {
	struct modules *m = skynet_malloc(sizeof(*m));
	m->count = 0;
	m->path = skynet_strdup(path);

	SPIN_INIT(m)

	M = m;
}
