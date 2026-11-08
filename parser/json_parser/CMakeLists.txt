set(TARGET_NAME json_parser)

# Generated files
cmake_add_grammar(JSON
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/json_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/json_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

cmake_add_grammar(JSONPATH
  LEMON_TARGET lemon
  LEMON_TEMPLATE "${LEMPAR}"
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/jsonpath_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# Library
cmake_add_source(SOURCE_FILES DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)
list(REMOVE_ITEM SOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/json_cserde_reader.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/json_cserde_writer.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${JSON_LEXER_GEN}
                                  ${JSON_GRAMMAR_GEN} ${JSONPATH_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${JSON_LEXER_TARGET} ${JSON_GRAMMAR_TARGET}
                               ${JSONPATH_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS Salts::JsonParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME JsonParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR})
find_path(SIMDE_INCLUDE_DIRS "simde/arm/neon.h")
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})
# JsonParser owns its private object-key index; the removed CSTL library is
# neither a provider nor a valid Salts 3 installed dependency.
target_link_libraries(${TARGET_NAME}
  PUBLIC Salts::Core Salts::QueryVM)

add_library(json_cserde_adapter STATIC
  src/json_cserde_reader.c
  src/json_cserde_writer.c)
cmake_config_target(json_cserde_adapter
  ALIAS Salts::JsonCSerdeAdapter
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME JsonCSerdeAdapter)
target_include_directories(json_cserde_adapter
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser)
target_link_libraries(json_cserde_adapter
  PUBLIC Salts::JsonParser Salts::CSerde)

if(BUILD_TESTS)
  get_target_property(JSON_PARSER_PUBLIC_LINKS
                      ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
  if(JSON_PARSER_PUBLIC_LINKS MATCHES "CSerde")
    message(FATAL_ERROR
      "Salts::JsonParser must not expose the CSerde adapter dependency")
  endif()
  if(NOT TARGET json_cserde_adapter)
    message(FATAL_ERROR
      "The JSON CSerde adapter must be a separate target")
  endif()
  get_target_property(JSON_CSERDE_ADAPTER_PUBLIC_LINKS
                      json_cserde_adapter INTERFACE_LINK_LIBRARIES)
  if(NOT JSON_CSERDE_ADAPTER_PUBLIC_LINKS STREQUAL
     "Salts::JsonParser;Salts::CSerde")
    message(FATAL_ERROR
      "Salts::JsonCSerdeAdapter dependency contract changed")
  endif()
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
