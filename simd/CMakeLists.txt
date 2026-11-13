# Keep the original SIMDe implementation private to one native SIMD owner.
# Missing SIMDe is a configuration error, never an alternate implementation.
find_path(SIMDE_INCLUDE_DIRS "simde/wasm/simd128.h"
  PATHS "${SALTS_DEPENDENCY_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)

cmake_add_source(simd_sources DIRS src)
cmake_add_source(simd_sources_headers RECURSE DIRS include PATTERNS "*.h" "*.hpp")
list(APPEND simd_sources ${simd_sources_headers})
add_library(simd STATIC ${simd_sources})
add_library(Salts::SIMD ALIAS simd)
target_compile_features(simd PUBLIC c_std_11)
set_target_properties(simd PROPERTIES
  EXPORT_NAME SIMD C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden)
target_include_directories(simd PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(simd SYSTEM PRIVATE "${SIMDE_INCLUDE_DIRS}")

# SIMDe 0.8.2's GNU vector fallback uses signed abs/neg/multiply expressions
# for modular integer instructions. Define their two's-complement wrapping
# semantics in every GNU build, including sanitizer builds, without raising
# the minimum ISA or exporting compiler flags to consumers.
target_compile_options(simd PRIVATE $<$<C_COMPILER_ID:GNU>:-fwrapv>)

# SIMDe's portable nearest operations use nearbyint/nearbyintf. For the static
# target, CMake preserves this private dependency as LINK_ONLY for consumers.
if(NOT WIN32)
  target_link_libraries(simd PRIVATE m)
endif()

install(TARGETS simd EXPORT SaltsBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
