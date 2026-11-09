
# Independent bounded C11 serialization tokens, not CMeta reflection.
cmake_add_source(module_sources RECURSE DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_cserde STATIC ${module_sources})
cmake_config_target(saltscore_cserde ALIAS Salts::CSerde FOLDER "cserde")
cmake_config_target(saltscore_cserde
  EXPORT_NAME CSerde)
set_target_properties(saltscore_cserde PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(saltscore_cserde PUBLIC c_std_11)
target_include_directories(saltscore_cserde PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS saltscore_cserde EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/cserde DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
