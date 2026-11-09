cmake_add_benchmark(benchmark_markdown_scan
  SOURCES benchmark_markdown_scan.c
  LIBS Salts::MarkdownParser Salts::TinyTest
  FOLDER "parser/markdown/benchmarks")
add_test(NAME markdown.scan_benchmark COMMAND benchmark_markdown_scan)
set_tests_properties(markdown.scan_benchmark PROPERTIES LABELS "benchmark;markdown"
  TIMEOUT 60)
