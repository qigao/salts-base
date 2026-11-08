set(TEST_NAME test_salts_cron)

cmake_add_test(${TEST_NAME}
  SOURCES test_salts_cron.c
  LIBS Salts::Cron Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cron/tests")
set_target_properties(${TEST_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
