# Detect other test files automatically
cmake_add_source(TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "test*.c")

foreach(test_source ${TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  cmake_add_test(${test_name}
    SOURCES ${test_source}
    FOLDER "parser/${TARGET_NAME}/test"
    LIBS Salts::TinyTest ${TARGET_NAME})
endforeach()
