/* Orbit microbenchmark that snapshots one page and triggers one page fault
 * in the main program for every iteration. */

#include "orbit.h"
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

using namespace std::chrono;

int N = 1000000;

unsigned long checker_empty(void *store, void *argbuf) {
	(void)store;
	/* pointer to an int in the pool */
	int *obj = *(int**)argbuf;
	return (unsigned int)*obj;
}

void bench_empty() {
	struct orbit_pool *pool;
	struct orbit_allocator *alloc;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);
	alloc = orbit_allocator_from_pool(pool, false);
	assert(alloc != NULL);

	obj = (int*)orbit_alloc(alloc, sizeof(int));
	*obj = 100;

	m = orbit_create("test_module", checker_empty, NULL);
	assert(m != NULL);

	auto t1 = high_resolution_clock::now();
	for (int i = 0; i < N; ++i) {
		*obj = i;
		ret = orbit_call(m, 1, &pool, NULL, &obj, sizeof(obj));
		if (unlikely(ret != i)) {
			printf("In parent: i=%d, ret=%d, obj=%d\n", i, ret, *obj);
			abort();
		}
	}
	auto t2 = high_resolution_clock::now();

	long long duration = duration_cast<nanoseconds>(t2 - t1).count();
	printf("checker call %d times takes %lld ns, %.2f ops\n", N, duration,
		(double)N / duration * 1000000000LL);
}

void bench_empty_async() {
	struct orbit_pool *pool;
	struct orbit_allocator *alloc;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);
	alloc = orbit_allocator_from_pool(pool, false);
	assert(alloc != NULL);

	obj = (int*)orbit_alloc(alloc, sizeof(int));
	*obj = 100;

	m = orbit_create("test_module", checker_empty, NULL);
	assert(m != NULL);

	auto t1 = high_resolution_clock::now();
	for (int i = 0; i < N - 1; ++i) {
		*obj = i;
		ret = orbit_call_async(m, ORBIT_NORETVAL, 1, &pool,
				NULL, &obj, sizeof(obj), NULL);
		if (unlikely(ret != 0)) {
			printf("In parent: i=%d ret=%d, obj=0x%x\n", i, ret, *obj);
			abort();
		}
	}

	*obj = 0xdeadbeef;
	struct orbit_task task;
	ret = orbit_call_async(m, 0, 1, &pool, NULL, &obj, sizeof(obj), &task);
	assert(ret == 0);

	/* We do not wait on any former tasks, but only the last task.
	 * Since the tasks are handled in FIFO, the last task will
	 * send a sentinel retval back (see checker_empty). */
	union orbit_result result;
	ret = orbit_recvv(&result, &task);

	auto t2 = high_resolution_clock::now();

	long long duration = duration_cast<nanoseconds>(t2 - t1).count();
	printf("checker call %d times takes %lld ns, %.2f ops\n", N, duration,
		(double)N / duration * 1000000000LL);

	printf("ob recvv returned %d, data = %lu\n", ret, result.retval);

	assert(ret == 0);
	assert(result.retval == 0xdeadbeef);
}

int main(int argc, char *argv[]) {
	char **arg = argv + 1;
	bool async = false;

	if (*arg && !strcmp(*arg, "-a")) {
		async = true;
		++arg;
	}
	if (*arg) {
		if (sscanf(*arg, "%d\n", &N) != 1) {
			fprintf(stderr, "Usage: %s [-a]\n", argv[0]);
			return 1;
		}
		if (N < 0)
			N = 100;
	}

	printf("Benchmark with N = %d in %s mode\n", N, async ? "async" : "sync");

	if (async)
		bench_empty_async();
	else
		bench_empty();
	return 0;
}
