cmake_add_benchmark(benchmark_scan
  SOURCES benchmark_scan.c
  LIBS Salts::Core Salts::TinyTest
  INCLUDES "${PROJECT_SOURCE_DIR}/core/src"
  FOLDER "core/benchmarks")
target_include_directories(benchmark_scan SYSTEM PRIVATE "${CORE_SIMDE_INCLUDE_DIR}")
add_test(NAME saltscore.scan_benchmark COMMAND benchmark_scan)
set_tests_properties(saltscore.scan_benchmark PROPERTIES LABELS "benchmark;core"
  TIMEOUT 60)
