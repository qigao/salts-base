# Portable coroutine engine (minicoro remains private, no CMeta facade).
cmake_add_source(module_sources DIRS src include)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(saltscore_coroutine SHARED ${module_sources})
cmake_config_target(saltscore_coroutine ALIAS Salts::Coroutine FOLDER "coroutine")
cmake_config_target(saltscore_coroutine
  EXPORT_NAME Coroutine
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_coroutine PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(saltscore_coroutine PUBLIC c_std_11)
target_include_directories(saltscore_coroutine
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${PROJECT_SOURCE_DIR}/vendor/minicoro)
target_link_libraries(saltscore_coroutine PUBLIC Salts::Concurrency)
if(APPLE)
  target_compile_definitions(saltscore_coroutine PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(saltscore_coroutine PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

if(WIN32)
  cmake_config_target(saltscore_coroutine
    OUTPUT_NAME "saltscore_coroutine-${SALTSCORE_NATIVE_SOVERSION}")
endif()
install(TARGETS saltscore_coroutine EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/coro.h include/coro_pool.h include/coro_executor.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${PROJECT_SOURCE_DIR}/vendor/minicoro/LICENSE"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/minicoro)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
