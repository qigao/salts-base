cmake_add_benchmark(benchmark_parser_scan
  SOURCES benchmark_parser_scan.c
  LIBS Salts::XmlParser Salts::CYaml Salts::TomlParser Salts::CsvParser
       Salts::TinyTest
  FOLDER "parser/benchmarks")
add_test(NAME parser.scan_benchmark COMMAND benchmark_parser_scan)
set_tests_properties(parser.scan_benchmark PROPERTIES LABELS "benchmark;parser"
  TIMEOUT 60)

cmake_add_benchmark(benchmark_query_scan
  SOURCES benchmark_query_scan.c
  LIBS Salts::Selector Salts::CmdParser Salts::CYaml Salts::CsvParser
       Salts::Core Salts::TinyTest
  INCLUDES "${PROJECT_SOURCE_DIR}/parser/cmd_parser/src"
           "${PROJECT_SOURCE_DIR}/parser/cyaml/src"
  FOLDER "parser/benchmarks")
add_test(NAME parser.query_scan_benchmark COMMAND benchmark_query_scan)
set_tests_properties(parser.query_scan_benchmark PROPERTIES LABELS "benchmark;parser"
  TIMEOUT 60)
