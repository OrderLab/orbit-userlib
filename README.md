# Obi-Wan User-Level Library

![build workflow](https://github.com/OrderLab/obiwan-userlib/actions/workflows/build.yml/badge.svg)

## Usage

```bash
mdkir build && cd build
cmake ..
make -j$(nproc)
```

cmake directly supports generating compilation databases with the appropriate 
flag. The CMakeLists.txt has turned on the flag to generate `compile_commands.json` 
by default.

## Test

Each test case can be individually run, e.g.,

```bash
$ cd build/tests
$ ./pool-basic
...
Calling orbit task to sum a buffer {10, 15, 36, 74, 48, 57, 10, 46, 49, 23, 26, 66, 76, 59, 39}
Received result from orbit task=634
  pool-basic.c:99: Check sum == ret... OK
  pool-basic.c:101: Check orbit_exists(ptr_ob)... OK
  pool-basic.c:104: Check orbit_destroy_all() == 0... OK
  pool-basic.c:105: Check orbit_gone(ptr_ob)... OK
Destroyed ptr_ob orbit 4524
  SUCCESS: All 20 checks have passed.

Summary:
  Count of all unit tests:        2
  Count of run unit tests:        2
  Count of failed unit tests:     0
  Count of skipped unit tests:    0
SUCCESS: All 2 unit tests have passed.
```

An automatic test make target is also provided. 

```bash
cd build
make test 
```

The expected successful test result is something like:

```bash
Test project /home/ryan/userlib/build
    Start 1: sync-modify-simple
1/4 Test #1: sync-modify-simple ...............   Passed    0.03 sec
    Start 2: multi-orbits-simple
2/4 Test #2: multi-orbits-simple ..............   Passed    0.17 sec
    Start 3: pool-basic
3/4 Test #3: pool-basic .......................   Passed    0.04 sec
    Start 4: destroy-orbit
4/4 Test #4: destroy-orbit ....................   Passed    0.27 sec

100% tests passed, 0 tests failed out of 4
```

The unit tests should be run on every major change for checking regression. New test cases 
are also needed to cover the untested or new functionalities.

The `make test` basically just invokes `ctest` (from `cmake`). To run 
individual test, use `ctest -R <test_name>`, e.g.,

```bash
cd build
ctest -R pool-basic
```
