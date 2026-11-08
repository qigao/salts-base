set(TARGET_NAME modbus_parser)

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES})
cmake_config_target(${TARGET_NAME}
    ALIAS Salts::ModbusParser
    FOLDER "parser/modbus_parser"
    EXPORT_NAME ModbusParser)

target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)


if(BUILD_TESTS)
  add_subdirectory(test)
endif()
