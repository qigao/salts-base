set(TARGET_NAME toml_parser)

cmake_add_grammar(TOML
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/toml_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/toml_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Static library (independent of BUILD_SHARED_LIBS)
# =============================================================================

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${TOML_LEXER_GEN}
                                  ${TOML_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${TOML_LEXER_TARGET} ${TOML_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::TomlParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME TomlParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/parser
          ${CMAKE_CURRENT_SOURCE_DIR}/..
          ${CMAKE_CURRENT_BINARY_DIR})
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

# =============================================================================
# Tools / Examples (Optional)
# =============================================================================

if(BUILD_EXAMPLES)
  cmake_add_executable(toml2json
    SOURCES tools/toml2json.c
    LIBS Salts::TomlParser
    FOLDER "parser/${TARGET_NAME}/examples")
endif()

if(BUILD_TESTS)
  cmake_add_executable(toml-c-test
    SOURCES tools/toml-c-test.c
    LIBS Salts::TomlParser
    FOLDER "parser/${TARGET_NAME}/tests")

  cmake_add_test(test_toml
    SOURCES tests/test_toml.c
    LIBS Salts::TomlParser Salts::TinyTest
    FOLDER "parser/${TARGET_NAME}/tests")
endif()
