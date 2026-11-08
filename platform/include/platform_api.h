#ifndef SALTS_CORE_PLATFORM_API_H
#define SALTS_CORE_PLATFORM_API_H

/* The Platform DLL owns these functions. Explicit declarations are required
 * even when a toolchain cannot implement CMake WINDOWS_EXPORT_ALL_SYMBOLS.
 * Consumers import this ABI; they never compile a second Platform owner.
 */
#if defined(_WIN32)
# define SALTS_CORE_PLATFORM_CALL __cdecl
#else
# define SALTS_CORE_PLATFORM_CALL
#endif
#if defined(_WIN32) && defined(SALTS_CORE_PLATFORM_BUILD)
# define SALTS_CORE_PLATFORM_API __declspec(dllexport)
#elif defined(_WIN32)
# define SALTS_CORE_PLATFORM_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
# define SALTS_CORE_PLATFORM_API __attribute__((visibility("default")))
#else
# define SALTS_CORE_PLATFORM_API
#endif

#endif
