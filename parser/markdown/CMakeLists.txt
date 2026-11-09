# One C11 Markdown grammar owner. Stun and TurboAgent are consumers only.
# No UI runtime, CMeta/Widget, cmark/MD4C production fallback or second parser.
cmake_add_source(markdown_sources DIRS src PATTERNS "*.c" "*.h"
  EXCLUDES src/markdown_html*.c)
add_library(markdown_parser STATIC ${markdown_sources})
target_compile_features(markdown_parser PUBLIC c_std_11)
cmake_config_target(markdown_parser
  ALIAS Salts::MarkdownParser
  EXPORT_NAME MarkdownParser
  FOLDER "parser/markdown")
target_include_directories(markdown_parser
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(markdown_parser PUBLIC Salts::Unicode)

# Separate HTML presentation policy; it must never become a second parser.
cmake_add_source(markdown_html_sources DIRS src PATTERNS "markdown_html*.c")
add_library(markdown_html STATIC ${markdown_html_sources})
target_compile_features(markdown_html PUBLIC c_std_11)
cmake_config_target(markdown_html
  ALIAS Salts::MarkdownHtml
  EXPORT_NAME MarkdownHtml
  FOLDER "parser/markdown")
target_include_directories(markdown_html
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(markdown_html PUBLIC Salts::MarkdownParser)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
