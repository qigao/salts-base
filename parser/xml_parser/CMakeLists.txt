add_subdirectory(vendor)

add_library(xml_parser STATIC src/xml_parser.c src/xml_sax.c)
target_sources(xml_parser PRIVATE $<TARGET_OBJECTS:salts_cxml>)
cmake_config_target(xml_parser
  ALIAS Salts::XmlParser
  FOLDER "parser/xml_parser"
  EXPORT_NAME XmlParser)
target_compile_features(xml_parser PUBLIC c_std_11)
target_link_libraries(xml_parser
  PUBLIC Salts::QueryVM
  PRIVATE Salts::Core)
if(NOT WIN32)
  # cxml objects call libm; the final archive must carry that link requirement.
  target_link_libraries(xml_parser PRIVATE m)
endif()
target_include_directories(xml_parser
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cxml/include)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
