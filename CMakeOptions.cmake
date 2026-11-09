option(BUILD_TESTS "Enable TESTS" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
set(SALTS_TEST_GCC "" CACHE FILEPATH "Additional GCC compiler for the formal C11 provider/ABI tests")
set(SALTS_TEST_TCC "" CACHE FILEPATH "Additional TinyCC compiler for the formal Windows C ABI tests")
set(SALTS_UNICODE_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/build/unicode-data/17.0.0"
  CACHE PATH "Verified Unicode 17.0.0 source and conformance data cache")
option(SALTSCORE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SALTSCORE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SALTSCORE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Reject stale cache/environment injection before project() can load a file.
if(CMAKE_TOOLCHAIN_FILE OR NOT "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
  message(FATAL_ERROR "Toolchain files are unsupported. Select compilers in presets/toolkits; migrate old build caches with --fresh.")
endif()

foreach(_input IN ITEMS VCPKG_INSTALLED_DIR VCPKG_TARGET_TRIPLET)
  if(NOT DEFINED ${_input} OR "${${_input}}" STREQUAL "")
    message(FATAL_ERROR "Required preset input is empty: ${_input}")
  endif()
endforeach()
if(NOT IS_DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  message(FATAL_ERROR
    "Missing vcpkg-cache installation: ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}. Restore the matching prebuilt packages before configuring; source-build fallback is disabled.")
endif()
unset(_input)
