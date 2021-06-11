# The Obiwan Project
#
# Copyright (c) 2021, Johns Hopkins University - Order Lab.
#
#    All rights reserved.
#    Licensed under the Apache License, Version 2.0 (the "License");
#

cmake_minimum_required(VERSION 3.10)

project(obiwan-userlib)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/module ${CMAKE_MODULE_PATH})

# Disable in-source builds to prevent source tree corruption.
if(" ${CMAKE_SOURCE_DIR}" STREQUAL " ${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "FATAL: In-source builds are not allowed.
  You should create a build directory for building the tool.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

find_package(ClangFormat)

# add lib include in include path for other directories to use
include_directories(lib/include)
link_directories(lib)

add_subdirectory(lib)
add_subdirectory(benchmark)
add_subdirectory(test)
add_subdirectory(example)
