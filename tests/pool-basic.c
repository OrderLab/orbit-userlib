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

unsigned long pool_add_task_entry(void *args)
{
	add_args *p = (add_args *)args;
	printf("Received addition args (%d, %d) from main program\n", p->arg1,
	       p->arg2);
	return p->arg1 + p->arg2;
}

unsigned long pool_pointer_task_entry(void *args)
{
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
	struct orbit_module *add_ob;
	add_args *args;
	long ret;

	pool = orbit_pool_create(4096);
	TEST_ASSERT(pool != NULL);

	add_ob = orbit_create("test_pool_add", pool_add_task_entry);
	TEST_ASSERT(add_ob != NULL);
	TEST_ASSERT(add_ob->gobid > 0);
	for (int i = 1; i <= 5; i++) {
		int a = i * 101;
		int b = i * i * 11;
		args = (add_args*)orbit_pool_alloc(pool, sizeof(add_args));
		args->arg1 = a;
		args->arg2 = b;
		ret = orbit_call(add_ob, 1, &pool, args, sizeof(add_args));
		printf("Calling orbit task to add (%d, %d)\n", a, b);
		printf("Received result from orbit task=%ld\n", ret);
		TEST_CHECK(a + b == ret);
	}
}

void test_pool_pointer() {
	struct orbit_pool *pool;
	struct orbit_module *ptr_ob;
	pointer_args *args;
	long ret, sum;

	pool = orbit_pool_create(4096);
	TEST_ASSERT(pool != NULL);

	ptr_ob = orbit_create("test_pool_pointer", pool_pointer_task_entry);
	TEST_ASSERT(ptr_ob != NULL);
	TEST_ASSERT(ptr_ob->gobid > 0);

	for (int i = 1; i <= 3; i++) {
		args = (pointer_args *)orbit_pool_alloc(pool,
							sizeof(pointer_args));
		args->size = i * 5;
		args->buffer =
			(int *)orbit_pool_alloc(pool, args->size * sizeof(int));
		sum = 0;
		for (int j = 0; j < args->size; j++) {
			args->buffer[j] = rand() % 100;
			sum += args->buffer[j];
		}
		ret = orbit_call(ptr_ob, 1, &pool, &args, sizeof(pointer_args *));
		printf("Calling orbit task to sum a buffer {");
		for (int j = 0; j < args->size; j++) {
			printf("%d", args->buffer[j]);
			if (j != args->size - 1) printf(", ");
		}
		printf("}\n");
		printf("Received result from orbit task=%ld\n", ret);
		TEST_CHECK(sum == ret);
	}

}

TEST_LIST = {
    { "pool_add", test_pool_add },
    { "pool_pointer", test_pool_pointer },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
