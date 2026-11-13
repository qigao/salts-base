# BUILD_TESTS=ON always provides TinyTest; its original regression suites
# must be registered just like all other actual native tests.
foreach(_name IN ITEMS regression_fixes regression_runtime equality_test
    result_x_macro_test c11_generic_test)
  cmake_add_executable("tinytest_${_name}" SOURCES "${_name}.c"
    LIBS Salts::TinyTest FOLDER "tinytest/test")
  set_target_properties("tinytest_${_name}" PROPERTIES C_EXTENSIONS OFF)
  add_test(NAME "saltscore.native.tinytest_${_name}" COMMAND
           "tinytest_${_name}")
  set_tests_properties("saltscore.native.tinytest_${_name}" PROPERTIES TIMEOUT
                       300)
endforeach()
