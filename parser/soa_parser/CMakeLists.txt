set(TARGET_NAME soa_parser)

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES})
cmake_config_target(${TARGET_NAME}
    ALIAS Salts::SoaParser
    FOLDER "parser/soa_parser"
    EXPORT_NAME SoaParser)

target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${TARGET_NAME}
    PUBLIC

        Salts::URI

)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
