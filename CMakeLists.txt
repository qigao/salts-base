cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  SaltsParsers
  VERSION 0.1.0
  DESCRIPTION "Higher-level utilities built on Salts"
  HOMEPAGE_URL "https://github.com/qigao/salts-parsers"
  LANGUAGES ${_salts_utils_languages})
unset(_salts_utils_languages)

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
find_package(SaltsCore 0.1 CONFIG REQUIRED PATHS "${SALTSCORE_ROOT_PATH}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

include(FindTools)
add_subdirectory(tinytest)
add_subdirectory(platform)
add_subdirectory(concurrency)
add_subdirectory(fs)
add_subdirectory(core)
add_subdirectory(cron)
add_subdirectory(parser)
add_subdirectory(cserde)
add_subdirectory(media)
add_subdirectory(serial)
add_subdirectory(unicode)
add_subdirectory(idna)

add_subdirectory(coroutine)
add_subdirectory(vendor)
#Package Configuration
include(CMakePackageConfigHelpers)

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

# Install the config files
install(
  FILES "${CMAKE_BINARY_DIR}/SaltsUtilsConfig.cmake"
        "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils"
  COMPONENT Development)

# Install the export set
export(
  EXPORT SaltsUtilsTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsUtilsTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT SaltsUtilsTargets
  FILE SaltsUtilsTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils"
  COMPONENT Development)
