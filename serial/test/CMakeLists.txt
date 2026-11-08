# All original public tests run through the production shared owner. The
# previously unregistered review-fix test is also retained and executed.
foreach(_serial_suite IN ITEMS test_salts_serial test_salts_serial_public_abi test_fixes)
  cmake_add_executable(saltscore_${_serial_suite} SOURCES ${_serial_suite}.c
    FOLDER "serial/test")
  target_compile_features(saltscore_${_serial_suite} PRIVATE c_std_11)
  set_target_properties(saltscore_${_serial_suite} PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(saltscore_${_serial_suite} PRIVATE Salts::Serial Salts::TinyTest)
endforeach()
add_test(NAME saltscore.serial_original_c11 COMMAND saltscore_test_salts_serial)
add_test(NAME saltscore.serial_public_abi COMMAND saltscore_test_salts_serial_public_abi)
add_test(NAME saltscore.serial_original_fixes COMMAND saltscore_test_fixes)

# The same source/backend is compiled with only its original test-owned
# injection hooks enabled. This is never installed or linked to production.
get_target_property(_serial_test_sources saltscore_serial SOURCES)
add_library(saltscore_serial_test_impl STATIC ${_serial_test_sources})
target_compile_features(saltscore_serial_test_impl PRIVATE c_std_11)
set_target_properties(saltscore_serial_test_impl PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(saltscore_serial_test_impl PRIVATE
  SALTS_SERIAL_EXPORTS SALTS_SERIAL_TESTING)
target_include_directories(saltscore_serial_test_impl
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(saltscore_serial_test_impl PRIVATE
  Salts::Core Salts::Platform Salts::Concurrency)
if(WIN32)
  target_link_libraries(saltscore_serial_test_impl PRIVATE setupapi cfgmgr32)
endif()
if(UNIX)
  target_compile_definitions(saltscore_serial_test_impl PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L
    $<$<BOOL:${APPLE}>:_DARWIN_C_SOURCE=1>)
endif()

foreach(_serial_suite IN ITEMS test_salts_serial_mock test_serial_storage)
  cmake_add_executable(saltscore_${_serial_suite} SOURCES ${_serial_suite}.c
    FOLDER "serial/test")
  target_compile_features(saltscore_${_serial_suite} PRIVATE c_std_11)
  set_target_properties(saltscore_${_serial_suite} PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_definitions(saltscore_${_serial_suite} PRIVATE
    SALTS_SERIAL_EXPORTS SALTS_SERIAL_TESTING)
  target_include_directories(saltscore_${_serial_suite} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
  target_link_libraries(saltscore_${_serial_suite} PRIVATE
    saltscore_serial_test_impl Salts::Core Salts::Platform Salts::TinyTest)
endforeach()
add_test(NAME saltscore.serial_backend_contract COMMAND saltscore_test_salts_serial_mock)
add_test(NAME saltscore.serial_storage_ownership COMMAND saltscore_test_serial_storage)

if(UNIX)
  cmake_add_executable(saltscore_serial_pty SOURCES test_serial_pty.c
    FOLDER "serial/test")
  target_compile_features(saltscore_serial_pty PRIVATE c_std_11)
  set_target_properties(saltscore_serial_pty PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_definitions(saltscore_serial_pty PRIVATE
    _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1 _XOPEN_SOURCE=700)
  target_link_libraries(saltscore_serial_pty PRIVATE Salts::Serial Salts::TinyTest)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(saltscore_serial_pty PRIVATE util)
  endif()
  add_test(NAME saltscore.serial_posix_pty COMMAND saltscore_serial_pty)
  set_tests_properties(saltscore.serial_posix_pty PROPERTIES TIMEOUT 30)
endif()

set_tests_properties(saltscore.serial_original_c11 saltscore.serial_public_abi
  saltscore.serial_original_fixes saltscore.serial_backend_contract
  saltscore.serial_storage_ownership
  PROPERTIES TIMEOUT 60)
