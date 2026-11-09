# Public headers and private native implementations have separate source roots.
cmake_add_source(serial_sources DIRS  src EXCLUDES src/backend_*.c)
cmake_add_source(serial_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND serial_sources ${serial_sources_headers})
if(WIN32)
  cmake_add_source(serial_backend_sources DIRS src PATTERNS "*_win32.c")
else()
  cmake_add_source(serial_backend_sources DIRS src PATTERNS "*_posix.c")
endif()
list(APPEND serial_sources ${serial_backend_sources})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${serial_sources})
add_library(serial SHARED ${serial_sources})
cmake_config_target(serial ALIAS Salts::Serial FOLDER "serial")
target_compile_features(serial PUBLIC c_std_11)
cmake_config_target(serial
  EXPORT_NAME Serial
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(serial PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF DEFINE_SYMBOL SALTS_SERIAL_EXPORTS C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
target_include_directories(serial PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(serial PRIVATE
  Salts::Core)
if(WIN32)
  target_link_libraries(serial PRIVATE setupapi cfgmgr32)
endif()
if(MSVC)
  target_compile_options(serial PRIVATE /wd4996)
  cmake_config_target(serial
    OUTPUT_NAME "serial-${SALTSCORE_NATIVE_SOVERSION}")
elseif(UNIX)
  # Darwin hides the original high-baud termios constants in strict POSIX mode.
  target_compile_definitions(serial PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L
    $<$<BOOL:${APPLE}>:_DARWIN_C_SOURCE=1>)
endif()

install(TARGETS serial EXPORT SaltsBaseTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
