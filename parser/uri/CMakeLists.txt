cmake_add_grammar(uri_parser
  LEXER_RE parser/uri_lexer.re
  LEXER_OUTPUT uri_lexer_gen.c
  LEXER_DEPENDS parser/uri_parser_internal.h
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  FOLDER "parser/${TARGET_NAME}/")
cmake_add_source(uri_sources DIRS src  parser PATTERNS "*.c" "*.h" "*.re")
cmake_add_source(uri_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND uri_sources ${uri_sources_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${uri_sources})
source_group("Generated" FILES ${uri_parser_LEXER_GEN})
add_library(uri_parser STATIC ${uri_sources} ${uri_parser_LEXER_GEN})
cmake_config_target(uri_parser
  ALIAS Salts::URI EXPORT_NAME URI FOLDER "parser/uri")
set_target_properties(uri_parser PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_compile_features(uri_parser PUBLIC c_std_11)
target_include_directories(uri_parser
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/parser")
install(TARGETS uri_parser EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
