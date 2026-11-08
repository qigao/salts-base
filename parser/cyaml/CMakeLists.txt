set(TARGET_NAME cyaml)
set(TARGET_FOLDER "parser/${TARGET_NAME}")

cmake_add_grammar(CYAML_YPATH
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/cyaml_ypath_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/src/cyaml_ypath_expr_grammar.y
  LEXER_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cyaml_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cyaml_ypath_lexer.h
  FOLDER "${TARGET_FOLDER}/grammar")

set(CYAML_SOURCES
  src/cyaml.c
  src/cyaml_utf8.c
  src/cyaml_parser.c
  src/cyaml_emitter.c
  src/cyaml_events.c
  src/cyaml_json.c
  src/cyaml_path.c
  src/cyaml_modify.c
  ${CYAML_YPATH_LEXER_GEN}
  ${CYAML_YPATH_GRAMMAR_GEN})

if(MSVC)
  set_source_files_properties(${CYAML_YPATH_GRAMMAR_GEN}
    PROPERTIES COMPILE_OPTIONS /wd4189)
endif()

add_library(${TARGET_NAME} STATIC ${CYAML_SOURCES})
add_dependencies(${TARGET_NAME}
  ${CYAML_YPATH_LEXER_TARGET}
  ${CYAML_YPATH_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::CYaml
  EXPORT_NAME CYaml
  FOLDER "${TARGET_FOLDER}")
add_library(cyaml::cyaml ALIAS ${TARGET_NAME})
target_link_libraries(${TARGET_NAME} PRIVATE Salts::Core PUBLIC Salts::QueryVM)

target_include_directories(${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/cyaml>
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(${TARGET_NAME}
  PRIVATE CYAML_VERSION_STR="${PROJECT_VERSION}")

if(UNIX AND NOT APPLE)
  target_link_libraries(${TARGET_NAME} PRIVATE m)
endif()

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/cyaml.h DESTINATION include/cyaml)

# JSON support is an adapter target so the CYAML core stays independent.
add_library(cyaml_json_adapter STATIC
  adapter/cyaml_json_adapter.c
  adapter/cyaml_json_adapter.h)
target_include_directories(cyaml_json_adapter
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapter>
         $<INSTALL_INTERFACE:include/cyaml>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cyaml_json_adapter PUBLIC ${TARGET_NAME} json_parser)
cmake_config_target(cyaml_json_adapter
  ALIAS Salts::CYamlJsonAdapter
  EXPORT_NAME CYamlJsonAdapter
  FOLDER "${TARGET_FOLDER}/adapter")

if(BUILD_TESTS)
  cmake_add_test(test_api
    SOURCES tests/test_api.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")

  cmake_add_test(test_sax
    SOURCES tests/test_sax.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")

  cmake_add_test(test_ypath_lexer
    SOURCES tests/test_ypath_lexer.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src
    FOLDER "${TARGET_FOLDER}/tests")

  cmake_add_test(test_ypath_ast
    SOURCES tests/test_ypath_ast.c
    LIBS ${TARGET_NAME} Salts::QueryVM Salts::TinyTest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src
    FOLDER "${TARGET_FOLDER}/tests")

  cmake_add_test(test_cyaml_json_adapter
    SOURCES tests/test_json_adapter.c
    LIBS cyaml_json_adapter Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")

  cmake_add_test(ypath_test
    SOURCES tests/ypath_test.c
    LIBS ${TARGET_NAME}
    DEFS CYAML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FOLDER "${TARGET_FOLDER}/tests")
  set_tests_properties(ypath_test PROPERTIES LABELS "cyaml;ypath")

  foreach(TOOL cyaml_suite dump_debug parse_debug emit_debug json_debug
               results_query ypath_debug summary_gen)
    if(TOOL STREQUAL "cyaml_suite")
      set(TOOL_SOURCE tests/suite_runner.c)
    else()
      set(TOOL_SOURCE tests/${TOOL}.c)
    endif()

    cmake_add_executable(${TOOL}
      SOURCES ${TOOL_SOURCE}
      LIBS ${TARGET_NAME}
      FOLDER "${TARGET_FOLDER}/tools")
  endforeach()
endif()

if(BUILD_BENCHMARKS AND NOT WIN32)
  cmake_add_benchmark(benchmark
    SOURCES tests/benchmark.c
    LIBS ${TARGET_NAME}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src
    DEFS CYAML_VERSION_STR="${PROJECT_VERSION}"
    FOLDER "${TARGET_FOLDER}/benchmarks")
endif()
