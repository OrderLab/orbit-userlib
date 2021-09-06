#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <orbit.h>

#include "acutest.h"

typedef void (*mysigfunc) (int signo);

#define UNUSED __attribute__ ((unused))
#define CHILD_SLEEP_TIME 2  // 2 seconds

bool child_stopped, waited_child;
int child_pid;

void reset()
{
	child_stopped = false;
	waited_child = false;
	child_pid = -1;
}

void child_main(bool is_orbit)
{
	const char *name = is_orbit ? "orbit" : "child";
	printf("%s sleeps for %d seconds\n", name, CHILD_SLEEP_TIME);
	sleep(CHILD_SLEEP_TIME);
	printf("%s exits\n", name);
	exit(0);
}

void reaper(int signo UNUSED)
{
	int pid;	/* process id of dead child process */
	int exitstatus; /* its exit status */
	printf("parent reaper called\n");
	while ((pid = waitpid(-1, &exitstatus, WNOHANG)) > 0)
	{
		printf("parent reaps child pid %d\n", pid);
		// if we successfully catch the child pid status
		if (pid == child_pid)
			waited_child = true;
	}
	printf("parent reaper finished, waitpid ret=%d\n", pid);
	child_stopped = true;
}

mysigfunc install_sighandler(int signo, mysigfunc func)
{
	struct sigaction act, oact;
	act.sa_handler = func;
	if (sigaction(signo, &act, &oact) < 0) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	return oact.sa_handler;
}

int fork_version()
{
	switch (child_pid = fork()) {
	case -1:
		perror("could not fork child process\n");
		return -1;
	case 0:
		/* in child */
		child_main(false);
		return 0;
	}
	printf("created child %d\n", child_pid);
	/* in parent */
	int tries = 0;
	while (!child_stopped && tries < 2) {
		sleep(CHILD_SLEEP_TIME);
		tries++;
	}
	// sleep for twice as long as child, if at this point no signal
	// is received, the test fails
	TEST_ASSERT(child_stopped);
	if (!TEST_CHECK(waited_child))
		printf("child stopped, but parent did not get wait status\n");
	printf("successfully reaped child %d\n", child_pid);
	return 0;
}

unsigned long orbit_child_entry(void *store UNUSED, void *args UNUSED)
{
	child_main(true);
	return 0;
}

int orbit_version()
{
	int dummy_arg;
	struct orbit_module *child_orbit;
	child_orbit = orbit_create("orbit_child", orbit_child_entry, NULL);
	TEST_ASSERT(child_orbit != NULL);
	child_pid = child_orbit->gobid;
	orbit_call_async(child_orbit, 0, 0, NULL, NULL, &dummy_arg,
			 sizeof(int), NULL);
	printf("created child %d\n", child_pid);
	/* in parent */
	int tries = 0;
	while (!child_stopped && tries < 2) {
		sleep(CHILD_SLEEP_TIME);
		tries++;
	}
	// sleep for twice as long as child, if at this point no signal
	// is received, the test fails
	TEST_ASSERT(child_stopped);
	if (!TEST_CHECK(waited_child))
		printf("child stopped, but parent did not get wait status\n");
	printf("successfully reaped child %d\n", child_pid);
	return 0;
}

void test_signal_handler()
{
	reset();
	install_sighandler(SIGCHLD, reaper);
	fork_version();
	reset(); //reset
	orbit_version();
}

TEST_LIST = {
    { "signal_handler", test_signal_handler },
    { NULL, NULL }
};


int main(int argc, char **argv)
{
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
