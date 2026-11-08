cmake_add_executable(saltscore_fs_test SOURCES test_fs.c test_fs_capabilities.c
  FOLDER "fs/tests")
target_compile_features(saltscore_fs_test PRIVATE c_std_11)
set_target_properties(saltscore_fs_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_fs_test PRIVATE Salts::FS Salts::TinyTest)
add_test(NAME saltscore.fs_owner_and_root COMMAND saltscore_fs_test)
set_tests_properties(saltscore.fs_owner_and_root PROPERTIES TIMEOUT 25)

foreach(_suite IN ITEMS fs_async fs_watch fs_file)
  cmake_add_test(saltscore_${_suite}_test SOURCES test_${_suite}.c
    LIBS Salts::FS Salts::TinyTest FOLDER "fs/tests")
  target_compile_features(saltscore_${_suite}_test PRIVATE c_std_11)
  set_target_properties(saltscore_${_suite}_test PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  set_tests_properties(saltscore_${_suite}_test PROPERTIES TIMEOUT 60)
endforeach()
target_sources(saltscore_fs_async_test PRIVATE test_fs_init_failure.c)
target_link_libraries(saltscore_fs_async_test PRIVATE Salts::Concurrency)
target_sources(saltscore_fs_watch_test PRIVATE test_fs_watch_init_failure.c)
if(MSVC)
  foreach(_test IN ITEMS saltscore_fs_async_test saltscore_fs_watch_test saltscore_fs_file_test)
    target_compile_options(${_test} PRIVATE -experimental:c11atomics)
  endforeach()
endif()
cmake_add_test(saltscore_fs_async_cpp_test SOURCES test_fs_async_cpp.cpp
  LIBS Salts::FS Salts::TinyTest FOLDER "fs/tests")
target_compile_features(saltscore_fs_async_cpp_test PRIVATE cxx_std_17)
set_target_properties(saltscore_fs_async_cpp_test PROPERTIES
  CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
