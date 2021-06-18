#include "orbit.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "acutest.h"

typedef struct {
	int arg1;
	int arg2;
} addition_args;

unsigned long test_addition_task_entry(void *args)
{
	addition_args *p = (addition_args *)args;
	printf("Received addition args (%d, %d) from main program\n", p->arg1,
	       p->arg2);
	return p->arg1 + p->arg2;
}

void test_addition()
{
	struct orbit_module *addition_ob;
	pid_t main_pid = getpid();
	addition_ob = orbit_create("test_addition", test_addition_task_entry);
	TEST_ASSERT(addition_ob != NULL);
	TEST_ASSERT(addition_ob->mpid == main_pid);
	TEST_ASSERT(addition_ob->lobid > 0);
	TEST_ASSERT(addition_ob->gobid > 0);
	for (int i = 0; i < 5; i++) {
		int a = rand() % 1000;
		int b = rand() % 1000;
		addition_args args = { a, b };
		printf("Calling orbit task to add (%d, %d)\n", a, b);
		long ret = orbit_call(addition_ob, 0, NULL, &args,
				      sizeof(addition_args));
		printf("Received result from orbit task=%ld\n", ret);
		TEST_CHECK(a + b == ret);
	}
}

TEST_LIST = {
    { "addition", test_addition },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	acutest_execute_main(argc, argv);
}
