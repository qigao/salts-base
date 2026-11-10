cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0174)
  cmake_policy(SET CMP0174 OLD)
endif()

set(SALTSCORE_NATIVE_SOVERSION 0.1)
include(CMakeOptions.cmake)
project(SaltsBase VERSION 0.1.2
        DESCRIPTION
        "Higher-level utilities for cross-platform purpose and c11 developments"
        HOMEPAGE_URL "https://github.com/qigao/salts-base" LANGUAGES
        C CXX)

# Project configuration - output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Normalize install prefix (remove trailing slashes)
get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# Language standards - unified approach
set(CMAKE_CXX_STANDARD 17) # C++17 for compatibility
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enable folder organization in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

if(BUILD_TESTS OR BUILD_BENCHMARKS)
  enable_testing()
endif()
include(Sanitizers)
include(CmakeUtils)
include(GNUInstallDirs)

# Consume the selected vcpkg-cache installation without loading a toolchain or
# running vcpkg's resolver/build machinery in this product configure.
set(SALTS_DEPENDENCY_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
list(PREPEND CMAKE_PREFIX_PATH
  "${SALTS_DEPENDENCY_PREFIX}/$ENV{VCPKG_CONFIGURATION_SUBDIR}"
  "${SALTS_DEPENDENCY_PREFIX}")
find_package(Threads REQUIRED)
include(FindTools)
add_subdirectory(vendor)
add_subdirectory(tinytest)
add_subdirectory(asn1)
add_subdirectory(native-io)
add_subdirectory(core)
add_subdirectory(simd)
add_subdirectory(coroutine)
add_subdirectory(cserde)
add_subdirectory(unicode)
add_subdirectory(cron)
add_subdirectory(parser)
add_subdirectory(media)
add_subdirectory(serial)
 
#Package Configuration
include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SaltsBaseConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/SaltsBaseConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsBase")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/SaltsBaseConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

# Install the config files
install(
  FILES "${CMAKE_BINARY_DIR}/SaltsBaseConfig.cmake"
        "${CMAKE_BINARY_DIR}/SaltsBaseConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsBase"
  COMPONENT Development)

# Install the export set
export(
  EXPORT SaltsBaseTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsBaseTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT SaltsBaseTargets
  FILE SaltsBaseTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsBase"
  COMPONENT Development)
