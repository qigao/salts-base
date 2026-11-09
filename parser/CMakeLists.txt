
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
add_subdirectory(uri)

set(SALTS_PARSER_TARGETS
  uri
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
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

