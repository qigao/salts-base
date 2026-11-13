# Portable coroutine engine (minicoro remains private, no CMeta facade).
cmake_add_source(module_sources DIRS src)
cmake_add_source(module_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND module_sources ${module_sources_headers})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_sources})
add_library(coroutine STATIC ${module_sources})
cmake_config_target(coroutine ALIAS Salts::Coroutine FOLDER "coroutine"
  EXPORT_NAME Coroutine)
set_target_properties(coroutine PROPERTIES C_EXTENSIONS OFF)
target_compile_features(coroutine PUBLIC c_std_11)
target_include_directories(coroutine
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${PROJECT_SOURCE_DIR}/vendor/minicoro)
target_link_libraries(coroutine PUBLIC Salts::Core)
if(APPLE)
  target_compile_definitions(coroutine PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(coroutine PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

install(TARGETS coroutine EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${PROJECT_SOURCE_DIR}/vendor/minicoro/LICENSE"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/minicoro)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
