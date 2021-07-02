set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

set(ORBIT_SOURCE_FILES 
  src/orbit.c
  src/bitmap_allocator.c
  src/linear_allocator.c
)

add_library(orbit SHARED ${ORBIT_SOURCE_FILES})
add_library(orbit_static STATIC ${ORBIT_SOURCE_FILES})
set_target_properties(orbit_static PROPERTIES OUTPUT_NAME orbit)

target_link_libraries(orbit_static
  Threads::Threads
)
target_link_libraries(orbit
  Threads::Threads
)
