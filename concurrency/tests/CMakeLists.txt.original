cmake_add_test(
  disruptor_test
  SOURCES
  disruptor_test.c
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER
  "concurrency/tests")
set_target_properties(
  disruptor_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  thread_pool_test
  SOURCES
  thread_pool_test.c
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER
  "concurrency/tests")
set_target_properties(
  thread_pool_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(thread_pool_lf_test
  SOURCES thread_pool_lf_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  FOLDER "concurrency/tests")
set_target_properties(thread_pool_lf_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_tests_properties(thread_pool_lf_test PROPERTIES TIMEOUT 60)

# The consumer links only the installed Concurrency target in a separate build.
# Sanitizer profiles qualify the in-tree lifecycle tests with their own flags.
if(NOT CMAKE_CROSSCOMPILING AND CMAKE_BUILD_TYPE STREQUAL "Release")
  add_test(NAME thread_pool_lf_installed_test
    COMMAND "${CMAKE_COMMAND}"
      "-DSALTS_BINARY_DIR=${CMAKE_BINARY_DIR}"
      "-DSALTS_TEST_VERSION=${PROJECT_VERSION}"
      "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_config/leader_followers"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-leader-followers"
      "-DSALTS_GENERATOR=${CMAKE_GENERATOR}"
      "-DSALTS_TEST_CONFIG=$<CONFIG>"
      "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
      -P "${PROJECT_SOURCE_DIR}/cmake/ci/verify-installed-consumer.cmake")
  set_tests_properties(thread_pool_lf_installed_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)
endif()

cmake_add_test(
  concurrency_deadline_queue_test
  SOURCES
  deadline_queue_test.c
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  FOLDER
  "concurrency/tests")
set_target_properties(
  concurrency_deadline_queue_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  concurrency_header_cpp_test
  SOURCES
  concurrency_header_cpp_test.cpp
  LIBS
  Salts::TinyTest
  Salts::Concurrency
  FOLDER
  "concurrency/tests")
set_target_properties(
  concurrency_header_cpp_test
  PROPERTIES CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)

cmake_add_test(
  concurrency_rcu_test
  SOURCES
  rcu_test.c
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  FOLDER
  "concurrency/tests")
set_target_properties(
  concurrency_rcu_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
set_tests_properties(concurrency_rcu_test PROPERTIES TIMEOUT 60)

cmake_add_test(
  concurrency_typed_primitives_test
  SOURCES
  typed_primitives_test.c
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  FOLDER
  "concurrency/tests")
set_target_properties(
  concurrency_typed_primitives_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
cmake_add_test(
  concurrency_typed_rcu_cpp_test
  SOURCES
  typed_rcu_cpp_test.cpp
  LIBS
  Salts::Concurrency
  Salts::TinyTest
  FOLDER
  "concurrency/tests")
set_target_properties(
  concurrency_typed_rcu_cpp_test
  PROPERTIES CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)
set_tests_properties(concurrency_typed_primitives_test
                     concurrency_typed_rcu_cpp_test PROPERTIES TIMEOUT 60)
