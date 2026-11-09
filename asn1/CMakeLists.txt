# Provider-neutral SaltsCore codec; package and language setup belong to the root.
cmake_add_source(module_sources DIRS src)
cmake_add_source(module_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND module_sources ${module_sources_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(asn1 STATIC ${module_sources})
cmake_config_target(asn1 ALIAS Salts::ASN1 FOLDER "asn1")
cmake_config_target(asn1
  OUTPUT_NAME asn1
  EXPORT_NAME ASN1)
set_target_properties(asn1 PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(asn1 PUBLIC c_std_11)
target_link_libraries(asn1 PRIVATE Salts::SIMD)
target_include_directories(asn1 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS asn1 EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# Tests use the root-owned TinyTest and CTest graph.
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
