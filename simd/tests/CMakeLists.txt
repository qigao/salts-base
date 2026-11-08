# Runtime admission independent from CMeta and the optional TinyTest owner.
add_executable(saltscore_simd_native_boundaries simd_native_boundaries.c)
target_compile_features(saltscore_simd_native_boundaries PRIVATE c_std_11)
set_target_properties(saltscore_simd_native_boundaries PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(saltscore_simd_native_boundaries PRIVATE Salts::SIMD)
add_test(NAME saltscore.simd_native_boundaries
  COMMAND saltscore_simd_native_boundaries)

if(TARGET Salts::TinyTest)
  add_executable(saltscore_simd_original_c11 simd_test.c)
  target_compile_features(saltscore_simd_original_c11 PRIVATE c_std_11)
  set_target_properties(saltscore_simd_original_c11 PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(saltscore_simd_original_c11 PRIVATE Salts::SIMD Salts::TinyTest)
  add_test(NAME saltscore.simd_original_c11 COMMAND saltscore_simd_original_c11)

  add_executable(saltscore_simd_original_cpp17 simd_header_cpp_test.cpp)
  target_compile_features(saltscore_simd_original_cpp17 PRIVATE cxx_std_17)
  set_target_properties(saltscore_simd_original_cpp17 PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(saltscore_simd_original_cpp17 PRIVATE Salts::SIMD Salts::TinyTest)
  add_test(NAME saltscore.simd_original_cpp17 COMMAND saltscore_simd_original_cpp17)
endif()
