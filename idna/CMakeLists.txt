
# IDNA is a strict Unicode 17 UTS #46 implementation. No CMeta/CFlow/API shim.
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_idna STATIC ${module_sources})
cmake_config_target(saltscore_idna ALIAS Salts::IDNA FOLDER "idna")
cmake_config_target(saltscore_idna
  EXPORT_NAME IDNA)
set_target_properties(saltscore_idna PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_features(saltscore_idna PUBLIC c_std_11)
target_include_directories(saltscore_idna
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_idna PUBLIC Salts::Unicode)
install(TARGETS saltscore_idna EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/idna.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
