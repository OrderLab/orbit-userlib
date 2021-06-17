#include "orbit.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

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
	assert(addition_ob != NULL);
	assert(addition_ob->mpid == main_pid);
	assert(addition_ob->lobid > 0);
	assert(addition_ob->gobid > 0);
	for (int i = 0; i < 5; i++) {
		int a = rand() % 1000;
		int b = rand() % 1000;
		addition_args args = { a, b };
		printf("Calling orbit task to add (%d, %d)\n", a, b);
		long ret = orbit_call(addition_ob, 0, NULL, &args,
				      sizeof(addition_args));
		printf("Received result from orbit task=%ld\n", ret);
		assert(a + b == ret);
	}
}

int main()
{
	srand(time(NULL));
	test_addition();
}
