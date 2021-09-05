set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

add_library(orbit STATIC SHARED
  src/orbit.c
  src/orbit.cpp
)
target_link_libraries(orbit
  Threads::Threads
)
