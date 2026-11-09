cmake_add_executable(cron_example
  SOURCES cron_example.c
  LIBS Salts::Cron Salts::Core
  FOLDER "cron/examples")
set_target_properties(cron_example PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
