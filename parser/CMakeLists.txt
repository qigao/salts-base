# =============================================================================
# Submodules (must be added first so we can link to them)
# =============================================================================


add_subdirectory(binary)
add_subdirectory(utils)
add_subdirectory(data)
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
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Install the same public header layout exposed by the build targets.
foreach(_parser IN ITEMS data/ini_parser data/json_parser data/xml_parser data/csv_parser
      binary/tlv_parser binary/ltv_parser binary/modbus_parser binary/soa_parser
      utils/dotenv_parser utils/cmd_parser utils/toml utils/datetime_parser)
  install(DIRECTORY "${_parser}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
endforeach()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

