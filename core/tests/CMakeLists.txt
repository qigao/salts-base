

cmake_add_executable(saltscore_utf8_test SOURCES test_utf8.c
  FOLDER "core/tests")
target_link_libraries(saltscore_utf8_test PRIVATE Salts::Core Salts::TinyTest)
target_compile_features(saltscore_utf8_test PRIVATE c_std_11)
set_target_properties(saltscore_utf8_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.utf8_strict COMMAND saltscore_utf8_test)

cmake_add_executable(saltscore_runtime_abi_test SOURCES test_runtime_abi.c
  FOLDER "core/tests")
target_compile_features(saltscore_runtime_abi_test PRIVATE c_std_11)
target_link_libraries(saltscore_runtime_abi_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_runtime_abi_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.runtime_abi_0_1 COMMAND saltscore_runtime_abi_test)

# Plain C11 borrowed string view: no reflection headers, allocator or ABI shim.
cmake_add_executable(saltscore_vstr_contract_test SOURCES test_vstr_contract.c
  FOLDER "core/tests")
target_link_libraries(saltscore_vstr_contract_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_vstr_contract_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.vstr_c11 COMMAND saltscore_vstr_contract_test)

# Base64 must remain one installed native owner, no unrelated vendor codec.
cmake_add_executable(saltscore_base64_test SOURCES test_base64_utils.c
  FOLDER "core/tests")
target_compile_features(saltscore_base64_test PRIVATE c_std_11)
target_link_libraries(saltscore_base64_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_base64_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
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
set_target_properties(saltscore_bytes_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.bytes COMMAND saltscore_bytes_test)

# Preserve the original error registry and borrowed diagnostic lifetimes.
# The native contract includes INT_MIN decoding and concurrent registration.
cmake_add_executable(saltscore_error_contract SOURCES error_native_contract.c
  FOLDER "core/tests")
target_compile_features(saltscore_error_contract PRIVATE c_std_11)
target_link_libraries(saltscore_error_contract PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_error_contract PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.error_native_contract COMMAND saltscore_error_contract)
set_tests_properties(saltscore.error_native_contract PROPERTIES TIMEOUT 30)
cmake_add_executable(saltscore_original_error_test SOURCES test_cmeta_error.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_error_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_error_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_error COMMAND saltscore_original_error_test)



cmake_add_executable(saltscore_buffer_test SOURCES test_buffer.c
  FOLDER "core/tests")
target_link_libraries(saltscore_buffer_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_buffer_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.buffer COMMAND saltscore_buffer_test)

cmake_add_executable(saltscore_ring_test SOURCES test_ring.c
  FOLDER "core/tests")
target_link_libraries(saltscore_ring_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(saltscore_ring_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME saltscore.ring COMMAND saltscore_ring_test)







# Restore original tstr/vstr/memory-pool behavior, not just public headers.
cmake_add_executable(saltscore_original_string_parity_test SOURCES test_original_string_parity.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_string_parity_test PRIVATE c_std_11)
set_target_properties(saltscore_original_string_parity_test PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_string_parity_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_utils_string_parity
  COMMAND saltscore_original_string_parity_test)
set_tests_properties(saltscore.original_utils_string_parity PROPERTIES TIMEOUT 20)

# Literal copy of the original TinyTest tstr v2 contract from Salts utils.
# Optional TinyTest profile runs source-parity expectations; never a stub.
cmake_add_executable(saltscore_original_tstr_v2_contract SOURCES test_tstr_v2_contract.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_tstr_v2_contract PRIVATE c_std_11)
target_link_libraries(saltscore_original_tstr_v2_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tstr_v2_contract
  COMMAND saltscore_original_tstr_v2_contract)

# Original object_pool source and TinyTest contract, not a new allocator
# provider. The C11 smoke test also runs when TinyTest is disabled.
cmake_add_executable(saltscore_original_object_pool_smoke SOURCES test_original_object_pool_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_object_pool_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_original_object_pool_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_object_pool_smoke
  COMMAND saltscore_original_object_pool_smoke)
cmake_add_executable(saltscore_original_object_pool_contract SOURCES test_object_pool.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_object_pool_contract PRIVATE c_std_11)
target_link_libraries(saltscore_original_object_pool_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_object_pool_contract
  COMMAND saltscore_original_object_pool_contract)
set_tests_properties(saltscore.original_object_pool_contract PROPERTIES TIMEOUT 45)

# The original single-owner priority queue: FIFO within a priority and
# critical/high/normal/low ordering, with strict bounded storage.
cmake_add_executable(saltscore_original_priority_queue_smoke SOURCES test_original_priority_queue_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_priority_queue_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_original_priority_queue_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_priority_queue_smoke COMMAND saltscore_original_priority_queue_smoke)
cmake_add_executable(saltscore_original_priority_queue_contract SOURCES test_bucket_priority_queue.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_priority_queue_contract PRIVATE c_std_11)
target_link_libraries(saltscore_original_priority_queue_contract PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_priority_queue_contract
  COMMAND saltscore_original_priority_queue_contract)



# Original formatter lexer and C11 smoke both execute through TinyTest.
cmake_add_executable(saltscore_fmt_lexer_smoke SOURCES test_fmt_lexer_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_fmt_lexer_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_fmt_lexer_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_lexer_smoke COMMAND saltscore_fmt_lexer_smoke)
cmake_add_executable(saltscore_original_fmt_lexer_test SOURCES test_fmt_re2c.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_fmt_lexer_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_fmt_lexer_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_lexer_contract COMMAND saltscore_original_fmt_lexer_test)

# Original formatter implementation: typed C11 and C++17 metadata, bounded
# rendering, invalid specifier rejection, time formatting, dynamic tstr and views.
cmake_add_executable(saltscore_original_fmt_test SOURCES test_fmt.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_fmt_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_fmt_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_c11 COMMAND saltscore_original_fmt_test)

cmake_add_executable(saltscore_original_fmt_cpp_test SOURCES test_fmt_cpp.cpp
  FOLDER "core/tests")
target_compile_features(saltscore_original_fmt_cpp_test PRIVATE cxx_std_17)
target_link_libraries(saltscore_original_fmt_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_cpp17 COMMAND saltscore_original_fmt_cpp_test)

cmake_add_executable(saltscore_original_fmt_view_test SOURCES test_fmt_vstr_pattern.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_fmt_view_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_fmt_view_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_bounded_vstr COMMAND saltscore_original_fmt_view_test)
# Original formatter contract: TinyTest is the mandatory source assertion runner.
cmake_add_executable(saltscore_fmt_smoke SOURCES test_fmt_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_fmt_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_fmt_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_fmt_smoke COMMAND saltscore_fmt_smoke)

# Original log pattern lexer tokens, C11 parser/format ownership.
cmake_add_executable(saltscore_log_pattern_lexer_test SOURCES test_log_pattern_lexer.c
  FOLDER "core/tests")
target_compile_features(saltscore_log_pattern_lexer_test PRIVATE c_std_11)
target_link_libraries(saltscore_log_pattern_lexer_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_log_pattern_lexer
  COMMAND saltscore_log_pattern_lexer_test)

# Original async logger: the original TinyTest and native callback exercise
# real Disruptor/FS providers, thread drain, shutdown, not a logger mock.
cmake_add_executable(saltscore_tlog_smoke SOURCES test_tlog_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_tlog_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_tlog_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tlog_smoke COMMAND saltscore_tlog_smoke)
set_tests_properties(saltscore.original_tlog_smoke PROPERTIES TIMEOUT 40)
foreach(_tlog_suite IN ITEMS tlog tlog_vstr_abi tlog_header_collision)
  cmake_add_executable(saltscore_original_${_tlog_suite}_test SOURCES test_${_tlog_suite}.c
    FOLDER "core/tests")
  target_compile_features(saltscore_original_${_tlog_suite}_test PRIVATE c_std_11)
  target_link_libraries(saltscore_original_${_tlog_suite}_test PRIVATE
    Salts::Core Salts::TinyTest)
  add_test(NAME saltscore.original_${_tlog_suite}
    COMMAND saltscore_original_${_tlog_suite}_test)
  set_tests_properties(saltscore.original_${_tlog_suite} PROPERTIES TIMEOUT 120)
endforeach()
cmake_add_executable(saltscore_original_tlog_cpp_test SOURCES test_tlog_cpp.cpp
  FOLDER "core/tests")
target_compile_features(saltscore_original_tlog_cpp_test PRIVATE cxx_std_17)
target_link_libraries(saltscore_original_tlog_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_tlog_cpp COMMAND saltscore_original_tlog_cpp_test)
set_tests_properties(saltscore.original_tlog_cpp PROPERTIES TIMEOUT 120)

# Original RFC 4122/9562 UUID v4/v7 and canonical parse/format.
cmake_add_executable(saltscore_uuid_smoke SOURCES test_uuid_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_uuid_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_uuid_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_smoke COMMAND saltscore_uuid_smoke)
cmake_add_executable(saltscore_original_uuid_test SOURCES test_cmeta_uuid.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_uuid_test PRIVATE c_std_11)
target_link_libraries(saltscore_original_uuid_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_c11 COMMAND saltscore_original_uuid_test)
cmake_add_executable(saltscore_original_uuid_cpp_test SOURCES test_cmeta_uuid_cpp.cpp
  FOLDER "core/tests")
target_compile_features(saltscore_original_uuid_cpp_test PRIVATE cxx_std_17)
target_link_libraries(saltscore_original_uuid_cpp_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_uuid_cpp17 COMMAND saltscore_original_uuid_cpp_test)

# Restore the original single-owner, hard-capped byte accumulator. This is not
# the borrowed-only salts_core_bytes_view/slice contract.
cmake_add_executable(saltscore_cmeta_bytes_smoke SOURCES test_cmeta_bytes_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_cmeta_bytes_smoke PRIVATE c_std_11)
target_link_libraries(saltscore_cmeta_bytes_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bytes_accumulator_smoke COMMAND saltscore_cmeta_bytes_smoke)
cmake_add_executable(saltscore_original_bytes_accumulator SOURCES test_cmeta_bytes.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_bytes_accumulator PRIVATE c_std_11)
target_link_libraries(saltscore_original_bytes_accumulator PRIVATE
  Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_bytes_accumulator_contract
  COMMAND saltscore_original_bytes_accumulator)

# Original seven-function bounded byte scan contract. This is the canonical
# portable C11 implementation; SIMD throughput parity is tracked separately.
cmake_add_executable(saltscore_simd_scan_contract SOURCES test_simd_scan_contract.c
  FOLDER "core/tests")
target_compile_features(saltscore_simd_scan_contract PRIVATE c_std_11)
target_link_libraries(saltscore_simd_scan_contract PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_simd_scan_contract COMMAND saltscore_simd_scan_contract)


# Actual original qigao/salts utils native memory mapping, not a mock
# and not a CMeta/CFlow runtime. Real file page / range / write / group.
cmake_add_executable(saltscore_original_native_mmap_test SOURCES test_original_native_mmap.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_native_mmap_test PRIVATE c_std_11)
set_target_properties(saltscore_original_native_mmap_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_native_mmap_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_native_mmap
  COMMAND saltscore_original_native_mmap_test)
set_tests_properties(saltscore.original_native_mmap PROPERTIES TIMEOUT 30)

# Frozen source/accounting policies run once in the Migration gate.
# Native CTest below remains executable behavior only.

# qigao/salts original utils native byte regex, 10 owned public C11 functions.
# Always-on compiled source/runtime; TinyTest retains all 25 donor test cases.
cmake_add_executable(saltscore_original_re_smoke SOURCES test_re_smoke.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_re_smoke PRIVATE c_std_11)
set_target_properties(saltscore_original_re_smoke PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_re_smoke PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_re_smoke COMMAND saltscore_original_re_smoke)
set_tests_properties(saltscore.original_re_smoke PROPERTIES TIMEOUT 25)

cmake_add_executable(saltscore_original_re_contract SOURCES test_re.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_re_contract PRIVATE c_std_11)
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
cmake_add_executable(saltscore_original_zstd_test SOURCES test_cmeta_zstd.c
  FOLDER "core/tests")
target_compile_features(saltscore_original_zstd_test PRIVATE c_std_11)
set_target_properties(saltscore_original_zstd_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_original_zstd_test PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.original_zstd COMMAND saltscore_original_zstd_test)

cmake_add_executable(saltscore_zstd_contract SOURCES zstd_native_contract.c
  FOLDER "core/tests")
target_compile_features(saltscore_zstd_contract PRIVATE c_std_11)
set_target_properties(saltscore_zstd_contract PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_zstd_contract PRIVATE Salts::Core Salts::TinyTest)
add_test(NAME saltscore.zstd_native_contract COMMAND saltscore_zstd_contract)

if(CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME saltscore.zstd_private_archive
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/core/tests/test_zstd_binary_owner.py"
      --library "$<TARGET_FILE:saltscore_core>"
      --nm "${CMAKE_NM}" --readelf "${CMAKE_READELF}")
  set_tests_properties(saltscore.zstd_private_archive PROPERTIES TIMEOUT 60)
endif()
set_tests_properties(saltscore.original_zstd saltscore.zstd_native_contract
  PROPERTIES TIMEOUT 60)
