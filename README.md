## Introduction

A Leakage-Free Framework for Private Set Operations

## Dependences

Our repository depends on [Secure-Join](), [libOTe](), [2PC_eq_cmp](), [Blake3]() and [OpenMP]().

## Code Structure

```bash
- README.md
- CMakeLists.txt
- pbssPMT           // our code
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

### OpenMP

### Blake3

```bash

```

### libOTe

### Secure-Join

### Our code
Enter our repository root and run
```bash
mkdir build && cd build
cmake ..
make -j
```

## Test
Enter `build` and run
```bash
# all test
ctest -V

# OpenMP threads number = 4, run the specific test `workflow_test`
OMP_NUM_THREADS=4 ctest -R "workflow_test" -V
```