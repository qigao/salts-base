cmake_add_executable(saltscore_uri_test SOURCES test_uri.c
  FOLDER "cnet/uri/tests")
target_link_libraries(saltscore_uri_test PRIVATE Salts::URI Salts::TinyTest)
set_target_properties(saltscore_uri_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.cnet/uri COMMAND saltscore_uri_test)
