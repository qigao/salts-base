# Examples for toon library

cmake_add_source(EXAMPLE_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.c")

foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)

    cmake_add_executable(${EXAMPLE_NAME}
      SOURCES ${EXAMPLE_SOURCE}
      LIBS toon
      FOLDER "parser/toon/examples")

    if(UNIX)
        target_link_libraries(${EXAMPLE_NAME} PRIVATE m)
    endif()
endforeach()
