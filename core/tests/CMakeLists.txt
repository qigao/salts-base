cmake_add_executable(saltscore_runtime_abi_test SOURCES test_runtime_abi.c
  FOLDER "core/tests")
target_link_libraries(saltscore_runtime_abi_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_runtime_abi_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.runtime_abi_0_1 COMMAND saltscore_runtime_abi_test)

# Plain C11 borrowed string view: no reflection headers, allocator or ABI shim.
cmake_add_executable(saltscore_vstr_contract_test SOURCES test_vstr_contract.c
  FOLDER "core/tests")
target_link_libraries(saltscore_vstr_contract_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_vstr_contract_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.vstr_c11 COMMAND saltscore_vstr_contract_test)

# Base64 must remain one installed native owner, no unrelated vendor codec.
cmake_add_executable(saltscore_base64_test SOURCES test_base64_utils.c
  FOLDER "core/tests")
target_link_libraries(saltscore_base64_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_base64_test PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(saltscore_base64_test PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(saltscore_base64_test PRIVATE -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Werror)
endif()
add_test(NAME saltscore.base64_unique_owner COMMAND saltscore_base64_test)

cmake_add_executable(saltscore_bytes_test SOURCES test_bytes.c
  FOLDER "core/tests")
target_link_libraries(saltscore_bytes_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_bytes_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.bytes COMMAND saltscore_bytes_test)

# Preserve the original error registry and borrowed diagnostic lifetimes.
# The native contract includes INT_MIN decoding and concurrent registration.
cmake_add_executable(saltscore_error_contract SOURCES error_native_contract.c
  FOLDER "core/tests")
target_link_libraries(saltscore_error_contract PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_error_contract PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.error_native_contract COMMAND saltscore_error_contract)
set_tests_properties(saltscore.error_native_contract PROPERTIES TIMEOUT 30)
cmake_add_executable(saltscore_original_error_test SOURCES test_error.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_error_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_error COMMAND saltscore_original_error_test)

cmake_add_executable(saltscore_buffer_test SOURCES test_buffer.c
  FOLDER "core/tests")
target_link_libraries(saltscore_buffer_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_buffer_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.buffer COMMAND saltscore_buffer_test)

cmake_add_executable(saltscore_ring_test SOURCES test_ring.c
  FOLDER "core/tests")
target_link_libraries(saltscore_ring_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_ring_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.ring COMMAND saltscore_ring_test)

# Restore original tstr/vstr/memory-pool behavior, not just public headers.

# Literal copy of the original TinyTest tstr v2 contract from Salts utils.
# Optional TinyTest profile runs source-parity expectations; never a stub.
cmake_add_executable(saltscore_original_tstr_v2_contract SOURCES test_tstr_v2_contract.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_tstr_v2_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tstr_v2_contract
  COMMAND saltscore_original_tstr_v2_contract)

# Original object_pool source and TinyTest contract, not a new allocator
# provider. The C11 smoke test also runs when TinyTest is disabled.

cmake_add_executable(saltscore_original_object_pool_contract SOURCES test_object_pool.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_object_pool_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_object_pool_contract
  COMMAND saltscore_original_object_pool_contract)
set_tests_properties(saltscore.original_object_pool_contract PROPERTIES TIMEOUT 45)

# The original single-owner priority queue: FIFO within a priority and
# critical/high/normal/low ordering, with strict bounded storage.

cmake_add_executable(saltscore_original_priority_queue_contract SOURCES test_bucket_priority_queue.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_priority_queue_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_priority_queue_contract
  COMMAND saltscore_original_priority_queue_contract)

# Original formatter lexer and C11 smoke both execute through TinyTest.
cmake_add_executable(saltscore_fmt_lexer_smoke SOURCES test_fmt_lexer_smoke.c
  FOLDER "core/tests")
target_link_libraries(saltscore_fmt_lexer_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_lexer_smoke COMMAND saltscore_fmt_lexer_smoke)
cmake_add_executable(saltscore_original_fmt_lexer_test SOURCES test_fmt_re2c.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_fmt_lexer_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_lexer_contract COMMAND saltscore_original_fmt_lexer_test)

# Original formatter implementation: typed C11 and C++17 metadata, bounded
# rendering, invalid specifier rejection, time formatting, dynamic tstr and views.
cmake_add_executable(saltscore_original_fmt_test SOURCES test_fmt.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_fmt_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_c11 COMMAND saltscore_original_fmt_test)

cmake_add_executable(saltscore_original_fmt_cpp_test SOURCES test_fmt_cpp.cpp
  FOLDER "core/tests")
target_link_libraries(saltscore_original_fmt_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_cpp17 COMMAND saltscore_original_fmt_cpp_test)

cmake_add_executable(saltscore_original_fmt_view_test SOURCES test_fmt_vstr_pattern.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_fmt_view_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_bounded_vstr COMMAND saltscore_original_fmt_view_test)
# Original formatter contract: TinyTest is the mandatory source assertion runner.
cmake_add_executable(saltscore_fmt_smoke SOURCES test_fmt_smoke.c
  FOLDER "core/tests")
target_link_libraries(saltscore_fmt_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_smoke COMMAND saltscore_fmt_smoke)

# Original log pattern lexer tokens, C11 parser/format ownership.
cmake_add_executable(saltscore_log_pattern_lexer_test SOURCES test_log_pattern_lexer.c
  FOLDER "core/tests")
target_link_libraries(saltscore_log_pattern_lexer_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_log_pattern_lexer
  COMMAND saltscore_log_pattern_lexer_test)
foreach(_lexer_test IN ITEMS saltscore_fmt_lexer_smoke
    saltscore_original_fmt_lexer_test saltscore_log_pattern_lexer_test)
  target_include_directories(${_lexer_test} PRIVATE
    "${PROJECT_SOURCE_DIR}/core/parser")
endforeach()

# Original async logger: the original TinyTest and native callback exercise
# real Disruptor/FS providers, thread drain, shutdown, not a logger mock.
cmake_add_executable(saltscore_tlog_smoke SOURCES test_tlog_smoke.c
  FOLDER "core/tests")
target_link_libraries(saltscore_tlog_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tlog_smoke COMMAND saltscore_tlog_smoke)
set_tests_properties(saltscore.original_tlog_smoke PROPERTIES TIMEOUT 40)
foreach(_tlog_suite IN ITEMS tlog tlog_vstr_abi tlog_header_collision)
  cmake_add_executable(saltscore_original_${_tlog_suite}_test SOURCES test_${_tlog_suite}.c
    FOLDER "core/tests")
  target_link_libraries(saltscore_original_${_tlog_suite}_test PRIVATE
    Salts::Core Salts::TinyTest)
  add_test(NAME saltscore.original_${_tlog_suite}
    COMMAND saltscore_original_${_tlog_suite}_test)
  set_tests_properties(saltscore.original_${_tlog_suite} PROPERTIES TIMEOUT 120)
endforeach()
cmake_add_executable(saltscore_original_tlog_cpp_test SOURCES test_tlog_cpp.cpp
  FOLDER "core/tests")
target_link_libraries(saltscore_original_tlog_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tlog_cpp COMMAND saltscore_original_tlog_cpp_test)
set_tests_properties(saltscore.original_tlog_cpp PROPERTIES TIMEOUT 120)

# Original RFC 4122/9562 UUID v4/v7 and canonical parse/format.
cmake_add_executable(saltscore_uuid_smoke SOURCES test_uuid_smoke.c
  FOLDER "core/tests")
target_link_libraries(saltscore_uuid_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_smoke COMMAND saltscore_uuid_smoke)
cmake_add_executable(saltscore_original_uuid_test SOURCES test_uuid.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_uuid_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_c11 COMMAND saltscore_original_uuid_test)
cmake_add_executable(saltscore_original_uuid_cpp_test SOURCES test_uuid_cpp.cpp
  FOLDER "core/tests")
target_link_libraries(saltscore_original_uuid_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_cpp17 COMMAND saltscore_original_uuid_cpp_test)

# Restore the original single-owner, hard-capped byte accumulator. This is not
# the borrowed-only core_bytes_view/slice contract.
cmake_add_executable(saltscore_byte_buffer_smoke SOURCES test_byte_buffer_smoke.c
  FOLDER "core/tests")
target_link_libraries(saltscore_byte_buffer_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bytes_accumulator_smoke COMMAND saltscore_byte_buffer_smoke)
cmake_add_executable(saltscore_original_bytes_accumulator SOURCES test_byte_buffer.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_bytes_accumulator PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bytes_accumulator_contract
  COMMAND saltscore_original_bytes_accumulator)

# Public scanning semantics plus private scalar/SIMDe kernel equivalence.
cmake_add_executable(saltscore_simd_scan_contract SOURCES test_simd_scan_contract.c
  FOLDER "core/tests")
target_link_libraries(saltscore_simd_scan_contract PRIVATE Salts::Core Salts::TinyTest)
target_include_directories(saltscore_simd_scan_contract PRIVATE
  "${PROJECT_SOURCE_DIR}/core/src")
target_include_directories(saltscore_simd_scan_contract SYSTEM PRIVATE
  "${CORE_SIMDE_INCLUDE_DIR}")
if(UNIX)
  target_compile_definitions(saltscore_simd_scan_contract PRIVATE _DEFAULT_SOURCE=1)
endif()
add_test(NAME saltscore.original_simd_scan_contract COMMAND saltscore_simd_scan_contract)


# Actual original qigao/salts utils native memory mapping, not a mock
# and not a CMeta/CFlow runtime. Real file page / range / write / group.

# Frozen source/accounting policies run once in the Migration gate.
# Native CTest below remains executable behavior only.

# qigao/salts original utils native byte regex, 10 owned public C11 functions.
# Always-on compiled source/runtime; TinyTest retains all 25 donor test cases.
cmake_add_executable(saltscore_original_re_smoke SOURCES test_re_smoke.c
  FOLDER "core/tests")
set_target_properties(saltscore_original_re_smoke PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_re_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_re_smoke COMMAND saltscore_original_re_smoke)
set_tests_properties(saltscore.original_re_smoke PROPERTIES TIMEOUT 25)

cmake_add_executable(saltscore_original_re_contract SOURCES test_re.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_re_contract PRIVATE
  Salts::Core Salts::TinyTest)
# The first-byte scan is PRIVATE. Only the original tests may inspect it.
target_include_directories(saltscore_original_re_contract PRIVATE
  "${PROJECT_SOURCE_DIR}/core/src")
add_test(NAME saltscore.original_re_25_case_contract
  COMMAND saltscore_original_re_contract)
set_tests_properties(saltscore.original_re_25_case_contract
  PROPERTIES TIMEOUT 60)


# Regex/SIMD/Error source provenance also runs once in the Migration gate.
# The release-only migration-receipt harness is intentionally not part of PR CI.

# TinyTest is provided by the package root.
cmake_add_executable(saltscore_original_zstd_test SOURCES test_zstd.c
  FOLDER "core/tests")
set_target_properties(saltscore_original_zstd_test PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_zstd_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_zstd COMMAND saltscore_original_zstd_test)

cmake_add_executable(saltscore_zstd_contract SOURCES zstd_native_contract.c
  FOLDER "core/tests")
set_target_properties(saltscore_zstd_contract PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_zstd_contract PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.zstd_native_contract COMMAND saltscore_zstd_contract)

if(CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME saltscore.zstd_private_archive
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/core/tests/test_zstd_binary_owner.py"
      --library "$<TARGET_FILE:Salts::Core>"
      --nm "${CMAKE_NM}" --readelf "${CMAKE_READELF}")
  set_tests_properties(saltscore.zstd_private_archive PROPERTIES TIMEOUT 60)
endif()
set_tests_properties(saltscore.original_zstd saltscore.zstd_native_contract
  PROPERTIES TIMEOUT 60)

# Contracts migrated with the concurrency implementation into Core.
# Original native contracts, preserved independently of the package smoke test.
foreach(_case IN ITEMS disruptor_test thread_pool_test thread_pool_lf_test
    deadline_queue_test rcu_test typed_primitives_test)
  set(_target "saltscore_original_${_case}")
  cmake_add_executable(${_target} SOURCES "${_case}.c"
    FOLDER "core/tests")
  set_target_properties(${_target} PROPERTIES C_EXTENSIONS OFF)
  target_link_libraries(${_target} PRIVATE Salts::Core Salts::TinyTest)
  add_test(NAME "saltscore.original_concurrency.${_case}" COMMAND ${_target})
  set_tests_properties("saltscore.original_concurrency.${_case}" PROPERTIES TIMEOUT 120)
endforeach()
foreach(_case IN ITEMS concurrency_header_cpp_test typed_rcu_cpp_test)
  set(_target "saltscore_original_${_case}")
  cmake_add_executable(${_target} SOURCES "${_case}.cpp"
    FOLDER "core/tests")
  set_target_properties(${_target} PROPERTIES CXX_EXTENSIONS OFF)
  target_link_libraries(${_target} PRIVATE Salts::Core Salts::TinyTest)
  add_test(NAME "saltscore.original_concurrency.${_case}" COMMAND ${_target})
  set_tests_properties("saltscore.original_concurrency.${_case}" PROPERTIES TIMEOUT 120)
endforeach()


cmake_add_executable(saltscore_concurrency_test SOURCES test_concurrency.c
  FOLDER "core/tests")
target_link_libraries(saltscore_concurrency_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_concurrency_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.concurrency COMMAND saltscore_concurrency_test)

# Original native SPSC ring and bounded multi-priority SPSC queue. Their
# behavior is exercised by the original TinyTest suites, not a mock backend.
cmake_add_executable(saltscore_original_ring_spsc_test SOURCES test_ring_buffer_spsc.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_ring_spsc_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_ring_spsc COMMAND saltscore_original_ring_spsc_test)

cmake_add_executable(saltscore_original_bucket_spsc_test SOURCES test_bucket_priority_queue_spsc.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_bucket_spsc_test PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bucket_priority_spsc
  COMMAND saltscore_original_bucket_spsc_test)

# The original seven-function MPMC API is owned by the existing concurrency
# module. The original native TinyTest contract exercises that canonical owner.
cmake_add_executable(saltscore_original_bucket_mpmc_test SOURCES test_bucket_priority_queue_mpmc.c
  FOLDER "core/tests")
target_link_libraries(saltscore_original_bucket_mpmc_test PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bucket_mpmc COMMAND saltscore_original_bucket_mpmc_test)

set_tests_properties(saltscore.original_bucket_priority_spsc PROPERTIES TIMEOUT 50)
set_tests_properties(saltscore.original_bucket_mpmc PROPERTIES TIMEOUT 30)

# Contracts migrated with the platform implementation into Core.
# TinyTest is required for native runtime tests; migrate source-owned suites in
# batches while retaining all original behavior and CTest registrations.

# Core Platform is the sole owner of native fast-key state; no CMeta ABI.

cmake_add_executable(saltscore_platform_test SOURCES test_platform.c
  FOLDER "core/tests")
target_link_libraries(saltscore_platform_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_platform_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME saltscore.platform COMMAND saltscore_platform_test)

cmake_add_executable(saltscore_original_platform_cpp_test SOURCES test_platform_original_cpp.cpp
  FOLDER "core/tests")
target_link_libraries(saltscore_original_platform_cpp_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_original_platform_cpp_test PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME saltscore.original_platform_cpp17
  COMMAND saltscore_original_platform_cpp_test)

# Cross-compiler-compatible Atomic/Thread runtime; no consumer-native _Atomic.
cmake_add_executable(saltscore_portable_concurrency_test SOURCES test_portable_concurrency.c
  FOLDER "core/tests")
set_target_properties(saltscore_portable_concurrency_test PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_portable_concurrency_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.portable_concurrency COMMAND saltscore_portable_concurrency_test)

# M1: relative monotonic timed wait / sleep, same C ABI across compilers.
cmake_add_executable(saltscore_portable_timing_test SOURCES test_portable_timing.c
  FOLDER "core/tests")
target_link_libraries(saltscore_portable_timing_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.portable_timing COMMAND saltscore_portable_timing_test)

# M1: provider-owned thread-specific keys and destructor lifetime barriers.
cmake_add_executable(saltscore_portable_tss_test SOURCES test_portable_tss.c
  FOLDER "core/tests")
target_link_libraries(saltscore_portable_tss_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.portable_tss COMMAND saltscore_portable_tss_test)


# Monotonic relative timed mutex (MSVC / Clang / TinyCC C ABI).
cmake_add_executable(saltscore_portable_timed_mutex_test SOURCES test_portable_timed_mutex.c
  FOLDER "core/tests")
target_link_libraries(saltscore_portable_timed_mutex_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.portable_timed_mutex COMMAND saltscore_portable_timed_mutex_test)


# Select the Salts C11 threads implementation for this standard API suite.
cmake_add_executable(saltscore_c11_threads_test SOURCES test_c11_threads.c
  FOLDER "core/tests")
if(UNIX)
  target_sources(saltscore_c11_threads_test PRIVATE test_c11_sleep_posix.c)
endif()
set_target_properties(saltscore_c11_threads_test PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_c11_threads_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.c11_threads_facade COMMAND saltscore_c11_threads_test)


# Regression for Windows hrtime once-atomic QPC frequency publication.
cmake_add_executable(saltscore_clock_atomic_owner_test SOURCES test_clock_atomic_owner.c
  FOLDER "core/tests")
target_link_libraries(saltscore_clock_atomic_owner_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.clock_atomic_owner COMMAND saltscore_clock_atomic_owner_test)

# Backend contracts opt in to Core's private headers; SDK consumers do not.
foreach(_backend_test IN ITEMS saltscore_portable_concurrency_test
    saltscore_portable_timing_test saltscore_portable_tss_test
    saltscore_portable_timed_mutex_test saltscore_c11_threads_test)
  target_include_directories(${_backend_test} PRIVATE
    "${PROJECT_SOURCE_DIR}/core/src/threads")
endforeach()

set_tests_properties(saltscore.platform saltscore.original_platform_cpp17
  PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_concurrency PROPERTIES TIMEOUT 45)
set_tests_properties(saltscore.portable_timing PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_tss PROPERTIES TIMEOUT 30)
set_tests_properties(saltscore.portable_timed_mutex PROPERTIES TIMEOUT 35)
set_tests_properties(saltscore.c11_threads_facade PROPERTIES TIMEOUT 35)
set_tests_properties(saltscore.clock_atomic_owner PROPERTIES TIMEOUT 30)

# Contracts migrated with the fs implementation into Core.
cmake_add_executable(saltscore_fs_test SOURCES test_fs.c test_fs_capabilities.c
  FOLDER "core/tests")
set_target_properties(saltscore_fs_test PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_fs_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.fs_owner_and_root COMMAND saltscore_fs_test)
set_tests_properties(saltscore.fs_owner_and_root PROPERTIES TIMEOUT 25)

cmake_add_test(saltscore_fs_watch_test
  SOURCES test_fs_watch.c test_fs_watch_init_failure.c
  LIBS Salts::Core Salts::TinyTest FOLDER "core/tests")
set_target_properties(saltscore_fs_watch_test PROPERTIES C_EXTENSIONS OFF)
set_tests_properties(saltscore_fs_watch_test PROPERTIES TIMEOUT 60)
target_include_directories(saltscore_fs_watch_test PRIVATE
  "${PROJECT_SOURCE_DIR}/core/src/file")
if(MSVC)
  target_compile_options(saltscore_fs_watch_test PRIVATE -experimental:c11atomics)
endif()

# Contracts migrated with the process implementation into Core.
cmake_add_executable(saltscore_process_test SOURCES test_process.c
  FOLDER "core/tests")
set_target_properties(saltscore_process_test PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(saltscore_process_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.process_spawn_reap COMMAND saltscore_process_test)
set_tests_properties(saltscore.process_spawn_reap PROPERTIES TIMEOUT 30)

cmake_add_test(saltscore_c11_atomics_test SOURCES test_c11_atomics.c
  LIBS Salts::Core Salts::TinyTest FOLDER "core/tests")
set_tests_properties(saltscore_c11_atomics_test PROPERTIES TIMEOUT 30)

# Compile the same formal tests and production C11 provider with an additional
# GCC, independently of the compiler producing the SDK. No dependency ports or
# second consumer project are configured for these test executables.
if(SALTS_TEST_GCC)
  if(NOT EXISTS "${SALTS_TEST_GCC}")
    message(FATAL_ERROR "SALTS_TEST_GCC does not exist: ${SALTS_TEST_GCC}")
  endif()
  cmake_add_source(gcc_provider_sources RECURSE DIRS ../src
    PATTERNS "portable_atomic.c" "portable_once.c" "c11_*.c"
    EXCLUDES ../src/c11_sleep_*.c)
  if(WIN32)
    set(gcc_provider_os win)
    set(gcc_provider_flags -D_WIN32_WINNT=0x0601)
  else()
    set(gcc_provider_os posix)
    set(gcc_provider_flags -D_DEFAULT_SOURCE=1 -D_POSIX_C_SOURCE=200809L -pthread)
    if(APPLE)
      list(APPEND gcc_provider_flags -D_DARWIN_C_SOURCE=1)
    endif()
  endif()
  cmake_add_source(gcc_provider_native RECURSE DIRS ../src
    PATTERNS "portable_thread_${gcc_provider_os}.c"
      "portable_tss_${gcc_provider_os}.c" "c11_sleep_${gcc_provider_os}.c")
  cmake_add_source(gcc_test_runtime DIRS "${PROJECT_SOURCE_DIR}/tinytest/src"
    PATTERNS "*.c")
  foreach(_case IN ITEMS test_c11_atomics test_c11_threads test_portable_concurrency)
    set(_exe "${CMAKE_CURRENT_BINARY_DIR}/gcc_${_case}${CMAKE_EXECUTABLE_SUFFIX}")
    add_custom_command(OUTPUT "${_exe}"
      COMMAND "${SALTS_TEST_GCC}" -std=c11 -O2 -Werror=implicit-function-declaration
        -DSALTS_API= ${gcc_provider_flags}
        "-I${PROJECT_SOURCE_DIR}/core/include"
        "-I${PROJECT_SOURCE_DIR}/core/src/threads"
        "-I${PROJECT_SOURCE_DIR}/tinytest/include"
        ${gcc_provider_sources} ${gcc_provider_native} ${gcc_test_runtime}
        "${CMAKE_CURRENT_SOURCE_DIR}/${_case}.c" -o "${_exe}"
      DEPENDS ${gcc_provider_sources} ${gcc_provider_native} ${gcc_test_runtime}
        ${core_public_headers} ${core_private_headers} "${_case}.c"
      VERBATIM)
    add_custom_target(gcc_${_case} ALL DEPENDS "${_exe}")
    cmake_config_target(gcc_${_case} FOLDER "core/tests/compiler")
    add_test(NAME "saltscore.gcc.${_case}" COMMAND "${_exe}")
    set_tests_properties("saltscore.gcc.${_case}" PROPERTIES TIMEOUT 45)
  endforeach()
  if(WIN32)
    foreach(_case IN ITEMS test_c11_atomics test_c11_threads)
      set(_exe "${CMAKE_CURRENT_BINARY_DIR}/gcc_dll_${_case}.exe")
      add_custom_command(OUTPUT "${_exe}"
        COMMAND "${SALTS_TEST_GCC}" -std=c11 -O2 -Werror=implicit-function-declaration
          "-I${PROJECT_SOURCE_DIR}/core/include"
          "-I${PROJECT_SOURCE_DIR}/core/src/threads"
          "-I${PROJECT_SOURCE_DIR}/tinytest/include"
          ${gcc_test_runtime} "${CMAKE_CURRENT_SOURCE_DIR}/${_case}.c"
          "$<TARGET_LINKER_FILE:Salts::Core>" -o "${_exe}"
        DEPENDS salts ${gcc_test_runtime} ${core_public_headers}
          ${core_private_headers} "${_case}.c"
        VERBATIM)
      add_custom_target(gcc_dll_${_case} ALL DEPENDS "${_exe}")
      cmake_config_target(gcc_dll_${_case} FOLDER "core/tests/compiler")
      add_test(NAME "saltscore.gcc.dll.${_case}" COMMAND "${_exe}")
      set_tests_properties("saltscore.gcc.dll.${_case}" PROPERTIES TIMEOUT 45)
    endforeach()
  endif()
endif()

# Existing ABI cases load the actual SDK DLL and exercise callbacks, shared
# atomic slots, TSS cleanup and timed waits across compiler/CRT boundaries.
foreach(_compiler IN ITEMS GCC TCC)
  if(SALTS_TEST_${_compiler} AND WIN32)
    string(TOLOWER "${_compiler}" _compiler_name)
    if(NOT EXISTS "${SALTS_TEST_${_compiler}}")
      message(FATAL_ERROR "SALTS_TEST_${_compiler} does not exist")
    endif()
    foreach(_case IN ITEMS portable_tcc_windows_client portable_tcc_tss_windows)
      set(_exe "${CMAKE_CURRENT_BINARY_DIR}/${_compiler_name}_${_case}.exe")
      add_custom_command(OUTPUT "${_exe}"
        COMMAND "${SALTS_TEST_${_compiler}}" -std=c11
          "-I${PROJECT_SOURCE_DIR}/core/include"
          "-I${PROJECT_SOURCE_DIR}/core/src/threads"
          "${CMAKE_CURRENT_SOURCE_DIR}/${_case}.c" -o "${_exe}"
        DEPENDS "${_case}.c" ${core_public_headers} ${core_private_headers}
        VERBATIM)
      add_custom_target(${_compiler_name}_${_case} ALL DEPENDS "${_exe}")
      cmake_config_target(${_compiler_name}_${_case} FOLDER "core/tests/compiler")
      add_test(NAME "saltscore.${_compiler_name}.${_case}"
        COMMAND "${_exe}" "$<TARGET_FILE:Salts::Core>")
      set_tests_properties("saltscore.${_compiler_name}.${_case}" PROPERTIES TIMEOUT 45)
    endforeach()
  elseif(SALTS_TEST_${_compiler} AND _compiler STREQUAL "TCC")
    message(FATAL_ERROR "The TinyCC ABI cases require Windows")
  endif()
endforeach()

get_property(_core_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_core_tests} PROPERTIES LABELS core)
