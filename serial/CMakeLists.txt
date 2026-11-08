# Public headers and private native implementations have separate source roots.
cmake_add_source(serial_sources DIRS include src
  EXCLUDES src/backend_win32.c src/backend_posix.c)
if(WIN32)
  list(APPEND serial_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/backend_win32.c")
else()
  list(APPEND serial_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/backend_posix.c")
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${serial_sources})
add_library(saltscore_serial SHARED ${serial_sources})
cmake_config_target(saltscore_serial ALIAS Salts::Serial FOLDER "serial")
target_compile_features(saltscore_serial PUBLIC c_std_11)
cmake_config_target(saltscore_serial
  EXPORT_NAME Serial
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_serial PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF DEFINE_SYMBOL SALTS_SERIAL_EXPORTS C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
target_include_directories(saltscore_serial PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(saltscore_serial PRIVATE
  Salts::Core Salts::Platform Salts::Concurrency)
if(WIN32)
  target_link_libraries(saltscore_serial PRIVATE setupapi cfgmgr32)
endif()
if(MSVC)
  target_compile_options(saltscore_serial PRIVATE /wd4996)
  cmake_config_target(saltscore_serial
    OUTPUT_NAME "saltscore_serial-${SALTSCORE_NATIVE_SOVERSION}")
elseif(UNIX)
  # Darwin hides the original high-baud termios constants in strict POSIX mode.
  target_compile_definitions(saltscore_serial PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L
    $<$<BOOL:${APPLE}>:_DARWIN_C_SOURCE=1>)
endif()

install(TARGETS saltscore_serial EXPORT SaltsCoreTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/serial.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
add_subdirectory(examples)
