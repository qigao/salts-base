# The original bounded primitives have no Reflection/CFlow dependency.
# Keep all six original source units together for full API surface coverage.
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_concurrency SHARED ${module_sources})
cmake_config_target(saltscore_concurrency ALIAS Salts::Concurrency FOLDER "concurrency")
cmake_config_target(saltscore_concurrency
  EXPORT_NAME Concurrency
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_concurrency PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(saltscore_concurrency PUBLIC c_std_11)
target_include_directories(saltscore_concurrency PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_concurrency PUBLIC Salts::Platform)
if(MSVC)
  target_compile_options(saltscore_concurrency PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-experimental:c11atomics>)
endif()
if(APPLE)
  target_compile_definitions(saltscore_concurrency PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(saltscore_concurrency PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

if(WIN32)
  cmake_config_target(saltscore_concurrency
    OUTPUT_NAME "saltscore_concurrency-${SALTSCORE_NATIVE_SOVERSION}")
endif()
install(TARGETS saltscore_concurrency EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
  PATTERN "salts" EXCLUDE)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
