cmake_minimum_required(VERSION 3.15)
project(MyProj C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON_FLAGS "-Wall -Wfatal-errors -Wno-unused-variable -fcoroutines -std=c++20 -fopenmp -mavx2 -mpclmul -msse2 -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

set(LIBOTE_INSTALL_DIR "${CMAKE_SOURCE_DIR}/external/install")
list(APPEND CMAKE_PREFIX_PATH "${LIBOTE_INSTALL_DIR}")

find_package(libOTe REQUIRED)
find_package(cryptoTools REQUIRED)
# find_package(coproto REQUIRED)

include_directories(
    ${LIBOTE_INSTALL_DIR}/include
)

get_property(allTargets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
message(STATUS "Targets in this directory: ${allTargets}")

# Disable auto fetching libOTe for secure-join
set(FETCH_LIBOTE OFF CACHE BOOL "Disable auto fetching libOTe")
set(SECUREJOIN_THIRDPARTY_DIR "${LIBOTE_INSTALL_DIR}")
set(SECUREJOIN_THIRDPARTY_CLONE_DIR "${LIBOTE_INSTALL_DIR}")

add_subdirectory(external/secure-join)

if(NOT TARGET coproto)
    message(STATUS "add target coproto manually")

    set(BOOST_ROOT "${LIBOTE_INSTALL_DIR}")
    set(BOOST_INCLUDEDIR "${LIBOTE_INSTALL_DIR}/include")
    set(BOOST_LIBRARYDIR "${LIBOTE_INSTALL_DIR}/lib")
    find_package(Boost REQUIRED COMPONENTS system)

    add_library(coproto INTERFACE)
    target_include_directories(coproto INTERFACE
        ${CMAKE_SOURCE_DIR}/external/secure-join/out/coproto
    )
    target_compile_definitions(coproto INTERFACE COPROTO_ENABLE_BOOST)
    target_link_libraries(coproto INTERFACE 
        oc::cryptoTools
        pthread
        ssl
        crypto
        Boost::system
    )
endif()

# our pbssPMT implementation
add_subdirectory(pbsspmt)

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
# optional: enable SIMD (auto-detected)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(blake3 PRIVATE -msse2 -mavx2 -mavx512f -mavx512vl)
endif()

target_include_directories(blake3 PUBLIC
    ${CMAKE_SOURCE_DIR}/external/blake3/c
)

# add_executable(my_app main.cpp)

# target_include_directories(my_app PRIVATE
#     ${CMAKE_SOURCE_DIR}/external/secure-join
#     ${CMAKE_SOURCE_DIR}/external/secure-join/out
#     # ${CMAKE_SOURCE_DIR}/external/secure-join/out/coproto/
#     ${CMAKE_SOURCE_DIR}/external/secure-join/out/coproto/coproto
# )

# target_compile_definitions(my_app PRIVATE COPROTO_ENABLE_BOOST)

# target_sources(my_app PRIVATE
#     ${CMAKE_SOURCE_DIR}/external/secure-join/out/coproto/coproto/Socket/AsioSocket.cpp
# )

# target_link_libraries(my_app PRIVATE 
#     secureJoin
#     coproto
#     Boost::system
#     # oc::cryptoTools
#     # oc::libOTe
#     oprf
# )