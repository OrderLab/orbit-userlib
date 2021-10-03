#ifndef __ORBIT_H__
#define __ORBIT_H__

#ifdef __cplusplus
#include <cstddef>
#include <cstring>
#include <memory>
extern "C" {
#else
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#endif

#include <pthread.h>

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
 *     unsigned long func_orbit(..., void *argbuf) {
 *         struct func_args *args = (struct func_args *)argbuf;
 *         return func(args->arg1, args->arg2, args->arg3);
 *     }
 *
 * Such kind of wrapper functions can also be generated using orbit compiler,
 * or use some fancy tricks in languages with advanced type systems.
 *
 * The 'store' parameter is used for orbit's self-managed data.  It is
 * initialized by the init_func specified in orbit_call.  The init_func will
 * be called once after orbit has been successfully created.
 *
 * Return value is defined as an unsigned integer.  You can still use it to
 * represent negative values when calling in asynchronous mode.
 *
 * However, due to current implementation limitation, in synchronous orbit_call
 * you can not use the MSB.  This can be fixed if we do not reuse the syscall
 * return value as orbit_entry return value.
 */
typedef unsigned long(*orbit_entry)(void *store, void *argbuf);

typedef int pid_t;
typedef int obid_t;

#define ORBIT_NAME_LEN 16

/*
 * An orbit can be identified by either a <mpid, lobid> tuple or a <gobid>.
 *
 * <mpid> represents the main program the orbit is attached to.
 * <lobid> is the orbit id local to the main program, and is not globally unique.
 * <gobid> is the globally unique id assigned to an orbit.
 */
struct orbit_module {
	pid_t mpid; // PID of the main program
	obid_t lobid; // orbit id local to a main program
	pid_t gobid; // global orbit id, which can uniquely identify the kernel object

	orbit_entry entry_func;
	char name[ORBIT_NAME_LEN];
};

/*
 * Orbit pool mode
 *
 * This is used by kernel update_page_range as a hint to determine the strategy
 * to use when copying pages between orbit and main program.
 * Actual mode used depends on kernel implementation or strategies in kernel.
 *
 * Currently we define three modes: CoW, MOVE, and COPY.
 *   CoW:  use copy-on-write to share the pages between orbit and main program
 *   MOVE: move the PTEs; sender will immediately lose access to the sent data
 *   COPY: copy the data into kernel and then copy to the other side
 */
enum orbit_pool_mode { ORBIT_COW, ORBIT_MOVE, ORBIT_COPY, };

/*
 * Orbit pool
 *
 * Orbit pool manages an underlying VMA.  An orbit pool have a snapshot mode
 * that is used by the kernel when transferring data between orbit and main
 * program.  The default mode will be CoW.
 *
 * The pool structure also maintains the size of used space as an optimization
 * for snapshot.
 * To use dynamic memory management, create an allocator from the pool.
 * To use the pool as a raw memory region and snapshot the whole pool every
 * time, set `used` to `length`.
 */
struct orbit_pool {
	void *rawptr;
	size_t length;	// the pool should be page-aligned
	size_t used;
	enum orbit_pool_mode mode;
};

// typedef int(*orbit_callback)(struct orbit_update*);

struct orbit_task {
	struct orbit_module *orbit;
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
struct orbit_module *orbit_create(const char *module_name,
		orbit_entry entry_func, void*(*init_func)(void));
// void obDestroy(orbit_module*);

bool is_orbit_context(void);

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

/* Create an return a memory allocation pool of size 'init_pool_size'
 * to be used by an orbit.
 *
 * If argument <ob> is specified, the pool will be created and mapped to the
 * address space of <ob>. If <ob> is NULL, the pool will be created and mapped
 * later to a new orbit, which is a discouraged way.
*/
struct orbit_pool *orbit_pool_create(struct orbit_module *ob,
				     size_t init_pool_size);
struct orbit_pool *orbit_pool_create_at(struct orbit_module *ob,
					size_t init_pool_size, void *addr);

// void obPoolDestroy(pool);


/* ====== Allocator API ===== */

/*
 * A linear allocator.
 *
 * This allocator can be created from orbit_pool or orbit_scratch.
 *
 * This allocator is typically used to dynamically determine the size of useful
 * data in the underlying memory region.  Thus it is now coupled with the
 * back end on an "allocated size" in the back end.  It contains a pointer to
 * an external location that stores the allocated size.  For orbit_pool it is
 * `used`, and for orbit_any it is the `length` field.
 * After each alloc or free, the allocator will updates the external size field.
 *
 * The allocator has a "use_meta" option.  When this is set, the allocator will
 * use a small header before the allocated data.  This header is useful for
 * `free` and `realloc`.  For scenarios that does not need free or realloc,
 * unsetting this option can help save space used in the underlying memory
 * region.  This option shall not be changed after the creation.
 */
struct orbit_allocator {
	void *start;		/* Underlying memory region */
	size_t length;
	size_t *allocated;	/* External pointer to allocated size */
	pthread_spinlock_t lock;	/* alloc needs to be thread-safe */
	bool use_meta;
};

/* Create an allocator */
struct orbit_allocator *orbit_allocator_create(void *start, size_t length,
		size_t *allocated, bool use_meta);
/* Destroy an allocator */
void orbit_allocator_destroy(struct orbit_allocator *alloc);

/* Create an allocator using underlying pool */
struct orbit_allocator *orbit_allocator_from_pool(struct orbit_pool *pool, bool use_meta);

void *__orbit_alloc(struct orbit_allocator *alloc, size_t size,
			const char *file, int line);
static inline void *__orbit_calloc(struct orbit_allocator *alloc, size_t size,
			const char *file, int line)
{
	return memset(__orbit_alloc((alloc), size, file, line), 0, size);
}
#define orbit_alloc(alloc, size) \
	__orbit_alloc(alloc, size, __FILE__, __LINE__)
#define orbit_calloc(alloc, size) \
	__orbit_calloc(alloc, size, __FILE__, __LINE__)
void orbit_free(struct orbit_allocator *alloc, void *ptr);
void *orbit_realloc(struct orbit_allocator *alloc, void *oldptr, size_t newsize);
#define orbit_allocated_by(ptr, alloc) \
	(alloc != NULL && (ptr >= alloc->start) && (ptr < alloc->start + alloc->length))

/* ===== Scratch ADT ===== */

/*
 * Encoded array of orbit updates and operations.
 *
 * Scratch needs an underlying memory region.  We typically use a orbit pool.
 *
 * This works like vector<any>.
 * User can push scratch ADT elements: update, operation, any to a scratch.
 * User can also iterate througt the elements.
 */
struct orbit_scratch {
	void *ptr;
	size_t cursor;
	size_t size_limit;
	size_t count;	/* Number of elements */
	struct orbit_allocator *any_alloc;  /* allocator used by open_any */
};

/* TODO: specialized query APIs */
union orbit_result {
	unsigned long retval;
	struct orbit_scratch scratch;
};

/*
 * Set global pool to use to create orbit_scratch.
 *
 * Typically we use orbit pool mode "MOVE" for performance reason.
 *
 * This needs to be called before creating a scratch.
 */
int orbit_scratch_set_pool(struct orbit_pool *pool);

/*
 * Get a scratch space.
 *
 * After each successful sendv(), the caller needs to call this again to
 * allocate a new scratch space.
 */
int orbit_scratch_create(struct orbit_scratch *s);
// void orbit_scratch_free(orbit_scratch *s);

/* Push orbit_operation to scratch */
int orbit_scratch_push_operation(struct orbit_scratch *s,
		orbit_operation_func func, size_t argc, unsigned long argv[]);
/* Push orbit_update to scratch */
int orbit_scratch_push_update(struct orbit_scratch *s, void *ptr, size_t length);
/* Push orbit_any to scratch */
int orbit_scratch_push_any(struct orbit_scratch *s, void *ptr, size_t length);

#if defined(__cplusplus) && __cplusplus >= 201103L

// TODO: rewrite two versions with better C macro and pure C++11 arg forward.
// We can also provide compiler support for structuring lambda s.t. it can
// written ergonomically and be sent safely.
#define orbit_scratch_run1(s, arg1t, arg1n, body) do { \
		unsigned long argv[] { (unsigned long)(arg1n), }; \
		unsigned long (*f)(size_t argc, unsigned long argv[]) = \
		[](size_t argc, unsigned long argv[]) -> unsigned long { \
			arg1t arg1n = (arg1t)argv[0]; \
			body; \
			return 0; \
		}; \
		orbit_scratch_push_operation(s, f, 1, argv); \
	} while (0)
#define orbit_scratch_run2(s, arg1t, arg1n, arg2t, arg2n, body) do { \
		unsigned long argv[] { (unsigned long)(arg1n), (unsigned long)(arg2n), }; \
		unsigned long (*f)(size_t argc, unsigned long argv[]) = \
		[](size_t argc, unsigned long argv[]) -> unsigned long { \
			arg1t arg1n = (arg1t)argv[0]; \
			arg2t arg2n = (arg2t)argv[1]; \
			body; \
			return 0; \
		}; \
		orbit_scratch_push_operation(s, f, 2, argv); \
	} while (0)
#define orbit_scratch_run3(s, arg1t, arg1n, arg2t, arg2n, arg3t, arg3n, body) do { \
		unsigned long argv[] { (unsigned long)(arg1n), \
			(unsigned long)(arg2n), (unsigned long)(arg3n) }; \
		unsigned long (*f)(size_t argc, unsigned long argv[]) = \
		[](size_t argc, unsigned long argv[]) -> unsigned long { \
			arg1t arg1n = (arg1t)argv[0]; \
			arg2t arg2n = (arg2t)argv[1]; \
			arg3t arg3n = (arg3t)argv[2]; \
			body; \
			return 0; \
		}; \
		orbit_scratch_push_operation(s, f, 3, argv); \
	} while (0)

#endif /* C++11 */

/*
 * Create a pending "any" record in the scratch and return an allocator.
 *
 * This is useful to use allocation APIs in scratch space.
 *
 * Note that if the user calls any operations on this scratch other than
 * allocation APIs after this call, the "any" record will automatically be
 * closed and the allocator returned here will be invalidated and should not
 * be used any more.
 * */
struct orbit_allocator *orbit_scratch_open_any(struct orbit_scratch *s, bool use_meta);
/*
 * Close the pending "any" record and destroy the allocator for it.
 *
 * Note that after this function, the allocator returned from previous
 * "open_any" will be invalidated and should not be used any more.
 *
 * This will also be automatically called when the user calls any operations
 * on this scratch.  E.g. push_update, sendv, etc. will all close the
 * "open_any" automatically.
 */
int orbit_scratch_close_any(struct orbit_scratch *s);

/*
 * Send a scratch during orbit_call to main program.
 *
 * Return 0 on success, otherwise -1 and sets errno.
 *
 * Note: After success send, the scratch will not be accessible any more!
 * If the send fails, this scratch is still accessible, and the caller can
 * optionally update it and resend.
 * If the caller decides not to send it, the caller needs to call create()
 * again to request a new scratch space.
 */
int orbit_sendv(struct orbit_scratch *s);

/*
 * Receive in the main program.  Expect a scratch or return value from
 * orbit_call.
 *
 * Returns 1 if update available, and modifies result->scratch;
 * Returns 0 on end of updates, and modifies result->retval;
 * Returns -1 on error, and sets errno.
 */
int orbit_recvv(union orbit_result *result, struct orbit_task *task);

/*
 * Terminate the orbit identified by gobid for the current process.
 *
 * Return 0 on success.
 */
int orbit_destroy(obid_t gobid);

/*
 * Terminate all the orbits for the current process.
 */
int orbit_destroy_all();

bool orbit_exists(struct orbit_module *ob);
bool orbit_gone(struct orbit_module *ob);

enum orbit_type orbit_apply(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_apply_one(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_skip(struct orbit_scratch *s, bool yield);
enum orbit_type orbit_skip_one(struct orbit_scratch *s, bool yield);

struct orbit_repr *orbit_scratch_first(struct orbit_scratch *s);
struct orbit_repr *orbit_scratch_next(struct orbit_scratch *s);

#ifdef __cplusplus
}

#if __cplusplus >= 201103L
#define NOEXCEPT noexcept
#else
#define NOEXCEPT throw()
#endif

namespace orbit {

void *__orbit_allocate_wrapper(orbit_allocator *alloc, std::size_t n, std::size_t type_size);
void __orbit_deallocate_wrapper(orbit_allocator *alloc, void *ptr, std::size_t n) NOEXCEPT;

extern orbit_allocator *__global_allocator;
void set_global_allocator(orbit_allocator *alloc);

template<class T>
struct global_allocator {
public:
	typedef T value_type;
	T* allocate(std::size_t n) {
		if (__global_allocator) {
			return static_cast<T*>(__orbit_allocate_wrapper(
					__global_allocator, n, sizeof(T)));
		}
		return std::allocator<T>().allocate(n);
	}
	void deallocate(T *p, std::size_t n) NOEXCEPT {
		if (__global_allocator)
			return __orbit_deallocate_wrapper(__global_allocator, p, n);
		std::allocator<T>().deallocate(p, n);
	}
	bool operator==(const global_allocator &rhs) const { return true; }
	bool operator!=(const global_allocator &rhs) const { return true; }
};

/* A shim for new and delete operator. Inherit this struct to make the
 * subclass use orbit global allocator by default. */
struct global_new_operator {
	static void* operator new(std::size_t size);
	static void operator delete(void *ptr) NOEXCEPT;
};

// Note: this is currently only a wrapper on the orbit_alloc.
// Destructing this struct won't destroy the orbit_allocator.
template<class T>
struct allocator {
	typedef T value_type;
	allocator(orbit_allocator *alloc) : alloc(alloc) {}
	~allocator() {}
	T* allocate(std::size_t n) {
		if (alloc)
			return static_cast<T*>(__orbit_allocate_wrapper(alloc, n, sizeof(T)));
		return std::allocator<T>().allocate(n);
	}
	void deallocate(T *p, std::size_t n) NOEXCEPT {
		if (alloc)
			return __orbit_deallocate_wrapper(alloc, p, n);
		std::allocator<T>().deallocate(p, n);
	}
private:
	orbit_allocator *alloc;
};

#undef NOEXCEPT

}  // namespace orbit

#endif

#endif /* __ORBIT_H__ */
