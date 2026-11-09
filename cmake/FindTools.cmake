# Code generators are host executables, including when the libraries are cross-compiled.
if(NOT IS_DIRECTORY "${RE2C_ROOT}")
  message(FATAL_ERROR "RE2C_ROOT must name the installed host re2c package")
endif()
unset(RE2C_EXECUTABLE CACHE)
find_program(RE2C_EXECUTABLE NAMES re2c
  PATHS "${RE2C_ROOT}/bin" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
file(REAL_PATH "${RE2C_ROOT}" _re2c_root)
file(REAL_PATH "${RE2C_EXECUTABLE}" _re2c_executable)
cmake_path(IS_PREFIX _re2c_root "${_re2c_executable}" NORMALIZE _re2c_in_root)
if(NOT _re2c_in_root)
  message(FATAL_ERROR "re2c executable is outside RE2C_ROOT: ${RE2C_EXECUTABLE}")
endif()

foreach(_host_tool IN ITEMS LEMON_EXECUTABLE LEMON_TEMPLATE)
  if(NOT IS_ABSOLUTE "${${_host_tool}}" OR NOT EXISTS "${${_host_tool}}"
     OR IS_DIRECTORY "${${_host_tool}}")
    message(FATAL_ERROR "${_host_tool} must name an existing absolute host tool file")
  endif()
endforeach()
add_executable(lemon IMPORTED GLOBAL)
set_target_properties(lemon PROPERTIES IMPORTED_LOCATION "${LEMON_EXECUTABLE}")
set(LEMPAR "${LEMON_TEMPLATE}")
