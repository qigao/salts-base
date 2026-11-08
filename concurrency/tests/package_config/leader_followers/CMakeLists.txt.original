cmake_minimum_required(VERSION 3.20)
project(lf_installed_consumer LANGUAGES C CXX)
if(NOT DEFINED SALTS_TEST_PREFIX OR NOT DEFINED SALTS_TEST_VERSION)
  message(FATAL_ERROR "installed SDK prefix and expected version required")
endif()
find_package(Salts ${SALTS_TEST_VERSION} EXACT CONFIG REQUIRED
  PATHS "${SALTS_TEST_PREFIX}" NO_DEFAULT_PATH)
enable_testing()
add_executable(lf_installed_c main.c)
add_executable(lf_installed_cpp main.cpp)
target_link_libraries(lf_installed_c PRIVATE Salts::Concurrency)
target_link_libraries(lf_installed_cpp PRIVATE Salts::Concurrency)
set_target_properties(lf_installed_c PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(lf_installed_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_test(NAME lf_installed_c COMMAND lf_installed_c)
add_test(NAME lf_installed_cpp COMMAND lf_installed_cpp)
