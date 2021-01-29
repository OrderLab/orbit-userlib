#include "orbit.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

unsigned long checker_plus_one(void *obj) {
	// int val = *(int*)obj;
	// return val + 1;
	*(int*)obj += 1;
	return *(int*)obj;
}

void test_plus_one() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_plus_one);
	assert(m != NULL);

	*obj = 200;

	ret = obCall(m, pool, obj);
	printf("In parent: obj is %d\n", ret);
}

void test_multiple_plus_one() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;
	*obj = 100;

	m = obCreate("test_module", checker_plus_one);
	assert(m != NULL);

	for (int i = 200; i <= 1000; i += 100) {
		*obj = i;
		ret = obCall(m, pool, obj);
		printf("In parent: ret is %d, obj is %d\n", ret, *obj);
	}
}


unsigned long checker_fail(void *ptr) {
	return *(int*)(NULL);
}

void test_fail() {
	struct obPool *pool;
	struct obModule *m;
	int *obj, ret;

	pool = obPoolCreate(4096);
	assert(pool != NULL);

	obj = (int *)pool->rawptr;

	m = obCreate("test_module", checker_fail);
	assert(m != NULL);

	ret = obCall(m, pool, obj);
	printf("In parent: obj is %d\n", ret);
}


int main() {
	char c;

	//test_plus_one();
	test_multiple_plus_one();
	// test_fail();

	printf("type anything to exit...");
	scanf("%c", &c);
}
