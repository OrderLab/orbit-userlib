#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "dlmalloc.h"

#ifdef HAVE_TCMALLOC
#include <gperftools/tcmalloc.h>
#endif

#define SEC_TO_NSEC 1000000000L

enum ALLOC_TYPE {
  MALLOC,
  DLMALLOC,
#ifdef HAVE_TCMALLOC
  TCMALLOC
#endif
};

void benchmark(enum ALLOC_TYPE type)
{
  int N = 5000;
  uint64_t malloc_total, free_total, diff;
  struct timespec start, end;
  long *ptr;
  const char *malloc_name, *free_name;

  malloc_total = 0;
  free_total = 0;
  for (int i = 0; i < N; i++) {
    switch (type) {
      case DLMALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        ptr = (long *) dlmalloc(sizeof(long));
        clock_gettime(CLOCK_MONOTONIC, &end);
        break;
      case MALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        ptr = (long *) malloc(sizeof(long));
        clock_gettime(CLOCK_MONOTONIC, &end);
        break;
#ifdef HAVE_TCMALLOC
      case TCMALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        ptr = (long *) tc_malloc(sizeof(long));
        clock_gettime(CLOCK_MONOTONIC, &end);
        break;
#endif
    }

    diff = SEC_TO_NSEC * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
    malloc_total += diff;

    *ptr = i;
    *ptr = i + 1;
    *ptr = i + 2;
    *ptr = i + 3;

    switch (type) {
      case DLMALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        dlfree(ptr);
        clock_gettime(CLOCK_MONOTONIC, &end);
        break;
      case MALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        free(ptr);
        clock_gettime(CLOCK_MONOTONIC, &end);
        break;
#ifdef HAVE_TCMALLOC
      case TCMALLOC:
        clock_gettime(CLOCK_MONOTONIC, &start);
        tc_free(ptr);
        clock_gettime(CLOCK_MONOTONIC, &end);
#endif
        break;
    }

    diff = SEC_TO_NSEC * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
    free_total += diff;
  }

  switch (type) {
      case DLMALLOC:
        malloc_name = "dlmalloc";
        free_name = "dlfree";
        break;
      case MALLOC:
        malloc_name = "malloc";
        free_name = "free";
        break;
#ifdef HAVE_TCMALLOC
      case TCMALLOC:
        malloc_name = "tc_malloc";
        free_name = "tc_free";
        break;
#endif
  }
  printf("average latency: %s (%lu ns), %s (%lu ns)\n", malloc_name,
      malloc_total / N, free_name, free_total /N);
}

int main()
{
  benchmark(DLMALLOC);
#ifdef HAVE_TCMALLOC
  benchmark(TCMALLOC);
#endif
  benchmark(MALLOC);
}
