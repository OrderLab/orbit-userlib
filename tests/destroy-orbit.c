#include "orbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

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
	printf("New orbit's main PID is %d", ob->mpid);
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

	// check if the gobid exits by sending kill 0
	// TODO: should probably have a dedicated orbit existence check syscall
	ret = kill(ob->gobid, 0);
	TEST_CHECK(ret == 0);
	printf("Orbit exists with GOBID %d, ready to destroy\n", ob->gobid);

	ret = orbit_destroy(ob->gobid);
	TEST_CHECK(ret == 0);

	// check if the gobid exists again
	ret = kill(ob->gobid, 0);
	TEST_CHECK(ret < 0);

	/* ESRCH indicates the gobid does not exist
	 * TODO: there is a rare chance the PID is reused, so the test will
	 * be flaky. Having a dedicated orbit existence syscall check will
	 * help address the issue.
	 */
	TEST_CHECK(errno == ESRCH);
	printf("Orbit %d is successfully destroyed!\n", ob->gobid);
	return true;
}

void test_destroy_single()
{
	struct orbit_module *ob;

	ob = create_orbit_checked();
	TEST_ASSERT(ob != NULL);

	destroy_orbit_checked(ob);
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
	sleep(2);

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
}

TEST_LIST = {
    { "destroy_single", test_destroy_single },
    { "destroy_multiple", test_destroy_multi },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	main_pid = getpid();
	printf("Main program PID %d launched\n", main_pid);

	// setting no_exec is needed, otherwise acutest run
	// the tests in child processes (fork)
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	acutest_execute_main(argc, argv);
}
