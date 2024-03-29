#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include "acutest.h"

typedef struct {
	int arg1;
	int arg2;
} add_args;

typedef struct {
	int size;
	int *buffer;
} pointer_args;

unsigned long pool_add_task_entry(void *store, void *args)
{
	(void)store;
	add_args *p = (add_args *)args;
	printf("Received addition args (%d, %d) from main program\n", p->arg1,
	       p->arg2);
	return p->arg1 + p->arg2;
}

unsigned long pool_pointer_task_entry(void *store, void *args)
{
	(void)store;
	// assuming a pointer to the pointer_args is passed
	pointer_args *p = *(pointer_args **)args;
	printf("Received pointer_args %p from main program\n", p);
	printf("Computing the sum of the %d elements in the buffer\n", p->size);
	int sum = 0;
	for (int i = 0; i < p->size; ++i)
		sum += p->buffer[i];
	return sum;
}

void test_pool_add() {
	struct orbit_pool *pool;
	struct orbit_allocator *alloc;
	struct orbit_module *add_ob;
	add_args *args;
	long ret;

	add_ob = orbit_create("test_pool_add", pool_add_task_entry, NULL);
	TEST_ASSERT(add_ob != NULL);
	TEST_ASSERT(add_ob->gobid > 0);

	pool = orbit_pool_create(add_ob, 4096);
	TEST_ASSERT(pool != NULL);
	alloc = orbit_allocator_from_pool(pool, false);
	TEST_ASSERT(alloc != NULL);

	for (int i = 1; i <= 5; i++) {
		int a = i * 101;
		int b = i * i * 11;
		args = (add_args*)orbit_alloc(alloc, sizeof(add_args));
		args->arg1 = a;
		args->arg2 = b;
		ret = orbit_call(add_ob, 1, &pool, NULL, args, sizeof(add_args));
		printf("Calling orbit task to add (%d, %d)\n", a, b);
		printf("Received result from orbit task=%ld\n", ret);
		TEST_CHECK(a + b == ret);
	}
	TEST_CHECK(orbit_exists(add_ob));
	TEST_CHECK(orbit_destroy(add_ob->gobid) == 0);
	usleep(1000);
	if (TEST_CHECK(orbit_gone(add_ob)))
		printf("Destroyed add_ob orbit %d\n", add_ob->gobid);
	free(add_ob);
}

void test_pool_pointer() {
	struct orbit_pool *pool;
	struct orbit_allocator *alloc;
	struct orbit_module *ptr_ob;
	pointer_args *args;
	long ret, sum;

	ptr_ob = orbit_create("test_pool_pointer", pool_pointer_task_entry, NULL);
	TEST_ASSERT(ptr_ob != NULL);
	TEST_ASSERT(ptr_ob->gobid > 0);

	pool = orbit_pool_create(ptr_ob, 4096);
	TEST_ASSERT(pool != NULL);
	alloc = orbit_allocator_from_pool(pool, false);
	TEST_ASSERT(alloc != NULL);

	for (int i = 1; i <= 3; i++) {
		args = (pointer_args *)orbit_alloc(alloc, sizeof(pointer_args));
		args->size = i * 5;
		args->buffer =
			(int *)orbit_alloc(alloc, args->size * sizeof(int));
		sum = 0;
		for (int j = 0; j < args->size; j++) {
			args->buffer[j] = rand() % 100;
			sum += args->buffer[j];
		}
		ret = orbit_call(ptr_ob, 1, &pool, NULL, &args, sizeof(pointer_args *));
		printf("Calling orbit task to sum a buffer {");
		for (int j = 0; j < args->size; j++) {
			printf("%d", args->buffer[j]);
			if (j != args->size - 1) printf(", ");
		}
		printf("}\n");
		printf("Received result from orbit task=%ld\n", ret);
		TEST_CHECK(sum == ret);
	}
	TEST_CHECK(orbit_exists(ptr_ob));
	usleep(1000);
	// try testing orbit_destroy_all without specifying the gobid
	TEST_CHECK(orbit_destroy_all() == 0);
	if (TEST_CHECK(orbit_gone(ptr_ob)))
		printf("Destroyed ptr_ob orbit %d\n", ptr_ob->gobid);
	free(ptr_ob);
}

TEST_LIST = {
    { "pool_add", test_pool_add },
    { "pool_pointer", test_pool_pointer },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
