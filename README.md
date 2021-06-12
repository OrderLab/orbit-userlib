# Obi-Wan User-Level Library

## Usage

```bash
make
```

The built objects, libraries, and executable are by default in the `build` directory.

* With `cmake`:

```bash
mdkir build && cd build
cmake ..
make -j$(nproc)
```

The CMakeLists.txt enables outputting `compile_commands.json` by default for 
code autocompletion.
