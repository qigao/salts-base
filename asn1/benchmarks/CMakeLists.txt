cmake_add_benchmark(benchmark_asn1_scan
  SOURCES benchmark_asn1_scan.c
  LIBS Salts::ASN1 Salts::TinyTest
  FOLDER "asn1/benchmarks")
add_test(NAME asn1.scan_benchmark COMMAND benchmark_asn1_scan)
set_tests_properties(asn1.scan_benchmark PROPERTIES LABELS "benchmark;asn1" TIMEOUT 60)
