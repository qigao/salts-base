set(TARGET_NAME query_vm)
set(TARGET_FOLDER "parser/${TARGET_NAME}")

add_library(${TARGET_NAME} STATIC src/query_vm.c)
target_include_directories(${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/query_vm>)
cmake_config_target(${TARGET_NAME}
  ALIAS Salts::QueryVM
  EXPORT_NAME QueryVM
  FOLDER "${TARGET_FOLDER}")

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsUtilsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/query_vm.h DESTINATION include/query_vm)

if(BUILD_TESTS)
  cmake_add_test(test_query_vm
    SOURCES test/test_query_vm.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")
endif()
