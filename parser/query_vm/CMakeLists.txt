set(TARGET_NAME query_vm)
set(TARGET_FOLDER "parser/${TARGET_NAME}")

cmake_add_source(query_vm_sources DIRS src)
cmake_add_source(query_vm_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND query_vm_sources ${query_vm_sources_headers})
add_library(${TARGET_NAME} STATIC ${query_vm_sources})
target_include_directories(${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
cmake_config_target(${TARGET_NAME}
  ALIAS Salts::QueryVM
  EXPORT_NAME QueryVM
  FOLDER "${TARGET_FOLDER}")

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  cmake_add_test(test_query_vm
    SOURCES test/test_query_vm.c
    LIBS ${TARGET_NAME} Salts::TinyTest
    FOLDER "${TARGET_FOLDER}/tests")
endif()
