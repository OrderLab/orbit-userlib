#include "orbit.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

unsigned long checker_plus_one(void *obj) {
	// int val = *(int*)obj;
	// return val + 1;
	*(int*)obj += 1;
	return *(int*)obj;
}

unsigned long checker_plus_one_async(void *obj) {
	// char buffer[sizeof(struct obUpdate) + 4];
	//struct obUpdate *update = (struct obUpdate*)malloc(sizeof(struct obUpdate) + ORBIT_BUFFER_MAX);
	//struct obUpdate *update = (struct obUpdate*)buffer;
	struct obUpdate *update = (struct obUpdate*)(obj+4);
	// FIXME: currently we cannot use stack because it accesses fs register.
	//printf("In child, allocated struct update\n");

#if 1
	update->ptr = obj;
	update->length = sizeof(int);
	*(int*)update->data = *(int*)obj + 1;

	//sleep(1);

	//printf("In child, we will now send the update.\n");
	obSendUpdate(update);
#endif

	return 0;
}

void test_plus_one() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_plus_one);
	assert(m != NULL);

	*obj = 200;

	ret = obCall(m, pool, obj);
	printf("In parent: obj is %d\n", ret);
}

void *recv_worker(void *_task) {
	struct obTask *task = _task;
	struct obUpdate *update = (struct obUpdate*)malloc(sizeof(struct obUpdate) + ORBIT_BUFFER_MAX);
	while (1) {
		long ret = obRecvUpdate(task, update);
		printf("In parent, we received one update (ret=%ld, errno=%s) %d\n",
			ret, strerror(errno), *(int*)update->data);
		if (ret < 0)
			break;
		/* We can do some checks here to see if we want to really
		 * apply this update. */
		memcpy(update->ptr, update->data, update->length);
	}
	return NULL;
}

void test_plus_one_async(int recv_thread) {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_plus_one_async);
	assert(m != NULL);

	*obj = 200;

	struct obTask *task = obCallAsync(m, pool, obj);

	printf("In parent, we returned immediately %p, %ld\n", task, task->taskid);

	if (recv_thread) {
		pthread_t thd;
		pthread_create(&thd, NULL, recv_worker, task);
		pthread_join(thd, NULL);
	} else {
		recv_worker(task);
	}
}

void test_multiple_plus_one() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_plus_one);
	assert(m != NULL);

	for (int i = 200; i <= 1000; i += 100) {
		*obj = i;
		ret = obCall(m, pool, obj);
		printf("In parent: ret is %d, obj is %d\n", ret, *obj);
	}
}


unsigned long checker_fail(void *ptr) {
	return *(int*)(NULL);
}

void test_fail() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;

	m = obCreate("test_module", checker_fail);
	assert(m != NULL);

	ret = obCall(m, pool, obj);
	printf("In parent: obj is %d\n", ret);
}


int main() {
	char c;

	//test_plus_one();
	test_plus_one_async(1);
	//test_multiple_plus_one();
	// test_fail();

	printf("type anything to exit...");
	scanf("%c", &c);
}
