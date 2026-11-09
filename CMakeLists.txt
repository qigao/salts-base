cmake_minimum_required(VERSION 3.27)

project(SaltsBase VERSION 0.1.0 DESCRIPTION
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
set(CMAKE_C_STANDARD 11) # C17 to ensure C11 atomic support
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(-experimental:c11atomics)
endif()
# Enable folder organization in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

include(CMakeOptions.cmake)

if(BUILD_TESTS)
  enable_testing()
endif()
include(Sanitizers)
include(CmakeUtils)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

include(FindTools)
add_subdirectory(vendor)
add_subdirectory(tinytest)
add_subdirectory(asn1)
add_subdirectory(coroutine) 
add_subdirectory(core)
add_subdirectory(cron)
add_subdirectory(parser)
add_subdirectory(cserde)
add_subdirectory(media)
add_subdirectory(serial)
add_subdirectory(unicode)
add_subdirectory(idna)
 
#Package Configuration
include(CMakePackageConfigHelpers)

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
