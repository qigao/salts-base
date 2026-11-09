foreach(_case IN ITEMS reader value scan)
  cmake_add_executable(asn1_${_case}_test SOURCES ${_case}.c
    FOLDER "asn1/tests")
  target_link_libraries(asn1_${_case}_test PRIVATE Salts::ASN1 Salts::TinyTest)
  target_compile_features(asn1_${_case}_test PRIVATE c_std_11)
  set_target_properties(asn1_${_case}_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME asn1.${_case} COMMAND asn1_${_case}_test)
endforeach()
cmake_add_executable(asn1_header_cpp17 SOURCES header_cpp.cpp
  FOLDER "asn1/tests")
target_compile_features(asn1_header_cpp17 PRIVATE cxx_std_17)
target_link_libraries(asn1_header_cpp17 PRIVATE Salts::ASN1 Salts::TinyTest)
add_test(NAME asn1.header_cpp17 COMMAND asn1_header_cpp17)
