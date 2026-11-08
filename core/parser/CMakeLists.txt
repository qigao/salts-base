# Original utils/parser/fmt_lexer.re grammar is retained for parity review.
# The portable C11 version has exactly the original bounded-token semantics,
# without a mandatory re2c executable during a clean Core SDK build.
target_sources(saltscore_core PRIVATE fmt_lexer.c log_pattern_lexer.c)
target_include_directories(saltscore_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
install(FILES fmt_lexer.h log_pattern_lexer.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
