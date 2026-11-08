# Original native contracts, preserved independently of the package smoke test.
foreach(_case IN ITEMS disruptor_test thread_pool_test thread_pool_lf_test
    deadline_queue_test rcu_test typed_primitives_test)
  set(_target "saltscore_original_${_case}")
  cmake_add_executable(${_target} SOURCES "${_case}.c"
    FOLDER "concurrency/tests")
  target_compile_features(${_target} PRIVATE c_std_11)
  set_target_properties(${_target} PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(${_target} PRIVATE Salts::Concurrency Salts::TinyTest)
  add_test(NAME "saltscore.original_concurrency.${_case}" COMMAND ${_target})
  set_tests_properties("saltscore.original_concurrency.${_case}" PROPERTIES TIMEOUT 120)
endforeach()
foreach(_case IN ITEMS concurrency_header_cpp_test typed_rcu_cpp_test)
  set(_target "saltscore_original_${_case}")
  cmake_add_executable(${_target} SOURCES "${_case}.cpp"
    FOLDER "concurrency/tests")
  target_compile_features(${_target} PRIVATE cxx_std_17)
  set_target_properties(${_target} PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(${_target} PRIVATE Salts::Concurrency Salts::TinyTest)
  add_test(NAME "saltscore.original_concurrency.${_case}" COMMAND ${_target})
  set_tests_properties("saltscore.original_concurrency.${_case}" PROPERTIES TIMEOUT 120)
endforeach()


cmake_add_executable(saltscore_concurrency_test SOURCES test_concurrency.c
  FOLDER "concurrency/tests")
target_link_libraries(saltscore_concurrency_test PRIVATE Salts::Concurrency Salts::TinyTest)
set_target_properties(saltscore_concurrency_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.concurrency COMMAND saltscore_concurrency_test)

# Original native SPSC ring and bounded multi-priority SPSC queue. Their
# behavior is exercised by the original TinyTest suites, not a mock backend.
cmake_add_executable(saltscore_original_ring_spsc_test SOURCES test_ring_buffer_spsc.c
  FOLDER "concurrency/tests")
target_compile_features(saltscore_original_ring_spsc_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_ring_spsc_test PRIVATE Salts::Concurrency Salts::TinyTest)
add_test(NAME saltscore.original_ring_spsc COMMAND saltscore_original_ring_spsc_test)

cmake_add_executable(saltscore_original_bucket_spsc_test SOURCES test_bucket_priority_queue_spsc.c
  FOLDER "concurrency/tests")
target_compile_features(saltscore_original_bucket_spsc_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_bucket_spsc_test PRIVATE
  Salts::Concurrency Salts::TinyTest)
add_test(NAME saltscore.original_bucket_priority_spsc
  COMMAND saltscore_original_bucket_spsc_test)

cmake_add_executable(saltscore_original_spsc_smoke SOURCES test_original_spsc_smoke.c
  FOLDER "concurrency/tests")
target_compile_features(saltscore_original_spsc_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_original_spsc_smoke PRIVATE Salts::Concurrency Salts::TinyTest)
add_test(NAME saltscore.original_spsc_smoke COMMAND saltscore_original_spsc_smoke)

# The original seven-function MPMC API is owned by the existing concurrency
# module. The original native TinyTest contract exercises that canonical owner.
cmake_add_executable(saltscore_original_bucket_mpmc_test SOURCES test_bucket_priority_queue_mpmc.c
  FOLDER "concurrency/tests")
target_compile_features(saltscore_original_bucket_mpmc_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_bucket_mpmc_test PRIVATE
  Salts::Concurrency Salts::TinyTest)
add_test(NAME saltscore.original_bucket_mpmc COMMAND saltscore_original_bucket_mpmc_test)

set_tests_properties(saltscore.original_bucket_priority_spsc PROPERTIES TIMEOUT 50)
set_tests_properties(saltscore.original_bucket_mpmc PROPERTIES TIMEOUT 30)
