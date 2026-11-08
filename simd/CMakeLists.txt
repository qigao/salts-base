# Keep the original SIMDe implementation private to one native SIMD owner.
# Missing SIMDe is a configuration error, never an alternate implementation.
find_path(SIMDE_INCLUDE_DIRS "simde/wasm/simd128.h" REQUIRED)

add_library(saltscore_simd STATIC src/simd.c src/simd_types.c)
add_library(Salts::SIMD ALIAS saltscore_simd)
target_compile_features(saltscore_simd PUBLIC c_std_11)
set_target_properties(saltscore_simd PROPERTIES
  EXPORT_NAME SIMD C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(saltscore_simd PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(saltscore_simd SYSTEM PRIVATE "${SIMDE_INCLUDE_DIRS}")

# SIMDe 0.8.2's GNU vector fallback uses signed abs/neg/multiply expressions
# for modular integer instructions. Define their two's-complement wrapping
# semantics in every GNU build, including sanitizer builds, without raising
# the minimum ISA or exporting compiler flags to consumers.
target_compile_options(saltscore_simd PRIVATE $<$<C_COMPILER_ID:GNU>:-fwrapv>)

# SIMDe's portable nearest operations use nearbyint/nearbyintf. For the static
# target, CMake preserves this private dependency as LINK_ONLY for consumers.
if(NOT WIN32)
  target_link_libraries(saltscore_simd PRIVATE m)
endif()

install(TARGETS saltscore_simd EXPORT SaltsCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/salts DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
