
# TinyTest owns no CMeta metadata or mocking runtime. TinyMock stays in Salts.
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_tinytest STATIC ${module_sources})
cmake_config_target(saltscore_tinytest ALIAS Salts::TinyTest FOLDER "tinytest")
cmake_config_target(saltscore_tinytest
  EXPORT_NAME TinyTest)
set_target_properties(saltscore_tinytest PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(saltscore_tinytest PUBLIC c_std_11)
target_include_directories(saltscore_tinytest PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS saltscore_tinytest EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/tinytest.h include/tinytest.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/tinytest/detail
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tinytest
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
