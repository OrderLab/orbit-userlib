/**
 * This test file is used to test the implementation of incremental
 * snapshotting optimization.
 *
 * However, some of the edge cases cannot be tested in this file, and can only
 * be get from the debug printing.
 */

#include "orbit.h"
#include <stdio.h>
#include "acutest.h"

typedef struct {
	int arg1;
	int arg2;
	bool do_write;
} add_args;

typedef struct {
	add_args *args;
	bool should_write;
} test_args;

#define WRONG_ANSWER 0x5555UL

unsigned long add(void *store, void *args)
{
	int sum;
	test_args *test = (test_args*)args;
	add_args *p = test->args;
	(void)store;
	printf("Received addition args %p\n", p);
	printf("Received addition args (%d, %d) from main program\n",
		p->arg1, p->arg2);
	sum = p->arg1 + p->arg2;
	printf("Found do_write to be %d, and should be %d\n",
		p->do_write, test->should_write);
	if (p->do_write != test->should_write)
		return WRONG_ANSWER;
	if (p->do_write) {
		printf("Changing do_write to be false\n");
		p->do_write = false;
	}
	return sum;
}

void test_pool_add() {
	struct orbit_module *add_ob;
	struct orbit_pool *front_pool, *pool;
	struct orbit_allocator *alloc;
	void *front_addr = (void*)(0x82UL * 0x40000000UL + 0x1000);
	void *pool_addr = (void*)(0x82UL * 0x40000000UL + 0x9000);
	void *padding;
	add_args *args;
	test_args test;
	int a, b;
	long ret;

	/* The incremental snapshot optimization uses a bitmap in PMD.
	 * It then iterate through the bitmap in update_pte_range.
	 * However, it needs to first skip some pages if the src_pte is not
	 * the first PTE in the PMD. */
	front_pool = orbit_pool_create_at(NULL, 4096, front_addr);
	TEST_ASSERT(front_pool != NULL);
	/* Furthermore, each bit in the bitmap represents 8 PTEs of a group.
	 * However, if the src_pte is not the beginning of a group, the
	 * incremental snapshot also needs to skip the first few PTEs in the
	 * group. */
	pool = orbit_pool_create_at(NULL, 32 * 4096, pool_addr);
	TEST_ASSERT(pool != NULL);

	/* FIXME: currently pool created by mmap_pair after orbit_create will
	 * result into segfault with error code 7 (present, write-protect,
	 * user) when executing `p->arg1 = 0` in add(). */
	add_ob = orbit_create("test_add", add, NULL);
	TEST_ASSERT(add_ob != NULL);
	TEST_ASSERT(add_ob->gobid > 0);

	/* Make 0x1000 page dirty */
	*(char*)front_addr = 'a';

	alloc = orbit_allocator_from_pool(pool, false);
	TEST_ASSERT(alloc != NULL);

	/* Make the 0x9000 page dirty */
	args = (add_args*)orbit_alloc(alloc, sizeof(add_args));
	args->arg1 = a = 15;
	args->arg2 = b = 67;
	args->do_write = false;
	test = (test_args) { args, false };
	/* Used for the next call to make 0x10000 page dirty */
	padding = orbit_alloc(alloc, 7 * 0x1000);
	TEST_ASSERT(padding != NULL);

	/* In this call, the bitmap should be 0b0011, and kernel should skip
	 * the 0b0001, and only 0b0010 left. Then the first group should have
	 * only 7 iterations. */
	ret = orbit_call(add_ob, 1, &pool, NULL, &test, sizeof(test));
	printf("Calling orbit task to add (%d, %d)\n", a, b);
	printf("Received result from orbit task=%ld\n", ret);
	TEST_CHECK(a + b == ret);

	/* New alloc at 0x10000 page, make it dirty */
	args = (add_args*)orbit_alloc(alloc, sizeof(add_args));
	args->arg1 = a = 90;
	args->arg2 = b = 82;
	args->do_write = true;
	test = (test_args) { args, true };
	/* Used to let kernel clear bit for this group */
	padding = orbit_alloc(alloc, 8 * 0x1000);
	TEST_ASSERT(padding != NULL);

	/* In this call, the bitmap should be 0b0101 (and skip to 0b0100),
	 * and kernel would * start with 8 iterations for the first group */
	ret = orbit_call(add_ob, 1, &pool, NULL, &test, sizeof(test));
	printf("Calling orbit task to add (%d, %d)\n", a, b);
	printf("Received result from orbit task=%ld\n", ret);
	TEST_CHECK(a + b == ret);

	/* In this call, the bitmap should be 0b0101 (because last orbit_call
	 * modifies the `modified` field), and skip to (0b0100) and kernel
	 * would start with 8 * iterations for the first group */
	ret = orbit_call(add_ob, 1, &pool, NULL, &test, sizeof(test));
	printf("Calling orbit task to add (%d, %d)\n", a, b);
	printf("Received result from orbit task=%ld\n", ret);
	TEST_CHECK(a + b == ret);

	TEST_CHECK(orbit_exists(add_ob));
	TEST_CHECK(orbit_destroy(add_ob->gobid) == 0);
	usleep(1000);
	if (TEST_CHECK(orbit_gone(add_ob)))
		printf("Destroyed add_ob orbit %d\n", add_ob->gobid);
	free(add_ob);
}

TEST_LIST = {
    { "pool_add", test_pool_add },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	srand(time(NULL));
	return acutest_execute_main(argc, argv);
}
