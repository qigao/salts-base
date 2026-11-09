cmake_add_benchmark(benchmark_toon
  SOURCES benchmark_toon.c
  LIBS toon
  FOLDER "parser/toon/benchmarks")

if(UNIX)
  target_link_libraries(benchmark_toon PRIVATE m)
endif()
