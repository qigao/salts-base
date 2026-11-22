
# TinyTest owns no CMeta metadata or mocking runtime. TinyMock stays in Salts.
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(tinytest STATIC ${module_sources})
cmake_config_target(tinytest ALIAS Salts::TinyTest FOLDER "tinytest"
  EXPORT_NAME TinyTest)
set_target_properties(tinytest PROPERTIES C_EXTENSIONS OFF)
target_compile_features(tinytest PUBLIC c_std_11)
target_include_directories(tinytest PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS tinytest EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/tinytest.h include/tinytest.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/tinytest/detail
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tinytest
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
