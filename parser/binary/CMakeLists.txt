
add_subdirectory(tlv_parser)
add_subdirectory(ltv_parser)
add_subdirectory(modbus_parser)
add_subdirectory(soa_parser)

set(SALTS_PARSER_TARGETS
  tlv_parser
  ltv_parser
  modbus_parser
  soa_parser
  )
set(SALTS_PARSER_TARGETS ${SALTS_PARSER_TARGETS} PARENT_SCOPE)

install(
  TARGETS ${SALTS_PARSER_TARGETS}
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Install the same public header layout exposed by the build targets.
foreach(_parser IN ITEMS   tlv_parser ltv_parser modbus_parser  )
  install(DIRECTORY "${_parser}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
endforeach()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

