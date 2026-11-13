option(BUILD_TESTS "Enable TESTS" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)

set(SALTS_UNICODE_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/build/unicode-data/17.0.0"
  CACHE PATH "Verified Unicode 17.0.0 source and conformance data cache")
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
