cmake_add_grammar(ASN1_SCHEMA
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET Lemon::Lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE parser/scanner.re
  GRAMMAR_Y parser/parser.y
  LEXER_DEPENDS_ON_GRAMMAR
  FOLDER "asn1/grammar")

cmake_add_source(module_sources DIRS src)
cmake_add_source(module_sources_headers RECURSE DIRS include PATTERNS "*.h"
                 "*.hpp")
list(APPEND module_sources ${module_sources_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
list(APPEND module_sources ${ASN1_SCHEMA_LEXER_GEN} ${ASN1_SCHEMA_GRAMMAR_GEN})
add_library(asn1 STATIC ${module_sources})
add_dependencies(asn1 ${ASN1_SCHEMA_LEXER_TARGET} ${ASN1_SCHEMA_GRAMMAR_TARGET})
cmake_config_target(asn1 ALIAS Salts::ASN1 FOLDER "asn1"
  EXPORT_NAME ASN1)
set_target_properties(asn1 PROPERTIES C_EXTENSIONS OFF)
target_compile_features(asn1 PUBLIC c_std_11)
target_link_libraries(asn1 PRIVATE Salts::SIMD Salts::Core)
target_include_directories(asn1 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
install(TARGETS asn1 EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
cmake_add_executable(mib_compiler SOURCES tools/mib_compiler_main.c
  LIBS Salts::ASN1 Salts::Core INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src"
  FOLDER "asn1/tools")
install(TARGETS mib_compiler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(BUILD_EXAMPLES)
  cmake_add_executable(asn1_schema_example SOURCES examples/schema.c
    LIBS Salts::ASN1 FOLDER "asn1/examples")
endif()
# Tests use the root-owned TinyTest and CTest graph.
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
