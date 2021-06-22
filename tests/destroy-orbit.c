#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include "acutest.h"

pid_t main_pid;

unsigned long test_task_entry(void *args)
{
	(void) args;
	long sum = 0;
	for (int i = 0; i < 1000; i++) {
		if (i % 100 == 0)
			sleep(1);
		sum += i;
	}
	return sum;
}

struct orbit_module * create_orbit_checked()
{
	struct orbit_module *ob;
	int ret, dummy;
	struct orbit_task task;

	ob = orbit_create("test_destroy", test_task_entry);
	TEST_ASSERT(ob != NULL);
	if (!TEST_CHECK(ob->mpid == main_pid)) {
		// assuming main PID mismatch is non-fatal
		TEST_MSG("Expected main PID: %d, got main PID: %d", main_pid,
			 ob->mpid);
	}
	TEST_ASSERT(ob->lobid > 0);
	TEST_ASSERT(ob->gobid > 0);

	// Launching an async task that lasts for a while
	dummy = 100;
	ret = orbit_call_async(ob, 0, 0, NULL, &dummy, sizeof(int), &task);
	TEST_CHECK(ret == 0);
	printf("Async orbit call returns task id %ld\n", task.taskid);

	return ob;
}

bool destroy_orbit_checked(struct orbit_module *ob)
{
	int ret;

	TEST_CHECK(orbit_exists(ob));
	printf("Orbit exists with GOBID %d, ready to destroy\n", ob->gobid);

	ret = orbit_destroy(ob->gobid);
	TEST_CHECK(ret == 0);

	// destroy can be a bit laggy under high load and making the test flaky.
	usleep(1000);

	// check if the gobid exists again
	if (TEST_CHECK(orbit_gone(ob)))
		printf("Orbit %d is successfully destroyed!\n", ob->gobid);
	return true;
}

void test_destroy_single()
{
	struct orbit_module *ob;

	ob = create_orbit_checked();
	TEST_ASSERT(ob != NULL);

	destroy_orbit_checked(ob);
	free(ob);
}

void do_work()
{
	int i, cnt, N;
	double x, y;

	N = 1000;
	cnt = 0;

	for (i = 0; i < N; i++) {
		x = (double) rand() / RAND_MAX;
		y = (double) rand() / RAND_MAX;
		if (x * x + y * y <= 1)
			cnt++;
	}
	printf("Work result is %g\n", (double) cnt / N * 4);
}

void test_destroy_multi()
{
	int N = 10;
	struct orbit_module *ob;
	struct orbit_module **orbits;
	bool ok;

	orbits = (struct orbit_module **)malloc(
		N * sizeof(struct orbit_module *));

	printf("Creating %d orbits\n", N);
	// Create N orbits one by one
	for (int i = 0; i < N; ++i) {
		ob = create_orbit_checked();
		TEST_ASSERT(ob != NULL);
		orbits[i] = ob;
		printf("- orbit %d created\n", ob->lobid);
	}

	// Do some work
	do_work();

	printf("Destroying %d orbits\n", N);
	// Destroy the N orbits one by one.
	for (int i = 0; i < N; ++i) {
		ob = orbits[i];
		ok = destroy_orbit_checked(ob);
		TEST_ASSERT(ok);
		printf("- orbit %d destroyed\n", ob->lobid);
		free(ob);
		orbits[i] = NULL;
	}

	free(orbits);
}

void test_destroy_all()
{
	int N = 5;
	struct orbit_module *ob;
	struct orbit_module **orbits;

	orbits = (struct orbit_module **)malloc(
		N * sizeof(struct orbit_module *));

	printf("Creating %d orbits\n", N);

	// Create N orbits one by one
	for (int i = 0; i < N; ++i) {
		ob = create_orbit_checked();
		TEST_ASSERT(ob != NULL);
		TEST_CHECK(orbit_exists(ob));
		printf("- orbit %d created\n", ob->lobid);
		orbits[i] = ob;
	}

	// Do some work
	do_work();

	// Destroy them all at once
	orbit_destroy_all();

	for (int i = 0; i < N; ++i) {
		ob = orbits[i];
		TEST_CHECK(orbit_gone(ob));
		printf("- orbit %d destroyed\n", ob->lobid);
		free(ob);
	}
	free(orbits);
}

TEST_LIST = {
    { "destroy_single", test_destroy_single },
    { "destroy_multiple", test_destroy_multi },
    { "destroy_all", test_destroy_all },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	main_pid = getpid();
	srand(time(NULL));
	printf("Main program PID %d launched\n", main_pid);

	// setting no_exec is needed, otherwise acutest run
	// the tests in child processes (fork)
	acutest_no_exec_ = 1;
	// acutest_verbose_level_ = 3;
	return acutest_execute_main(argc, argv);
}
