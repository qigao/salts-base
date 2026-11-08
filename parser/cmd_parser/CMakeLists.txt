set(TARGET_NAME cmd_parser)

cmake_add_grammar(CMD_ARGER
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_arger.re
  LEXER_OUTPUT cmd_arger_gen.c
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

add_library(${TARGET_NAME} STATIC
    ${CMD_ARGER_LEXER_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_arger.c
)
add_dependencies(${TARGET_NAME} ${CMD_ARGER_LEXER_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::CmdParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME CmdParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${TARGET_NAME} PRIVATE Salts::DotEnvParser)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
