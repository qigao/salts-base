cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_process SHARED ${module_sources})
cmake_config_target(saltscore_process ALIAS Salts::Process FOLDER "process")
target_compile_features(saltscore_process PUBLIC c_std_11)
cmake_config_target(saltscore_process
  EXPORT_NAME Process
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_process PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(saltscore_process PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_process PUBLIC Salts::Platform Salts::NativeIO)
target_link_libraries(saltscore_process PRIVATE Salts::Core)
if(MSVC)
  target_compile_options(saltscore_process PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-experimental:c11atomics>)
endif()
if(WIN32)
  target_compile_definitions(saltscore_process PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
  cmake_config_target(saltscore_process
    OUTPUT_NAME "saltscore_process-${SALTSCORE_NATIVE_SOVERSION}")
elseif(APPLE)
  target_compile_definitions(saltscore_process PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(saltscore_process PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    # pipe2(O_CLOEXEC) must be atomic with respect to other fork/exec callers.
    target_compile_definitions(saltscore_process PRIVATE _GNU_SOURCE=1)
  endif()
endif()
install(TARGETS saltscore_process EXPORT SaltsCoreTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/process.h include/process_async.h include/shell.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
