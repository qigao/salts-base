message(STATUS "JSON Parser generation enabled")
message(STATUS "  re2c: ${RE2C_EXECUTABLE}")
message(STATUS "  lemon: in-tree target")
message(STATUS "Parser module enabled")
# =============================================================================
# Submodules (must be added first so we can link to them)
# =============================================================================

add_subdirectory(query_vm)
add_subdirectory(ini_parser)
add_subdirectory(json_parser)
add_subdirectory(xml_parser)
add_subdirectory(csv_parser)
add_subdirectory(tlv_parser)
add_subdirectory(ltv_parser)
add_subdirectory(modbus_parser)
add_subdirectory(soa_parser)
add_subdirectory(dotenv_parser)
add_subdirectory(cmd_parser)
add_subdirectory(toon)
add_subdirectory(toml)
add_subdirectory(datetime_parser)
add_subdirectory(cyaml)
add_subdirectory(selector)
add_subdirectory(markdown)

set(SALTS_PARSER_TARGETS
  ini_parser
  json_parser
  json_cserde_adapter
  xml_parser
  csv_parser
  tlv_parser
  ltv_parser
  modbus_parser
  soa_parser
  dotenv_parser
  cmd_parser
  toon
  toml_parser
  datetime_parser
  cyaml_json_adapter
  markdown_parser
  markdown_html)
set(SALTS_PARSER_TARGETS ${SALTS_PARSER_TARGETS} PARENT_SCOPE)

install(
  TARGETS ${SALTS_PARSER_TARGETS}
  EXPORT SaltsUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

set(SALTS_PARSER_FLAT_INCLUDE_DIRS
  ini_parser/include
  json_parser/include
  xml_parser/include
  csv_parser/include
  tlv_parser/include
  ltv_parser/include
  modbus_parser/include
  soa_parser/include
  dotenv_parser/include
  cmd_parser/include
  toml/include
  datetime_parser/include)

foreach(parser_include_dir IN LISTS SALTS_PARSER_FLAT_INCLUDE_DIRS)
  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${parser_include_dir}/"
    DESTINATION include
    FILES_MATCHING PATTERN "*.h")
endforeach()

install(
  FILES toon/toonc.h toon/adapter/toon_json_adapter.h
  DESTINATION include)
install(
  FILES cyaml/adapter/cyaml_json_adapter.h
  DESTINATION include/cyaml)
