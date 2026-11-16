
add_subdirectory(ini_parser)
add_subdirectory(json_parser)
add_subdirectory(xml_parser)
add_subdirectory(csv_parser)


set(SALTS_PARSER_TARGETS
  ini_parser
  json_parser 
  xml_parser
  csv_parser
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
foreach(_parser IN ITEMS ini_parser json_parser xml_parser csv_parser
      dotenv_parser cmd_parser toml datetime_parser)
  install(DIRECTORY "${_parser}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
endforeach()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

