# Keep the minimal foundation target usable without platform/transport providers.
cmake_add_source(core_sources DIRS src src/sds include
  EXCLUDES src/cmeta_zstd.c include/cmeta_zstd.h)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${core_sources})
add_library(saltscore_core SHARED ${core_sources})
cmake_config_target(saltscore_core ALIAS Salts::Core FOLDER "core")
target_compile_features(saltscore_core PUBLIC c_std_11)
cmake_config_target(saltscore_core
  EXPORT_NAME Core
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${SALTSCORE_NATIVE_SOVERSION}")
set_target_properties(saltscore_core PROPERTIES
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(saltscore_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Public mem_pool_t and mem_buffer_t contain C11 atomic fields on MSVC.
# Consumers including cmeta_buffer.h require the same C compiler setting.
if(MSVC)
  target_compile_options(saltscore_core PUBLIC
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:-experimental:c11atomics>)
endif()

# Core-owned buffer/pool, byte utility and sole native Base64 implementation.
# No third-party Base64 DSO, vcpkg port or alternate codec is required.
# SDS is original private C string storage, not an exported provider target.
target_include_directories(saltscore_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/sds")
if(APPLE)
  target_compile_definitions(saltscore_core PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(saltscore_core PRIVATE _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L)
endif()
# Original TLog owns real file and async queue operations, using canonical
# module implementations rather than replacing these features with stubs.
target_link_libraries(saltscore_core PUBLIC
  Salts::Platform Salts::Concurrency Salts::FS)

# Original one-shot Zstd ABI remains in the sole Core owner. The registry pins
# its exact source revision; never substitute the system codec or a second DSO.
find_package(zstd 1.6.0 EXACT CONFIG REQUIRED)
get_target_property(_saltscore_zstd_type zstd::libzstd_static TYPE)
if(NOT _saltscore_zstd_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Core Zstd provider must be the original static archive")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/zstd-provider-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:zstd::libzstd_static>\n")
target_sources(saltscore_core PRIVATE src/cmeta_zstd.c)
target_link_libraries(saltscore_core PRIVATE zstd::libzstd_static)
if(CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF")
  # PRIVATE alone does not hide archive symbols. Bind the pinned provider
  # inside Core so another process-level Zstd cannot interpose its calls.
  target_link_options(saltscore_core PRIVATE
    "LINKER:--exclude-libs,$<TARGET_FILE_NAME:zstd::libzstd_static>")
endif()
target_compile_definitions(saltscore_core PUBLIC SALTSCORE_HAS_ZSTD=1)
install(FILES include/cmeta_zstd.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# The pinned vcpkg provider owns the third-party license text.
install(FILES "${zstd_DIR}/copyright"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/zstd RENAME LICENSE)


if(WIN32)
  cmake_config_target(saltscore_core
    OUTPUT_NAME "saltscore_core-${SALTSCORE_NATIVE_SOVERSION}")
endif()
install(TARGETS saltscore_core EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
  include/bytes.h include/runtime_abi.h include/utf8.h
  include/base64_utils.h include/vstr.h include/salts_api.h
  include/cmeta_buffer.h include/ring_buffer.h include/cmeta_error.h
  include/tstr.h include/str.h include/memory_pool.h include/object_pool.h
  include/bucket_priority_queue.h
  include/fmt.h include/fmt_pp.h include/tlog.h include/tlog_helper.h
  include/cmeta_uuid.h include/cmeta_bytes.h include/cmeta_simd_scan.h include/cmeta_mmap.h include/re.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Original upstream SDS BSD license is mandatory for this private source.
install(FILES src/sds/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SaltsCore/sds)

# Keep the original formatter lexer grammar in its own nested module.
add_subdirectory(parser)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
