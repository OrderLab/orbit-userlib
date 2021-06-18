#include "orbit.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

void test_mult(int no)
{
	struct orbit_module *mult_ob;
	pid_t main_pid = getpid();
	char name[32];
	snprintf(name, 32, "test_mult_%d", no);
	mult_ob = orbit_create(name, test_mult_task_entry);
	assert(mult_ob != NULL);
	assert(mult_ob->mpid == main_pid);
	// local obid should have the property of monotonic increasing
	assert(mult_ob->lobid > last_lobid);
	assert(mult_ob->gobid > 0);
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
		assert(a * b == ret);
	}
}

int main()
{
	srand(time(NULL));
	int N = 10;
	for (int i = 1; i <= N; i++)
		test_mult(i);
	printf("Successfully created and tested %d orbit tasks\n", N);
}
