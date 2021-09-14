set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

set(ORBIT_SOURCE_FILES 
  src/dlmalloc.c
  src/bitmap_allocator.c
  src/linear_allocator.c
  src/orbit.c
  src/orbit.cpp
)

add_library(orbit STATIC SHARED ${ORBIT_SOURCE_FILES})

target_link_libraries(orbit
  Threads::Threads
)
