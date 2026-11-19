## Introduction

This project provides an experimental framework for evaluating leakage-free private set operations (PSO). The goal of the repository is to benchmark the performance of our protocols and implementation techniques, rather than to serve as a production-ready library. Users are encouraged to review the implementation carefully before integrating any components outside benchmarking or academic use.

> 中文文档见 [README.zh-CN.md](README.zh-CN.md).

## Dependencies

Our system is built on top of several cryptographic and parallel-computing libraries.

 - [secure-join](https://github.com/ladnir/secure-join.git) provides the implementation of permutation correlation generator (PermCG).
 - [libOTe](https://github.com/osu-crypto/libOTe.git) provides core primitives used in two-party computation and oblivious transfer.
 - [2PC_eq_cmp](https://zenodo.org/records/17217396) provides equality and comparison protocols used within our PSO workflow.
 - [BLAKE3](https://github.com/BLAKE3-team/BLAKE3.git) is included as a fast, modern hash function.
 - [libsodium](https://github.com/jedisct1/libsodium) supplies elliptic-curve and hashing primitives used by libOTe.
 - [Boost](https://www.boost.org) is required by libOTe and secure-join.
 - [OpenMP](https://www.openmp.org) enables configurable multi-threading to improve performance on multi-core CPUs.

All of these are built into `external/install`, which is used as the single
installation prefix for the whole build. Nothing is taken from the system
prefix, so a checkout produces the same binaries regardless of which versions
the host distribution happens to ship.

| Dependency | Pinned version | Reason |
|---|---|---|
| libOTe | `657f6da9` | Required for secure-join compatibility |
| secure-join | `377ca63b` | Tested against the libOTe commit above |
| libsodium | 1.0.19 | `crypto_scalarmult_noclamp`, used by cryptoTools, was added in 1.0.19 |
| Boost | 1.86 | Built automatically by libOTe |

### Supported platforms

The build is verified on Linux (x86-64) with GCC 13 and CMake 3.28. Windows
users are expected to build inside WSL2 rather than with MSVC.

CPU requirements: SSE2, AVX2, PCLMUL and AES are needed. **AVX-512 is optional** —
see [CPU features](#cpu-features-and-avx-512) for details.

## Code Structure

The repository separates our own implementation from external dependencies and build artifacts.

 - The `pbsspmt` directory contains all source files, headers, and unit tests for our protocol and benchmarking framework.
 - The `external` directory holds third-party libraries along with an `install` subfolder that acts as a custom installation prefix for all dependencies.
 - The `build_deps.sh` script builds every dependency into `external/install`.
 - The `build` directory is where CMake generates configuration files and where compilation outputs are stored.

```
- README.md
- README.zh-CN.md      // Chinese translation
- CMakeLists.txt
- build_deps.sh        // builds all dependencies into external/install
- pbsspmt              // our code
  |-- src              // source code
  |-- include          // headers
  |-- test             // test code
- external             // external libs
  |-- install          // installation prefix for all dependencies (generated)
  |-- .deps            // downloaded libsodium sources (generated)
  |-- blake3           // blake3 (submodule)
  |-- libOTe           // libOTe (submodule)
  |-- secure-join      // secure-join (submodule)
  |-- 2PC_eq_cmp_v2    // 2PC equality/comparison protocols
- build                // build directory (generated)
```

## Build

### Quick start

```bash
git clone --recursive git@github.com:sabakioukenasai/sPSO.git
cd sPSO

# 1. Build every third-party dependency into external/install
./build_deps.sh

# 2. Build the project itself
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
```

`build_deps.sh` is idempotent: re-running it skips components that are already
installed. Use `./build_deps.sh --clean` to rebuild everything from scratch, and
`--jobs N` to limit parallelism. Full output is also written to `build_deps.log`.

#### System packages

Only the build toolchain and OpenMP come from the system:

```bash
sudo apt install -y build-essential cmake git \
                    libomp-dev autoconf automake libtool
```

Boost and libsodium are compiled into `external/install` by `build_deps.sh`, so
the distribution's `libboost-all-dev` and `libsodium-dev` packages are **not**
required — and not used, even if present.

### Manual build

The steps below describe what `build_deps.sh` does, for users who prefer to run
each stage by hand.

#### 1. Submodules

```bash
git submodule update --init --recursive

# libOTe must be on the exact commit secure-join expects
cd external/libOTe
git fetch origin
git checkout 657f6da90bff5774a2d01c824e997572d5e8ba00
git submodule update --init --recursive
cd ../..

cd external/secure-join
git fetch origin
git checkout 377ca63b9d8f4f6aede0d3a2e3d9078973a3ee10
git submodule update --init --recursive
cd ../..
```

#### 2. libsodium

`cryptoTools` calls `crypto_scalarmult_noclamp`, which does not exist before
libsodium 1.0.19. Ubuntu 24.04 ships 1.0.18, so build 1.0.19 into
`external/install`:

```bash
curl -fsSL -o libsodium-1.0.19.tar.gz \
  https://github.com/jedisct1/libsodium/releases/download/1.0.19-RELEASE/libsodium-1.0.19.tar.gz
tar xzf libsodium-1.0.19.tar.gz
cd libsodium-stable
./configure --prefix="$(pwd)/../../external/install" --enable-shared --disable-static
make -j"$(nproc)" && make install
```

#### 3. libOTe

```bash
cd external/libOTe
python3 build.py \
  -DENABLE_CIRCUITS=ON -DENABLE_BOOST=ON -DENABLE_SODIUM=ON \
  -DSODIUM_MONTGOMERY=OFF \
  -DENABLE_SSE=ON -DENABLE_AES=ON -DCOPROTO_ENABLE_BOOST=ON \
  -DENABLE_ALL_OT=ON -DENABLE_SILENT_VOLE=ON \
  -DCMAKE_PREFIX_PATH="$(pwd)/../install" \
  -DCMAKE_CXX_FLAGS="-march=native"

cd out/build/linux
cmake --install . --prefix "$(pwd)/../../../../install"
```

`-DSODIUM_MONTGOMERY=OFF` matters: cryptoTools defaults this option to `ON` and
force-overrides the cache, which pulls in Montgomery-curve code calling
`crypto_scalarmult_noclamp`. Nothing in this project needs it, so keep it off.

`-DCMAKE_PREFIX_PATH` pointing at `external/install` makes the build pick up the
libsodium and Boost built there rather than the system copies.

#### 4. secure-join

```bash
cd external/secure-join
cmake -S . -B out/build/linux \
  -D CMAKE_BUILD_TYPE=Release \
  -D SODIUM_MONTGOMERY=false \
  -D COPROTO_ENABLE_BOOST=ON \
  -DFETCH_LIBOTE=OFF \
  -DSECUREJOIN_THIRDPARTY_DIR="$(pwd)/../install" \
  -DCMAKE_PREFIX_PATH="$(pwd)/../install"

cmake --build out/build/linux --parallel "$(nproc)"
cd out/build/linux
cmake --install . --prefix "$(pwd)/../../../../install"
```

`-DFETCH_LIBOTE=OFF` reuses the libOTe installed above. Without it, secure-join
clones and builds a private copy of libOTe under its own `out/` directory.

### 2PC_eq_cmp_v2

The `2PC_eq_cmp_v2` directory contains our modified version of the two-party
secure equality test and comparison protocols. It provides the `eq2<T>` and
`cmp1<T>` template classes used in the PSO workflow.

This library depends on libOTe (already built and installed in `external/install`
as described above). The `CMAKE_PREFIX_PATH` in its `CMakeLists.txt` is
pre-configured to look for libOTe under `${CMAKE_SOURCE_DIR}/external/install/`.

**Build standalone:**

```bash
cd external/2PC_eq_cmp_v2
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

**Run standalone equality test (two terminals):**

```bash
# Terminal 1 (sender)
./build/eq_cmp -sender -n 100 -l 4 -c 0

# Terminal 2 (receiver)
./build/eq_cmp -receiver -n 100 -l 4 -c 0
```

**Run standalone comparison test:**

```bash
# Terminal 1 (sender)
./build/eq_cmp -sender -n 100 -l 4 -c 1

# Terminal 2 (receiver)
./build/eq_cmp -receiver -n 100 -l 4 -c 1
```

**Command-line parameters:**

| Flag | Description |
|------|-------------|
| `-sender` | Run as sender |
| `-receiver` | Run as receiver |
| `-n <num>` | Number of elements to compare |
| `-l <bits>` | Bit-length of each element |
| `-c <type>` | `0` = equality test, `1` = comparison |
| `-ip <addr>` | IP address (default: `localhost:1213`) |

> **Note**: In the main PSO workflow (`pbsspmt/test/test_flow.cpp`), the `eq2`
> protocol from `neweq.h` is included directly rather than linked as a separate
> library. The standalone build above is useful for testing the
> equality/comparison protocols independently.

### Our code

The top-level `CMakeLists.txt` adds `external/install` to `CMAKE_PREFIX_PATH`
and presets `SODIUM_LIBRARIES` / `SODIUM_INCLUDE_DIRS` to the copies found there,
so `find_package` cannot silently fall back to a system libsodium. If the
bundled copy is missing, the configure step prints a warning naming the system
library it fell back to.

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
```

### CPU features and AVX-512

`external/blake3` is compiled with SSE2/AVX2 unconditionally and with AVX-512
only for the single file that needs it. Some recent consumer CPUs (for example
Intel Arrow Lake) do not implement AVX-512 at all. GCC will still compile
AVX-512 code for such a CPU without any diagnostic, but **executing** it raises
`SIGILL`.

Because of that, the top-level `CMakeLists.txt` probes CPU support at configure
time using `__builtin_cpu_supports`, which reads CPUID without executing any
AVX-512 instruction. A compile-only check is not sufficient here — CMake's
`check_cxx_source_runs` can report success on CPUs where AVX-512 actually
faults. When the CPU lacks AVX-512, BLAKE3's runtime dispatch simply never
selects that path.

If you see `***Exception: Illegal` from `ctest`, this is the first thing to
suspect.

## Test

The unit and benchmark tests live in `pbsspmt/test`. Run them with CTest. The
OpenMP thread count is set at runtime, which lets you measure how parallelization
affects performance.

```bash
cd build

# all tests
ctest --output-on-failure

# a single test with 4 OpenMP threads
OMP_NUM_THREADS=4 ctest -R workflow_test -V
```

Registered tests: `oprf_gf128_test`, `oprf_okvs_basic_test`, `oprf_test`,
`cuckoo3_test`, `workflow_test`. Note that these do not exercise the permutation
correlation generator — it lives in secure-join and is measured separately, see
[Measuring PermCG](#measuring-permcg) below.

## Measuring PermCG

The permutation correlation generator (PermCG) comes from secure-join
(`secure-join/secure-join/Perm/AltModPerm.h`) rather than from this repository,
so its performance is measured with secure-join's own harness, `secJoinfrontend`.
`build_deps.sh` builds that harness; no extra step is required.

The executable ends up in the secure-join build tree:

```
external/secure-join/out/build/linux/frontend/secJoinfrontend
```

### Correctness check

`AltModPerm_setup_test` runs the generator end to end over a local socket pair
and verifies the permutation correlation. It should report `Passed`:

```bash
cd external/secure-join/out/build/linux/frontend
./secJoinfrontend -u AltModPerm_setup_test
```

### Performance benchmark

The benchmark reports wall-clock time and the bandwidth per evaluation:

```bash
cd external/secure-join/out/build/linux/frontend
./secJoinfrontend -bench -AltModPerm -nn 20
```

Output format is `n <size>, nt <threads>, <time>ms <sent>+<recv>=<total> bytes/eval`:

```
AltModPerm: n 1048576, nt 1, 1184ms 192.031+64.0313=256.063 bytes/eval
```

| Flag | Meaning | Default |
|---|---|---|
| `-nn <log2n>` | log2 of the permutation size | `10` |
| `-n <size>` | permutation size directly (overrides `-nn`) | `1 << nn` |
| `-m <bytes>` | row size per element | `16` |
| `-b <bits>` | log2 of the OLE batch size (`1 << b`) | `16` |
| `-nt <threads>` | threads per party | `1` |

`-b` controls the OLE batch size, not the width of the permuted values; use
`-m` for the latter.

### Representative results

Measured on Ubuntu 24.04 (WSL2), Intel Core Ultra 5 225H, GCC 13.3, single
thread unless noted. Bandwidth per evaluation is independent of `n`.

| `-nn` | n | time | bytes/eval |
|---|---|---|---|
| 16 | 65,536 | 48 ms | 256.066 |
| 18 | 262,144 | 211 ms | 256.063 |
| 20 | 1,048,576 | 1113–1184 ms | 256.063 |
| 22 | 4,194,304 | 11555 ms | 256.063 |
| 20 (`-nt 4`) | 1,048,576 | 1023 ms | 256.063 |

Treat these as a rough baseline on one machine rather than absolute figures.

### Matching the workflow's parameter setting

`pbsspmt/test/test_flow.cpp` operates on a set of `n = 1 << 20`. To report PermCG
cost under the same setting, use `-nn 20`. Note that the current workflow does
not call PermCG in code — `AltModPerm` appears only in comments there — so this
benchmark is the stand-alone measurement of the sub-protocol.
