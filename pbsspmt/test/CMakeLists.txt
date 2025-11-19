# Enable testing for this directory
enable_testing()

# ---- Build test executables ----
add_executable(test_oprf_gf128 test_galois128.cpp)
add_executable(test_oprf_okvs_basic test_okvs_basic.cpp)
add_executable(test_oprf test_oprf.cpp)
add_executable(test_cuckoo3 test_cuckoo3.cpp)
add_executable(test_workflow test_flow.cpp)
add_executable(test_permcg test_permcg.cpp)

# ---- Link libraries ----
target_link_libraries(test_oprf_gf128 PRIVATE pbsspmt)
target_link_libraries(test_oprf_okvs_basic PRIVATE pbsspmt)
target_link_libraries(test_oprf PRIVATE pbsspmt)
target_link_libraries(test_cuckoo3 PRIVATE pbsspmt)
target_link_libraries(test_workflow PRIVATE pbsspmt)
target_link_libraries(test_permcg PRIVATE pbsspmt secureJoin)

# ---- Register tests ----
add_test(NAME oprf_gf128_test COMMAND test_oprf_gf128)
add_test(NAME oprf_okvs_basic_test COMMAND test_oprf_okvs_basic)
add_test(NAME oprf_test COMMAND test_oprf)
add_test(NAME cuckoo3_test COMMAND test_cuckoo3)
add_test(NAME workflow_test COMMAND test_workflow)
add_test(NAME permcg_test COMMAND test_permcg)
