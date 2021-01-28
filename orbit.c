#include "orbit.h"

#include <stdlib.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#define SYS_ORBIT_CREATE 436
#define SYS_ORBIT_CALL   437
#define SYS_ORBIT_RETURN 438


struct obModule *obCreate(const char *module_name /* UNUSED */, obEntry entry_func) {
	struct obModule *ob;
	unsigned long obid, ret;
	void *arg = NULL;

	ob = (struct obModule*)malloc(sizeof(struct obModule));
	if (ob == NULL) return NULL;

	obid = syscall(SYS_ORBIT_CREATE, &arg);
	if (obid == -1) {
		free(ob);
		return NULL;
	} else if (obid == 0) {
		/* We are now in child, we should run the function  */

		/* TODO: current a hack to return for the first time for
		 * initialization. */
		syscall(SYS_ORBIT_RETURN, 0);
		
		/* TODO: allow the child to stop */
		while (1) {
			ret = entry_func(arg);
			syscall(SYS_ORBIT_RETURN, ret);
		}
	}

	/* Now we are in parent. */
	ob->obid = obid;
	ob->entry_func = entry_func;

	return ob;
}
// void obDestroy(obModule*);

unsigned long obCall(struct obModule *module, struct obPool* pool, void *aux) {
	return syscall(SYS_ORBIT_CALL, module->obid,
			pool->rawptr, pool->rawptr + pool->length,
			module->entry_func, aux);
}

/* Return a memory allocation pool. */
struct obPool *obPoolCreate(size_t init_pool_size /*, int raw = 0 */ ) {
	struct obPool *pool;
	void *area;

	pool = (struct obPool*)malloc(sizeof(struct obPool));
	if (pool == NULL) return NULL;

	area = mmap(NULL, init_pool_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == NULL) {
		free(pool);
		return NULL;
	}

	pool->rawptr = area;
	pool->length = init_pool_size;

	return area;
}
// void obPoolDestroy(pool);
