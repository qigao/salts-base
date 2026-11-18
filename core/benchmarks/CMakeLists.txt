cmake_add_benchmark(benchmark_scan
  SOURCES benchmark_scan.c
  LIBS Salts::Core Salts::TinyTest
  INCLUDES "${PROJECT_SOURCE_DIR}/core/src"
  FOLDER "core/benchmarks")
target_include_directories(benchmark_scan SYSTEM PRIVATE "${CORE_SIMDE_INCLUDE_DIR}")
add_test(NAME saltscore.scan_benchmark COMMAND benchmark_scan)
set_tests_properties(saltscore.scan_benchmark PROPERTIES LABELS "benchmark;core"
  TIMEOUT 60)

if(WIN32)
  cmake_add_benchmark(benchmark_win_threads SOURCES thread_runtime_win.c
    LIBS Salts::Core Salts::TinyTest ntdll
    INCLUDES "${PROJECT_SOURCE_DIR}/core/src/threads"
    FOLDER "core/benchmarks")
  if(CMAKE_USE_PTHREADS_INIT)
    target_compile_definitions(benchmark_win_threads PRIVATE BENCH_PTHREAD=1)
    target_link_libraries(benchmark_win_threads PRIVATE Threads::Threads)
  endif()
  add_test(NAME saltscore.win_threads_benchmark COMMAND benchmark_win_threads)
  set_tests_properties(saltscore.win_threads_benchmark PROPERTIES LABELS "benchmark;core" TIMEOUT 90)
endif()
