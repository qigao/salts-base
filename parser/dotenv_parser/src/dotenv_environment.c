#include "dotenv.h"
#include "dotenv_environment_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int dotenv_environment_get_copy(const char *name, char **value_out) {
  char *copy;
  if (value_out) *value_out = NULL;
  if (!name || !name[0] || !value_out) return -1;

#if defined(_WIN32)
  char probe;
  DWORD required;
  DWORD length;
  DWORD error;

  SetLastError(ERROR_SUCCESS);
  required = GetEnvironmentVariableA(name, &probe, 1u);
  if (required == 0u) {
    error = GetLastError();
    if (error == ERROR_ENVVAR_NOT_FOUND) return 0;
    if (error != ERROR_SUCCESS) return -1;
    copy = (char *)malloc(1u);
    if (!copy) return -1;
    copy[0] = '\0';
  } else {
    copy = (char *)malloc((size_t)required);
    if (!copy) return -1;
    length = GetEnvironmentVariableA(name, copy, required);
    if (length >= required) {
      free(copy);
      return -1;
    }
  }
#else
  const char *value = getenv(name);
  size_t length;
  if (!value) return 0;
  length = strlen(value);
  copy = (char *)malloc(length + 1u);
  if (!copy) return -1;
  memcpy(copy, value, length + 1u);
#endif

  *value_out = copy;
  return 1;
}

int dotenv_environment_set(const char *name, const char *value, int overwrite) {
  char *existing = NULL;
  int found;
  if (!name || !name[0] || !value) return -1;
  if (!overwrite) {
    found = dotenv_environment_get_copy(name, &existing);
    free(existing);
    if (found != 0) return found > 0 ? 0 : -1;
  }
#if defined(_WIN32)
  return _putenv_s(name, value);
#else
  return setenv(name, value, overwrite ? 1 : 0);
#endif
}

int dotenv_sync_environment(const char *name) {
  if (!name || !name[0]) return -1;
#if defined(_WIN32)
  char *value = NULL;
  int found = dotenv_environment_get_copy(name, &value);
  int rc;
  if (found < 0) return -1;
  rc = _putenv_s(name, found > 0 ? value : "");
  free(value);
  return rc;
#else
  return 0;
#endif
}
