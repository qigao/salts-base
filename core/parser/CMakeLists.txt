# Original utils/parser/fmt_lexer.re grammar is retained for parity review.
# The portable C11 version has exactly the original bounded-token semantics,
# without a mandatory re2c executable during a clean Core SDK build.
cmake_add_source(core_parser_sources DIRS . PATTERNS "*.c" "*.h")
target_sources(salts  PRIVATE ${core_parser_sources})
target_include_directories(salts PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
