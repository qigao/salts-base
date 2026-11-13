cmake_add_executable(uri_parser_test SOURCES test_uri.c
  FOLDER "parser/uri/tests")
target_link_libraries(uri_parser_test PRIVATE Salts::URI Salts::TinyTest)
set_target_properties(uri_parser_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.cnet/uri COMMAND uri_parser_test)
