#include "orbit.h"

#include <limits>

namespace orbit {

orbit_allocator *__global_allocator;

void set_global_allocator(orbit_allocator *alloc) {
	__global_allocator = alloc;
}

void *__orbit_allocate_wrapper(orbit_allocator *alloc, std::size_t n, std::size_t type_size) {
	if (n > std::numeric_limits<std::size_t>::max() / type_size)
		throw std::bad_array_new_length();
	if (void *p = orbit_alloc(alloc, n * type_size))
		return p;
	throw std::bad_alloc();
}

void __orbit_deallocate_wrapper(orbit_allocator *alloc, void *ptr, std::size_t n) noexcept {
	(void)n;
	orbit_free(alloc, ptr);
}


void* global_new_operator::operator new(std::size_t size) {
	void *ptr = orbit_alloc(__global_allocator, size);
	if (ptr == nullptr) throw std::bad_alloc();
	return ptr;
}

void global_new_operator::operator delete(void *ptr) noexcept {
	orbit_free(__global_allocator, ptr);
}

}  // namespace orbit
