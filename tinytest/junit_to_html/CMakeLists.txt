cmake_add_executable(junit_to_html
  SOURCES junit_to_html.c
  LIBS Salts::Core Salts::CmdParser Salts::XmlParser
  FOLDER "tools/junit_to_html")

target_compile_features(junit_to_html PRIVATE c_std_11)
set_target_properties(junit_to_html PROPERTIES MACOSX_BUNDLE FALSE)

add_custom_command(
  TARGET junit_to_html POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html
          $<TARGET_FILE_DIR:junit_to_html>/dashboard.html
  COMMENT "Copying JUnit dashboard template"
  VERBATIM)

if(BUILD_TESTS)
  add_test(
    NAME junit_to_html_test
    COMMAND ${CMAKE_COMMAND}
      "-DJUNIT_TO_HTML_EXECUTABLE=$<TARGET_FILE:junit_to_html>"
      "-DJUNIT_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/test/junit_to_html_input.xml"
      "-DJUNIT_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html"
      "-DHTML_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/junit_to_html_output.html"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test/junit_to_html_test.cmake")
endif()
