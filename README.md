## Introduction

This project provides an experimental framework for evaluating leakage-free private set operations (PSO). The goal of the repository is to benchmark the performance of our protocols and implementation techniques, rather than to serve as a production-ready library. Users are encouraged to review the implementation carefully before integrating any components outside benchmarking or academic use.

## Dependences -- Explanation

Our system is built on top of several cryptographic and parallel-computing libraries.
 - [secure-join](https://github.com/ladnir/secure-join.git) provides the implementation of permutation correlation generator (PermCG).
 - [libOTe](https://github.com/osu-crypto/libOTe.git) provide core primitives used in two-party computation and oblivious transfer.
 - [2PC_eq_cmp](https://zenodo.org/records/17217396) provides equality and comparison protocols used within our PSO workflow.
 - [Blake3](https://github.com/BLAKE3-team/BLAKE3.git) is included as a fast, modern hash function.
 - [OpenMP](https://www.openmp.org) enables configurable multi-threading to improve performance on multi-core CPUs.
These dependencies must be compiled and installed before building our own code, as they supply most of the foundational cryptographic operations used throughout the framework.

## Code Structure
The repository is organized to clearly separate our own implementation from external dependencies and build artifacts.
 - The `pbsspmt` directory contains all source files, headers, and unit tests for our protocol and benchmarking framework.
 - The `external` directory holds third-party libraries along with an `install` subfolder that acts as a custom installation prefix for all dependencies.
 - The `build` directory is where CMake generates configuration files and where compilation outputs are stored.
```bash
- README.md
- CMakeLists.txt
- pbsspmt           // our code
  |-- src           // source code
  |-- include       // headers
  |-- test          // test code
- external          // external lib
  |-- install       // external lib install path
  |-- libOTe        // libOTe download path
  |-- blake3        // blake3 download path
  |-- secure-join   // secure-join download path
- build             // build directory
```

## Build
Before building our framework, all external dependencies must be downloaded, configured, and installed correctly. The instructions in this section explain how to retrieve compatible versions of each library and how to compile them with the required options. After the dependencies are installed into `external/install`, you can build our code using CMake as usual.

```bash
mkdir -p external/install
```

### OpenMP

[OpenMP](https://www.openmp.org) is used to parallelize computationally expensive tasks in our implementation. You may need to install or enable OpenMP support depending on your compiler and operating system. The official OpenMP documentation provides detailed installation steps. Once installed, the number of threads can be controlled dynamically via the `OMP_NUM_THREADS` environment variable during testing.

### Blake3

Blake3 is a modern, high-performance hashing library that we use for internal hashing operations. The repository includes instructions to clone the official Blake3 implementation. By building and linking against Blake3 manually, we ensure consistent performance results across different systems for benchmarking purposes.

```bash
cd external
git clone https://github.com/BLAKE3-team/BLAKE3.git blake3
```

### libOTe

We rely heavily on `libOTe` for oblivious transfer operations and other low-level cryptographic primitives. Because `secure-join` requires compatibility with a specific commit of `libOTe`, we explicitly instruct users to check out the required version. The build script build.py simplifies the compilation process by enabling all necessary modules, including **circuits**, **AES**, **SSE**, **silent VOLE**, and **Boost** support. Finally, `libOTe` is installed into the unified `external/install` directory to make integration straightforward.

1. Download libOTe

We use a specific branch `657f6da90bff5774a2d01c824e997572d5e8ba00` to be compatible with `secure-join`
```bash
cd external
git clone https://github.com/osu-crypto/libOTe.git
cd libOTe
git checkout 657f6da90bff5774a2d01c824e997572d5e8ba00 --
```

2. Build libOTe
```bash
python build.py -DENABLE_CIRCUITS=ON -DENABLE_BOOST=ON -DENABLE_SODIUM=ON -DENABLE_SSE=ON -DENABLE_AES=ON -DCOPROTO_ENABLE_BOOST=ON -DENABLE_ALL_OT=ON -DENABLE_SILENT_VOLE=ON -DCMAKE_CXX_FLAGS="-march=native"
```

3. Install libOTe
Install `libOTe` to `external/install`
```bash
cd out/build/linux
cmake --install . --prefix $(pwd)/../../../../install
```

### secure-join

secure-join provides efficient permutation correlation generator (PermCG) construction blocks that we integrate into our benchmarking environment. After downloading the repository, you must compile it with **Boost** and the required **sodium** configuration flags.

1. Download `secure-join`
```bash
cd external
git clone https://github.com/ladnir/secure-join.git
```

2. Build
```bash
cd secure-join
python build.py -D SODIUM_MONTGOMERY=false -D COPROTO_ENABLE_BOOST=ON
```

3. Install
```bash
cd out/build/linux
cmake --install . --prefix $(pwd)/../../../../install
```

### Our code

Once all dependencies are prepared, compiling our code follows the standard CMake workflow. The build process automatically detects the external libraries installed in `external/install`. After running `cmake` and `make`, the resulting binaries include both the protocol implementations and the benchmark tools used to evaluate them.

```bash
mkdir build && cd build
cmake ..
make -j
```

## Test

The project includes unit tests and benchmarking tests located in the test directory. You can run all tests using CTest or execute specific tests by name. OpenMP thread counts can be set at runtime to evaluate how parallelization affects performance. This allows users to perform reproducible measurements under different computational settings.

```bash
# all test
ctest -V

# OpenMP threads number = 4, run the specific test `workflow_test`
OMP_NUM_THREADS=4 ctest -R "workflow_test" -V
```

## LICENSE
This project is licensed under the **Apache License 2.0**.
You may use, distribute, and modify this software in accordance with the terms of the license.

For more details, please refer to the full license text in the LICENSE file included in this repository, or visit:
```arduino
http://www.apache.org/licenses/LICENSE-2.0
```