set(TARGET_NAME dotenv_parser)

cmake_add_grammar(DOTENV
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/dotenv_lexer.re
  LEXER_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dotenv_lexer.h
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

cmake_add_source(dotenv_sources DIRS src)
cmake_add_source(dotenv_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND dotenv_sources ${dotenv_sources_headers})
add_library(${TARGET_NAME} STATIC ${dotenv_sources} ${DOTENV_LEXER_GEN})
add_dependencies(${TARGET_NAME} ${DOTENV_LEXER_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::DotEnvParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME DotEnvParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

target_link_libraries(${TARGET_NAME} PUBLIC Salts::Core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
