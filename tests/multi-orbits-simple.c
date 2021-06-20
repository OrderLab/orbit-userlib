#include "orbit.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "acutest.h"

typedef struct {
	int arg1;
	int arg2;
} mult_args;

unsigned long test_mult_task_entry(void *args)
{
	mult_args *p = (mult_args *)args;
	printf("Received mult args (%d, %d) from main program\n", p->arg1,
	       p->arg2);
	return p->arg1 * p->arg2;
}

obid_t last_lobid = 0;

struct orbit_module * test_mult_(int no)
{
	struct orbit_module *mult_ob;
	pid_t main_pid = getpid();
	char name[32];
	snprintf(name, 32, "test_mult_%d", no);
	mult_ob = orbit_create(name, test_mult_task_entry);
	TEST_ASSERT(mult_ob != NULL);
	TEST_ASSERT(mult_ob->mpid == main_pid);
	// local obid should have the property of monotonic increasing
	TEST_ASSERT(mult_ob->lobid > last_lobid);
	TEST_ASSERT(mult_ob->gobid > 0);
	last_lobid = mult_ob->lobid;
	for (int i = 0; i < 3; i++) {
		int a = rand() % 100;
		int b = rand() % 100;
		mult_args args = { a, b };
		printf("Calling orbit task %d to multiply (%d, %d)\n",
		       mult_ob->lobid, a, b);
		long ret = orbit_call(mult_ob, 0, NULL, &args, sizeof(mult_args));
		printf("Received result from orbit task %d: %ld\n",
		       mult_ob->lobid, ret);
		TEST_CHECK(a * b == ret);
	}
	return mult_ob;
}

void test_mult()
{
	int N = 10;
	struct orbit_module **orbits;
	struct orbit_module *ob;

	orbits = (struct orbit_module **)malloc(N *
						sizeof(struct orbit_module *));
	for (int i = 1; i <= N; i++) {
		ob = test_mult_(i);
		TEST_ASSERT(ob != NULL);
		orbits[i - 1] = ob;
	}
	printf("Successfully created and tested %d orbit tasks\n", N);
	printf("Trying to destroy all orbits\n");

	TEST_CHECK(orbit_destroy_all() == 0);
	for (int i = 0; i < N; i++) {
		ob = orbits[i];
		if (TEST_CHECK(orbit_gone(ob)))
			printf("Destroyed orbit %d\n", ob->lobid);
		free(ob);
	}
	free(orbits);
}

TEST_LIST = {
    { "test_mult", test_mult },
    { NULL, NULL }
};


int main(int argc, char **argv)
{
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
