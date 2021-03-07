#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#define SYS_ORBIT_CREATE	436
#define SYS_ORBIT_CALL		437
#define SYS_ORBIT_RETURN	438
#define SYS_ORBIT_SEND		439
#define SYS_ORBIT_RECV		440
#define SYS_ORBIT_COMMIT	441
#define SYS_ORBIT_SENDV		442
#define SYS_ORBIT_RECVV		443

/* Orbit flags */
#define ORBIT_ASYNC	1


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
		/* TODO: check return value? */
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

unsigned long orbit_commit(void) {
	return syscall(SYS_ORBIT_COMMIT);
}

/* Return a memory allocation pool. */
struct obPool *obPoolCreate(size_t init_pool_size /*, int raw = 0 */ ) {
	struct obPool *pool;
	void *area;

	pool = (struct obPool*)malloc(sizeof(struct obPool));
	if (pool == NULL) return NULL;

	area = mmap((void*)0x8000000, init_pool_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == NULL) {
		free(pool);
		return NULL;
	}

	pool->rawptr = area;
	pool->length = init_pool_size;

	pool->allocated = 0;

	return pool;
}
// void obPoolDestroy(pool);

/* TODO: currently we are ony using a linear allocating mechanism.
 * In the future we will need to design an allocation algorithm aiming for
 * compactness of related data. */
void *obPoolAllocate(struct obPool *pool, size_t size)
{
	void *ptr;

	if (!(pool->allocated + size < pool->length)) {
		printf("Pool %p is full.\n", pool);
		return NULL;
	}

	ptr = (char*)pool->rawptr + pool->allocated;

	pool->allocated += size;

	return ptr;
}

void obPoolDeallocate(struct obPool *pool, void *ptr, size_t size)
{
	/* Let it leak. */
}

/* === sendv/recvv === */

struct orbit_operation {
	orbit_operation_func func;
	size_t argc;
	unsigned long argv[];	/* `void*` type argument */
};

struct orbit_update {
	void *ptr;
	size_t length;
	char data[];
};

enum orbit_type { ORBIT_UPDATE, ORBIT_OPERATION, };

struct orbit_repr {
	enum orbit_type type;
	union {
		struct orbit_update update;
		struct orbit_operation operation;
	};
};

int orbit_scratch_create(struct orbit_scratch *s, size_t init_size)
{
	void *area;

	area = mmap((void*)0x900000, init_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == NULL) return -1;

	s->ptr = area;
	s->size_limit = init_size;
	s->cursor = 0;
	s->count = 0;

	return 0;
}

static inline size_t round_up_4(size_t value)
{
	size_t rem = value & 3;
	return rem == 0 ? value : value + 4 - rem;
}

int orbit_scratch_push_update(struct orbit_scratch *s, void *ptr, size_t length)
{
	size_t rec_size = sizeof(struct orbit_repr) + length;

	if (!(s->cursor + rec_size < s->size_limit))
		return -1;	/* No enough space */

	struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);

	record->type = ORBIT_UPDATE;
	record->update.ptr = ptr;
	record->update.length = length;
	memcpy(record->update.data, ptr, length);

	s->cursor += rec_size;
	s->cursor = round_up_4(s->cursor);

	return ++s->count;
}

int orbit_scratch_push_operation(struct orbit_scratch *s,
		orbit_operation_func func, size_t argc, unsigned long argv[])
{
	size_t length = argc * sizeof(*argv);
	size_t rec_size = sizeof(struct orbit_repr) + length;

	if (!(s->cursor + rec_size < s->size_limit))
		return -1;	/* No enough space */

	struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);

	record->type = ORBIT_OPERATION;
	record->operation.func = func;
	record->operation.argc = argc;
	memcpy(record->operation.argv, argv, length);

	s->cursor += rec_size;
	s->cursor = round_up_4(s->cursor);

	return ++s->count;
}

int orbit_sendv(struct orbit_scratch *s)
{
	return syscall(SYS_ORBIT_SENDV, s);
}

int orbit_recvv(struct orbit_scratch *s, struct obTask *task)
{
	int ret = syscall(SYS_ORBIT_RECVV, s, task->taskid);
	s->cursor = 0;
	return ret;
}

int orbit_apply(struct orbit_scratch *s)
{
	const int DBG = 1;

	if (DBG) fprintf(stderr, "Orbit: Applying scratch, total %lu\n", s->count);

	while (s->count--) {
		struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);

		if (record->type == ORBIT_UPDATE) {
			struct orbit_update *update = &record->update;
			if (DBG) fprintf(stderr, "Orbit: Found update %p, %lu\n",
					update->ptr, update->length);

			memcpy(update->ptr, update->data, update->length);

			s->cursor += sizeof(struct orbit_repr) + update->length;
			s->cursor = round_up_4(s->cursor);
		} else if (record->type == ORBIT_OPERATION) {
			struct orbit_operation *op = &record->operation;
			if (DBG) fprintf(stderr, "Orbit: Found operation %p, %lu\n",
					op->func, op->argc);

			/* TODO: send this back? */
			unsigned long ret = op->func(op->argc, op->argv);
			(void)ret;

			s->cursor += sizeof(struct orbit_repr) + op->argc * sizeof(*op->argv);
			s->cursor = round_up_4(s->cursor);
		} else {
			printf("unsupported update type: %d\n", record->type);
			break;
		}
	}
	return 0;
}

int orbit_recvv_finish(struct orbit_scratch *s)
{
	/* return munmap(s->ptr, s->size_limit); */
	return 0;
}
