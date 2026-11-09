#include <threads/threads.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

static int g_single_threaded = 0;

void sync_set_single_threaded(int enabled) { g_single_threaded = enabled; }

int sync_is_single_threaded(void) { return g_single_threaded; }

int process_id(void) {
#ifdef _WIN32
  return (int)GetCurrentProcessId();
#else
  return (int)getpid();
#endif
}
