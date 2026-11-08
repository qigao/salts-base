cmake_add_benchmark(benchmark_toon
  SOURCES benchmark_toon.c
  LIBS toon
  FOLDER "parser/toon/benchmarks")

if(UNIX)
  target_link_libraries(benchmark_toon PRIVATE m)
endif()

cmake_add_benchmark(benchmark_simd_scan
  SOURCES benchmark_simd_scan.c
  LIBS Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${SIMDE_INCLUDE_DIRS}
  FOLDER "parser/toon/benchmarks")
