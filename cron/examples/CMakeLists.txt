cmake_add_executable(cron_example
  SOURCES cron_example.c
  LIBS Salts::Cron Salts::Core
  FOLDER "cron/examples")
