cmake_add_source(TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "test*.c")
set(folder_name  parser/${TARGET_NAME}/test)

foreach(test_source ${TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  cmake_add_test(${test_name}
    SOURCES ${test_source}
    LIBS Salts::TinyTest ${TARGET_NAME}
    FOLDER "${folder_name}")
endforeach()
