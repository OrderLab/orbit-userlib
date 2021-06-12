# Obi-Wan User-Level Library

## Usage

* With `cmake`:

```bash
mdkir build && cd build
cmake ..
make -j$(nproc)
```

cmake directly supports generating compilation databases with the appropriate 
flag. The CMakeLists.txt has turned on the flag to generate `compile_commands.json` 
by default.

* With `make`:

```bash
make
```

The built objects, libraries, and executable are by default in the `build` directory.

To generate a compilation database for code autocompletion, use tools like `bear` or `compiledb`.
