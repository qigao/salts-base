cmake_add_executable(saltscore_coroutine_test SOURCES test_coroutine.c
  FOLDER "coroutine/tests")
target_link_libraries(saltscore_coroutine_test PRIVATE Salts::Coroutine Salts::TinyTest)
set_target_properties(saltscore_coroutine_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.coroutine COMMAND saltscore_coroutine_test)

set_tests_properties(saltscore.coroutine PROPERTIES TIMEOUT 30)
