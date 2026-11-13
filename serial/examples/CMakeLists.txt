# Retain all original examples as real build inputs when this profile is chosen.
foreach(_serial_example IN ITEMS
    await_events handle_errors list_ports port_config port_info send_receive)
  cmake_add_executable(saltscore_serial_example_${_serial_example} SOURCES ${_serial_example}.c
    FOLDER "serial/examples")
  set_target_properties(saltscore_serial_example_${_serial_example} PROPERTIES C_EXTENSIONS OFF)
  target_link_libraries(saltscore_serial_example_${_serial_example} PRIVATE Salts::Serial)
endforeach()
