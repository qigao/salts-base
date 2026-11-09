set(SALTS_CXML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/cxml)

# Match the enabled upstream components; the separate SAX implementation is
# not part of this object owner (the public SAX adapter lives in ../src).
cmake_add_source(SALTS_CXML_SOURCES
  DIRS cxml/src/core cxml/src/cxml cxml/src/query cxml/src/utils
    cxml/src/xml cxml/src/xpath
  PATTERNS "*.c")

add_library(cxml OBJECT ${SALTS_CXML_SOURCES})
target_compile_features(cxml PRIVATE c_std_11)
target_include_directories(cxml PRIVATE
  ${SALTS_CXML_ROOT}/include)
target_link_libraries(cxml PRIVATE
  Salts::Core Salts::QueryVM)
target_compile_definitions(cxml PRIVATE
  CXML_USE_QUERY_MOD CXML_USE_XPATH_MOD)

if(MSVC)
  target_compile_options(cxml PRIVATE /W4 /wd4100 /wd4244 /wd4267)
else()
  target_compile_options(cxml PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-conversion)
endif()

cmake_config_target(cxml
  FOLDER "parser/xml_parser/vendor")

# This private engine is embedded in multiple shared libraries. Its globals and
# functions must not escape those images or interpose on another engine copy.
set_target_properties(cxml PROPERTIES C_VISIBILITY_PRESET hidden)
