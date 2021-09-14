#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "acutest.h"

struct task1_args {
	int size;
	int data[128];
};

unsigned long task_entry1(void *store, void *args)
{
	(void)store;
	struct task1_args *real_args = (struct task1_args *) args;
	printf("Orbit received %d items from main program\n", real_args->size);
	uint64_t result = 0;
	for (int i = 0; i < real_args->size; ++i) {
		result += real_args->data[i];
	}
	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + 32);
	update->ptr = NULL;
	update->length = sizeof(uint64_t);
	*(uint64_t *)update->data = result;
	printf("Sending update %lu\n", result);
	TEST_ASSERT(orbit_send(update) == 0);
	return 0;
}

void do_work()
{
	usleep(10000);
	printf("Done with work\n");
}

void test_async_update()
{
	struct orbit_pool *pool;
	struct orbit_allocator *alloc;
	struct orbit_module *m;
	struct task1_args *args;
	struct orbit_task task;
	uint64_t received = 0, expected = 0;
	struct orbit_update *update;

	m = orbit_create("async_update", task_entry1, NULL);
	pool = orbit_pool_create(m, 4096 * 16);
	printf("created pool addr at %p\n", pool->rawptr);
	alloc = orbit_allocator_from_pool(pool, false);
	args = (struct task1_args *) orbit_alloc(
		alloc, sizeof(struct task1_args));
	update = (struct orbit_update*) malloc(sizeof(struct orbit_update) + 32);

	args->size = 100;
	for (int i = 0; i < args->size; i++) {
		args->data[i] = rand() % 10000;
		args->data[i] = i * (i + 1) * args->data[i];
		expected += args->data[i];
	}

	int ret = orbit_call_async(m, 0, 1, &pool, NULL, args,
				   sizeof(struct task1_args), &task);
	TEST_ASSERT(ret == 0);
	do_work();

	ret = orbit_recv(&task, update);
	TEST_ASSERT(ret == 0);
	TEST_CHECK(update->ptr == NULL);
	TEST_CHECK(update->length == 8);
	received = *((uint64_t *)update->data);
	if (!TEST_CHECK(received == expected))
		TEST_MSG("Expected :%lu; Received: %lu\n", expected, received);

	ret = orbit_destroy(m->gobid);
	TEST_ASSERT(ret == 0);
}


TEST_LIST = {
    { "async_update", test_async_update },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
