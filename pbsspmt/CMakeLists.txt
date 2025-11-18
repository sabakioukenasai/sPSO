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

# ---- Add test directory ----
add_subdirectory(test)