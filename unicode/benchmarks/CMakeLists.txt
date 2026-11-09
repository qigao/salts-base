cmake_add_benchmark(benchmark_unicode_ascii
  SOURCES benchmark_unicode_ascii.c
  LIBS Salts::Unicode Salts::TinyTest
  FOLDER "unicode/benchmarks")
add_test(NAME saltscore.unicode_ascii_benchmark COMMAND benchmark_unicode_ascii)
set_tests_properties(saltscore.unicode_ascii_benchmark PROPERTIES
  LABELS "benchmark;unicode" TIMEOUT 60)
