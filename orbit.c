#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <stdatomic.h>

#define SYS_ORBIT_CREATE	436
#define SYS_ORBIT_CALL		437
#define SYS_ORBIT_RETURN	438
#define SYS_ORBIT_SEND		439
#define SYS_ORBIT_RECV		440

/* Orbit flags */
#define ORBIT_ASYNC	1


struct obModule {
	unsigned long obid;    // module id used by syscalls
	obEntry entry_func;
	// TODO
};

struct obPool {
	void *rawptr;
	size_t length;	// the pool should be page-aligned
	// bool raw;  // true to enable the following API, false to be used as raw memory segments.
	/* ... other metadata */
	atomic_ulong right;
	atomic_ulong left;
};

struct obTask {
	unsigned long obid;
	unsigned long taskid;
	// obCallback callback;
};

struct obUpdate {
	void *ptr;
	size_t length;
	char data[];
};

struct obModule *obCreate(const char *module_name /* UNUSED */, obEntry entry_func) {
	struct obModule *ob;
	unsigned long obid, ret;
	void *arg = NULL;

	ob = (struct obModule*)malloc(sizeof(struct obModule));
	if (ob == NULL) return NULL;

	obid = syscall(SYS_ORBIT_CREATE, &arg);
	if (obid == -1) {
		free(ob);
		printf("syscall failed with errno: %s\n", strerror(errno));
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
	return syscall(SYS_ORBIT_CALL, 0, module->obid,
			pool->rawptr, pool->rawptr + pool->length,
			module->entry_func, aux);
}

int obCallAsync(struct obModule *module, struct obPool* pool, void *aux, struct obTask *task) {
	long ret;

	ret = syscall(SYS_ORBIT_CALL, ORBIT_ASYNC, module->obid,
			pool->rawptr, pool->rawptr + pool->length,
			module->entry_func, aux);

	// printf("In obCallAsync, ret=%ld\n", ret);

	if (ret < 0)
		return ret;

	if (task)
		task->taskid = ret;

	return 0;
}

unsigned long obSendUpdate(const struct obUpdate *update) {
	return syscall(SYS_ORBIT_SEND, update);
}

unsigned long obRecvUpdate(struct obTask *task, struct obUpdate *update) {
	return syscall(SYS_ORBIT_RECV, task->obid, task->taskid, update);
}

/* Return a memory allocation pool. */
struct obPool *obPoolCreate(size_t init_pool_size /*, int raw = 0 */ ) {
	struct obPool *pool;
	void *area;

	pool = (struct obPool*)malloc(sizeof(struct obPool));
	if (pool == NULL) return NULL;

	area = mmap((void*)0x7ffffff, init_pool_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == NULL) {
		free(pool);
		return NULL;
	}

	pool->rawptr = area;
	pool->length = init_pool_size;

	atomic_init(&pool->right, OB_BLOCK);
	atomic_init(&pool->left, 0);

	return pool;
}
// void obPoolDestroy(pool);

/* TODO: currently we are ony using a linear allocating mechanism.
 * In the future we will need to design an allocation algorithm aiming for
 * compactness of related data. */
void *obPoolAllocate(struct obPool *pool, size_t size)
{
	unsigned long right, left, new_right;

	do {

		right = atomic_load(&pool->right);
		left = atomic_load(&pool->left);

		if (0 && left == right) {
			printf("Pool %p is full.\n", pool);
			return NULL;
		}

		new_right = (right + OB_BLOCK == pool->length) ?
					0 : right + OB_BLOCK;

	} while (!atomic_compare_exchange_strong(&pool->right, &right, new_right));

	return (char*)pool->rawptr + right;
}

void obPoolDeallocate(struct obPool *pool, void *ptr, size_t size)
{
	unsigned long left, new_left;

	do {

		left = atomic_load(&pool->left);

		new_left = (left + OB_BLOCK == pool->length) ?
					0 : left + OB_BLOCK;

	} while (!atomic_compare_exchange_strong(&pool->left, &left, new_left));
}
