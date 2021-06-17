#ifndef __ORBIT_H__
#define __ORBIT_H__

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIT_NORETVAL		2


/*
 * Orbit entry function signature.
 *
 * It is defined similar to pthread functions, but there are huge differences.
 *
 * The argbuf points to a buffer in orbit's address space.  The user can use
 * any data representation they want in the buffer.
 *
 * For an original entry function that is defined as:
 *
 *     unsigned long func(int arg1, float arg2, char *arg3);
 *
 * To port to orbit, user is expected to define a custom arguments struct:
 *
 *     struct func_args {
 *         int arg1;
 *         float arg2;
 *         char *arg3;
 *     } args;
 *
 * This argument struct will be copied to the orbit's address space when
 * calling orbit_call:
 *
 *     orbit_call(ob, ..., &args, sizeof(func_args));
 *
 * And the orbit entry function can be used as a wrapper function that extracts
 * actual arguments from func_args and call the original function:
 *
 *     unsigned long func_orbit(void *argbuf) {
 *         struct func_args *args = (struct func_args *)argbuf;
 *         return func(args->arg1, args->arg2, args->arg3);
 *     }
 *
 * Such kind of wrapper functions can also be generated using orbit compiler,
 * or use some fancy tricks in languages with advanced type systems.
 *
 * Return value is defined as an unsigned integer.  You can still use it to
 * represent negative values when calling in asynchronous mode.
 *
 * However, due to current implementation limitation, in synchronous orbit_call
 * you can not use the MSB.  This can be fixed if we do not reuse the syscall
 * return value as orbit_entry return value.
 */
typedef unsigned long(*orbit_entry)(void *argbuf);

struct orbit_module {
	unsigned long obid;    // module id used by syscalls
	orbit_entry entry_func;
	// TODO
};

struct orbit_pool {
	void *rawptr;
	size_t length;	// the pool should be page-aligned
	// bool raw;  // true to enable the following API, false to be used as raw memory segments.
	/* ... other metadata */
	size_t allocated;	/* linear allocator */
	pthread_spinlock_t	lock;	/* alloc needs to be thread-safe */
	bool cow;
};

// typedef int(*orbit_callback)(struct orbit_update*);

struct orbit_task {
	unsigned long obid;
	unsigned long taskid;
	// orbit_callback callback;
};

#define ORBIT_BUFFER_MAX 1024	/* Maximum buffer size of orbit_update data field */

/*
 * Information of where to update and what to update to.
 *
 * Data is a dynamically sized buffer that can contain anything.
 */
struct orbit_update {
	void *ptr;
	size_t length;
	char data[];
};

/*
 * We may later change this to accept (void*) instead of argc, argv.
 */
typedef unsigned long(*orbit_operation_func)(size_t, unsigned long[]);

/*
 * Information of operation to execute, just like a closure.
 *
 * Argv is a dynamically sized array that can contain arguments smaller than
 * `long`.
 */
struct orbit_operation {
	orbit_operation_func func;
	size_t argc;
	unsigned long argv[];	/* `void*` type argument */
};

struct orbit_any {
	unsigned long length;
	char data[];
};

enum orbit_type { ORBIT_END, ORBIT_UNKNOWN, ORBIT_ANY,
		  ORBIT_UPDATE, ORBIT_OPERATION, };

struct orbit_repr {
	enum orbit_type type;
	/* Maybe consider type safety of this union? */
	union {
		struct orbit_update update;
		struct orbit_operation operation;
		struct orbit_any any;
	};
};

/*
 * Spawn an orbit.
 *
 * Underlying syscall: orbit_create, 0 args, return obid.
 * entry_func is stored in the orbit task handle loop.
 */
struct orbit_module *orbit_create(const char *module_name /* UNUSED */, orbit_entry entry_func);
// void obDestroy(orbit_module*);

/*
 * Create an orbit call.
 *
 * Note that for synchronous orbit_call, the entry_func cannot call orbit_send
 * or orbit_sendv.
 *
 * Return non-negative values on success. Return -1 on error, and stores error
 * code in `errno'.
 */
long orbit_call(struct orbit_module *module,
		size_t npool, struct orbit_pool** pools,
		orbit_entry func, void *arg, size_t argsize);

/*
 * Create an async orbit call.
 *
 * If the call succeeds and `task` is not NULL, task information will be stored in `task`.
 * Return 0 on success. Other value indicates failure.
 */
int orbit_call_async(struct orbit_module *module, unsigned long flags,
		size_t npool, struct orbit_pool** pools,
		orbit_entry func, void *arg, size_t argsize, struct orbit_task *task);

// Functions to send updates in the checker and receive in the main program.
unsigned long orbit_send(const struct orbit_update *update);
unsigned long orbit_recv(struct orbit_task *task, struct orbit_update *update);

/* Page level granularity update */
unsigned long orbit_commit(void);

/* User land runtime function that will be called by the kernel in orbit
 * context and will then call the real function. We do not really need....*/
// void obCallWrapper(orbit_entry entry_point, void *auxptr);

/* Return a memory allocation pool. */
struct orbit_pool *orbit_pool_create(size_t init_pool_size /*, int raw = 0 */ );
struct orbit_pool *orbit_pool_create_at(size_t init_pool_size, void *addr);
// void obPoolDestroy(pool);

void *__orbit_pool_alloc(struct orbit_pool *pool, size_t size,
			const char *file, int line);
#define orbit_pool_alloc(pool, size) \
	__orbit_pool_alloc(pool, size, __FILE__, __LINE__)
void orbit_pool_free(struct orbit_pool *pool, void *ptr);
void *orbit_pool_realloc(struct orbit_pool *pool, void *oldptr, size_t newsize);

/* Encoded orbit updates and operations. */
struct orbit_scratch {
	void *ptr;
	size_t cursor;
	size_t size_limit;
	size_t count;	/* Number of elements */
};

union orbit_result {
	unsigned long retval;
	struct orbit_scratch scratch;
};

/* Get a scratch space.
 * After each successful sendv(), the caller needs to call this again to
 * allocate a new scratch space. */
int orbit_scratch_create(struct orbit_scratch *s, size_t size_hint);
// void orbit_scratch_free(orbit_scratch *s);

int orbit_scratch_push_operation(struct orbit_scratch *s,
		orbit_operation_func func, size_t argc, unsigned long argv[]);
int orbit_scratch_push_update(struct orbit_scratch *s, void *ptr, size_t length);
int orbit_scratch_push_any(struct orbit_scratch *s, void *ptr, size_t length);

/* Return 0 on success, otherwise -1 and sets errno.
 * Note: After success send, the scratch will not be accessible any more!
 * If the send fails, this scratch is still accessible, and the caller can
 * optionally update it and resend.
 * If the caller decides not to send it, the caller needs to call create()
 * again to request a new scratch space. */
int orbit_sendv(struct orbit_scratch *s);
/* Returns 1 if update available, and modifies result->scratch;
 * Returns 0 on end of updates, and modifies result->retval;
 * Returns -1 on error, and sets errno. */
int orbit_recvv(union orbit_result *result, struct orbit_task *task);

enum orbit_type orbit_apply(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_apply_one(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_skip(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_skip_one(struct orbit_scratch *s, bool yield);

struct orbit_repr *orbit_scratch_first(struct orbit_scratch *s);

#ifdef __cplusplus
}
#endif

#endif /* __ORBIT_H__ */
