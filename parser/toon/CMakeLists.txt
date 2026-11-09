set(TARGET_NAME toon)

cmake_add_grammar(TOON
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET Lemon::Lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/toon_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/toon_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

cmake_add_source(toon_sources DIRS . adapter)
cmake_add_source(toon_headers RECURSE DIRS include PATTERNS "*.h")
list(APPEND toon_sources ${toon_headers})
add_library(${TARGET_NAME} STATIC ${toon_sources}
  ${TOON_LEXER_GEN} ${TOON_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${TOON_LEXER_TARGET} ${TOON_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::Toon
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME Toon)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_SOURCE_DIR}/..
          ${CMAKE_CURRENT_BINARY_DIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

# Link dependencies
target_link_libraries(${TARGET_NAME}
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
