set(TARGET_NAME selector)
set(TARGET_FOLDER "parser/selector")

cmake_add_grammar(SELECTOR_V1
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET Lemon::Lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/selector_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/selector_grammar.y
  LEXER_DEPENDS_ON_GRAMMAR
  FOLDER "${TARGET_FOLDER}/grammar")

cmake_add_source(selector_sources DIRS src)
cmake_add_source(selector_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND selector_sources ${selector_sources_headers})
add_library(${TARGET_NAME} STATIC ${selector_sources}
  ${SELECTOR_V1_LEXER_GEN} ${SELECTOR_V1_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME}
  ${SELECTOR_V1_LEXER_TARGET}
  ${SELECTOR_V1_GRAMMAR_TARGET})
target_include_directories(${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/parser
    ${CMAKE_CURRENT_BINARY_DIR})
cmake_config_target(${TARGET_NAME}
  ALIAS Salts::Selector
  EXPORT_NAME Selector
  FOLDER "${TARGET_FOLDER}")

install(TARGETS ${TARGET_NAME}
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  cmake_add_test(test_selector
    SOURCES test/test_selector.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")
endif()
