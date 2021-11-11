#include "acutest.h"
#include "private/bitmap_allocator_private.h"

void test_bitmap_wrapper() {
	TEST_ASSERT(test_bitmap());
}

TEST_LIST = {
	{ "bitmap_allocator", test_bitmap_wrapper },
	{ NULL, NULL }
};

int main(int argc, char *argv[]) {
	// setting no_exec is needed, otherwise acutest run
	// the tests in child processes (fork)
	acutest_no_exec_ = 1;
	acutest_verbose_level_ = 3;
	return acutest_execute_main(argc, argv);
}
