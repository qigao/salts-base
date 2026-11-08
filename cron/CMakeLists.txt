set(TARGET_NAME salts_cron)

set(CRON_LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/src/cron_lexer.re)
set(CRON_LEXER_GEN ${CMAKE_CURRENT_BINARY_DIR}/cron_lexer_gen.c)

add_custom_command(
  OUTPUT ${CRON_LEXER_GEN}
  COMMAND ${RE2C_EXECUTABLE} -o ${CRON_LEXER_GEN} ${CRON_LEXER_RE}
  DEPENDS ${CRON_LEXER_RE} ${CMAKE_CURRENT_SOURCE_DIR}/src/cron_lexer.h
  COMMENT "Generating cron lexer with re2c"
  VERBATIM)

set(CRON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/salts_cron.c ${CRON_LEXER_GEN})
set(CRON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/salts_cron.h)

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
  EXPORT SaltsUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${CRON_HEADERS} DESTINATION include/cron)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
