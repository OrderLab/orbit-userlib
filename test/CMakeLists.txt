add_executable(deadlock-detector-test
  deadlock-detector-test.cc
)
target_link_libraries(deadlock-detector-test
  PRIVATE orbit
)
add_executable(ttaslock
  ttaslock.cpp
)
target_link_libraries(ttaslock
  PRIVATE orbit
)
