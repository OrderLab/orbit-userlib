set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(forktest
  forktest.cpp
)
target_link_libraries(forktest 
  Threads::Threads
)

add_executable(micro
  micro.cpp
)
target_link_libraries(micro
  PRIVATE orbit
  Threads::Threads
)

add_executable(alloc-bench
  alloc-bench.c
)
target_link_libraries(alloc-bench
  PRIVATE orbit
)
