foreach(_case IN ITEMS reader value scan)
  cmake_add_executable(asn1_${_case}_test SOURCES ${_case}.c
    FOLDER "asn1/tests")
  target_link_libraries(asn1_${_case}_test PRIVATE Salts::ASN1 Salts::TinyTest)
  set_target_properties(asn1_${_case}_test PROPERTIES C_EXTENSIONS OFF)
  add_test(NAME asn1.${_case} COMMAND asn1_${_case}_test)
endforeach()
foreach(_case IN ITEMS crypto_random x509_integration mib_compiler)
  cmake_add_executable(asn1_${_case}_test SOURCES test_${_case}.c
    FOLDER "asn1/tests")
  target_link_libraries(asn1_${_case}_test PRIVATE Salts::ASN1 Salts::TinyTest)
  set_target_properties(asn1_${_case}_test PROPERTIES C_EXTENSIONS OFF)
  add_test(NAME asn1.${_case} COMMAND asn1_${_case}_test)
endforeach()
target_include_directories(asn1_mib_compiler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(mib_generated_sources)
foreach(_fixture IN ITEMS basic empty)
  if(_fixture STREQUAL "basic")
    set(_module Basic-MIB)
  else()
    set(_module Empty-MIB)
  endif()
  set(_outputs "${CMAKE_CURRENT_BINARY_DIR}/${_module}.h"
    "${CMAKE_CURRENT_BINARY_DIR}/${_module}.c"
    "${CMAKE_CURRENT_BINARY_DIR}/${_module}_runtime.c")
  add_custom_command(OUTPUT ${_outputs}
    COMMAND mib_compiler -o "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${_fixture}.mib"
    DEPENDS mib_compiler "${CMAKE_CURRENT_SOURCE_DIR}/${_fixture}.mib"
    VERBATIM)
  list(APPEND mib_generated_sources ${_outputs})
endforeach()
cmake_add_executable(asn1_mib_generated_test
  SOURCES test_mib_generated.c ${mib_generated_sources}
  LIBS Salts::ASN1 Salts::TinyTest INCLUDES "${CMAKE_CURRENT_BINARY_DIR}"
  FOLDER "asn1/tests")
set_target_properties(asn1_mib_generated_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME asn1.mib_generated COMMAND asn1_mib_generated_test)
find_package(Python3 3.12 COMPONENTS Interpreter REQUIRED)
add_test(NAME asn1.mib_cli COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_mib_cli.py" "$<TARGET_FILE:mib_compiler>")
cmake_add_executable(asn1_header_cpp17 SOURCES header_cpp.cpp
  FOLDER "asn1/tests")
target_link_libraries(asn1_header_cpp17 PRIVATE Salts::ASN1 Salts::TinyTest)
add_test(NAME asn1.header_cpp17 COMMAND asn1_header_cpp17)
set_tests_properties(asn1.x509_integration PROPERTIES ENVIRONMENT "TZ=EST5")
