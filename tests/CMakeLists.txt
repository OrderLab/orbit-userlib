include_directories("${CMAKE_CURRENT_SOURCE_DIR}/acutest")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")

set(UNIT_TEST_SOURCES
  sync-modify-simple.c
  multi-orbits-simple.c
  pool-basic.c
  destroy-orbit.c
  async-update-basic.c
  signal-handler.c
  crash-handling.c
)

# they not been rewritten into unit tests
set(OTHER_TEST_SOURCES
  deadlock-detector-test.cc
  ttaslock.cpp
)

set(TEST_SOURCES
  ${UNIT_TEST_SOURCES}
  ${OTHER_TEST_SOURCES}
)

foreach(TEST_SOURCE_FILE ${TEST_SOURCES})
  get_filename_component(TEST_EXECUTABLE_NAME ${TEST_SOURCE_FILE} NAME_WE)
  add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCE_FILE})
  target_link_libraries(${TEST_EXECUTABLE_NAME} PUBLIC orbit)
  if (${TEST_SOURCE_FILE} IN_LIST UNIT_TEST_SOURCES)
    # if the test is a unit test, add it to the ctest target
    add_test(NAME ${TEST_EXECUTABLE_NAME} COMMAND ${TEST_EXECUTABLE_NAME})
  endif()
endforeach(TEST_SOURCE_FILE ${TEST_SOURCES})
