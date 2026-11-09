foreach(required_variable IN ITEMS
        JUNIT_TO_HTML_EXECUTABLE JUNIT_INPUT JUNIT_TEMPLATE HTML_OUTPUT)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required test variable: ${required_variable}")
  endif()
endforeach()

if(NOT EXISTS "${JUNIT_TO_HTML_EXECUTABLE}")
  message(FATAL_ERROR
    "junit_to_html executable does not exist: ${JUNIT_TO_HTML_EXECUTABLE}")
endif()

file(REMOVE "${HTML_OUTPUT}")
execute_process(
  COMMAND "${JUNIT_TO_HTML_EXECUTABLE}"
          --input "${JUNIT_INPUT}"
          --output "${HTML_OUTPUT}"
          --template "${JUNIT_TEMPLATE}"
          --no-color
  RESULT_VARIABLE junit_to_html_result
  OUTPUT_VARIABLE junit_to_html_stdout
  ERROR_VARIABLE junit_to_html_stderr)

if(NOT junit_to_html_result EQUAL 0)
  message(FATAL_ERROR
    "junit_to_html failed with ${junit_to_html_result}\n"
    "stdout:\n${junit_to_html_stdout}\n"
    "stderr:\n${junit_to_html_stderr}")
endif()

file(READ "${HTML_OUTPUT}" dashboard)
foreach(expected IN ITEMS
        "Suite &amp;amp; One"
        ">2</div>"
        ">1</div>"
        "test-item passed"
        "test-item failed"
        "reports &amp;lt;failure&amp;gt;"
        "expected &amp;lt;actual&amp;gt;"
        "stack &amp;amp; detail")
  string(FIND "${dashboard}" "${expected}" expected_offset)
  if(expected_offset EQUAL -1)
    message(FATAL_ERROR "Dashboard is missing expected content: ${expected}")
  endif()
endforeach()
