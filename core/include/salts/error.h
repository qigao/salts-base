#ifndef SALTS_ERROR_H
#define SALTS_ERROR_H

#include <salts/api.h>
#include <salts/error_codes.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native error domains have no CMeta descriptor dependency. Reflection for
 * this original enum remains an explicit upper-layer migration obligation. */
typedef enum error_domain_t {
  SALTS_ERROR_DOMAIN_NONE = 0,
  SALTS_ERROR_DOMAIN_SALTS = 1,
  SALTS_ERROR_DOMAIN_CUSTOM = 2,
  SALTS_ERROR_DOMAIN_POSIX = 3,
  SALTS_ERROR_DOMAIN_WIN32 = 4,
  SALTS_ERROR_DOMAIN_UNKNOWN = 5
} error_domain_t;

static inline size_t error_domain_count(void) {
  return 6u;
}

static inline const char *error_domain_to_string(error_domain_t value) {
  switch (value) {
  case SALTS_ERROR_DOMAIN_NONE: return "none";
  case SALTS_ERROR_DOMAIN_SALTS: return "salts";
  case SALTS_ERROR_DOMAIN_CUSTOM: return "custom";
  case SALTS_ERROR_DOMAIN_POSIX: return "posix";
  case SALTS_ERROR_DOMAIN_WIN32: return "win32";
  case SALTS_ERROR_DOMAIN_UNKNOWN: return "unknown";
  default: return "unknown";
  }
}

static inline int error_domain_from_string(const char *text,
                                                 error_domain_t *out) {
  error_domain_t parsed;
  if (!text || !out) return -1;
  for (int value = SALTS_ERROR_DOMAIN_NONE; value <= SALTS_ERROR_DOMAIN_UNKNOWN; ++value) {
    parsed = (error_domain_t)value;
    if (strcmp(error_domain_to_string(parsed), text) == 0) {
      *out = parsed;
      return 0;
    }
  }
  return -1;
}

static inline bool error_domain_is_valid(error_domain_t value) {
  return (unsigned int)value <= (unsigned int)SALTS_ERROR_DOMAIN_UNKNOWN;
}

static inline bool error_domain_equals(error_domain_t lhs,
                                             error_domain_t rhs) {
  return lhs == rhs;
}

#define SALTS_ERROR_CUSTOM_DOMAIN_MIN 1
#define SALTS_ERROR_CUSTOM_DOMAIN_MAX 32767
#define SALTS_ERROR_CUSTOM_LOCAL_MAX 65535
#define SALTS_ERROR_CUSTOM(domain, local)                                                           \
  (-(int)(((((domain) & 0x7fff) << 16) | ((local) & 0xffff))))
/* Unsigned magnitude also decodes INT_MIN without signed overflow. */
#define SALTS_ERROR_CUSTOM_DOMAIN(code) ((int)(((0u - (unsigned int)(code)) >> 16) & 0x7fffu))
#define SALTS_ERROR_CUSTOM_LOCAL(code) ((int)((0u - (unsigned int)(code)) & 0xffffu))

typedef struct {
  int code;
  int custom_domain;
  error_domain_t domain;
  const char *domain_name;
  const char *name;
  const char *message;
} error_info_t;

typedef struct {
  int code;
  const char *name;
  const char *message;
} error_entry_t;

typedef struct {
  int domain;
  const char *domain_name;
  const error_entry_t *entries;
  size_t count;
} error_domain_desc_t;

typedef struct {
  bool ok;
  int code;
  const char *message;
} result_t;

/** Return human-readable text for SALTS_*, negative errno, or negative Win32 error codes. */
SALTS_C_API const char *error_code_string(int err);

/** Return structured metadata for an error code. */
SALTS_C_API error_info_t error_info(int err);

/**
 * Register a custom error domain.
 *
 * The descriptor and entry table must remain alive until process exit or until
 * error_unregister_domain() is called. Domain ids must be in
 * [SALTS_ERROR_CUSTOM_DOMAIN_MIN, SALTS_ERROR_CUSTOM_DOMAIN_MAX].
 */
SALTS_C_API int error_register_domain(const error_domain_desc_t *domain);

/** Remove a previously registered custom domain. */
SALTS_C_API int error_unregister_domain(int domain);

static inline result_t result_ok(void) {
  result_t r;
  r.ok = true;
  r.code = SALTS_OK;
  r.message = "success";
  return r;
}

static inline result_t result_err(int code) {
  result_t r;
  r.ok = false;
  r.code = code;
  r.message = error_code_string(code);
  return r;
}

static inline result_t result_from_code(int code) {
  return code == SALTS_OK ? result_ok() : result_err(code);
}

static inline bool result_is_ok(result_t r) { return r.ok; }
static inline bool result_is_err(result_t r) { return !r.ok; }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_ERROR_H */
