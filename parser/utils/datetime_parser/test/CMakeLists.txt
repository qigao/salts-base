set(TARGET_NAME test_datetime_parser)

cmake_add_test(${TARGET_NAME}
  SOURCES test_datetime_parser.cpp
  LIBS Salts::DateTimeParser Salts::TinyTest
  FOLDER "parser/utils/datetime_parser/test")
