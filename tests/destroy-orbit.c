#include "orbit.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "acutest.h"

unsigned long test_task_entry(void *args)
{
	long sum = 0;
	for (int i = 0; i < 1000; i++) {
		if (i % 100 == 0)
			sleep(1);
		sum += i;
	}
	return sum;
}

void test_destroy()
{
	struct orbit_module *ob;
	int ret, err;
	struct orbit_task task;
	int dummy;
	struct stat sts;
	char proc_path[32];

	pid_t main_pid = getpid();
	ob = orbit_create("test_destroy", test_task_entry);
	TEST_ASSERT(ob != NULL);
	TEST_ASSERT(ob->mpid == main_pid);
	TEST_ASSERT(ob->lobid > 0);
	TEST_ASSERT(ob->gobid > 0);

	dummy = 100;
	ret = orbit_call_async(ob, 0, 0, NULL, &dummy, sizeof(int), &task);
	printf("Async orbit call with task id %ld\n", task.taskid);
	snprintf(proc_path, 32, "/proc/%d", ob->gobid);
	TEST_CHECK(ret == 0);
	ret = kill(ob->gobid, 0);
	TEST_CHECK(ret == 0);
	printf("kill(0) returns %d\n", ret);
	printf("getpgid returns %d\n", getpgid(ob->gobid));
	ret = stat(proc_path, &sts);
	err = errno;
	printf("stat %s returns %d with err %d\n", proc_path, ret, err);
	printf("The orbit task exists, ready to destroy\n");

	ret = orbit_destroy(ob->gobid);
	TEST_CHECK(ret == 0);
	ret = kill(ob->gobid, 0);
	err = errno;
	TEST_CHECK(ret < 0);
	TEST_CHECK(err == ESRCH);
	printf("Checking PID returned %d\n", ret);
	printf("getpgid returns %d\n", getpgid(ob->gobid));
	ret = stat(proc_path, &sts);
	err = errno;
	printf("stat %s returns %d with err %d\n", proc_path, ret, err);
	printf("Orbit successfully destroyed!\n");
	// printf("Enter anything to exit");
	// char c;
	// scanf("%c", &c);
}

TEST_LIST = {
    { "destroy", test_destroy },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_verbose_level_ = 3;
	acutest_execute_main(argc, argv);
}
