#include "orbit.h"
#include "orbit_lowlevel.h"
#include "linear_allocator.h"
#include "bitmap_allocator.h"

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
/* #define SYS_ORBIT_SEND		439 */
/* #define SYS_ORBIT_RECV		440 */
#define SYS_ORBIT_COMMIT	441
#define SYS_ORBIT_PUSH		442
#define SYS_ORBIT_PULL		443
#define SYS_ORBIT_DESTROY	444
#define SYS_ORBIT_DESTROY_ALL	445
#define SYS_ORBIT_STATE		446
#define SYS_ORBIT_MMAP		447
#define SYS_ORBIT_MMAP_PAIR	448
#define SYS_ORBIT_CANCEL	449

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
#define ORBIT_ASYNC	(1<<0)

#define ARG_SIZE_MAX 1024

#define _define_round_up(base) \
	static inline size_t round_up_##base(size_t value) { \
		return (value + base - 1) & ~(base - 1); \
	}

_define_round_up(4)
_define_round_up(4096)
#define round_up_page round_up_4096

#undef _define_round_up

static struct {
	/* Underlying global pool used to create scratch. */
	struct orbit_area *scratch_pool;
} info;

/* Block list data structure for orbit update */

struct __orbit_block_list_elem {
	TAILQ_ENTRY(__orbit_block_list_elem) elem;
#define __ORBIT_BLOCK_SIZE ((512 - sizeof(TAILQ_ENTRY(__orbit_block_list_elem))) \
				/ sizeof(struct orbit_repr))
	struct orbit_repr array[__ORBIT_BLOCK_SIZE];
};
TAILQ_HEAD(__orbit_block_list_head, __orbit_block_list_elem);

struct __orbit_block_list {
	size_t count;	/* Number of elements */
	size_t head, tail;	/* Cursor in the first and last snap_block */
	struct __orbit_block_list_head list;
};

static void scratch_renew(size_t size_hint)
{
	// FIXME: should create the pool for a specific orbit
	if (/* info.auto_renew && */ !info.scratch_pool)
		info.scratch_pool = orbit_area_create(NULL, size_hint,
				orbit_bitmap_default);
}

static void info_init(void)
{
	// FIXME: should create the pool for a specific orbit
	(void)scratch_renew;
	info.scratch_pool = orbit_area_create(NULL, 1024 * 1024,
			orbit_bitmap_default);
}

long orbit_taskid;
static bool orbit_context = false;

struct orbit *orbit_create(const char *module_name,
		orbit_entry entry_func, void*(*init_func)(void))
{
	struct orbit *ob;
	struct orbit_result ret;
	char argbuf[ARG_SIZE_MAX];
	orbit_entry func_once = NULL;
	void *store = NULL;

	ob = (struct orbit*)malloc(sizeof(struct orbit));
	if (ob == NULL) return NULL;

	pid_t mpid;
	obid_t lobid, gobid;

	if (!info.scratch_pool)
		orbit_update_set_area(orbit_area_create(NULL, 64 * 1024 * 1024, NULL));

	gobid = syscall(SYS_ORBIT_CREATE, module_name, argbuf, &mpid, &lobid, &func_once);
	if (gobid == -1) {
		free(ob);
		printf("orbit_create failed with errno: %s\n", strerror(errno));
		return NULL;
	} else if (gobid == 0) {
		/* We are now in child, we should run the function  */
		/* FIXME: we should create scratch in orbit! */
		orbit_context = true;  /* Should this be in info_init()? */
		// info_init();
		(void)info_init;
		if (init_func)
			store = init_func();

		/* TODO: allow the child to stop */
		while (1) {
			/* TODO: currently a hack to return for the first time
			 * for initialization. */
			orbit_taskid = syscall(SYS_ORBIT_RETURN, ret.retval);
			if (orbit_taskid < 0) {
				fprintf(stderr, "orbit returns ERROR, exit\n");
				break;
			}
			ret = func_once ? func_once(store, argbuf)
					: entry_func(store, argbuf);
			orbit_push(&ret);
		}
		exit(0);
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

bool is_orbit_context(void)
{
	return orbit_context;
}

struct pool_range_kernel {
	unsigned long start;
	unsigned long end;
	enum orbit_area_mode mode;
};

struct orbit_call_args_kernel {
	unsigned long flags;
	obid_t gobid;
	size_t narea;
	struct pool_range_kernel *areas;
	orbit_entry func;
	void *arg;
	size_t argsize;
};

static long orbit_call_inner(struct orbit *module, unsigned long flags,
		size_t narea, struct orbit_area** areas,
		orbit_entry func, void *arg, size_t argsize)
{
	long ret;

	/* This requries C99.  We can limit number of areas otherwise.*/
	struct pool_range_kernel pools_kernel[narea];

	struct orbit_call_args_kernel args = { flags, module->gobid,
			narea, pools_kernel, func, arg, argsize, };

	for (size_t i = 0; i < narea; ++i) {
		struct orbit_area *pool = areas[i];
		unsigned long start = (unsigned long)pool->data_start;
		/* TODO: directly using data_{start,length} is not actually safe.
		 * However, if we hold all alloc->lock until orbit_call ends,
		 * it might be too long. */
		unsigned long length = (unsigned long)round_up_page(pool->data_length);
		pools_kernel[i].start = start;
		pools_kernel[i].end = start + length;
		pools_kernel[i].mode = pool->mode;
	}

	ret = syscall(SYS_ORBIT_CALL, &args);
	// printf("In orbit_call_inner, ret=%ld\n", ret);
	return ret;
}

long orbit_call(struct orbit *module,
		size_t narea, struct orbit_area** areas,
		orbit_entry func, void *arg, size_t argsize)
{
	return orbit_call_inner(module, 0, narea, areas, func, arg, argsize);
}

int orbit_call_async(struct orbit *module, unsigned long flags,
		size_t narea, struct orbit_area** areas,
		orbit_entry func, void *arg, size_t argsize, struct orbit_future *future)
{
	long ret = orbit_call_inner(module, flags | ORBIT_ASYNC,
			narea, areas, func, arg, argsize);
	if (ret < 0)
		return ret;
	if (future) {
		future->orbit = module;
		future->taskid = ret;
	}
	return 0;
}

enum orbit_cancel_kind { ORBIT_CANCEL_ARGS, ORBIT_CANCEL_TASKID,
			 ORBIT_CANCEL_KIND_ANY, };

struct orbit_cancel_args {
	obid_t gobid;
	enum orbit_cancel_kind kind;
	union {
		struct {
			void *arg;
			size_t argsize;
		};
		unsigned long taskid;
	};
};

int orbit_cancel_by_task(struct orbit_future *future) {
	struct orbit_cancel_args args = {
		.gobid = future->orbit->gobid,
		.kind = ORBIT_CANCEL_TASKID,
		.taskid = future->taskid,
	};
	return syscall(SYS_ORBIT_CANCEL, &args);
}

int orbit_cancel_by_arg(struct orbit *module, void *arg, size_t argsize) {
	struct orbit_cancel_args args = {
		.gobid = module->gobid,
		.kind = ORBIT_CANCEL_ARGS,
		.arg = arg,
		.argsize = argsize,
	};
	return syscall(SYS_ORBIT_CANCEL, &args);
}

unsigned long orbit_commit(void) {
	return syscall(SYS_ORBIT_COMMIT);
}

void orbit_area_init(struct orbit_area *area, void *rawptr, unsigned long length,
		enum orbit_area_mode mode)
{
	area->rawptr = rawptr;
	area->length = length;
	/* Creating allocator on area will rewrite these values. */
	area->data_start = rawptr;
	area->data_length = 0;
	area->mode = mode;
	area->alloc = NULL;
}

struct orbit_area *orbit_area_create(struct orbit *ob,
				     size_t init_pool_size,
				     const struct orbit_allocator_method *method)
{
	const int DBG = 0;
	void *MMAP_HINT = DBG ? (void*)0x8000000 : NULL;
	return orbit_area_create_at(ob, init_pool_size, MMAP_HINT, method);
}

struct orbit_area *orbit_area_create_at(struct orbit *ob,
					size_t init_pool_size, void *addr,
					const struct orbit_allocator_method *method)
{
	struct orbit_area *area;
	void *mem;

	init_pool_size = round_up_page(init_pool_size);

	area = (struct orbit_area*)malloc(sizeof(struct orbit_area));
	if (area == NULL) goto pool_malloc_fail;

	if (ob != NULL) {
		long ret = syscall(SYS_ORBIT_MMAP_PAIR, ob->gobid, addr,
				   init_pool_size, PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS);
		mem = ret < 0 ? NULL : (void *) ret;
	} else {
		mem = mmap(addr, init_pool_size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}
	if (mem == NULL) goto mmap_fail;

	orbit_area_init(area, mem, init_pool_size, ORBIT_COW);
	area->alloc = method ? orbit_allocator_from_area(area, method) : NULL;

	return area;

mmap_fail:
	free(area);
pool_malloc_fail:
	return NULL;
}
// void orbit_area_destroy(area);


/* ====== Allocator API ===== */

/* === Low-level APIs === */

#ifdef ORBIT_DEBUG
void *__orbit_allocator_alloc(struct orbit_allocator *alloc, size_t size,
			const char *file, int line);
#else
void *orbit_allocator_alloc(struct orbit_allocator *alloc, size_t size)
{
	void *ptr;

	ptr = alloc->vtable->alloc(alloc, size);

#define OUTPUT_ORBIT_ALLOC 0
#ifdef ORBIT_DEBUG
#if OUTPUT_ORBIT_ALLOC
	if (!ptr) return NULL;
	void __mysql_orbit_alloc_callback(void *, size_t, const char *, int);
	__mysql_orbit_alloc_callback(ptr, size, file, line);
#else
	(void)file;
	(void)line;
#endif
#endif

	return ptr;
}
#endif

void orbit_allocator_free(struct orbit_allocator *alloc, void *ptr)
{
	alloc->vtable->free(alloc, ptr);
}

void *orbit_allocator_realloc(struct orbit_allocator *alloc, void *oldptr, size_t newsize)
{
	return alloc->vtable->realloc(alloc, oldptr, newsize);
}

/* Destroy an allocator */
void orbit_allocator_destroy(struct orbit_allocator *alloc)
{
	alloc->vtable->destroy(alloc);
}

/* Create an allocator using underlying alloc */
struct orbit_allocator *orbit_allocator_from_area(struct orbit_area *area,
		const struct orbit_allocator_method *method)
{
	if (!method)
		return NULL;
	return method->__create(area->rawptr, area->length,
			&area->data_start, &area->data_length, method);
}

/* === Public APIs === */

#ifdef ORBIT_DEBUG
void *__orbit_alloc(struct orbit_area *area, size_t size,
			const char *file, int line)
{
	return __orbit_allocator_alloc(area->alloc, size, file, line);
}
#else
void *orbit_alloc(struct orbit_area *area, size_t size)
{
	return orbit_allocator_alloc(area->alloc, size);
}
#endif

void orbit_free(struct orbit_area *area, void *ptr)
{
	orbit_allocator_free(area->alloc, ptr);
}

void *orbit_realloc(struct orbit_area *area, void *oldptr, size_t newsize)
{
	return orbit_allocator_realloc(area->alloc, oldptr, newsize);
}


/* ===== Update ADT ===== */

/* Set global area used to create orbit_update. */
int orbit_update_set_area(struct orbit_area *area)
{
	if (!area)
		return -1;
	info.scratch_pool = area;
	return 0;
}

int orbit_update_create(struct orbit_update *s)
{
	struct orbit_area *info_s = info.scratch_pool;

	if (!info_s || info_s->length == info_s->data_length)
		return -1;

	orbit_area_init(&s->area, (char*)info_s->rawptr + info_s->data_length,
			info_s->length - info_s->data_length, info_s->mode);
	s->area.alloc = orbit_allocator_from_area(&s->area, orbit_linear_default);

	s->updates = orbit_alloc(&s->area, sizeof(*s->updates));
	if (s->updates == NULL) {
		orbit_allocator_destroy(s->area.alloc);
		return -1;
	}
	s->updates->count = s->updates->head = s->updates->tail = 0;
	TAILQ_INIT(&s->updates->list);

	return 0;
}


static struct orbit_repr *
orbit_update_front(struct orbit_update *s)
{
	struct __orbit_block_list *updates = s->updates;
	struct __orbit_block_list_elem *block;
	if (updates->count == 0)
		return NULL;
	block = TAILQ_FIRST(&updates->list);
	return &block->array[updates->head];
}

static void orbit_update_pop(struct orbit_update *s)
{
	struct __orbit_block_list *updates = s->updates;
	struct __orbit_block_list_elem *block;
	if (updates->count == 0) return;
	--updates->count;
	++updates->head;
	if (updates->head == __ORBIT_BLOCK_SIZE || updates->count == 0) {
		block = TAILQ_FIRST(&updates->list);
		TAILQ_REMOVE(&updates->list, block, elem);
		/* if (s->area.alloc)
			orbit_free(&s->area, block); */
		if (updates->count == 0)
			updates->tail = 0;
		updates->head = 0;
	}
}

static struct orbit_repr *
orbit_update_push_allocate(struct orbit_update *s, size_t size, void **data)
{
	struct __orbit_block_list *updates = s->updates;
	struct __orbit_block_list_elem *block;
	struct orbit_repr *repr;

	if (size > __ORBIT_REPR_SMALL_DATA) {
		*data = orbit_alloc(&s->area, size);
		if (*data == NULL)
			return NULL;
	}

	if (TAILQ_EMPTY(&updates->list) || updates->tail == __ORBIT_BLOCK_SIZE) {
		block = orbit_alloc(&s->area, sizeof(struct __orbit_block_list_elem));
		if (block == NULL)
			return NULL;
		TAILQ_INSERT_TAIL(&updates->list, block, elem);
		updates->tail = 0;
	} else {
		block = TAILQ_LAST(&updates->list, __orbit_block_list_head);
	}
	++updates->count;

	repr = &block->array[updates->tail++];

	if (size <= __ORBIT_REPR_SMALL_DATA)
		*data = repr->small_data;

	return repr;
};

int orbit_update_add_modify(struct orbit_update *s, void *ptr, size_t length)
{
	void *data;
	struct orbit_repr *record = orbit_update_push_allocate(s, length, &data);

	if (!record)
		return -1;	/* No enough space */

	record->type = ORBIT_MODIFY;
	record->modify = (struct orbit_modify) { ptr, length, data };
	memcpy(data, ptr, length);

	return s->updates->count;
}

void *orbit_update_add_data(struct orbit_update *s, void *ptr, size_t length)
{
	void *data;
	struct orbit_repr *record = orbit_update_push_allocate(s, length, &data);

	if (!record)
		return NULL;	/* No enough space */

	record->type = ORBIT_ANY;
	record->any = (struct orbit_any) { data, length };
	// Act as preallocating fixed area
	if (ptr) memcpy(data, ptr, length);

	return data;
}

int orbit_update_add_operation(struct orbit_update *s,
		orbit_operation_func func, void *arg, size_t size)
{
	void *data;
	struct orbit_repr *record = orbit_update_push_allocate(s, size, &data);

	if (!record)
		return -1;	/* No enough space */

	record->type = ORBIT_OPERATION;
	record->operation = (struct orbit_operation) { func, data };
	memcpy(data, arg, size);

	return s->updates->count;
}

static void scratch_trunc(const struct orbit_update *s)
{
	struct orbit_area *info_s = info.scratch_pool;

	orbit_allocator_destroy(s->area.alloc);

	info_s->data_length = round_up_page(s->area.data_start
			+ s->area.data_length - info_s->data_start);

	if (info_s->data_length >= info_s->length) {
		/* TODO: unmap safety in the kernel
		 * If we decide to copy page range at recvv instead of at sendv,
		 * we need to consider another mechanism to unmap pages. */
		/* orbit_area_destroy() */
		/* munmap(info_s->ptr, info_s->size_limit); */
		info.scratch_pool = NULL;
		/* TODO: a mechanism to renew/auto create new pool? */
		/* scratch_renew(); */
	}
}

struct orbit_result_kernel {
	unsigned long retval;
	void* data_start;
	size_t data_length;
	struct __orbit_block_list* updates;
};

int orbit_push(struct orbit_result *result)
{
	int ret;
	struct orbit_result_kernel buf;

	if (result->update == NULL)
		return 0;

	buf = (struct orbit_result_kernel) {
		.retval = result->retval,
		.data_start = result->update->area.data_start,
		.data_length = round_up_page(result->update->area.data_length),
		.updates = result->update->updates,
	};

	ret = syscall(SYS_ORBIT_PUSH, &buf);
	if (ret < 0)
		return ret;

	/* If the send is not successful, we do not call trunc(), and it is
	 * safe to allocate a new scratch in the same area since we will
	 * rewrite it later anyway. */
	scratch_trunc(result->update);

	return ret;
}

int pull_orbit(struct orbit_result *result, struct orbit_future *future)
{
	/* Currently, we still use the old underlying multi-return API, but
	 * pack only one result for the `future' API. This could allow
	 * multi-return in the future. */

	/* for update */
	struct orbit_result_kernel buf;
	/* for retval */
	struct orbit_result_kernel buf2;

	*result = (struct orbit_result) { .retval = 0, .update = NULL, };

	int ret = syscall(SYS_ORBIT_PULL, &buf, future->orbit->gobid,
			  future->taskid);
	if (ret == 1) {
		int ret2 = syscall(SYS_ORBIT_PULL, &buf2, future->orbit->gobid,
			  future->taskid);
		if (ret2 != 0) {
			fprintf(stderr, "ret2 is %d, expecting 0!", ret2);
			abort();
		}
		*result = (struct orbit_result) {
			.retval = buf2.retval,
			.update = malloc(sizeof(struct orbit_area)),
		};
		result->update->updates = buf.updates;

		orbit_area_init(&result->update->area,
				buf.data_start, buf.data_length, ORBIT_MOVE);
		result->update->area.data_length = buf.data_length;

	} else if (ret == 0) {
		*result = (struct orbit_result) {
			.retval = buf2.retval,
			.update = NULL,
		};
	}

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

bool orbit_exists(struct orbit *ob)
{
	int ret;
	enum orbit_state state;
	ret = syscall(SYS_ORBIT_STATE, ob->gobid, &state);
	return ret == 0 && state != ORBIT_DEAD;
}

bool orbit_gone(struct orbit *ob)
{
	int ret;
	enum orbit_state state;
	ret = syscall(SYS_ORBIT_STATE, ob->gobid, &state);
	return ret < 0 || state == ORBIT_DEAD;
}

enum orbit_type orbit_apply_one(struct orbit_update *s, bool yield)
{
	const int DBG = 0;
	struct __orbit_block_list *updates = s->updates;

	if (DBG) fprintf(stderr, "Orbit: Applying scratch, total %lu\n", updates->count);

	if (updates->count == 0)
		return ORBIT_END;

	struct orbit_repr *record = orbit_update_front(s);
	enum orbit_type type = record->type;

	if (type == ORBIT_MODIFY) {
		struct orbit_modify *modify = &record->modify;
		if (DBG) fprintf(stderr, "Orbit: Found modify %p, %lu\n",
				modify->ptr, modify->length);

		memcpy(modify->ptr, modify->data, modify->length);
	} else if (type == ORBIT_OPERATION) {
		unsigned long ret;
		struct orbit_operation *op = &record->operation;

		if (DBG) fprintf(stderr, "Orbit: Found operation %p, %p\n",
				op->func, op->arg);

		/* TODO: send this back? */
		ret = op->func(op->arg);
		(void)ret;
	} else if (type == ORBIT_ANY) {
		if (yield)
			return ORBIT_ANY;
		/* Otherwise, skip this data. */
	} else if (type == ORBIT_END) {
	} else {
		if (DBG) fprintf(stderr, "Orbit: unsupported update type: %d\n", record->type);

		return ORBIT_UNKNOWN;
	}

	orbit_update_pop(s);

	if (DBG) fprintf(stderr, "Orbit: Applied one update normally\n");

	return type;
}

enum orbit_type orbit_apply(struct orbit_update *s, bool yield)
{
	while (s->updates->count) {
		enum orbit_type type = orbit_apply_one(s, yield);

		if ((type == ORBIT_END) || type == ORBIT_UNKNOWN ||
			(type == ORBIT_ANY && yield))
			return type;
	}

	return ORBIT_END;
}

enum orbit_type orbit_skip_one(struct orbit_update *s, bool yield)
{
	if (s->updates->count == 0)
		return ORBIT_END;

	struct orbit_repr *record = orbit_update_front(s);
	enum orbit_type type = record->type;

	if (type == ORBIT_ANY && yield)
		return ORBIT_ANY;

	orbit_update_pop(s);

	return type;
}

enum orbit_type orbit_skip(struct orbit_update *s, bool yield)
{
	while (s->updates->count) {
		enum orbit_type type = orbit_skip_one(s, yield);

		if (type == ORBIT_END || type == ORBIT_UNKNOWN ||
			(type == ORBIT_ANY && yield))
			return type;
	}

	return ORBIT_END;
}

struct orbit_repr *orbit_update_first(struct orbit_update *s)
{
	if (s->updates->count == 0)
		return NULL;

	struct orbit_repr *record = orbit_update_front(s);

	switch (record->type) {
	case ORBIT_ANY:
	case ORBIT_MODIFY:
	case ORBIT_OPERATION:
		return record;
	case ORBIT_END:
	case ORBIT_UNKNOWN:
	default:
		return NULL;
	}
}

struct orbit_repr *orbit_update_next(struct orbit_update *s)
{
	orbit_skip_one(s, false);
	return orbit_update_first(s);
}

bool orbit_update_empty(struct orbit_update *s) {
	return s->updates->count == 0;
}

size_t orbit_update_size(struct orbit_update *s) {
	return s->updates->count;
}

int orbit_pull_finish(struct orbit_update *s)
{
	/* return munmap(s->ptr, s->size_limit); */
	(void)s;
	return 0;
}
