set(TEST_NAME test_cron)

cmake_add_test(${TEST_NAME}
  SOURCES test_cron.c
  LIBS Salts::Cron Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cron/tests")
