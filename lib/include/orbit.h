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
#include <sys/queue.h>

#include <pthread.h>

#define ORBIT_NORETVAL		(1<<1)
#define ORBIT_CANCELLABLE	(1<<2)
/* Skippable (new future) and cancel (previous tasks) must be mutual exclusive */
#define ORBIT_SKIP_SAME_ARG	(1<<3)
#define ORBIT_SKIP_ANY		(1<<4)
#define ORBIT_CANCEL_SAME_ARG	(1<<5)
#define ORBIT_CANCEL_ANY	(1<<6)
/* #define ORBIT_CANCEL_ALL	(1<<7) */


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
typedef struct orbit_result(*orbit_entry)(void *store, void *argbuf);

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
struct orbit {
	pid_t mpid; // PID of the main program
	obid_t lobid; // orbit id local to a main program
	pid_t gobid; // global orbit id, which can uniquely identify the kernel object

	orbit_entry entry_func;
	char name[ORBIT_NAME_LEN];
};

/*
 * Orbit area mode
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
enum orbit_area_mode { ORBIT_COW, ORBIT_MOVE, ORBIT_COPY, };

/*
 * Orbit area
 *
 * Orbit area manages an underlying VMA.  An orbit area have a snapshot mode
 * that is used by the kernel when transferring data between orbit and main
 * program.  The default mode will be CoW.
 *
 * The area structure also maintains the size of used space as an optimization
 * for snapshot.
 * To use dynamic memory management, create an allocator from the area.
 * To use the area as a raw memory region and snapshot the whole area every
 * time, set `used` to `length`.
 */
struct orbit_area {
	void *rawptr;
	size_t length;	// the area should be page-aligned
	void* data_start;
	size_t data_length;
	enum orbit_area_mode mode;
	struct orbit_allocator *alloc;
};

// typedef int(*orbit_callback)(struct orbit_update*);

struct orbit_future {
	struct orbit *orbit;
	unsigned long taskid;
	// orbit_callback callback;
};

#define ORBIT_BUFFER_MAX 1024	/* Maximum buffer size of orbit_modify data field */

/*
 * Information of where to update and what to update to.
 *
 * Data is a dynamically sized buffer that can contain anything.
 */
struct orbit_modify {
	void *ptr;
	size_t length;
	void *data;
};

/*
 * We may later change this to accept (void*) instead of argc, argv.
 */
typedef unsigned long(*orbit_operation_func)(void *arg);

/*
 * Information of operation to execute, just like a closure.
 *
 * Argv is a dynamically sized array that can contain arguments smaller than
 * `long`.
 */
struct orbit_operation {
	orbit_operation_func func;
	void *arg;
};

struct orbit_any {
	void *data;
	unsigned long length;
};

enum orbit_type { ORBIT_END, ORBIT_UNKNOWN, ORBIT_ANY,
		  ORBIT_MODIFY, ORBIT_OPERATION, };

#define __ORBIT_REPR_SMALL_DATA 16

struct orbit_repr {
	enum orbit_type type;
	/* Maybe consider type safety of this union? */
	union {
		struct orbit_modify modify;
		struct orbit_operation operation;
		struct orbit_any any;
	};
	char small_data[__ORBIT_REPR_SMALL_DATA];
};

/*
 * Spawn an orbit.
 *
 * Underlying syscall: orbit_create, 0 args, return obid.
 * entry_func is stored in the orbit future handle loop.
 */
struct orbit *orbit_create(const char *module_name,
		orbit_entry entry_func, void*(*init_func)(void));

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
long orbit_call(struct orbit *ob,
		size_t narea, struct orbit_area** areas,
		orbit_entry func, void *arg, size_t argsize);

/*
 * Create an async orbit call.
 *
 * If the call succeeds and `future` is not NULL, future information will be stored in `future`.
 * Return 0 on success. Other value indicates failure.
 */
int orbit_call_async(struct orbit *ob, unsigned long flags,
		size_t narea, struct orbit_area** areas,
		orbit_entry func, void *arg, size_t argsize, struct orbit_future *future);

/** Cancel orbit future by taskid */
int orbit_cancel(struct orbit_future *future);

/**
 * Cancel orbit future by argument
 *
 * Kernel will byte-wise compare the args with the args in the queue.
 */
int orbit_cancel_by_arg(struct orbit *ob, void *arg, size_t argsize);

/* Page level granularity update */
unsigned long orbit_commit(void);

struct orbit_allocator_method;

/* User land runtime function that will be called by the kernel in orbit
 * context and will then call the real function. We do not really need....*/
// void obCallWrapper(orbit_entry entry_point, void *auxptr);

/* Create an return a memory allocation area of size 'init_pool_size'
 * to be used by an orbit.
 *
 * If argument <ob> is specified, the area will be created and mapped to the
 * address space of <ob>. If <ob> is NULL, the area will be created and mapped
 * later to a new orbit, which is a discouraged way.
*/
struct orbit_area *orbit_area_create(struct orbit *ob,
				     size_t init_pool_size,
				     const struct orbit_allocator_method *method);
struct orbit_area *orbit_area_create_at(struct orbit *ob,
					size_t init_pool_size, void *addr,
					const struct orbit_allocator_method *method);

// void obPoolDestroy(area);


/* ====== Allocator API ===== */

struct orbit_allocator;

typedef struct orbit_allocator *(*__orbit_allocator_create_f)
	(void *start, size_t size, void **data_start, size_t *data_length,
	 const struct orbit_allocator_method *method);

struct orbit_allocator_method {
	__orbit_allocator_create_f __create;
};

struct orbit_linear_allocator_method {
	struct orbit_allocator_method method;
	bool use_meta;
};

extern const struct orbit_allocator_method *orbit_linear_default;
extern const struct orbit_allocator_method *orbit_bitmap_default;

/* Get useful data range. This is used by orbit call or send. */
// void orbit_allocator_data_range(struct orbit_allocator *alloc, void **start, size_t *length);

#ifdef ORBIT_DEBUG

void *__orbit_alloc(struct orbit_area *area, size_t size,
			const char *file, int line);
static inline void *__orbit_calloc(struct orbit_area *area, size_t size,
			const char *file, int line)
{
	return memset(__orbit_alloc(area, size, file, line), 0, size);
}
#define orbit_alloc(area, size) \
	__orbit_alloc(area, size, __FILE__, __LINE__)
#define orbit_calloc(area, size) \
	__orbit_calloc(area, size, __FILE__, __LINE__)

#else

void *orbit_alloc(struct orbit_area *area, size_t size);
static inline void *orbit_calloc(struct orbit_area *area, size_t size)
{
	return memset(orbit_alloc(area, size), 0, size);
}

#endif

void orbit_free(struct orbit_area *area, void *ptr);
void *orbit_realloc(struct orbit_area *area, void *oldptr, size_t newsize);
/* #define orbit_allocated_by(ptr, area) \
	(area != NULL && (ptr >= area->start) && (ptr < area->start + area->length)) */

bool orbit_linear_allocator_reset(struct orbit_allocator *base);

/* ===== Update ADT ===== */

/*
 * Encoded array of orbit updates and operations.
 *
 * Update's backing memory is a orbit area. The updates is allocated in the area.
 *
 * This works like deque<any>.
 * User can push orbit_update ADT elements: update, operation, any to a orbit_update.
 * User can also iterate througt the elements.
 */
struct __orbit_block_list;
struct orbit_update {
	struct __orbit_block_list *updates;
	struct orbit_area area;
};

/* TODO: specialized query APIs */
struct orbit_result {
	unsigned long retval;
	struct orbit_update *update;
};

/*
 * Get a orbit_update scratch space.
 *
 * After each successful sendv(), the caller needs to call this again to
 * allocate a new scratch space.
 */
int orbit_update_create(struct orbit_update *s);
// void orbit_update_free(orbit_update *s);

int orbit_update_set_area(struct orbit_area *area);

/* Push orbit_operation to orbit_update */
int orbit_update_add_operation(struct orbit_update *s,
		orbit_operation_func func, void *arg, size_t size);
/* Push orbit_modify to orbit_update */
int orbit_update_add_modify(struct orbit_update *s, void *ptr, size_t length);
/* Push orbit_data to orbit_update.  Returns a space of size `length`.
 * If ptr is non-NULL, it will copy the data into that area.
 * If ptr is NULL, it returns `length` space from scratch to be filled by the caller. */
void *orbit_update_add_data(struct orbit_update *s, void *ptr, size_t length);

#if defined(__cplusplus) && __cplusplus >= 201103L

// TODO: rewrite two versions with better C macro and pure C++11 arg forward.
// We can also provide compiler support for structuring lambda s.t. it can
// written ergonomically and be sent safely.
#define orbit_update_run1(s, arg1t, arg1n, body) do { \
		unsigned long argv[] { (unsigned long)(arg1n), }; \
		unsigned long (*f)(size_t argc, unsigned long argv[]) = \
		[](size_t argc, unsigned long argv[]) -> unsigned long { \
			arg1t arg1n = (arg1t)argv[0]; \
			body; \
			return 0; \
		}; \
		orbit_update_push_operation(s, f, 1, argv); \
	} while (0)
#define orbit_update_run2(s, arg1t, arg1n, arg2t, arg2n, body) do { \
		unsigned long argv[] { (unsigned long)(arg1n), (unsigned long)(arg2n), }; \
		unsigned long (*f)(size_t argc, unsigned long argv[]) = \
		[](size_t argc, unsigned long argv[]) -> unsigned long { \
			arg1t arg1n = (arg1t)argv[0]; \
			arg2t arg2n = (arg2t)argv[1]; \
			body; \
			return 0; \
		}; \
		orbit_update_push_operation(s, f, 2, argv); \
	} while (0)
#define orbit_update_run3(s, arg1t, arg1n, arg2t, arg2n, arg3t, arg3n, body) do { \
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
		orbit_update_push_operation(s, f, 3, argv); \
	} while (0)

#endif /* C++11 */

/*
 * Receive in the main program.  Expect a return value or with update from
 * orbit_call.
 *
 * Returns 1 if update available, and modifies result->scratch & retval;
 * Returns 0 on end of updates, and modifies result->retval;
 * Returns -1 on error, and sets errno.
 */
int pull_orbit(struct orbit_result *result, struct orbit_future *future);

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

bool orbit_exists(struct orbit *ob);
bool orbit_gone(struct orbit *ob);

enum orbit_type orbit_apply(struct orbit_update *s, bool yield);
enum orbit_type orbit_apply_one(struct orbit_update *s, bool yield);
enum orbit_type orbit_skip(struct orbit_update *s, bool yield);
enum orbit_type orbit_skip_one(struct orbit_update *s, bool yield);

struct orbit_repr *orbit_update_first(struct orbit_update *s);
struct orbit_repr *orbit_update_next(struct orbit_update *s);
bool orbit_update_empty(struct orbit_update *s);
size_t orbit_update_size(struct orbit_update *s);

#ifdef __cplusplus
}

#if __cplusplus >= 201103L
#define NOEXCEPT noexcept
#else
#define NOEXCEPT throw()
#endif

namespace ob {

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
