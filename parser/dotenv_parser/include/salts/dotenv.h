#ifndef SALTS_DOTENV_H
#define SALTS_DOTENV_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Loads environment variables from a .env file into the process environment.
 *
 * If path is a directory, it looks for a file named ".env" inside it.
 * If path is a file, it loads that file.
 *
 * @param path Directory or file path.
 * @param overwrite Whether to overwrite existing environment variables.
 * @return 0 on success, -1 on failure (e.g., file not found).
 */
int dotenv_load(const char *path, bool overwrite);

/**
 * @brief Loads environment variables from the default ".env" file in the current directory.
 *
 * @param overwrite Whether to overwrite existing environment variables.
 * @return 0 on success, -1 on failure.
 */
int dotenv_load_default(bool overwrite);

/**
 * @brief Synchronizes one process environment variable with the C runtime.
 *
 * This is required on platforms where the operating-system environment and
 * the C runtime environment can be updated independently. On other platforms
 * the function is a no-op.
 *
 * @param name Environment variable name.
 * @return 0 on success, -1 if the name is invalid or synchronization fails.
 */
int dotenv_sync_environment(const char *name);

#ifdef __cplusplus
}
#endif

#endif // SALTS_DOTENV_H
