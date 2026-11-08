# Provider-neutral SaltsCore codec; package and language setup belong to the root.
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(salts_asn1 STATIC ${module_sources})
cmake_config_target(salts_asn1 ALIAS Salts::ASN1 FOLDER "asn1")
cmake_config_target(salts_asn1
  OUTPUT_NAME salts_asn1
  EXPORT_NAME ASN1)
set_target_properties(salts_asn1 PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(salts_asn1 PUBLIC c_std_11)
target_include_directories(salts_asn1 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS salts_asn1 EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "asn1*.h"
  PATTERN "salts" EXCLUDE)
# Tests use the root-owned TinyTest and CTest graph.
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
