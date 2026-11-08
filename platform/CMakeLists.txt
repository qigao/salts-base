# Platform extraction from Salts: OS-owned time, CSPRNG, and synchronization.
# Historical cmeta_ symbol names remain until a separately qualified ABI epoch.
# Platform owns the original registration/dispatch state. Never compile
# another copy into CFlow or NativeIO. No provider/ACE dependency.
set(_saltscore_platform_sources
    src/clock.c src/random.c src/platform.c src/thread_policy.c
    src/readiness.c src/portable_atomic.c src/portable_once.c
    src/c11_threads.c src/fastpath.c)
if(WIN32)
  list(APPEND _saltscore_platform_sources
    src/thread_win32.c src/portable_thread_win32.c src/portable_tss_win32.c)
else()
  list(APPEND _saltscore_platform_sources
    src/thread_posix.c src/portable_thread_posix.c src/portable_tss_posix.c)
endif()
if(UNIX)
  list(APPEND _saltscore_platform_sources src/readiness_poll.c)
  if(APPLE)
    list(APPEND _saltscore_platform_sources src/readiness_kqueue.c)
  elseif(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
    list(APPEND _saltscore_platform_sources src/readiness_epoll.c)
  endif()
endif()
add_library(saltscore_platform SHARED ${_saltscore_platform_sources})
cmake_config_target(saltscore_platform ALIAS Salts::Platform FOLDER "platform")
cmake_config_target(saltscore_platform
  EXPORT_NAME Platform
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_platform PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
# Atomic public header is owned by original Concurrency module; Platform's
# portable C11 atomic sources only borrow the header without owning its ABI.
target_include_directories(saltscore_platform PRIVATE
  "${PROJECT_SOURCE_DIR}/concurrency/include")

cmake_add_source(module_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${module_headers})
target_sources(saltscore_platform PRIVATE ${module_headers})
target_compile_features(saltscore_platform PUBLIC c_std_11)
# Export one owner-controlled C ABI to TinyCC, MSVC, and llvm-mingw clients.
target_compile_definitions(saltscore_platform PRIVATE SALTS_CORE_PLATFORM_BUILD=1)
target_include_directories(saltscore_platform PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/core/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(saltscore_platform PUBLIC Threads::Threads)
# Windows MSVC C11 atomic contract is mandatory for the readiness handles.
if(MSVC)
  target_compile_options(saltscore_platform PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-experimental:c11atomics>)
endif()
if(UNIX)
  target_compile_definitions(saltscore_platform PRIVATE
    SALTS_ENABLE_POLL_READINESS=1)
endif()
if(APPLE)
  target_compile_definitions(saltscore_platform PRIVATE
    SALTS_ENABLE_KQUEUE_READINESS=1)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
  target_compile_definitions(saltscore_platform PRIVATE
    SALTS_ENABLE_EPOLL_READINESS=1)
endif()
if(WIN32)
  target_link_libraries(saltscore_platform PRIVATE bcrypt iphlpapi ws2_32)
elseif(APPLE)
  target_compile_definitions(saltscore_platform PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(saltscore_platform PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

if(WIN32)
  cmake_config_target(saltscore_platform
    OUTPUT_NAME "saltscore_platform-${SALTSCORE_NATIVE_SOVERSION}")
endif()
install(TARGETS saltscore_platform EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
  PATTERN "salts" EXCLUDE
  PATTERN "salts_core" EXCLUDE)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
