# TMP117 host example

The driver on your PC, with no hardware: the host port in
[`host_tmp117_port.hpp`](host_tmp117_port.hpp) connects the real driver to a simulated TMP117 (the register model
the host tests use), and [`main.cpp`](main.cpp) starts it, reads the temperature three times and takes one on-demand sample.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/tmp117_host_example
```

## Expected output

```text
TMP117 driver 0.1.0 on the host
tmp117: initialised
23.351 C
23.351 C
23.351 C
one-shot 23351 mC
```

The host tests build this folder too and run the example (CTest `tmp117_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: start, read, one-shot, print |
| [`host_tmp117_port.hpp`](host_tmp117_port.hpp) | The host port: bus hooks over the simulated chip, `DelayUs()`, `ReadAlertPin()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
