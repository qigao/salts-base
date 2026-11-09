cmake_add_test(xml_parser_test
  SOURCES xml_parser_test.c
  LIBS Salts::XmlParser Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "parser/xml_parser/test")

target_include_directories(xml_parser_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/cxml/include)

set_target_properties(xml_parser_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(xml_sax_parser_test
  SOURCES xml_sax_parser_test.c
  LIBS Salts::XmlParser Salts::TinyTest
  FOLDER "parser/xml_parser/test")
