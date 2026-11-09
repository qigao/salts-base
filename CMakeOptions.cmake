option(BUILD_TESTS "Enable TESTS" OFF)
option(SALTSCORE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SALTSCORE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SALTSCORE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Presets require explicit profile identities; empty values must not select a
# default host ABI or install into CMake's default system prefix.
foreach(_saltscore_profile_input IN ITEMS
    CMAKE_INSTALL_PREFIX VCPKG_TARGET_TRIPLET VCPKG_HOST_TRIPLET)
   if(NOT DEFINED ${_saltscore_profile_input} OR
     "${${_saltscore_profile_input}}" STREQUAL "")
      message(FATAL_ERROR "Required preset input is empty: ${_saltscore_profile_input}")
   endif()
endforeach()
unset(_saltscore_profile_input)
if("${CMAKE_C_COMPILER}" STREQUAL "" AND
   "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}" STREQUAL "")
   message(FATAL_ERROR "The selected toolkit must provide a compiler or TARGET_TOOLCHAIN_FILE")
endif()

# Core and Media are unconditional modules. Resolve their providers before
# project() initializes the vcpkg manifest toolchain.
set(VCPKG_MANIFEST_FEATURES media zstd)
if(BUILD_TESTS)
   list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()
