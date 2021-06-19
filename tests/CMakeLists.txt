include_directories("${CMAKE_CURRENT_SOURCE_DIR}/acutest")

set(TEST_SOURCES
  deadlock-detector-test.cc
  ttaslock.cpp
  sync-modify-simple.c
  multi-orbits-simple.c
  pool-basic.c
  destroy-orbit.c
)

foreach(TEST_SOURCE_FILE ${TEST_SOURCES})
  get_filename_component(TEST_EXECUTABLE_NAME ${TEST_SOURCE_FILE} NAME_WE)
  add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCE_FILE})
  target_link_libraries(${TEST_EXECUTABLE_NAME} PUBLIC orbit)
endforeach(TEST_SOURCE_FILE ${TEST_SOURCES})
