set(TARGET_NAME tlv_parser)

# =============================================================================
# Static parser library (independent of BUILD_SHARED_LIBS)
# =============================================================================

# Find all source files
cmake_add_source(SOURCE_FILES RECURSE
    DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
    PATTERNS "*.c" "*.h")
# Exclude memory_pool files (moved to memory module)
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*memory_pool\\.(c|h)$")

# Library
add_library(${TARGET_NAME} STATIC ${SOURCE_FILES})
cmake_config_target(${TARGET_NAME}
    ALIAS Salts::TLVParser
    FOLDER "parser/tlv_parser"
    EXPORT_NAME TLVParser)

target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

# Link against dependencies
# - Memory module for allocators
# - uri_parser for backward compatibility (uri.h includes uri_parser.h)
target_link_libraries(${TARGET_NAME}
    PUBLIC
        Salts::Core
        Salts::URI
)


# =============================================================================
# Examples
# =============================================================================

if(BUILD_EXAMPLES)
  cmake_add_executable(production_example
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/example/production_example.c
    LIBS ${TARGET_NAME}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "parser/tlv_parser/examples")
  if(UNIX)
      target_link_libraries(production_example PRIVATE Threads::Threads)
  endif()
endif()

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
