#include "orbit.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

unsigned long checker_plus_one(void *obj) {
	int val = *(int*)obj;
	return val + 1;
}

void test_plus_one() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);
	m = obCreate("test_module", checker_plus_one);
	assert(m != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	sleep(1);

	ret = obCall(m, pool, obj);
	printf("In parent: obj is %d\n", ret);
}

int main() {
	char c;

	test_plus_one();

	printf("type anything to exit...");
	scanf("%c", &c);
}
