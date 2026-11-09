# Standalone CMakeLists.txt for parser examples
# Can be built independently without parent project
cmake_minimum_required(VERSION 3.15)
project(ParserExamples C)

# Find required tools
find_package(RE2C REQUIRED)
find_package(LEMON REQUIRED)

# Generate lexer from re2c
re2c_target(
    ProtoLexer
    ${CMAKE_CURRENT_SOURCE_DIR}/../parser/proto_lexer.re
    ${CMAKE_CURRENT_BINARY_DIR}/proto_lexer.c
)

# Generate parser from Lemon
lemon_target(
    ProtoParser
    ${CMAKE_CURRENT_SOURCE_DIR}/../parser/proto.y
    ${CMAKE_CURRENT_BINARY_DIR}/proto.c
)

# Collect all parser source files
set(PARSER_SOURCES
    # Core parser
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/crc32.c
    ${CMAKE_CURRENT_BINARY_DIR}/proto_lexer.c
    ${CMAKE_CURRENT_BINARY_DIR}/proto.c

    # Production features
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser_stats.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/memory_pool.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/stream_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser_context.c
)

# Common include directories
set(PARSER_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Example 1: Basic parser driver
add_executable(parser_driver_example
    parser_driver_example.c
    ${PARSER_SOURCES}
)

target_include_directories(parser_driver_example PRIVATE ${PARSER_INCLUDES})
target_compile_features(parser_driver_example PRIVATE c_std_99)

# Example 2: Production features
add_executable(production_example
    production_example.c
    ${PARSER_SOURCES}
)

target_include_directories(production_example PRIVATE ${PARSER_INCLUDES})
target_compile_features(production_example PRIVATE c_std_99)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(parser_driver_example PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(production_example PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Link with pthread on Unix for thread-local storage
if(UNIX)
    target_link_libraries(production_example PRIVATE pthread)
endif()

# Set output directory
set_target_properties(parser_driver_example production_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "Parser examples configured")
message(STATUS "  Examples: parser_driver_example, production_example")
message(STATUS "  Generated files: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "  Executables: ${CMAKE_BINARY_DIR}/bin")
