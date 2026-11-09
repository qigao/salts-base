cmake_add_executable(saltscore_native_io_test SOURCES test_native_io.c
  FOLDER "native-io/tests")
target_link_libraries(saltscore_native_io_test PRIVATE Salts::NativeIO Salts::TinyTest)
set_target_properties(saltscore_native_io_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.native_io COMMAND saltscore_native_io_test)

# File and process completion adapters share the NativeIO owner.
cmake_add_test(saltscore_fs_file_test SOURCES test_fs_file.c
  LIBS Salts::NativeIO Salts::TinyTest FOLDER "native-io/tests")
target_compile_features(saltscore_fs_file_test PRIVATE c_std_11)
set_target_properties(saltscore_fs_file_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_tests_properties(saltscore_fs_file_test PROPERTIES TIMEOUT 60)
if(MSVC)
  target_compile_options(saltscore_fs_file_test PRIVATE -experimental:c11atomics)
endif()

cmake_add_test(saltscore_process_async_test SOURCES test_process_async.c
  LIBS Salts::NativeIO Salts::TinyTest FOLDER "native-io/tests")
set_tests_properties(saltscore_process_async_test PROPERTIES TIMEOUT 45)
cmake_add_test(saltscore_process_async_cpp_test SOURCES test_process_async_cpp.cpp
  LIBS Salts::NativeIO Salts::TinyTest FOLDER "native-io/tests")
target_compile_features(saltscore_process_async_cpp_test PRIVATE cxx_std_17)
set_target_properties(saltscore_process_async_cpp_test PROPERTIES
  CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
cmake_add_test(saltscore_shell_test SOURCES test_shell.c
  LIBS Salts::NativeIO Salts::TinyTest FOLDER "native-io/tests")
set_tests_properties(saltscore_shell_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  ENVIRONMENT "SALTS_SHELL_PARENT_SENTINEL=parent-value"
  TIMEOUT 60)
foreach(_test IN ITEMS saltscore_process_async_test saltscore_shell_test)
  target_compile_features(${_test} PRIVATE c_std_11)
  set_target_properties(${_test} PROPERTIES C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endforeach()

if(UNIX)
  cmake_add_executable(saltscore_readiness_race_test SOURCES test_readiness_race.c
    FOLDER "native-io/tests")
  target_compile_features(saltscore_readiness_race_test PRIVATE c_std_11)
  target_link_libraries(saltscore_readiness_race_test PRIVATE Salts::NativeIO Salts::TinyTest)
  target_include_directories(saltscore_readiness_race_test PRIVATE
    "${PROJECT_SOURCE_DIR}/native-io/src/readiness")
  set_target_properties(saltscore_readiness_race_test PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME saltscore.readiness_close_race COMMAND saltscore_readiness_race_test)
  set_tests_properties(saltscore.readiness_close_race PROPERTIES TIMEOUT 35)
endif()

# Real copied readiness state machine with test-only injection of native
# backend hook failures: register, arm, unarm, close, shutdown, stale events.
cmake_add_executable(saltscore_readiness_faults_test SOURCES test_readiness_faults.c
  FOLDER "native-io/tests")
target_compile_features(saltscore_readiness_faults_test PRIVATE c_std_11)
target_link_libraries(saltscore_readiness_faults_test PRIVATE Salts::NativeIO Salts::TinyTest)
target_include_directories(saltscore_readiness_faults_test PRIVATE
  "${PROJECT_SOURCE_DIR}/native-io/src/readiness")
set_target_properties(saltscore_readiness_faults_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.readiness_faults COMMAND saltscore_readiness_faults_test)

cmake_add_executable(saltscore_readiness_test SOURCES test_readiness.c
  FOLDER "native-io/tests")
target_compile_features(saltscore_readiness_test PRIVATE c_std_11)
target_link_libraries(saltscore_readiness_test PRIVATE Salts::NativeIO Salts::TinyTest)
set_target_properties(saltscore_readiness_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.readiness_owner COMMAND saltscore_readiness_test)

set_tests_properties(saltscore.readiness_faults PROPERTIES TIMEOUT 20)
set_tests_properties(saltscore.readiness_owner PROPERTIES TIMEOUT 15)

cmake_add_test(saltscore_readiness_cpp_test SOURCES test_readiness_cpp.cpp
  LIBS Salts::NativeIO Salts::TinyTest FOLDER "native-io/tests")
target_compile_features(saltscore_readiness_cpp_test PRIVATE cxx_std_17)
set_target_properties(saltscore_readiness_cpp_test PROPERTIES
  CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

get_property(_native_io_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_native_io_tests} PROPERTIES LABELS native_io)
