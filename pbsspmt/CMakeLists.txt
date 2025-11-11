# pbsspmt/CMakeLists.txt
add_library(pbsspmt STATIC
    src/oprf.cpp
    src/galois128.cpp
    src/okvsbk.cpp
    src/cuckoo3.cpp
)

target_link_libraries(pbsspmt PUBLIC
    oc::libOTe
    oc::cryptoTools
    blake3
)

target_include_directories(pbsspmt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---- Link to OpenMp ----
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found: enabling")
  target_link_libraries(pbsspmt PUBLIC OpenMP::OpenMP_CXX)
  target_compile_definitions(pbsspmt PUBLIC HAVE_OPENMP=1)
else()
  message(STATUS "OpenMP not found: building without it")
  target_compile_definitions(pbsspmt PUBLIC HAVE_OPENMP=0)
endif()


# ---- Testing part ----
add_executable(test_oprf_gf128
    test/test_galois128.cpp
)

add_executable(test_oprf_okvs_basic
    test/test_okvs_basic.cpp
)

add_executable(test_oprf
    test/test_oprf.cpp
)

add_executable(test_cuckoo3
    test/test_cuckoo3.cpp
)

add_executable(test_workflow
    test/test_flow.cpp
)

target_link_libraries(test_oprf_gf128 PRIVATE
    pbsspmt
)

target_link_libraries(test_oprf_okvs_basic PRIVATE
    pbsspmt
)

target_link_libraries(test_oprf PRIVATE
    pbsspmt
)

target_link_libraries(test_cuckoo3 PRIVATE
    pbsspmt
)

target_compile_definitions(test_workflow PRIVATE COPROTO_ENABLE_BOOST ENABLE_SIMPLESTOT)

target_sources(test_workflow PRIVATE
    ${CMAKE_SOURCE_DIR}/external/secure-join/out/coproto/coproto/Socket/AsioSocket.cpp
    ${CMAKE_SOURCE_DIR}/external/libOTe/libOTe/Base/SimplestOT.cpp
)

target_link_libraries(test_workflow PRIVATE
    coproto
    pbsspmt
)

add_test(NAME oprf_gf128_test COMMAND test_oprf_gf128)
add_test(NAME oprf_okvs_basic_test  COMMAND test_oprf_okvs_basic)
add_test(NAME oprf_test  COMMAND test_oprf)
add_test(NAME cuckoo3_test  COMMAND test_cuckoo3)
add_test(NAME workflow_test COMMAND test_workflow)