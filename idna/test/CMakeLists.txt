foreach(_name IN ITEMS test_salts_idna test_salts_idna_conformance)
  cmake_add_executable("${_name}" SOURCES "${_name}.c"
    LIBS Salts::IDNA Salts::TinyTest FOLDER "idna/test")
  target_compile_features("${_name}" PRIVATE c_std_11)
  set_target_properties("${_name}" PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME "saltscore.native.${_name}" COMMAND "${_name}")
  set_tests_properties("saltscore.native.${_name}" PROPERTIES TIMEOUT 300)
endforeach()
target_compile_definitions(test_salts_idna_conformance PRIVATE
  "SALTS_IDNA_TEST_DATA=\"${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/IdnaTestV2.txt\"")
