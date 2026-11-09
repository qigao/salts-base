cmake_add_executable(saltscore_native_io_test SOURCES test_native_io.c
  FOLDER "native-io/tests")
target_link_libraries(saltscore_native_io_test PRIVATE Salts::NativeIO Salts::TinyTest)
set_target_properties(saltscore_native_io_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.native_io COMMAND saltscore_native_io_test)
