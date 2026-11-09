cmake_add_grammar(saltscore_uri
  LEXER_RE parser/uri_lexer.re
  LEXER_OUTPUT uri_lexer_gen.c
  LEXER_DEPENDS parser/uri_parser_internal.h
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  FOLDER "cnet/uri")
cmake_add_source(uri_sources DIRS src include parser PATTERNS "*.c" "*.h" "*.re")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${uri_sources})
source_group("Generated" FILES ${saltscore_uri_LEXER_GEN})
add_library(saltscore_uri STATIC ${uri_sources} ${saltscore_uri_LEXER_GEN})
cmake_config_target(saltscore_uri
  ALIAS Salts::URI EXPORT_NAME URI FOLDER "cnet/uri")
set_target_properties(saltscore_uri PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_compile_features(saltscore_uri PUBLIC c_std_11)
target_include_directories(saltscore_uri
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/parser")
install(TARGETS saltscore_uri EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/uri_parser.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
