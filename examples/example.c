#include "orbit.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

unsigned long checker_plus_one_simple(void *argbuf) {
	/* Extract copied int from arg buffer */
	int obj = *(int*)argbuf;
	return obj + 1;
}

unsigned long checker_plus_one_pool(void *argbuf) {
	/* pointer to an int in the pool */
	int *obj = *(int**)argbuf;

	*obj += 1;
	return *obj;
}

unsigned long checker_plus_one_async(void *argbuf) {
	/* pointer to an int in the pool */
	int *obj = *(int**)argbuf;

	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + sizeof(int));

	update->ptr = obj;
	update->length = sizeof(int);
	*(int*)update->data = *obj + 1;

	orbit_send(update);

	free(update);
	return 0;
}

unsigned long checker_plus_one_commit(void *argbuf) {
	/* pointer to an int in the pool */
	int *obj = *(int**)argbuf;

	*obj += 1;
	orbit_commit();

	return *obj;
}

void test_plus_one_simple() {
	struct orbit_module *m;
	int obj, ret;

	m = orbit_create("test_module", checker_plus_one_simple);
	assert(m != NULL);

	obj = 200;

	ret = orbit_call(m, 0, NULL, &obj, sizeof(obj));
	printf("In parent: ret = %d\n", ret);

	assert(ret == 201);
}

void test_plus_one_pool() {
	struct orbit_pool *pool;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);

	obj = (int*)orbit_pool_alloc(pool, sizeof(int));
	/* Early implementation of orbit_create will still copy the whole
	 * address space.  Set an initial value for debug use. */
	*obj = 100;

	m = orbit_create("test_module", checker_plus_one_pool);
	assert(m != NULL);

	*obj = 200;

	ret = orbit_call(m, 1, &pool, &obj, sizeof(obj));
	printf("In parent: *obj is %d, ret = %d\n", *obj, ret);

	assert(*obj == 200 && ret == 201);
	orbit_pool_free(pool, obj, sizeof(*obj));
}

void test_multiple_plus_one() {
	struct orbit_pool *pool;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);

	obj = (int*)orbit_pool_alloc(pool, sizeof(int));
	*obj = 100;

	m = orbit_create("test_module", checker_plus_one_pool);
	assert(m != NULL);

	for (int i = 200; i <= 1000; i += 100) {
		*obj = i;
		ret = orbit_call(m, 1, &pool, &obj, sizeof(obj));
		printf("In parent: ret is %d, obj is %d\n", ret, *obj);
		assert(ret == i + 1 && *obj == i);
	}

	orbit_pool_free(pool, obj, sizeof(*obj));
}


void *recv_worker(void *_task) {
	struct orbit_task *task = _task;
	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + ORBIT_BUFFER_MAX);
	while (true) {
		long ret = orbit_recv(task, update);
		int err = errno;
		printf("In parent, we received one update (ret=%ld, errno=%s) %d\n",
			ret, strerror(err), *(int*)update->data);
		if (ret < 0)
			break;
		/* We can do some checks here to see if we want to really
		 * apply this update. */
		memcpy(update->ptr, update->data, update->length);
	}
	free(update);
	return NULL;
}

void test_plus_one_async(bool recv_thread) {
	struct orbit_pool *pool;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);

	obj = (int*)orbit_pool_alloc(pool, sizeof(int));
	*obj = 100;

	m = orbit_create("test_module", checker_plus_one_async);
	assert(m != NULL);

	*obj = 200;

	struct orbit_task task;
	ret = orbit_call_async(m, 0, 1, &pool, &obj, sizeof(obj), &task);
	assert(ret == 0);

	printf("In parent, we returned immediately, id=%ld\n", task.taskid);

	if (recv_thread) {
		pthread_t thd;
		pthread_create(&thd, NULL, recv_worker, &task);
		pthread_join(thd, NULL);
	} else {
		recv_worker(&task);
	}

	printf("In parent: *obj is %d\n", *obj);
	assert(*obj == 201);
	orbit_pool_free(pool, obj, sizeof(*obj));
}

void test_plus_one_async_commit() {
	struct orbit_pool *pool;
	struct orbit_module *m;
	int *obj, ret;

	pool = orbit_pool_create(4096);
	assert(pool != NULL);

	obj = (int*)orbit_pool_alloc(pool, sizeof(int));
	*obj = 100;

	m = orbit_create("test_module", checker_plus_one_commit);
	assert(m != NULL);

	for (int i = 200; i <= 1000; i += 100) {
		*obj = i;

		struct orbit_task task;
		ret = orbit_call_async(m, 0, 1, &pool, &obj, sizeof(obj), &task);
		assert(ret == 0);

		printf("In parent, we returned immediately id=%ld\n", task.taskid);

		sleep(1);

		printf("Parent slept for 1 second, get *obj = %d\n", *obj);
		assert(*obj == i + 1);
	}

	orbit_pool_free(pool, obj, sizeof(*obj));
}


unsigned long checker_fail(void *ptr) {
	(void)ptr;
	return *(volatile int*)(NULL);
}

void test_fail() {
	struct orbit_module *m;
	int ret;

	m = orbit_create("test_module", checker_fail);
	assert(m != NULL);

	ret = orbit_call(m, 0, NULL, NULL, 0);
	printf("In parent: ret is %d\n", ret);
}


int main(int argc, char *argv[]) {
	char c;
	char **arg = argv + 1;
	bool wait = false;

	for (arg = argv + 1; *arg && **arg == '-'; ++arg) {
		if (!strcmp(*arg, "-l")) {
			printf("Available options: simple, pool, multi, "
					"async [bool], commit, fail\n");
			return 0;
		} else if (!strcmp(*arg, "-w")) {
			wait = true;
		} else {
			fprintf(stderr, "Unknown option %s\n", *arg);
			return 1;
		}
	}

	if (*arg) {
		if (!strcmp(*arg, "simple"))
			test_plus_one_simple();
		else if (!strcmp(*arg, "pool"))
			test_plus_one_pool();
		else if (!strcmp(*arg, "multi"))
			test_multiple_plus_one();
		else if (!strcmp(*arg, "async")) {
			bool thd = false;
			if (*(arg + 1))
				thd = !!atoi(*(arg + 1));
			test_plus_one_async(thd);
		} else if (!strcmp(*arg, "commit"))
			test_plus_one_async_commit();
		else if (!strcmp(*arg, "fail"))
			test_fail();
		else {
			fprintf(stderr, "Unknown option '%s'\n", *arg);
			return 1;
		}
	} else {
		test_plus_one_simple();
	}

	if (wait) {
		printf("type anything to exit...");
		c = scanf("%c", &c);	/* `c=' to silence warning */
	}
}
