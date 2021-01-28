#ifndef __ORBIT_H__
#define __ORBIT_H__

typedef (unsigned long)(*obEntry)(void*);

struct obModule {
	unsigned long obid;    // module id used by syscalls
	obEntry entry_func;
	// TODO
};

struct obPool {
	void *rawptr;
	size_t length;	// the pool should be page-aligned
	// bool raw;  // true to enable the following API, false to be used as raw memory segments.
	/* ... other metadata */
};


struct obModule *obCreate(const char *module_name UNUSED, obEntry entry_func);
// void obDestroy(obModule*);

// syscall: orbit_create, 0 args, return obid
// entry_point is stored 

// assume we now only share a single pool with snapshot
unsigned long obCall(struct obModule *module, struct obPool* pool, void *aux);

// syscall: orbit_call(int obid, entry_point, auxptr)

/* User land runtime function that will be called by the kernel in orbit
 * context and will then call the real function. We do not really need....*/
// void obCallWrapper(obEntry entry_point, void *auxptr);

/* Return a memory allocation pool. */
struct obPool *obPoolCreate(size_t init_pool_size, /* int raw = 0 */ );
// void obPoolDestroy(pool);

// syscall: orbit_pool_create(pages)
// in the kernel implementation, this will be almost the same function as
// `clone()`.

// ptr = pool.allocate(size);
// pool.deallocate(ptr);


#endif /* __ORBIT_H__ */
