set(TARGET_NAME cron)

set(CRON_LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/cron_lexer.re)
set(CRON_LEXER_GEN ${CMAKE_CURRENT_BINARY_DIR}/cron_lexer_gen.c)

add_custom_command(
  OUTPUT ${CRON_LEXER_GEN}
  COMMAND ${RE2C_EXECUTABLE} -o ${CRON_LEXER_GEN} ${CRON_LEXER_RE}
  DEPENDS ${CRON_LEXER_RE} ${CMAKE_CURRENT_SOURCE_DIR}/src/cron_lexer.h
  COMMENT "Generating cron lexer with re2c"
  VERBATIM)

cmake_add_source(CRON_SOURCES DIRS src PATTERNS "*.c" "*.h")
cmake_add_source(CRON_HEADERS RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND CRON_SOURCES ${CRON_LEXER_GEN})

add_library(${TARGET_NAME} STATIC ${CRON_SOURCES} ${CRON_HEADERS})

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::Cron
  FOLDER "cron"
  EXPORT_NAME Cron)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
         $<INSTALL_INTERFACE:include/cron>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${TARGET_NAME} PUBLIC Salts::Core)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
