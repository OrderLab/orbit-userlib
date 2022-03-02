#include "orbit.h"

/* Create an allocator */
struct orbit_allocator *orbit_linear_allocator_create(void *start, size_t length,
		void **data_start, size_t *data_length, bool use_meta);
