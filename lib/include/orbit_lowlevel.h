#ifndef __ORBIT_LOWLEVEL_H__
#define __ORBIT_LOWLEVEL_H__

#include "orbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Send an update during orbit_call to main program.
 *
 * Return 0 on success, otherwise -1 and sets errno.
 *
 * Note: After success send, the area will not be destroyed.
 * If the send fails, this area is still accessible, and the caller can
 * optionally update it and resend.
 * If the caller decides not to send it, the caller needs to call create()
 * again to request a new scratch space.
 */
int orbit_push(struct orbit_result *result);

/* Page level granularity update */
unsigned long orbit_commit(void);

/*
 * Set global area to use to create orbit_update.
 *
 * Typically we use orbit area mode "MOVE" for performance reason.
 *
 * This needs to be called before creating a scratch.
 */
int orbit_update_set_area(struct orbit_area *area);


struct orbit_allocator_vtable {
	void*(*alloc)(struct orbit_allocator *alloc, size_t size);
	void(*free)(struct orbit_allocator *alloc, void *ptr);
	void*(*realloc)(struct orbit_allocator *alloc, void *oldptr, size_t newsize);
	// void(*data_range)(struct orbit_allocator *alloc, void **start, size_t *length);
	void(*destroy)(struct orbit_allocator *alloc);
};

struct orbit_allocator {
	const struct orbit_allocator_vtable *vtable;
};

/* Create an allocator using underlying area */
struct orbit_allocator *orbit_allocator_from_area(struct orbit_area *area,
		const struct orbit_allocator_method *method);

/* Destroy an allocator */
void orbit_allocator_destroy(struct orbit_allocator *alloc);


#ifdef ORBIT_DBEUG

void *__orbit_allocator_alloc(struct orbit_allocator *alloc, size_t size,
			const char *file, int line);

static inline void *__orbit_allocator_calloc(struct orbit_allocator *alloc,
		size_t size, const char *file, int line)
{
	return memset(__orbit_allocator_alloc(alloc, size, file, line), 0, size);
}

#define orbit_allocator_alloc(alloc, size) \
	__orbit_allocator_alloc(alloc, size, __FILE__, __LINE__)
#define orbit_allocator_calloc(alloc, size) \
	__orbit_allocator_calloc(alloc, size, __FILE__, __LINE__)

#else

void *orbit_allocator_alloc(struct orbit_allocator *alloc, size_t size);

static inline void *orbit_allocator_calloc(struct orbit_allocator *alloc, size_t size)
{
	return memset(orbit_allocator_alloc(alloc, size), 0, size);
}

#endif

void orbit_allocator_free(struct orbit_allocator *alloc, void *ptr);
void *orbit_allocator_realloc(struct orbit_allocator *alloc, void *oldptr, size_t newsize);
/* #define orbit_allocator_allocated_by(ptr, alloc) \
	(alloc != NULL && (ptr >= alloc->start) && (ptr < alloc->start + alloc->length)) */

#ifdef __cplusplus
}
#endif

#endif /* __ORBIT_LOWLEVEL_H__ */
