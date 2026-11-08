# One native C11 filesystem owner; no CMeta/CFlow/old Salts references.
cmake_add_source(module_sources DIRS src include
  EXCLUDES src/fs_watch_windows.c src/fs_watch_linux.c src/fs_watch_unsupported.c)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_fs SHARED ${module_sources})
cmake_config_target(saltscore_fs ALIAS Salts::FS FOLDER "fs")
target_compile_features(saltscore_fs PUBLIC c_std_11)
if(MSVC)
  target_compile_options(saltscore_fs PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-experimental:c11atomics>)
endif()
cmake_config_target(saltscore_fs
  EXPORT_NAME FS
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_fs PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(saltscore_fs PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_fs PUBLIC Salts::Platform Salts::NativeIO PRIVATE Salts::Concurrency)
if(WIN32)
  target_sources(saltscore_fs PRIVATE src/fs_watch_windows.c)
  target_compile_definitions(saltscore_fs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
  cmake_config_target(saltscore_fs
    OUTPUT_NAME "saltscore_fs-${SALTSCORE_NATIVE_SOVERSION}")
elseif(APPLE)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJC)
    target_sources(saltscore_fs PRIVATE src/fs_watch_macos.m)
    target_link_libraries(saltscore_fs PRIVATE "-framework CoreServices")
  else()
    target_sources(saltscore_fs PRIVATE src/fs_watch_unsupported.c)
  endif()
  target_compile_definitions(saltscore_fs PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(saltscore_fs PRIVATE src/fs_watch_linux.c)
  else()
    target_sources(saltscore_fs PRIVATE src/fs_watch_unsupported.c)
  endif()
  target_compile_definitions(saltscore_fs PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()
install(TARGETS saltscore_fs EXPORT SaltsCoreTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/fs.h include/fs_async.h include/fs_watch.h include/fs_file.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
