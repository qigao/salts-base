set(TEST_NAME test_dotenv)

cmake_add_test(${TEST_NAME}
  SOURCES test_dotenv.c
  LIBS Salts::DotEnvParser Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "parser/dotenv_parser/test")
set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
