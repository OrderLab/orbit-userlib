#include "orbit.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <signal.h>

#define SYS_ORBIT_CREATE	436
#define SYS_ORBIT_CALL		437
#define SYS_ORBIT_RETURN	438
#define SYS_ORBIT_SEND		439
#define SYS_ORBIT_RECV		440
#define SYS_ORBIT_COMMIT	441
#define SYS_ORBIT_SENDV		442
#define SYS_ORBIT_RECVV		443
#define SYS_ORBIT_DESTROY	444
#define SYS_ORBIT_DESTROY_ALL	445
#define SYS_ORBIT_STATE		446

enum orbit_state
{
        ORBIT_NEW,
        ORBIT_ATTACHED,
        ORBIT_STARTED,
        ORBIT_STOPPED,
        ORBIT_DETTACHED,
        ORBIT_DEAD
};

/* Orbit flags */
#define ORBIT_ASYNC	1

#define ARG_SIZE_MAX 1024

#define _define_round_up(base) \
	static inline size_t round_up_##base(size_t value) { \
		return (value + base - 1) & ~(base - 1); \
	}

_define_round_up(4);
_define_round_up(4096);
#define round_up_page round_up_4096

#undef _define_round_up

static struct {
	/* Last allocated scratch. The fields are for a different usage:
	 * ptr is the ptr to the mmapped area;
	 * cursor contains the last scratch ptr given to the user;
	 * size_limit is the size of the mmapped area;
	 * count is unused.
	 */
	struct orbit_scratch last_scratch;
} info;

static void scratch_init(void)
{
	info.last_scratch = (struct orbit_scratch) {
		.ptr = NULL,
		.cursor = 0,
		.size_limit = 0,
		.count = 0,
	};
}

static void info_init(void)
{
	scratch_init();
}

long orbit_taskid;

struct orbit_module *orbit_create(const char *module_name,
		orbit_entry entry_func)
{
	struct orbit_module *ob;
	unsigned long ret = 0;
	char argbuf[ARG_SIZE_MAX];

	ob = (struct orbit_module*)malloc(sizeof(struct orbit_module));
	if (ob == NULL) return NULL;

	pid_t mpid;
	obid_t lobid, gobid;

	gobid = syscall(SYS_ORBIT_CREATE, module_name, argbuf, &mpid, &lobid);
	if (gobid == -1) {
		free(ob);
		printf("orbit_create failed with errno: %s\n", strerror(errno));
		return NULL;
	} else if (gobid == 0) {
		/* We are now in child, we should run the function  */
		/* FIXME: we should create scratch in orbit! */
		// info_init();
		(void)info_init;

		/* TODO: allow the child to stop */
		while (1) {
			/* TODO: currently a hack to return for the first time
			 * for initialization. */
			orbit_taskid = syscall(SYS_ORBIT_RETURN, ret);
			if (orbit_taskid < 0) {
				fprintf(stderr, "orbit returns ERROR, exit\n");
				break;
			}
			ret = entry_func(argbuf);
		}
	}

	printf("Created orbit <mpid %d, lobid %d, gobid %d>\n", mpid, lobid,
	       gobid);

	/* Now we are in parent. */
	ob->mpid = mpid;
	ob->lobid = lobid;
	ob->gobid = gobid;
	ob->entry_func = entry_func;
	if (module_name)
		strncpy(ob->name, module_name, ORBIT_NAME_LEN);
	else
		strcpy(ob->name, "anonymous");
	return ob;
}

struct pool_range_kernel {
	unsigned long start;
	unsigned long end;
	bool cow;
};

static long orbit_call_inner(struct orbit_module *module, unsigned long flags,
		size_t npool, struct orbit_pool** pools,
		void *arg, size_t argsize)
{
	long ret;

	/* This requries C99.  We can limit number of pools otherwise.*/
	struct pool_range_kernel pools_kernel[npool];

	for (size_t i = 0; i < npool; ++i) {
		struct orbit_pool *pool = pools[i];
		unsigned long start = (unsigned long)pool->rawptr;
		/* TODO: directly using `allocated` is not actually safe.
		 * However, if we hold all pool->lock until orbit_call ends,
		 * it might be too long. */
		unsigned long length = (unsigned long)round_up_page(pool->allocated);
		pools_kernel[i].start = start;
		pools_kernel[i].end = start + length;
		pools_kernel[i].cow = pool->cow;
	}

	ret = syscall(SYS_ORBIT_CALL, flags, module->gobid, npool, pools_kernel,
		      arg, argsize);
	// printf("In orbit_call_inner, ret=%ld\n", ret);
	return ret;
}

long orbit_call(struct orbit_module *module,
		size_t npool, struct orbit_pool** pools,
		void *arg, size_t argsize)
{
	return orbit_call_inner(module, 0, npool, pools, arg, argsize);
}

int orbit_call_async(struct orbit_module *module, unsigned long flags,
		size_t npool, struct orbit_pool** pools,
		void *arg, size_t argsize, struct orbit_task *task)
{
	long ret = orbit_call_inner(module, flags | ORBIT_ASYNC,
			npool, pools, arg, argsize);
	if (ret < 0)
		return ret;
	if (task) {
		task->orbit = module;
		task->taskid = ret;
	}
	return 0;
}

unsigned long orbit_send(const struct orbit_update *update) {
	return syscall(SYS_ORBIT_SEND, update);
}

unsigned long orbit_recv(struct orbit_task *task, struct orbit_update *update) {
	return syscall(SYS_ORBIT_RECV, task->orbit->gobid, task->taskid, update);
}

unsigned long orbit_commit(void) {
	return syscall(SYS_ORBIT_COMMIT);
}

/* Return a memory allocation pool. */
struct orbit_pool *orbit_pool_create(size_t init_pool_size /*, int raw = 0 */ ) {
	const int DBG = 0;
	void *MMAP_HINT = DBG ? (void*)0x8000000 : NULL;
	return orbit_pool_create_at(init_pool_size, MMAP_HINT);

}

struct orbit_pool *orbit_pool_create_at(size_t init_pool_size, void *addr) {
	struct orbit_pool *pool;
	void *area;
	int ret;

	init_pool_size = round_up_page(init_pool_size);

	pool = (struct orbit_pool*)malloc(sizeof(struct orbit_pool));
	if (pool == NULL) goto pool_malloc_fail;

	area = mmap(addr, init_pool_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == NULL) goto mmap_fail;

	ret = pthread_spin_init(&pool->lock, PTHREAD_PROCESS_PRIVATE);
	if (ret != 0) goto lock_init_fail;

	pool->rawptr = area;
	pool->length = init_pool_size;
	pool->cow = true;

	pool->allocated = 0;

	return pool;

lock_init_fail:
	munmap(area, init_pool_size);
mmap_fail:
	free(pool);
pool_malloc_fail:
	return NULL;
}
// void orbit_pool_destroy(pool);

/* TODO: currently we are ony using a linear allocating mechanism.
 * In the future we will need to design an allocation algorithm aiming for
 * compactness of related data. */
void *__orbit_pool_alloc(struct orbit_pool *pool, size_t size,
	const char *file, int line)
{
	void *ptr;
	int ret;

	ret = pthread_spin_lock(&pool->lock);
	if (ret != 0) return NULL;

	if (!(pool->allocated + size < pool->length)) {
		fprintf(stderr, "Pool %p is full.\n", pool);
		abort();
		return NULL;
	}

	ptr = (char*)pool->rawptr + pool->allocated;

	pool->allocated += size;

	pthread_spin_unlock(&pool->lock);

#define OUTPUT_ORBIT_ALLOC 0
#if OUTPUT_ORBIT_ALLOC
	void __mysql_orbit_alloc_callback(void *, size_t, const char *, int);
	__mysql_orbit_alloc_callback(ptr, size, file, line);
#else
	(void)file;
	(void)line;
#endif

	return ptr;
}

void orbit_pool_free(struct orbit_pool *pool, void *ptr, size_t size)
{
	/* Let it leak. */
	(void)pool;
	(void)ptr;
	(void)size;
}

/* === sendv/recvv === */

int orbit_scratch_create(struct orbit_scratch *s, size_t size_hint)
{
	const int DBG = 0;
	void *MMAP_HINT = DBG ? (void*)0x900000 : NULL;

	void *area;
	struct orbit_scratch *info_s = &info.last_scratch;

	if (info_s->ptr == NULL) {
		size_hint = round_up_page(size_hint);
		area = mmap(MMAP_HINT, size_hint, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (area == MAP_FAILED) return -1;

		info_s->ptr = area;
		info_s->size_limit = size_hint;
	}

	s->ptr = info_s->ptr;
	s->size_limit = info_s->size_limit - info_s->cursor;
	s->cursor = 0;
	s->count = 0;

	return 0;
}

int orbit_scratch_push_update(struct orbit_scratch *s, void *ptr, size_t length)
{
	struct orbit_repr *record;
	size_t rec_size = sizeof(struct orbit_repr) + length;

	if (!(s->cursor + rec_size < s->size_limit))
		return -1;	/* No enough space */

	record = (struct orbit_repr*)(s->ptr + s->cursor);

	record->type = ORBIT_UPDATE;
	record->update.ptr = ptr;
	record->update.length = length;
	memcpy(record->update.data, ptr, length);

	s->cursor += rec_size;
	s->cursor = round_up_4(s->cursor);

	return ++s->count;
}

int orbit_scratch_push_any(struct orbit_scratch *s, void *ptr, size_t length)
{
	struct orbit_repr *record;
	size_t rec_size = sizeof(struct orbit_repr) + length;

	if (!(s->cursor + rec_size < s->size_limit))
		return -1;	/* No enough space */

	record = (struct orbit_repr*)(s->ptr + s->cursor);

	record->type = ORBIT_ANY;
	record->any.length = length;
	memcpy(record->any.data, ptr, length);

	s->cursor += rec_size;
	s->cursor = round_up_4(s->cursor);

	return ++s->count;
}

int orbit_scratch_push_operation(struct orbit_scratch *s,
		orbit_operation_func func, size_t argc, unsigned long argv[])
{
	struct orbit_repr *record;
	size_t length = argc * sizeof(*argv);
	size_t rec_size = sizeof(struct orbit_repr) + length;

	if (!(s->cursor + rec_size < s->size_limit))
		return -1;	/* No enough space */

	record = (struct orbit_repr*)(s->ptr + s->cursor);

	record->type = ORBIT_OPERATION;
	record->operation.func = func;
	record->operation.argc = argc;
	memcpy(record->operation.argv, argv, length);

	s->cursor += rec_size;
	s->cursor = round_up_4(s->cursor);

	return ++s->count;
}

static void scratch_trunc(const struct orbit_scratch *s)
{
	struct orbit_scratch *info_s = &info.last_scratch;
	size_t size = round_up_page(s->cursor);

	if (info_s->size_limit <= info_s->cursor + size) {
		/* TODO: unmap safety in the kernel
		 * If we decide to copy page range at recvv instead of at sendv,
		 * we need to consider another mechanism to unmap pages. */
		munmap(info_s->ptr, info_s->size_limit);
		scratch_init();
	} else {
		info_s->cursor += size;
	}
}

int orbit_sendv(struct orbit_scratch *s)
{
	int ret;

	struct orbit_scratch buf = {
		.ptr = s->ptr,
		.cursor = 0,
		.size_limit = round_up_page(s->cursor),
		.count = s->count,
	};

	ret = syscall(SYS_ORBIT_SENDV, &buf);
	if (ret < 0)
		return ret;

	/* If the send is not successful, we do not call trunc(), and it is
	 * safe to allocate a new scratch in the same area since we will
	 * rewrite it later anyway. */
	scratch_trunc(s);

	return ret;
}

int orbit_recvv(union orbit_result *result, struct orbit_task *task)
{
	int ret = syscall(SYS_ORBIT_RECVV, result, task->orbit->gobid,
			  task->taskid);
	if (ret == 1)
		result->scratch.cursor = 0;
	return ret;
}

int orbit_destroy(obid_t gobid)
{
	return syscall(SYS_ORBIT_DESTROY, gobid);
}

int orbit_destroy_all()
{
	return syscall(SYS_ORBIT_DESTROY_ALL);
}

bool orbit_exists(struct orbit_module *ob)
{
	int ret;
	enum orbit_state state;
	ret = syscall(SYS_ORBIT_STATE, ob->gobid, &state);
	return ret == 0 && state != ORBIT_DEAD;
}

bool orbit_gone(struct orbit_module *ob)
{
	int ret;
	enum orbit_state state;
	ret = syscall(SYS_ORBIT_STATE, ob->gobid, &state);
	return ret < 0 || state == ORBIT_DEAD;
}

enum orbit_type orbit_apply_one(struct orbit_scratch *s, bool yield)
{
	const int DBG = 0;
	if (DBG) fprintf(stderr, "Orbit: Applying scratch, total %lu\n", s->count);

	if (s->count == 0)
		return ORBIT_END;

	struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);
	enum orbit_type type = record->type;
	size_t extra_size = 0;

	if (type == ORBIT_UPDATE) {
		struct orbit_update *update = &record->update;
		if (DBG) fprintf(stderr, "Orbit: Found update %p, %lu\n",
				update->ptr, update->length);

		memcpy(update->ptr, update->data, update->length);

		extra_size = update->length;
	} else if (type == ORBIT_OPERATION) {
		unsigned long ret;
		struct orbit_operation *op = &record->operation;

		if (DBG) fprintf(stderr, "Orbit: Found operation %p, %lu\n",
				op->func, op->argc);

		/* TODO: send this back? */
		ret = op->func(op->argc, op->argv);
		(void)ret;

		extra_size = op->argc * sizeof(*op->argv);
	} else if (type == ORBIT_ANY) {
		if (yield)
			return ORBIT_ANY;
		/* Otherwise, skip this data. */
		extra_size = record->any.length;
	} else if (type == ORBIT_END) {
		extra_size = 0;
	} else {
		if (DBG) fprintf(stderr, "Orbit: unsupported update type: %d\n", record->type);

		return ORBIT_UNKNOWN;
	}

	s->cursor += sizeof(struct orbit_repr) + extra_size;
	s->cursor = round_up_4(s->cursor);

	--s->count;

	if (DBG) fprintf(stderr, "Orbit: Applied one update normally\n");

	return type;
}

enum orbit_type orbit_apply(struct orbit_scratch *s, bool yield)
{
	while (s->count) {
		enum orbit_type type = orbit_apply_one(s, yield);

		if ((type == ORBIT_END) || type == ORBIT_UNKNOWN ||
			(type == ORBIT_ANY && yield))
			return type;
	}

	return ORBIT_END;
}

enum orbit_type orbit_skip_one(struct orbit_scratch *s, bool yield)
{
	if (s->count == 0)
		return ORBIT_END;

	struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);
	enum orbit_type type = record->type;
	size_t extra_size;

	switch (type) {
	case ORBIT_UPDATE:
		extra_size = record->update.length;
		break;
	case ORBIT_OPERATION:
		extra_size = record->operation.argc *
				sizeof(*record->operation.argv);
		break;
	case ORBIT_ANY:
		if (yield)
			return ORBIT_ANY;
		extra_size = record->any.length;
		break;
	case ORBIT_END:
		extra_size = 0;
		break;
	case ORBIT_UNKNOWN:
	default:
		return ORBIT_UNKNOWN;
	}

	s->cursor += sizeof(struct orbit_repr) + extra_size;
	s->cursor = round_up_4(s->cursor);
	--s->count;

	return type;
}

enum orbit_type orbit_skip(struct orbit_scratch *s, bool yield)
{
	while (s->count) {
		enum orbit_type type = orbit_skip_one(s, yield);

		if (type == ORBIT_END || type == ORBIT_UNKNOWN ||
			(type == ORBIT_ANY && yield))
			return type;
	}

	return ORBIT_END;
}

struct orbit_repr *orbit_scratch_first(struct orbit_scratch *s)
{
	if (s->count == 0)
		return NULL;

	struct orbit_repr *record = (struct orbit_repr*)(s->ptr + s->cursor);

	switch (record->type) {
	case ORBIT_ANY:
	case ORBIT_UPDATE:
	case ORBIT_OPERATION:
		return record;
	case ORBIT_END:
	case ORBIT_UNKNOWN:
	default:
		return NULL;
	}
}

int orbit_recvv_finish(struct orbit_scratch *s)
{
	/* return munmap(s->ptr, s->size_limit); */
	(void)s;
	return 0;
}
