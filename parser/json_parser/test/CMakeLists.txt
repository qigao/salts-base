# JSON Parser tests
set(TEST_SOURCES test_json_parser.c)

cmake_add_test(test_json_parser
    SOURCES ${TEST_SOURCES}
    FOLDER "parser/${TARGET_NAME}/test"
    LIBS json_parser Salts::TinyTest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

cmake_add_test(test_json_parser_re
    SOURCES test_re.c
    FOLDER "parser/${TARGET_NAME}/test"
    LIBS json_parser Salts::TinyTest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
             ${CMAKE_CURRENT_SOURCE_DIR}/../parser
             ${SIMDE_INCLUDE_DIRS}
)

if(BUILD_TESTS)
  cmake_add_test(test_json_cserde_reader
      SOURCES test_json_cserde_reader.c
      FOLDER "parser/${TARGET_NAME}/test"
      LIBS json_cserde_adapter Salts::TinyTest
      INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  )
  cmake_add_test(test_json_cserde_writer
      SOURCES test_json_cserde_writer.c
      FOLDER "parser/${TARGET_NAME}/test"
      LIBS json_cserde_adapter Salts::TinyTest
      INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  )
endif()
