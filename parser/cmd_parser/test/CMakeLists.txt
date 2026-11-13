set(TEST_NAME test_cmd_parser)

cmake_add_test(${TEST_NAME}
  SOURCES test_cmd_parser.c
  LIBS Salts::CmdParser Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "parser/cmd_parser/test")
