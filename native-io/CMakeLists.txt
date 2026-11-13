# Platform native completion/readiness ownership (no CFlow or CMeta).
cmake_add_source(native_io_sources RECURSE DIRS src PATTERNS "*.c"
  EXCLUDES src/*_iocp.c src/*_windows.c src/*_linux.c src/*_epoll.c
    src/*_io_uring.c src/*_readiness.c src/*_posix.c src/*_bsd.c
    src/*_kqueue.c src/*_darwin_aio.c src/*_unsupported.c)
add_library(native_io STATIC ${native_io_sources})
cmake_config_target(native_io
  ALIAS Salts::NativeIO EXPORT_NAME NativeIO FOLDER "native-io")
if(WIN32)
  set(native_io_backend_patterns "*_iocp.c" "*_windows.c")
  target_compile_definitions(native_io PRIVATE
    WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
  # Android's NDK provides the Linux epoll/eventfd and io_uring syscall ABI.
  # Runtime backend initialization still reports kernel/policy failures; an
  # unavailable io_uring instance is never replaced with a success stub.
  set(native_io_backend_patterns
    "*_linux.c" "*_epoll.c" "*_io_uring.c" "*_readiness.c" "*_posix.c")
  target_compile_definitions(native_io PRIVATE
    _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700
    SALTS_ENABLE_POLL_READINESS=1 SALTS_ENABLE_EPOLL_READINESS=1)
elseif((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
       AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(native_io_backend_patterns "*_bsd.c" "*_kqueue.c" "*_readiness.c" "*_posix.c")
  target_compile_definitions(native_io PRIVATE
    SALTS_ENABLE_POLL_READINESS=1 SALTS_ENABLE_KQUEUE_READINESS=1)
  if(APPLE)
    target_compile_definitions(native_io PRIVATE _DARWIN_C_SOURCE=1)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      list(APPEND native_io_backend_patterns "*_darwin_aio.c")
      target_compile_definitions(native_io PRIVATE SALTS_NATIVE_DARWIN_AIO=1)
    endif()
  endif()
else()
  message(FATAL_ERROR
    "NativeIO has no backend for ${CMAKE_SYSTEM_NAME} (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
endif()
cmake_add_source(native_io_backend_sources RECURSE DIRS src
  PATTERNS ${native_io_backend_patterns})
if(NOT native_io_backend_sources)
  message(FATAL_ERROR "NativeIO backend implementations are missing for ${CMAKE_SYSTEM_NAME}: ${native_io_backend_patterns}")
endif()
target_sources(native_io PRIVATE ${native_io_backend_sources})
set_target_properties(native_io PROPERTIES C_EXTENSIONS OFF)
cmake_add_source(module_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
cmake_add_source(native_io_private_headers RECURSE DIRS src PATTERNS "*.h")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES ${native_io_sources} ${native_io_backend_sources} ${module_headers}
    ${native_io_private_headers})
target_sources(native_io PRIVATE ${module_headers} ${native_io_private_headers})
target_compile_features(native_io PUBLIC c_std_11)
if(MSVC)
  target_compile_options(native_io PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-experimental:c11atomics>)
endif()
target_include_directories(native_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(native_io
  PUBLIC Salts::Core
  PRIVATE Salts::Coroutine)
if(WIN32)
  target_link_libraries(native_io PRIVATE ws2_32)
endif()

install(TARGETS native_io EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
