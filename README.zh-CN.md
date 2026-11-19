## 简介

本仓库提供了一个用于评估无泄漏私有集合运算（Private Set Operations，PSO）的实验框架。其目标是评测我们所提协议及其实现技术的性能，而非作为生产级密码库发布。在将其中任何组件用于基准测试或学术研究之外的场景之前，请务必审慎研读实现细节。

> English documentation: [README.md](README.md)

## 依赖

本系统基于若干密码学与并行计算库构建。

 - [secure-join](https://github.com/ladnir/secure-join.git)：提供置换关联生成器（Permutation Correlation Generator，PermCG）的实现。
 - [libOTe](https://github.com/osu-crypto/libOTe.git)：提供两方计算与不经意传输所需的核心原语。
 - [2PC_eq_cmp](https://zenodo.org/records/17217396)：提供 PSO 流程中使用的相等性与比较协议。
 - [BLAKE3](https://github.com/BLAKE3-team/BLAKE3.git)：作为快速的现代哈希函数引入。
 - [libsodium](https://github.com/jedisct1/libsodium)：为 libOTe 提供椭圆曲线与哈希原语。
 - [Boost](https://www.boost.org)：libOTe 与 secure-join 所必需。
 - [OpenMP](https://www.openmp.org)：提供可配置的多线程支持，以发挥多核 CPU 的性能。

以上依赖全部编译安装到 `external/install`，并作为整个构建过程唯一的安装前缀。构建不会使用系统前缀中的任何副本，因此无论宿主机发行版自带何种版本，同一份代码都会产出相同的二进制文件。

| 依赖 | 固定版本 | 说明 |
|---|---|---|
| libOTe | `657f6da9` | 须与 secure-join 兼容 |
| secure-join | `377ca63b` | 对应上述 libOTe 版本完成测试 |
| libsodium | 1.0.19 | cryptoTools 使用的 `crypto_scalarmult_noclamp` 自 1.0.19 起才提供 |
| Boost | 1.86 | 由 libOTe 构建过程自动编译 |

### 支持的平台

构建流程在 Linux（x86-64）、GCC 13 与 CMake 3.28 上验证通过。Windows 用户建议在 WSL2 中构建，而非使用 MSVC。

CPU 要求：需要 SSE2、AVX2、PCLMUL 与 AES 指令集。**AVX-512 为可选项**，详见[CPU 指令集与 AVX-512](#cpu-指令集与-avx-512)一节。

## 代码结构

仓库将自有实现与外部依赖、构建产物清晰分离。

 - `pbsspmt` 目录包含我们协议与基准测试框架的全部源码、头文件与单元测试。
 - `external` 目录存放第三方库，其下的 `install` 子目录作为所有依赖的自定义安装前缀。
 - `build_deps.sh` 脚本将全部依赖构建到 `external/install`。
 - `build` 目录用于存放 CMake 生成的配置与编译输出。

```
- README.md
- README.zh-CN.md      // 中文文档
- CMakeLists.txt
- build_deps.sh        // 构建全部依赖到 external/install
- pbsspmt              // 我们的代码
  |-- src              // 源代码
  |-- include          // 头文件
  |-- test             // 测试代码
- external             // 外部库
  |-- install          // 所有依赖的安装前缀（生成）
  |-- .deps            // 下载的 libsodium 源码（生成）
  |-- blake3           // blake3（子模块）
  |-- libOTe           // libOTe（子模块）
  |-- secure-join      // secure-join（子模块）
  |-- 2PC_eq_cmp_v2    // 两方相等性与比较协议
- build                // 构建目录（生成）
```

## 构建

### 快速开始

```bash
git clone --recursive git@github.com:sabakioukenasai/sPSO.git
cd sPSO

# 1. 将全部第三方依赖构建到 external/install
./build_deps.sh

# 2. 构建项目本体
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
```

`build_deps.sh` 具备幂等性：重复运行会跳过已安装的组件，耗时约两秒。`--jobs N` 可限制并行度，完整输出同时写入 `build_deps.log`。

`--clean` 会删除 `external/install` 与依赖构建目录并从头重建。该过程需重新编译 Boost 与 libOTe，耗时约 20 至 40 分钟，仅在需要排除依赖树损坏时才使用。脚本会列出即将删除的内容并请求确认，`--clean --yes` 可跳过确认。

请在多次构建之间保留 `external/install`。常规的修改—编译—测试循环为：

```bash
./build_deps.sh          # 约 2 秒，无变化时不做任何事
cd build && make -j"$(nproc)" && OMP_NUM_THREADS=4 ctest
```

修改 `pbsspmt/` 下的源码后只需执行第二行；只有在子模块或依赖构建发生变化时，才需要重新运行 `build_deps.sh`。

#### 系统依赖

仅构建工具链与 OpenMP 来自系统包：

```bash
sudo apt install -y build-essential cmake git \
                    libomp-dev autoconf automake libtool
```

Boost 与 libsodium 由 `build_deps.sh` 编译进 `external/install`，因此**不需要**也**不会使用**发行版的 `libboost-all-dev` 与 `libsodium-dev` 包。

### 手动构建

以下步骤描述了 `build_deps.sh` 的具体内容，供希望逐段手动执行的用户参考。

#### 1. 子模块

```bash
git submodule update --init --recursive

# libOTe 必须位于 secure-join 所要求的精确提交上
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

`cryptoTools` 调用了 `crypto_scalarmult_noclamp`，该接口在 libsodium 1.0.19 之前并不存在。Ubuntu 24.04 自带 1.0.18，因此需将 1.0.19 构建到 `external/install`：

```bash
curl -fsSL -o libsodium-1.0.19.tar.gz \
  https://github.com/jedisct1/libsodium/releases/download/1.0.19-RELEASE/libsodium-1.0.19.tar.gz
tar xzf libsodium-1.0.19.tar.gz
cd libsodium-stable
./configure --prefix="$(pwd)/../../external/install" --enable-shared --enable-static
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
  -DSODIUM_LIBRARIES="$(pwd)/../install/lib/libsodium.so" \
  -DSODIUM_INCLUDE_DIRS="$(pwd)/../install/include" \
  -DCMAKE_CXX_FLAGS="-march=native"

cd out/build/linux
cmake --install . --prefix "$(pwd)/../../../../install"
```

其中 `-DSODIUM_MONTGOMERY=OFF` 至关重要：cryptoTools 将该选项默认设为 `ON`，并以 FORCE 方式覆盖缓存，这会引入调用 `crypto_scalarmult_noclamp` 的 Montgomery 曲线代码。本项目并不需要该功能，因此保持关闭。

`-DSODIUM_LIBRARIES` 与 `-DSODIUM_INCLUDE_DIRS` 显式指向 `external/install` 中的副本。若省略这两个参数，cryptoTools 会按默认搜索路径查找，可能链接到系统静态库，从而与 SimplestOT 的 `ge25519_*` 符号产生重定义冲突。

#### 4. secure-join

```bash
cd external/secure-join
cmake -S . -B out/build/linux \
  -D CMAKE_BUILD_TYPE=Release \
  -D SODIUM_MONTGOMERY=false \
  -D COPROTO_ENABLE_BOOST=ON \
  -DFETCH_LIBOTE=OFF \
  -DSECUREJOIN_THIRDPARTY_DIR="$(pwd)/../install" \
  -DCMAKE_PREFIX_PATH="$(pwd)/../install" \
  -DSODIUM_LIBRARIES="$(pwd)/../install/lib/libsodium.so" \
  -DSODIUM_INCLUDE_DIRS="$(pwd)/../install/include"

cmake --build out/build/linux --parallel "$(nproc)"
cd out/build/linux
cmake --install . --prefix "$(pwd)/../../../../install"
```

`-DFETCH_LIBOTE=OFF` 使其复用上文已安装的 libOTe。若不加此参数，secure-join 会在自身 `out/` 目录下克隆并另行构建一份 libOTe。

### 2PC_eq_cmp_v2

`2PC_eq_cmp_v2` 目录包含我们改进版的两方安全相等性测试与比较协议，提供 PSO 流程中使用的 `eq2<T>` 与 `cmp1<T>` 模板类。

该库依赖 libOTe（已按上文构建并安装至 `external/install`）。其 `CMakeLists.txt` 中的 `CMAKE_PREFIX_PATH` 已预配置为在 `${CMAKE_SOURCE_DIR}/external/install/` 下查找 libOTe。

**独立构建：**

```bash
cd external/2PC_eq_cmp_v2
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

**运行独立相等性测试（需要两个终端）：**

```bash
# 终端 1（发送方）
./build/eq_cmp -sender -n 100 -l 4 -c 0

# 终端 2（接收方）
./build/eq_cmp -receiver -n 100 -l 4 -c 0
```

**运行独立比较测试：**

```bash
# 终端 1（发送方）
./build/eq_cmp -sender -n 100 -l 4 -c 1

# 终端 2（接收方）
./build/eq_cmp -receiver -n 100 -l 4 -c 1
```

**命令行参数：**

| 参数 | 说明 |
|------|------|
| `-sender` | 以发送方身份运行 |
| `-receiver` | 以接收方身份运行 |
| `-n <num>` | 参与比较的元素个数 |
| `-l <bits>` | 每个元素的比特长度 |
| `-c <type>` | `0` = 相等性测试，`1` = 比较 |
| `-ip <addr>` | IP 地址（默认 `localhost:1213`） |

> **注意**：在主 PSO 流程（`pbsspmt/test/test_flow.cpp`）中，`neweq.h` 里的 `eq2` 协议是被直接包含使用，而非作为独立库链接。上述独立构建适用于单独测试相等性与比较协议。

### 构建本项目

顶层 `CMakeLists.txt` 将 `external/install` 加入 `CMAKE_PREFIX_PATH`，并把 `SODIUM_LIBRARIES` / `SODIUM_INCLUDE_DIRS` 预设为该目录下的副本，从而避免 `find_package` 静默回退到系统 libsodium。若该副本缺失，配置阶段会打印警告并指明所回退到的系统库。

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
```

### CPU 指令集与 AVX-512

`external/blake3` 始终以 SSE2/AVX2 编译，AVX-512 仅对确实需要的那一个源文件启用。部分较新的消费级 CPU（例如 Intel Arrow Lake）根本不支持 AVX-512 指令集。GCC 会为这类 CPU 正常编译 AVX-512 代码且不给任何提示，但**执行**时会触发 `SIGILL`。

因此，顶层 `CMakeLists.txt` 在配置阶段通过 `__builtin_cpu_supports` 探测 CPU 能力，该内建函数仅读取 CPUID，不会执行任何 AVX-512 指令。此处仅做编译期检查是不够的——CMake 的 `check_cxx_source_runs` 在 AVX-512 实际会出错的 CPU 上也可能报告成功。当 CPU 不支持 AVX-512 时，BLAKE3 的运行时分发逻辑不会选中该代码路径。

若在 `ctest` 中看到 `***Exception: Illegal`，应首先排查此原因。

## 测试

单元测试与基准测试位于 `pbsspmt/test`，可通过 CTest 运行。OpenMP 线程数在运行时指定，便于评估并行度对性能的影响。

```bash
cd build

# 运行全部测试
ctest --output-on-failure

# 以 4 个 OpenMP 线程运行指定测试
OMP_NUM_THREADS=4 ctest -R workflow_test -V
```

已注册的测试包括：`oprf_gf128_test`、`oprf_okvs_basic_test`、`oprf_test`、`cuckoo3_test`、`workflow_test`。需要注意，这些测试并不覆盖置换关联生成器——它位于 secure-join 中，需单独测量，详见下节。

## 测量 PermCG

置换关联生成器（Permutation Correlation Generator，PermCG）来自 secure-join（`secure-join/secure-join/Perm/AltModPerm.h`），而非本仓库，因此其性能通过 secure-join 自带的测试程序 `secJoinfrontend` 测量。`build_deps.sh` 会一并构建该程序，无需额外步骤。

可执行文件位于 secure-join 的构建目录：

```
external/secure-join/out/build/linux/frontend/secJoinfrontend
```

### 正确性检验

`AltModPerm_setup_test` 在本地套接字对上完整运行生成器并校验置换关联，应输出 `Passed`：

```bash
cd external/secure-join/out/build/linux/frontend
./secJoinfrontend -u AltModPerm_setup_test
```

### 性能基准

基准程序输出实际耗时与每次求值的通信量：

```bash
cd external/secure-join/out/build/linux/frontend
./secJoinfrontend -bench -AltModPerm -nn 20
```

输出格式为 `n <规模>, nt <线程数>, <耗时>ms <发送>+<接收>=<合计> bytes/eval`：

```
AltModPerm: n 1048576, nt 1, 1184ms 192.031+64.0313=256.063 bytes/eval
```

| 参数 | 含义 | 默认值 |
|---|---|---|
| `-nn <log2n>` | 置换规模的以 2 为底对数 | `10` |
| `-n <size>` | 直接指定置换规模（优先于 `-nn`） | `1 << nn` |
| `-m <bytes>` | 每个元素的行大小 | `16` |
| `-b <bits>` | OLE 批量大小的以 2 为底对数（`1 << b`） | `16` |
| `-nt <threads>` | 每一方的线程数 | `1` |

其中 `-b` 控制 OLE 的批量大小，而非被置换数值的位宽；后者请使用 `-m`。

### 参考结果

测试环境：Ubuntu 24.04（WSL2）、Intel Core Ultra 5 225H、GCC 13.3，除注明外均为单线程。每次求值的通信量与 `n` 无关。

| `-nn` | n | 耗时 | bytes/eval |
|---|---|---|---|
| 16 | 65,536 | 48 ms | 256.066 |
| 18 | 262,144 | 211 ms | 256.063 |
| 20 | 1,048,576 | 1113–1184 ms | 256.063 |
| 22 | 4,194,304 | 11555 ms | 256.063 |
| 20（`-nt 4`） | 1,048,576 | 1023 ms | 256.063 |

上述数值仅为单机粗略基线，不宜视为绝对值。

### 与工作流参数设置对齐

`pbsspmt/test/test_flow.cpp` 在规模 `n = 1 << 20` 下运行。若要在同一参数设置下报告 PermCG 开销，应使用 `-nn 20`。需要注意的是，当前工作流代码并未实际调用 PermCG（`AltModPerm` 仅出现在注释中），因此该基准是这一子协议的独立测量结果。

## 许可证

本项目采用 **Apache License 2.0** 许可。您可以在遵守该许可的前提下使用、分发与修改本软件。

详见仓库内 LICENSE 文件全文，或访问：
<http://www.apache.org/licenses/LICENSE-2.0>
