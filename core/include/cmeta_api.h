#ifndef SALTS_API_H
#define SALTS_API_H

/* Core linkage markers. Build-system producer/consumer state is supplied by
 * the Salts::Core target; this header does not inspect CMake internals. */
#ifndef SALTS_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_API __attribute__((visibility("default")))
  #else
    #define SALTS_API
  #endif
#endif

#ifndef SALTS_C_API
  #ifdef __cplusplus
    #define SALTS_C_API extern "C" SALTS_API
  #else
    #define SALTS_C_API SALTS_API
  #endif
#endif

#endif /* SALTS_API_H */
