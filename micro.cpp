#include "orbit.h"
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

using namespace std::chrono;

#define N 1000000
//#define N 100

unsigned long checker_empty(void *obj) {
	return (unsigned long)obj;
}

void bench_empty() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_empty);
	assert(m != NULL);

	auto t1 = high_resolution_clock::now();
	for (int i = 0; i < N; ++i) {
		*obj = i;
		ret = obCall(m, pool, obj);
		// printf("In parent: ret is %d, obj is %d\n", ret, *obj);
	}
	auto t2 = high_resolution_clock::now();

	auto duration = duration_cast<nanoseconds>(t2 - t1).count();
	printf("checker call %d times takes %ld ns\n", N, duration);
}

unsigned long checker_empty_async(void *obj) {
	if (*(int*)obj == N) {
		struct obUpdate *update = (struct obUpdate*)(obj+sizeof(int));
		update->ptr = obj;
		update->length = sizeof(int);
		*(unsigned int*)update->data = 0xdeadbeef;
		obSendUpdate(update);
	}
	return (unsigned long)obj;
}

void bench_empty_async() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_empty_async);
	assert(m != NULL);

	auto t1 = high_resolution_clock::now();
	struct obTask task;
	struct obUpdate *update = (struct obUpdate*)malloc(sizeof(struct obUpdate) + ORBIT_BUFFER_MAX);
	for (int i = 0; i < N; ++i) {
		*obj = i + 1;
		ret = obCallAsync(m, pool, obj, &task);
		if (ret != 0) {
			printf("async obCall failed with %d\n", ret);
			break;
		}
		// printf("In parent: ret is %d, obj is %d\n", ret, *obj);
	}
	/* We do not wait on any former tasks, but instead reuse the last
	 * task. Since the tasks are handled in FIFO, the last task will
	 * send a sentinel update back (see checker_empty). */
	ret = obRecvUpdate(&task, update);
	auto t2 = high_resolution_clock::now();

	printf("ob recv returned %d, data = 0x%x\n", ret, *(unsigned int*)update->data);

	auto duration = duration_cast<nanoseconds>(t2 - t1).count();
	printf("checker call %d times takes %ld ns, %.2f ops\n", N, duration,
		(double)N / duration * 1000000000);
}

int main(int argc, char *argv[]) {
	char c;

	if (argc == 2 && !strcmp(argv[1], "-a"))
		bench_empty_async();
	else
		bench_empty();

	return 0;
	printf("type anything to exit...");
	scanf("%c", &c);
	return 0;
}
