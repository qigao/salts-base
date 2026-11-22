# Use the same host re2c binary and Unicode 17 property tables provided by
# the canonical vcpkg-cache setup-re2c-tools action.  Runtime consumers do
# not require re2c, ICU, SaltsBase or external Unicode data files.
set(RE2C_UNICODE_PROPERTIES "${RE2C_ROOT}/share/re2c/stdlib/unicode_properties.re")
set(RE2C_UNICODE_CATEGORIES "${RE2C_ROOT}/share/re2c/stdlib/unicode_categories.re")
include("${CMAKE_CURRENT_SOURCE_DIR}/data/VerifyRelease.cmake")

cmake_add_grammar(saltscore_unicode
  LEXER_RE src/unicode.re
  LEXER_OUTPUT unicode_gen.c
  RE2C_EXECUTABLE "${RE2C_EXECUTABLE}"
  LEXER_DEPENDS "${RE2C_UNICODE_PROPERTIES}" "${RE2C_UNICODE_CATEGORIES}"
  LEXER_OPTIONS --lang c --utf8 --encoding-policy fail
    --no-generation-date -W -Werror -I "${RE2C_ROOT}/share/re2c/stdlib"
  FOLDER "unicode")
cmake_add_source(unicode_sources DIRS src)
cmake_add_source(unicode_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND unicode_sources ${unicode_sources_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${unicode_sources})
source_group("Generated" FILES ${saltscore_unicode_LEXER_GEN})
add_library(saltscore_unicode STATIC ${unicode_sources} ${saltscore_unicode_LEXER_GEN})
cmake_config_target(saltscore_unicode ALIAS Salts::Unicode FOLDER "unicode"
  EXPORT_NAME Unicode)
set_target_properties(saltscore_unicode PROPERTIES C_EXTENSIONS OFF)
target_compile_features(saltscore_unicode PUBLIC c_std_11)
target_include_directories(saltscore_unicode
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_unicode PUBLIC Salts::Core PRIVATE Salts::SIMD)
install(TARGETS saltscore_unicode EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES data/LICENSE.txt
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/unicode)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
