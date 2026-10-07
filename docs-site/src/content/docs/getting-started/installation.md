---
title: Installation
description: How to build mixed-precision-dsp and integrate it into your project
---

mixed-precision-dsp is a header-only C++20 library. Its two dependencies,
[Universal](https://github.com/stillwater-sc/universal) (number types) and
[MTL5](https://github.com/stillwater-sc/mtl5) (linear algebra), are fetched
automatically by CMake's FetchContent mechanism -- you do not need to install
them separately.

## Requirements

| Requirement | Minimum version |
|-------------|----------------|
| C++ standard | C++20 |
| CMake | 4.0 |
| Compiler | GCC 11+, Clang 14+, MSVC 2022+ |

## Clone and build

```bash
git clone https://github.com/stillwater-sc/mp-dsp.git
cd mp-dsp

cmake -B build -Wno-dev
cmake --build build -j4
```

The first configure step will clone Universal and MTL5 into the build tree.
Subsequent configures reuse the cached sources.

## Run the tests

```bash
ctest --test-dir build --output-on-failure
```

## Build with Clang

To build with Clang instead of GCC, specify the compiler at configure time:

```bash
cmake -B build_clang -DCMAKE_CXX_COMPILER=clang++ -Wno-dev
cmake --build build_clang -j4
ctest --test-dir build_clang --output-on-failure
```

## RISC-V cross-compilation

A toolchain file is provided for RISC-V 64-bit targets:

```bash
cmake -B build_rv64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-gcc.cmake \
      -Wno-dev
cmake --build build_rv64 -j4
```

## Using in your own CMake project

### Option A: FetchContent (recommended)

Add the following to your project's `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(dsp
    GIT_REPOSITORY https://github.com/stillwater-sc/mp-dsp.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dsp)

target_link_libraries(your_target PRIVATE sw::dsp)
```

This pulls in the library and its transitive dependencies (Universal, MTL5)
automatically.

### Option B: find_package (system install)

If you have installed mixed-precision-dsp to a system prefix:

```cmake
find_package(dsp CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE sw::dsp)
```

### Option C: add as a subdirectory

Clone or add the repository as a Git submodule, then:

```cmake
add_subdirectory(external/mp-dsp)
target_link_libraries(your_target PRIVATE sw::dsp)
```

## Verify your setup

Create a minimal `main.cpp`:

```cpp
#include <sw/dsp/dsp.hpp>
#include <iostream>

int main() {
    sw::dsp::iir::ButterworthLowPass<4> filter;
    filter.setup(4, 44100.0, 1000.0);
    std::cout << "Cascade has "
              << filter.cascade().num_stages()
              << " biquad stages\n";
    return 0;
}
```

Build and run:

```bash
cmake -B build -Wno-dev && cmake --build build -j4
./build/verify
```

If you see `Cascade has 2 biquad stages`, everything is working.
