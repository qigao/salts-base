cmake_add_source(BENCHMARK_SOURCES DIRS . PATTERNS "benchmark_*.c")

foreach(benchmark_source ${BENCHMARK_SOURCES})
  get_filename_component(benchmark_name ${benchmark_source} NAME_WE)
  cmake_add_benchmark(${benchmark_name}
    SOURCES ${benchmark_source}
    FOLDER "parser/${TARGET_NAME}/benchmarks"
    LIBS json_parser Salts::TinyTest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
             ${CMAKE_CURRENT_SOURCE_DIR}/../parser
             ${SIMDE_INCLUDE_DIRS})
endforeach()
