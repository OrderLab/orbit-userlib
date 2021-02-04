#ifndef __ORBIT_H__
#define __ORBIT_H__

#include <stddef.h>

#define OB_BLOCK (4096*2)

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long(*obEntry)(void*);

struct obModule;

struct obPool;

// typedef int(*obCallback)(struct obUpdate*);

struct obTask;

#define ORBIT_BUFFER_MAX 1024	/* Maximum buffer size of orbit_update data field */

/* Information of how and what to update, received from chcecker */
struct obUpdate;

struct obModule *obCreate(const char *module_name /* UNUSED */, obEntry entry_func);
// void obDestroy(obModule*);

// syscall: orbit_create, 0 args, return obid
// entry_point is stored 

// assume we now only share a single pool with snapshot
unsigned long obCall(struct obModule *module, struct obPool* pool, void *aux);

/*
 * Create an async orbit call.
 * If the call succeeds and `task` is not NULL, task information will be stored in `task`.
 * Return 0 on success. Other value indicates failure.
 */
int obCallAsync(struct obModule *module, struct obPool* pool, void *aux, struct obTask *task);

// syscall: orbit_call(int obid, entry_point, auxptr)

// Functions to send updates in the checker and receive in the main program.
unsigned long obSendUpdate(const struct obUpdate *update);
unsigned long obRecvUpdate(struct obTask *task, struct obUpdate *update);

/* User land runtime function that will be called by the kernel in orbit
 * context and will then call the real function. We do not really need....*/
// void obCallWrapper(obEntry entry_point, void *auxptr);

/* Return a memory allocation pool. */
struct obPool *obPoolCreate(size_t init_pool_size /*, int raw = 0 */ );
// void obPoolDestroy(pool);

void *obPoolAllocate(struct obPool *pool, size_t size);
void obPoolDeallocate(struct obPool *pool, void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __ORBIT_H__ */
