set(TARGET_NAME ini_parser)

# Generated files
cmake_add_grammar(INI
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/ini_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/ini_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# Library
cmake_add_source(SOURCE_FILES DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${INI_LEXER_GEN}
                                  ${INI_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${INI_LEXER_TARGET} ${INI_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::IniParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME IniParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PUBLIC Salts::Core)

# Suppress warnings for generated code
if(MSVC)
  target_compile_options(
    ${TARGET_NAME}
    PRIVATE /wd4101 # Unreferenced local variable
            /wd4102 # Unreferenced label
            /wd4244 # Conversion, possible loss of data
            /wd4267 # Conversion from size_t
            /wd4996 # Deprecated functions
  )
else()
  target_compile_options(
    ${TARGET_NAME} PRIVATE -Wno-unused-parameter -Wno-unused-function
                           -Wno-unused-variable -Wno-unused-but-set-variable)
endif()

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
