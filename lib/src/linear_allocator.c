#include "orbit.h"
#include "orbit_lowlevel.h"
#include "linear_allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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
struct orbit_linear_allocator {
	struct orbit_allocator base;	/* Allocator base struct, includes vtable */
	void *start;		/* Underlying memory region */
	size_t length;
	size_t *allocated;	/* External pointer to allocated size */
	pthread_spinlock_t lock;	/* alloc needs to be thread-safe */
	bool use_meta;
};

struct alloc_meta {
	size_t size;
};

extern struct orbit_allocator_vtable orbit_linear_allocator_vtable;

struct orbit_allocator *orbit_linear_allocator_create(void *start, size_t length,
		void **data_start, size_t *data_length,
		const struct orbit_allocator_method *method)
{
	int ret;
	struct orbit_linear_allocator* alloc;
	const struct orbit_linear_allocator_method *options = (void*)method;

	if (!data_length)
		return NULL;

	alloc = (struct orbit_linear_allocator*)malloc(sizeof(struct orbit_linear_allocator));
	if (alloc == NULL) return NULL;

	ret = pthread_spin_init(&alloc->lock, PTHREAD_PROCESS_PRIVATE);
	if (ret != 0) goto lock_init_fail;

	alloc->base.vtable = &orbit_linear_allocator_vtable;
	alloc->start = start;
	alloc->length = length;
	alloc->allocated = data_length;
	alloc->use_meta = options->use_meta;

	if (data_start)
		*data_start = start;

	return (struct orbit_allocator*)alloc;

lock_init_fail:
	free(alloc);
	return NULL;
}

void orbit_linear_allocator_destroy(struct orbit_allocator *base)
{
	struct orbit_linear_allocator *alloc = (struct orbit_linear_allocator*)base;
	pthread_spin_destroy(&alloc->lock);
	memset(alloc, 0, sizeof(*alloc));
	free(alloc);
}

void *orbit_linear_alloc(struct orbit_allocator *base, size_t size)
{
	struct orbit_linear_allocator *alloc = (struct orbit_linear_allocator*)base;
	void *ptr;
	int ret;

	if (alloc->use_meta)
		size += sizeof(struct alloc_meta);

	ret = pthread_spin_lock(&alloc->lock);
	if (ret != 0) return NULL;

	if (size > alloc->length - *alloc->allocated) {
		fprintf(stderr, "Pool %p is full.\n", alloc);
		abort();
		return NULL;
	}

	ptr = (char*)alloc->start + *alloc->allocated;

	*alloc->allocated += size;

	pthread_spin_unlock(&alloc->lock);

	if (alloc->use_meta)
		*(struct alloc_meta*)ptr = (struct alloc_meta) {
			.size = size - sizeof(struct alloc_meta),
		};

	return (struct alloc_meta*)ptr + 1;
}

void orbit_linear_free(struct orbit_allocator *base, void *ptr)
{
	/* Let it leak. */
	(void)base;
	(void)ptr;
}

void *orbit_linear_realloc(struct orbit_allocator *base, void *oldptr, size_t newsize)
{
	struct orbit_linear_allocator *alloc = (struct orbit_linear_allocator*)base;
	void *mem;
	struct alloc_meta *meta;

	if (!oldptr || !alloc->use_meta)
		return orbit_linear_alloc(base, newsize);

	meta = (struct alloc_meta*)oldptr - 1;
	if (meta->size >= newsize) {
		meta->size = newsize;
		return oldptr;
	}

	mem = orbit_linear_alloc(base, newsize);
	memcpy(mem, oldptr, meta->size);
	orbit_linear_free(base, oldptr);
	return mem;
}

bool orbit_linear_allocator_reset(struct orbit_allocator *base)
{
	struct orbit_linear_allocator *alloc;

	if (base->vtable != &orbit_linear_allocator_vtable) {
		fprintf(stderr, "liborbit: not a valid linear allocator!");
		return false;
	}

	alloc = (struct orbit_linear_allocator*)base;
	*alloc->allocated = 0;

	return true;
}

struct orbit_allocator_vtable orbit_linear_allocator_vtable = {
	.alloc = orbit_linear_alloc,
	.free = orbit_linear_free,
	.realloc = orbit_linear_realloc,
	.destroy = orbit_linear_allocator_destroy,
};

static const struct orbit_linear_allocator_method orbit_linear_default_value = {
	.method = (struct orbit_allocator_method) {
		.__create = orbit_linear_allocator_create,
	},
	.use_meta = true,
};
const struct orbit_allocator_method *orbit_linear_default =
		(struct orbit_allocator_method*) &orbit_linear_default_value;
