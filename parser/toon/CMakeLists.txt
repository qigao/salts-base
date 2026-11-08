set(TARGET_NAME toon)

cmake_add_grammar(TOON
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/toon_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/toon_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

add_library(${TARGET_NAME} STATIC
  toonc.c
  adapter/toon_json_adapter.c
  ${TOON_LEXER_GEN}
  ${TOON_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${TOON_LEXER_TARGET} ${TOON_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::Toon
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME Toon)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapter>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_SOURCE_DIR}/..
          ${CMAKE_CURRENT_BINARY_DIR}
)
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

# Link dependencies
target_link_libraries(${TARGET_NAME}
  PRIVATE CMeta::CSTL
  PUBLIC Salts::Core Salts::JsonParser)
if(UNIX)
  target_link_libraries(${TARGET_NAME} PRIVATE m)
endif()

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)


  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# =============================================================================
# Examples
# =============================================================================

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
