#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <orbit.h>
#include <signal.h>
#include <sys/wait.h>

#include "acutest.h"

#define UNUSED __attribute__ ((unused))

struct task_args {
	int *left;
	int *right;
};

void handle_orbit_exit(int signo)
{
	int pid;
	int exitstatus;
	printf("orbit exit handler invoked for signal %d\n", signo);
	while ((pid = waitpid(-1, &exitstatus, WNOHANG)) > 0)
		printf("orbit %d dies\n", pid);
	printf("orbit exit handler finished\n");
}

unsigned long buggy_orbit_entry(void *store UNUSED, void *args)
{
	struct task_args *p = (struct task_args *)args;
	int result = 0;
	printf("orbit receives argument at %p\n", p);
	if (p->left == NULL || p->right == NULL) {
		printf("buggy orbit will perform null pointer dereference\n");
	} else {
		printf("orbit computes sum of (%d, %d)\n", *p->left, *p->right);
	}
	result = *p->left + *p->right;
	struct orbit_update *update = (struct orbit_update*)
		malloc(sizeof(struct orbit_update) + sizeof(int));
	update->ptr = p;
	update->length = sizeof(int);
	*(int *) update->data = result;
	orbit_send(update);
	free(update);
	return 0;
}

void test_orbit_crash()
{
	int left = 3, right = 30, result = 0;
	struct task_args args = {NULL, NULL};
	struct orbit_module *buggy_orbit;
	struct orbit_update *update;
	struct orbit_task task;

	buggy_orbit = orbit_create("buggy_orbit", buggy_orbit_entry, NULL);
	TEST_ASSERT(buggy_orbit != NULL);
	TEST_ASSERT(buggy_orbit->gobid > 0);
	int ret = orbit_call_async(buggy_orbit, 0, 0, NULL, NULL, &args,
			 sizeof(struct task_args), NULL);
	TEST_ASSERT(ret == 0);
	printf("created orbit %d\n", buggy_orbit->gobid);
	sleep(3);
	bool gone = orbit_gone(buggy_orbit);
	printf("check existence of orbit %d: %s", buggy_orbit->gobid, gone ?
	       "no" : "yes");
	if (!gone) {
		TEST_MSG("orbit should not exist at this point...\n");
		TEST_ASSERT(false);
		args.left = &left;
		args.right = &right;
		update = (struct orbit_update*)
			malloc(sizeof(struct orbit_update) + sizeof(int));
		ret = orbit_call_async(buggy_orbit, 0, 0, NULL, NULL, &args,
			 sizeof(struct task_args), &task);
		TEST_ASSERT(ret == 0);
		TEST_ASSERT(task.orbit != NULL);
		TEST_ASSERT(task.taskid > 0);
		ret = orbit_recv(&task, update);
		TEST_ASSERT(ret == 0);
		TEST_ASSERT(update->length > 0);
		TEST_ASSERT(update->data != NULL);
		result = *(int *) update->data;
		if (!TEST_CHECK(result == left + right))
			TEST_MSG("Expected :%d; Received: %d\n",
				 left + right, result);
	}
}

void test_main_crash()
{
	// TODO: test handling of main program crash
}

TEST_LIST = {
    { "orbit_crash", test_orbit_crash },
    { "main_crash", test_main_crash },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	struct sigaction act, oact;
	act.sa_handler = handle_orbit_exit;
	if (sigaction(SIGCHLD, &act, &oact) < 0) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
