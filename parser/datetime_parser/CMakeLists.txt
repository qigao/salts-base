set(TARGET_NAME datetime_parser)

cmake_add_grammar(DATETIME
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/datetime_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/datetime_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${DATETIME_LEXER_GEN}
                                  ${DATETIME_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${DATETIME_LEXER_TARGET}
                 ${DATETIME_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::DateTimeParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME DateTimeParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
