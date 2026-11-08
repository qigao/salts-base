cmake_add_executable(saltscore_process_test SOURCES test_process.c
  FOLDER "process/tests")
target_compile_features(saltscore_process_test PRIVATE c_std_11)
set_target_properties(saltscore_process_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_process_test PRIVATE Salts::Process Salts::TinyTest)
add_test(NAME saltscore.process_spawn_reap COMMAND saltscore_process_test)
set_tests_properties(saltscore.process_spawn_reap PROPERTIES TIMEOUT 30)

cmake_add_test(saltscore_process_async_test SOURCES test_process_async.c
  LIBS Salts::Process Salts::TinyTest FOLDER "process/tests")
set_tests_properties(saltscore_process_async_test PROPERTIES TIMEOUT 45)
cmake_add_test(saltscore_process_async_cpp_test SOURCES test_process_async_cpp.cpp
  LIBS Salts::Process Salts::TinyTest FOLDER "process/tests")
target_compile_features(saltscore_process_async_cpp_test PRIVATE cxx_std_17)
set_target_properties(saltscore_process_async_cpp_test PROPERTIES
  CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
cmake_add_test(saltscore_shell_test SOURCES test_shell.c
  LIBS Salts::Process Salts::TinyTest FOLDER "process/tests")
set_tests_properties(saltscore_shell_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  ENVIRONMENT "SALTS_SHELL_PARENT_SENTINEL=parent-value"
  TIMEOUT 60)
foreach(_test IN ITEMS saltscore_process_async_test saltscore_shell_test)
  target_compile_features(${_test} PRIVATE c_std_11)
  set_target_properties(${_test} PROPERTIES C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endforeach()
