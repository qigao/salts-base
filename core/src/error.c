#include <core/error.h>
#include <threads/threads.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define SALTS_ERROR_MAX_CUSTOM_DOMAINS 64

#define SALTS_ERROR_TABLE_ITEM(code, value, name, message) {code, name, message},

static const error_entry_t error_table[] = {
  SALTS_ERROR_CODE_ITEMS(SALTS_ERROR_TABLE_ITEM)
};
#undef SALTS_ERROR_TABLE_ITEM

static sync_once_t error_registry_once = SALTS_ONCE_INIT;
static sync_mutex_t error_registry_mutex;
static const error_domain_desc_t *error_custom_domains[SALTS_ERROR_MAX_CUSTOM_DOMAINS];
static size_t error_custom_domain_count;

static void error_registry_init(void) {
  sync_mutex_init(&error_registry_mutex);
}

static const error_entry_t *find_error(int err) {
  for (size_t i = 0; i < sizeof(error_table) / sizeof(error_table[0]); ++i) {
    if (error_table[i].code == err) return &error_table[i];
  }
  return NULL;
}

static int custom_domain_valid(const error_domain_desc_t *domain) {
  if (!domain || !domain->domain_name || !domain->entries || domain->count == 0) return 0;
  if (domain->domain < SALTS_ERROR_CUSTOM_DOMAIN_MIN ||
      domain->domain > SALTS_ERROR_CUSTOM_DOMAIN_MAX)
    return 0;
  for (size_t i = 0; i < domain->count; ++i) {
    const error_entry_t *entry = &domain->entries[i];
    if (!entry->name || !entry->message) return 0;
    if (entry->code >= 0) return 0;
    if (SALTS_ERROR_CUSTOM_DOMAIN(entry->code) != domain->domain) return 0;
    if (SALTS_ERROR_CUSTOM_LOCAL(entry->code) == 0) return 0;
  }
  return 1;
}

static const error_entry_t *find_custom_error_locked(
    int err, const error_domain_desc_t **out_domain) {
  int domain_id;

  if (err >= 0) return NULL;
  domain_id = SALTS_ERROR_CUSTOM_DOMAIN(err);
  if (domain_id < SALTS_ERROR_CUSTOM_DOMAIN_MIN ||
      domain_id > SALTS_ERROR_CUSTOM_DOMAIN_MAX) {
    return NULL;
  }

  for (size_t i = 0; i < error_custom_domain_count; ++i) {
    const error_domain_desc_t *domain = error_custom_domains[i];
    if (!domain || domain->domain != domain_id) continue;
    for (size_t j = 0; j < domain->count; ++j) {
      if (domain->entries[j].code == err) {
        if (out_domain) *out_domain = domain;
        return &domain->entries[j];
      }
    }
  }
  return NULL;
}

int error_register_domain(const error_domain_desc_t *domain) {
  sync_once(&error_registry_once, error_registry_init);
  if (!custom_domain_valid(domain)) return SALTS_EINVAL;

  sync_mutex_lock(&error_registry_mutex);
  for (size_t i = 0; i < error_custom_domain_count; ++i) {
    if (error_custom_domains[i] &&
        error_custom_domains[i]->domain == domain->domain) {
      sync_mutex_unlock(&error_registry_mutex);
      return SALTS_EALREADY;
    }
  }
  if (error_custom_domain_count >= SALTS_ERROR_MAX_CUSTOM_DOMAINS) {
    sync_mutex_unlock(&error_registry_mutex);
    return SALTS_ENOSPC;
  }
  error_custom_domains[error_custom_domain_count++] = domain;
  sync_mutex_unlock(&error_registry_mutex);
  return SALTS_OK;
}

int error_unregister_domain(int domain) {
  sync_once(&error_registry_once, error_registry_init);
  if (domain < SALTS_ERROR_CUSTOM_DOMAIN_MIN || domain > SALTS_ERROR_CUSTOM_DOMAIN_MAX) {
    return SALTS_EINVAL;
  }

  sync_mutex_lock(&error_registry_mutex);
  for (size_t i = 0; i < error_custom_domain_count; ++i) {
    if (error_custom_domains[i] && error_custom_domains[i]->domain == domain) {
      error_custom_domains[i] =
          error_custom_domains[error_custom_domain_count - 1];
      error_custom_domains[error_custom_domain_count - 1] = NULL;
      --error_custom_domain_count;
      sync_mutex_unlock(&error_registry_mutex);
      return SALTS_OK;
    }
  }
  sync_mutex_unlock(&error_registry_mutex);
  return SALTS_ENOENT;
}

static const char *unknown_message(int err) {
  static SALTS_THREAD_LOCAL char buf[64];
  snprintf(buf, sizeof(buf), "unknown error %d", err);
  return buf;
}

static const char *posix_strerror(int posix_code) {
  const char *msg;

  if (posix_code <= 0) return NULL;
  msg = strerror(posix_code);
  if (!msg || msg[0] == '\0') return NULL;
  if (strstr(msg, "Unknown error") != NULL || strstr(msg, "unknown error") != NULL) return NULL;
  return msg;
}

#ifdef _WIN32
static const char *win32_strerror(int win32_code) {
  static SALTS_THREAD_LOCAL char buf[256];
  DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD written;

  if (win32_code <= 0) return NULL;
  written = FormatMessageA(flags, NULL, (DWORD)win32_code, 0, buf, (DWORD)sizeof(buf), NULL);
  if (written == 0) return NULL;
  while (written > 0 && (buf[written - 1] == '\n' || buf[written - 1] == '\r' ||
                         buf[written - 1] == ' ' || buf[written - 1] == '\t')) {
    buf[--written] = '\0';
  }
  return buf;
}
#endif

error_info_t error_info(int err) {
  error_info_t info;
  const error_entry_t *entry;
  const error_domain_desc_t *custom_domain = NULL;
  int native_code;
  const char *native_msg;

  info.code = err;
  info.custom_domain = 0;
  info.domain = SALTS_ERROR_DOMAIN_UNKNOWN;
  info.domain_name = "unknown";
  info.name = "UNKNOWN";
  info.message = NULL;

  if (err == SALTS_OK) {
    info.domain = SALTS_ERROR_DOMAIN_NONE;
    info.domain_name = "none";
    info.name = "SALTS_OK";
    info.message = "success";
    return info;
  }

  entry = find_error(err);
  if (entry) {
    info.domain = SALTS_ERROR_DOMAIN_SALTS;
    info.domain_name = "salts";
    info.name = entry->name;
    info.message = entry->message;
    return info;
  }

  sync_once(&error_registry_once, error_registry_init);
  sync_mutex_lock(&error_registry_mutex);
  entry = find_custom_error_locked(err, &custom_domain);
  if (entry) {
    info.custom_domain = custom_domain->domain;
    info.domain = SALTS_ERROR_DOMAIN_CUSTOM;
    info.domain_name = custom_domain->domain_name;
    info.name = entry->name;
    info.message = entry->message;
    sync_mutex_unlock(&error_registry_mutex);
    return info;
  }
  sync_mutex_unlock(&error_registry_mutex);

  if (err < 0) {
    int domain_id = SALTS_ERROR_CUSTOM_DOMAIN(err);
    if (domain_id >= SALTS_ERROR_CUSTOM_DOMAIN_MIN &&
        domain_id <= SALTS_ERROR_CUSTOM_DOMAIN_MAX) {
      info.custom_domain = domain_id;
      info.domain_name = "custom";
      info.message = unknown_message(err);
      return info;
    }
  }

  /* INT_MIN has no positive int counterpart for strerror/FormatMessageA. */
  if (err == INT_MIN) {
    info.message = unknown_message(err);
    return info;
  }
  native_code = err < 0 ? -err : err;
  native_msg = posix_strerror(native_code);
  if (native_msg) {
    info.domain = SALTS_ERROR_DOMAIN_POSIX;
    info.domain_name = "posix";
    info.name = "POSIX";
    info.message = native_msg;
    return info;
  }

#ifdef _WIN32
  native_msg = win32_strerror(native_code);
  if (native_msg) {
    info.domain = SALTS_ERROR_DOMAIN_WIN32;
    info.domain_name = "win32";
    info.name = "WIN32";
    info.message = native_msg;
    return info;
  }
#endif

  info.message = unknown_message(err);
  return info;
}

const char *error_code_string(int err) {
  return error_info(err).message;
}
