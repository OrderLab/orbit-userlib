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

unsigned long task_entry1(void *args)
{
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
	struct orbit_module *m = orbit_create("async_update", task_entry1);
	struct orbit_pool *pool = orbit_pool_create(4096 * 16);
	struct task1_args *args = (struct task1_args *)orbit_pool_alloc(
		pool, sizeof(struct task1_args));
	struct orbit_task task;
	uint64_t received = 0, expected = 0;
	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + 32);

	args->size = 100;
	for (int i = 0; i < args->size; i++) {
		args->data[i] = rand() % 1000;
		args->data[i] = i * i * args->data[i];
		expected += args->data[i];
	}

	int ret = orbit_call_async(m, 0, 1, &pool, args,
				   sizeof(struct task1_args *), &task);
	TEST_ASSERT(ret == 0);
	do_work();

	ret = orbit_recv(&task, update);
	TEST_ASSERT(ret == 0);
	TEST_CHECK(update->ptr == NULL);
	TEST_CHECK(update->length == 8);
	received = *((uint64_t *)update->data);
	TEST_CHECK(received == expected);
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
