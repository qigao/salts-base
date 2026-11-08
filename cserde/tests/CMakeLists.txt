foreach(_name IN ITEMS cserde_token_test cserde_reader_test cserde_writer_test)
  cmake_add_executable("${_name}" SOURCES "${_name}.c"
    LIBS Salts::CSerde Salts::TinyTest FOLDER "cserde/tests")
  target_compile_features("${_name}" PRIVATE c_std_11)
  set_target_properties("${_name}" PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME "saltscore.native.${_name}" COMMAND "${_name}")
  set_tests_properties("saltscore.native.${_name}" PROPERTIES TIMEOUT 300)
endforeach()
add_library(saltscore_cserde_recording_support STATIC support/recording.c)
cmake_config_target(saltscore_cserde_recording_support FOLDER "cserde/tests")
target_compile_features(saltscore_cserde_recording_support PRIVATE c_std_11)
target_include_directories(saltscore_cserde_recording_support PUBLIC
  "${PROJECT_SOURCE_DIR}/cserde/tests/support")
target_link_libraries(saltscore_cserde_recording_support PUBLIC Salts::CSerde)
cmake_add_executable(cserde_recording_test SOURCES "cserde_recording_test.c"
  LIBS Salts::CSerde Salts::TinyTest FOLDER "cserde/tests")
target_compile_features(cserde_recording_test PRIVATE c_std_11)
set_target_properties(cserde_recording_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME "saltscore.native.cserde_recording_test" COMMAND cserde_recording_test)
set_tests_properties("saltscore.native.cserde_recording_test" PROPERTIES TIMEOUT 300)
target_link_libraries(cserde_recording_test
  PRIVATE saltscore_cserde_recording_support)



cmake_add_executable(cserde_header_cpp_test SOURCES cserde_header_cpp_test.cpp
  LIBS Salts::CSerde Salts::TinyTest FOLDER "cserde/tests")
target_compile_features(cserde_header_cpp_test PRIVATE cxx_std_17)
add_test(NAME saltscore.native.cserde_header_cpp_test COMMAND cserde_header_cpp_test)
set_tests_properties(saltscore.native.cserde_header_cpp_test PROPERTIES TIMEOUT 120)
