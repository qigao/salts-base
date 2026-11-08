# TinyTest is required for native runtime tests; migrate source-owned suites in
# batches while retaining all original behavior and CTest registrations.

if(UNIX)
  cmake_add_executable(saltscore_readiness_race_test SOURCES test_readiness_race.c
    FOLDER "platform/tests")
  target_compile_features(saltscore_readiness_race_test PRIVATE c_std_11)
  target_link_libraries(saltscore_readiness_race_test PRIVATE Salts::Platform Salts::TinyTest)
  set_target_properties(saltscore_readiness_race_test PROPERTIES
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME saltscore.readiness_close_race COMMAND saltscore_readiness_race_test)
  set_tests_properties(saltscore.readiness_close_race PROPERTIES TIMEOUT 35)
endif()

# Real copied readiness state machine with test-only injection of native
# backend hook failures: register, arm, unarm, close, shutdown, stale events.
cmake_add_executable(saltscore_readiness_faults_test SOURCES test_readiness_faults.c
  FOLDER "platform/tests")
target_compile_features(saltscore_readiness_faults_test PRIVATE c_std_11)
target_link_libraries(saltscore_readiness_faults_test PRIVATE Salts::Platform Salts::TinyTest)
set_target_properties(saltscore_readiness_faults_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.readiness_faults COMMAND saltscore_readiness_faults_test)

cmake_add_executable(saltscore_readiness_test SOURCES test_readiness.c
  FOLDER "platform/tests")
target_compile_features(saltscore_readiness_test PRIVATE c_std_11)
target_link_libraries(saltscore_readiness_test PRIVATE Salts::Platform Salts::TinyTest)
set_target_properties(saltscore_readiness_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.readiness_owner COMMAND saltscore_readiness_test)

# Core Platform is the sole owner of native fast-key state; no CMeta ABI.
cmake_add_executable(saltscore_fastpath_test SOURCES test_fastpath.c
  FOLDER "platform/tests")
target_compile_features(saltscore_fastpath_test PRIVATE c_std_11)
target_link_libraries(saltscore_fastpath_test PRIVATE Salts::Platform Salts::TinyTest)
set_target_properties(saltscore_fastpath_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.fastpath COMMAND saltscore_fastpath_test)

cmake_add_executable(saltscore_platform_test SOURCES test_platform.c
  FOLDER "platform/tests")
target_link_libraries(saltscore_platform_test PRIVATE Salts::Platform Salts::TinyTest)
set_target_properties(saltscore_platform_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.platform COMMAND saltscore_platform_test)

cmake_add_executable(saltscore_original_platform_cpp_test SOURCES test_platform_original_cpp.cpp
  FOLDER "platform/tests")
target_compile_features(saltscore_original_platform_cpp_test PRIVATE cxx_std_17)
target_link_libraries(saltscore_original_platform_cpp_test PRIVATE Salts::Platform Salts::TinyTest)
set_target_properties(saltscore_original_platform_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_test(NAME saltscore.original_platform_cpp17
  COMMAND saltscore_original_platform_cpp_test)

# Cross-compiler-compatible Atomic/Thread runtime; no consumer-native _Atomic.
cmake_add_executable(saltscore_portable_concurrency_test SOURCES test_portable_concurrency.c
  FOLDER "platform/tests")
target_compile_features(saltscore_portable_concurrency_test PRIVATE c_std_11)
set_target_properties(saltscore_portable_concurrency_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_portable_concurrency_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.portable_concurrency COMMAND saltscore_portable_concurrency_test)

# M1: relative monotonic timed wait / sleep, same C ABI across compilers.
cmake_add_executable(saltscore_portable_timing_test SOURCES test_portable_timing.c
  FOLDER "platform/tests")
target_compile_features(saltscore_portable_timing_test PRIVATE c_std_11)
target_link_libraries(saltscore_portable_timing_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.portable_timing COMMAND saltscore_portable_timing_test)

# M1: provider-owned thread-specific keys and destructor lifetime barriers.
cmake_add_executable(saltscore_portable_tss_test SOURCES test_portable_tss.c
  FOLDER "platform/tests")
target_compile_features(saltscore_portable_tss_test PRIVATE c_std_11)
target_link_libraries(saltscore_portable_tss_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.portable_tss COMMAND saltscore_portable_tss_test)


# Monotonic relative timed mutex (MSVC / Clang / TinyCC C ABI).
cmake_add_executable(saltscore_portable_timed_mutex_test SOURCES test_portable_timed_mutex.c
  FOLDER "platform/tests")
target_compile_features(saltscore_portable_timed_mutex_test PRIVATE c_std_11)
target_link_libraries(saltscore_portable_timed_mutex_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.portable_timed_mutex COMMAND saltscore_portable_timed_mutex_test)


# Explicit C11 threads.h source-compatibility admission (not a global CRT override).
cmake_add_executable(saltscore_c11_threads_test SOURCES test_c11_threads.c
  FOLDER "platform/tests")
target_include_directories(saltscore_c11_threads_test BEFORE PRIVATE
  "${PROJECT_SOURCE_DIR}/platform/include/c11")
target_compile_definitions(saltscore_c11_threads_test PRIVATE
  SALTS_C11_THREADS_EXPERIMENTAL=1)
set_target_properties(saltscore_c11_threads_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_c11_threads_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.c11_threads_facade COMMAND saltscore_c11_threads_test)


# Regression for Windows cmeta_hrtime once-atomic QPC frequency publication.
cmake_add_executable(saltscore_clock_atomic_owner_test SOURCES test_clock_atomic_owner.c
  FOLDER "platform/tests")
target_compile_features(saltscore_clock_atomic_owner_test PRIVATE c_std_11)
target_link_libraries(saltscore_clock_atomic_owner_test PRIVATE Salts::Platform Salts::TinyTest)
add_test(NAME saltscore.clock_atomic_owner COMMAND saltscore_clock_atomic_owner_test)

set_tests_properties(saltscore.readiness_faults PROPERTIES TIMEOUT 20)
set_tests_properties(saltscore.readiness_owner PROPERTIES TIMEOUT 15)
set_tests_properties(saltscore.platform saltscore.original_platform_cpp17
  PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_concurrency PROPERTIES TIMEOUT 45)
set_tests_properties(saltscore.portable_timing PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_tss PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_timed_mutex PROPERTIES TIMEOUT 35)
set_tests_properties(saltscore.c11_threads_facade PROPERTIES TIMEOUT 35)
set_tests_properties(saltscore.clock_atomic_owner PROPERTIES TIMEOUT 30)
