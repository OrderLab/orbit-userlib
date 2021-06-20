# Obi-Wan User-Level Library

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

It should be run on every major change for checking regression. New test cases 
are also needed to cover the untested or new functionalities.

The `make test` basically just invokes `ctest` (from `cmake`). To run 
individual test, use `ctest -R <test_name>`, e.g.,

```bash
cd build
ctest -R pool-basic
```

