# One Core owner for utilities, platform primitives, concurrency, FS and processes.
# Platform suffixes identify native backends; other files are shared.
cmake_add_source(core_sources RECURSE DIRS src sds PATTERNS "*.c"
  EXCLUDES src/*_win.c src/*_linux.c src/*_posix.c src/*_macos.c
    src/*_bsd.c src/*_unsupported.c)
set(platform_patterns "*_posix.c" "*_unsupported.c")
if(WIN32)
  set(platform_patterns "*_win.c")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(platform_patterns "*_posix.c" "*_linux.c")
elseif(APPLE)
  set(platform_patterns "*_posix.c" "*_bsd.c")
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJC)
    list(APPEND platform_patterns "*_macos.m")
  else()
    list(APPEND platform_patterns "*_unsupported.c")
  endif()
endif()
cmake_add_source(core_platform_sources RECURSE DIRS src PATTERNS ${platform_patterns})
if(NOT core_platform_sources)
  message(FATAL_ERROR "Core platform implementations are missing for ${CMAKE_SYSTEM_NAME}: ${platform_patterns}")
endif()
list(APPEND core_sources ${core_platform_sources})
cmake_add_source(core_public_headers RECURSE DIRS include PATTERNS "*.h")
cmake_add_source(core_private_headers RECURSE DIRS src PATTERNS "*.h")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES ${core_sources} ${core_public_headers} ${core_private_headers})
add_library(salts SHARED ${core_sources} ${core_public_headers} ${core_private_headers})
cmake_config_target(salts ALIAS Salts::Core FOLDER "core"
  EXPORT_NAME Core)
set_target_properties(salts PROPERTIES
  C_EXTENSIONS OFF WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(salts PUBLIC c_std_11)
target_compile_definitions(salts PRIVATE SALTS_CORE_BUILD=1)
target_include_directories(salts
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
          "${CMAKE_CURRENT_SOURCE_DIR}/sds")
target_link_libraries(salts PUBLIC Threads::Threads)
find_path(CORE_SIMDE_INCLUDE_DIR "simde/x86/sse2.h"
  PATHS "${SALTS_DEPENDENCY_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
target_include_directories(salts SYSTEM PRIVATE "${CORE_SIMDE_INCLUDE_DIR}")
# The public buffer/pool records contain C11 atomic fields on MSVC.
if(MSVC)
  target_compile_options(salts PUBLIC
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:-experimental:c11atomics>)
endif()
if(WIN32)
  target_compile_definitions(salts PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
  target_link_libraries(salts PRIVATE bcrypt iphlpapi ws2_32)
elseif(APPLE)
  target_compile_definitions(salts PRIVATE
    _DARWIN_C_SOURCE=1)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(salts PRIVATE "-framework CoreServices")
  endif()
elseif(UNIX)
  target_compile_definitions(salts PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

find_package(zstd CONFIG REQUIRED
  PATHS "${SALTS_DEPENDENCY_PREFIX}/share/zstd" NO_DEFAULT_PATH)
target_link_libraries(salts PRIVATE zstd::libzstd_static)
if(CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF")
  target_link_options(salts PRIVATE
    "LINKER:--exclude-libs,$<TARGET_FILE_NAME:zstd::libzstd_static>")
endif()
target_compile_definitions(salts PUBLIC HAS_ZSTD=1)

install(TARGETS salts EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# include/ is exclusively SDK payload; implementation-only files belong in
# src/ or parser/. Install the complete directory without filtering exceptions.
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(parser)
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
