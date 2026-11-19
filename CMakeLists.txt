cmake_minimum_required(VERSION 3.15)
project(sPSO C CXX)

enable_testing()

# ===== Compiler Settings =====
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common flags
set(COMMON_FLAGS "-Wall -Wfatal-errors -Wno-unused-variable -fopenmp -mavx2 -mpclmul -msse2 -O3")

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -fcoroutines")

# ===== Dependency isolation =====
# Everything the build needs is expected to live under external/install, so a
# build does not depend on whichever boost / libsodium the host distribution
# happens to ship. That keeps builds reproducible across machines and avoids
# linking against APIs the system copy may not have.
set(THIRDPARTY_INSTALL_DIR "${CMAKE_SOURCE_DIR}/external/install")
list(APPEND CMAKE_PREFIX_PATH "${THIRDPARTY_INSTALL_DIR}")

# --- libsodium ---------------------------------------------------------
# cryptoTools' own FIND_SODIUM() searches the system path and will happily pick
# up /usr/lib/.../libsodium.so (e.g. 1.0.18 on Ubuntu 24.04). Preset the two
# cache entries to the copy in external/install so the find_* calls inside
# cryptoTools are skipped. Passing -DSODIUM_LIBRARIES=... on the command line
# still overrides this, because we only fill in values that are unset.
if(EXISTS "${THIRDPARTY_INSTALL_DIR}/include/sodium.h"
   AND NOT SODIUM_LIBRARIES AND NOT SODIUM_INCLUDE_DIRS)
    find_library(_sPSO_SODIUM_LIB NAMES sodium
        PATHS "${THIRDPARTY_INSTALL_DIR}/lib" NO_DEFAULT_PATH)
    if(_sPSO_SODIUM_LIB)
        set(SODIUM_INCLUDE_DIRS "${THIRDPARTY_INSTALL_DIR}/include" CACHE PATH
            "libsodium include directory" FORCE)
        set(SODIUM_LIBRARIES "${_sPSO_SODIUM_LIB}" CACHE FILEPATH
            "libsodium library" FORCE)
        message(STATUS "libsodium: using bundled copy at ${THIRDPARTY_INSTALL_DIR}")
    else()
        message(WARNING
            "external/install/include/sodium.h exists but no libsodium library "
            "was found in external/install/lib. Falling back to the system copy.")
    endif()
    unset(_sPSO_SODIUM_LIB CACHE)
endif()

# --- SODIUM_MONTGOMERY -------------------------------------------------
# cryptoTools defaults this to ON and force-overrides the cache, which pulls in
# Montgomery-curve code calling crypto_scalarmult_noclamp() -- an API that only
# exists in libsodium >= 1.0.19. Nothing in this project needs McRosRoyTwist,
# so keep it off. Note cryptoTools would otherwise reset this via FORCE, so the
# value is asserted again after find_package below.
set(SODIUM_MONTGOMERY OFF CACHE BOOL "Disable Montgomery sodium curves" FORCE)

# Let the binaries locate shared libraries shipped under external/install
# (libsodium in particular) without exporting LD_LIBRARY_PATH before each run.
set(CMAKE_BUILD_RPATH "${THIRDPARTY_INSTALL_DIR}/lib")
set(CMAKE_INSTALL_RPATH "${THIRDPARTY_INSTALL_DIR}/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
set(CMAKE_SKIP_BUILD_RPATH OFF)

# ===== Third-party packages =====
find_package(libOTe REQUIRED)
find_package(cryptoTools REQUIRED)
find_package(coproto REQUIRED)
find_package(secureJoin REQUIRED)

# cryptoTools/secure-join set these unconditionally; re-assert after the
# find_package calls so the isolated settings actually stick.
set(SODIUM_MONTGOMERY OFF CACHE BOOL "Disable Montgomery sodium curves" FORCE)
if(SODIUM_LIBRARIES AND NOT SODIUM_LIBRARIES MATCHES "^${THIRDPARTY_INSTALL_DIR}/")
    message(WARNING
        "libsodium resolved to ${SODIUM_LIBRARIES}, which is outside "
        "external/install. Builds may not be reproducible across machines.")
endif()

# Disable auto fetching libOTe for secure-join
set(FETCH_LIBOTE OFF CACHE BOOL "Disable auto fetching libOTe")
set(SECUREJOIN_THIRDPARTY_DIR "${THIRDPARTY_INSTALL_DIR}")
set(SECUREJOIN_THIRDPARTY_CLONE_DIR "${THIRDPARTY_INSTALL_DIR}")

# --- Add BLAKE3 ---
add_library(blake3 STATIC
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_dispatch.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_portable.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_sse2.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_sse41.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_avx2.c
    ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_avx512.c
)
# optional: enable SIMD (auto-detected at configure time)
# NOTE: some recent consumer CPUs (e.g. Intel Arrow Lake) lack AVX-512. gcc
# still compiles AVX-512 code for them without complaint, but executing it
# raises SIGILL -- which shows up only at runtime, as "Illegal instruction"
# from ctest. Hence the explicit CPUID probe below.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(blake3 PRIVATE -msse2 -mavx2)

    # Probe via CPUID (__builtin_cpu_supports) without executing any AVX-512
    # instruction. check_cxx_source_runs is unreliable here: on some CPUs the
    # test executable is reported as succeeding even though AVX-512 faults.
    include(CheckCXXSourceRuns)
    check_cxx_source_runs("
        int main(){ return __builtin_cpu_supports(\"avx512f\") ? 0 : 1; }
    " CPU_SUPPORTS_AVX512)

    # blake3_avx512.c needs the AVX-512 ISA to assemble at all, so those flags
    # are applied per-source-file. blake3 dispatches at runtime and only calls
    # into this code when the CPU reports support, so building it is safe even
    # on CPUs that cannot execute it.
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/external/blake3/c/blake3_avx512.c
        PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512vl"
    )
    if (CPU_SUPPORTS_AVX512)
        message(STATUS "blake3: AVX-512 runnable on this CPU, enabled")
    else()
        message(STATUS "blake3: AVX-512 not runnable on this CPU (runtime dispatch will skip it)")
    endif()
endif()

target_include_directories(blake3 PUBLIC
    ${CMAKE_SOURCE_DIR}/external/blake3/c
)

# our pbssPMT library
add_subdirectory(pbsspmt)
