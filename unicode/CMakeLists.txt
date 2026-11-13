# Use the same host re2c binary and Unicode 17 property tables provided by
# the canonical vcpkg-cache setup-re2c-tools action.  Runtime consumers do
# not require re2c, ICU, SaltsBase or external Unicode data files.
set(RE2C_UNICODE_PROPERTIES "${RE2C_ROOT}/share/re2c/stdlib/unicode_properties.re")
set(RE2C_UNICODE_CATEGORIES "${RE2C_ROOT}/share/re2c/stdlib/unicode_categories.re")
foreach(_table IN ITEMS "${RE2C_UNICODE_PROPERTIES}" "${RE2C_UNICODE_CATEGORIES}")
  if(NOT EXISTS "${_table}")
    message(FATAL_ERROR "Required Unicode 17 re2c table is missing: ${_table}")
  endif()
endforeach()
# Generated Unicode 17 semantics cannot silently change when the *latest* re2c
# tool package upgrades. These content hashes are for the Unicode 17.0.0
# upstream re2c 4.6 stdlib data, not a pin on the re2c tool package version.
# Normalize Windows CRLF (Git checkout) to canonical LF before hashing.
file(READ "${RE2C_UNICODE_PROPERTIES}" _unicode17_properties)
string(REPLACE "\r\n" "\n" _unicode17_properties "${_unicode17_properties}")
string(SHA256 _unicode17_properties_sha256 "${_unicode17_properties}")
if(NOT _unicode17_properties_sha256 STREQUAL
       "56b5f16e4e516a598125760ee7f4fae60a5db6727b8cf3dfb5f020152ef2aa21")
  message(FATAL_ERROR
    "re2c Unicode property database differs from canonical Unicode 17.0.0; "
    "explicit data/API/conformance upgrade required")
endif()
file(READ "${RE2C_UNICODE_CATEGORIES}" _unicode17_categories)
string(REPLACE "\r\n" "\n" _unicode17_categories "${_unicode17_categories}")
string(SHA256 _unicode17_categories_sha256 "${_unicode17_categories}")
if(NOT _unicode17_categories_sha256 STREQUAL
       "3d0a319806e42a7b476f9d474c104f7e734c42eeab8fb36b2b82591cd55b0689")
  message(FATAL_ERROR
    "re2c Unicode category database differs from canonical Unicode 17.0.0; "
    "explicit data/API/conformance upgrade required")
endif()
unset(_unicode17_properties)
unset(_unicode17_categories)
unset(_unicode17_properties_sha256)
unset(_unicode17_categories_sha256)

# Default Full Case Folding belongs to the same pinned Unicode 17.0.0
# database as re2c properties/categories. The committed generated C11 table
# is an immutable release input. Restore raw data only for tests or regeneration.
# Normalize checkout CRLF before hashing (Windows); regeneration requires an explicit
# new data/API review, never a silent SDK semantic change.
set(_unicode17_casefold_generated "${CMAKE_CURRENT_SOURCE_DIR}/src/unicode_casefold_data.h")
if(NOT EXISTS "${_unicode17_casefold_generated}")
  message(FATAL_ERROR "Required Unicode 17 casefold release fixture is missing: ${_unicode17_casefold_generated}")
endif()
file(READ "${_unicode17_casefold_generated}" _unicode17_casefold_generated_text)
string(REPLACE "\r\n" "\n" _unicode17_casefold_generated_text "${_unicode17_casefold_generated_text}")
string(SHA256 _unicode17_casefold_generated_sha256 "${_unicode17_casefold_generated_text}")
if(NOT _unicode17_casefold_generated_sha256 STREQUAL
       "370670365c3d9ab9ab852cc9f97b5bfde666909f0eab3fc599af7ce5d4a8d19d")
  message(FATAL_ERROR
    "Generated Unicode 17 Default Full Case Folding table differs from "
    "its qualified immutable fixture; rerun generator and qualify changes")
endif()
unset(_unicode17_casefold_generated)
unset(_unicode17_casefold_generated_text)
unset(_unicode17_casefold_generated_sha256)

execute_process(
  COMMAND "${RE2C_EXECUTABLE}" --version
  RESULT_VARIABLE _re2c_exit
  OUTPUT_VARIABLE _re2c_output
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _re2c_exit EQUAL 0 OR
   NOT _re2c_output MATCHES "re2c ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
  message(FATAL_ERROR "Cannot determine re2c version for Salts::Unicode")
endif()
set(_re2c_version "${CMAKE_MATCH_1}")
if(_re2c_version VERSION_LESS 4.6)
  message(FATAL_ERROR "Salts::Unicode requires re2c 4.6 or newer (found ${_re2c_version})")
endif()
get_filename_component(_re2c_actual "${RE2C_EXECUTABLE}" REALPATH)
get_filename_component(_re2c_root_actual "${RE2C_ROOT}" REALPATH)
cmake_path(IS_PREFIX _re2c_root_actual "${_re2c_actual}" NORMALIZE _unicode_re2c_in_root)
if(NOT _unicode_re2c_in_root)
  message(FATAL_ERROR "re2c executable does not belong to RE2C_ROOT: ${RE2C_EXECUTABLE}")
endif()

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
