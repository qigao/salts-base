#include "cmd_arger.h"
#include "cmd_arger_internal.h"
#include "tstr.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dotenv.h>

// ============================================================================
// Descriptor Builder Functions
// ============================================================================

/**
 * @brief Create a boolean flag descriptor.
 */
CmdArgerDesc cmd_arger_desc_flag(CmdArgerBool *value_out, const char *name, const char *info) {
  return cmd_arger_desc_flag_sh(value_out, name, NULL, info);
}

/**
 * @brief Create a string argument descriptor.
 */
CmdArgerDesc cmd_arger_desc_string(char **value_out, const char *name, const char *info) {
  return cmd_arger_desc_string_sh(value_out, name, NULL, info);
}

/**
 * @brief Create an integer argument descriptor.
 */
CmdArgerDesc cmd_arger_desc_integer(int64_t *value_out, const char *name, const char *info) {
  return cmd_arger_desc_integer_sh(value_out, name, NULL, info);
}

/**
 * @brief Create a floating point argument descriptor.
 */
CmdArgerDesc cmd_arger_desc_float(double *value_out, const char *name, const char *info) {
  return cmd_arger_desc_float_sh(value_out, name, NULL, info);
}

/**
 * @brief Create an enum argument descriptor.
 */
CmdArgerDesc cmd_arger_desc_enum(int64_t *value_out, const char *name, const char *info,
                                 CmdArgerEnumDesc *enum_descs, uint32_t enum_descs_count) {
  return cmd_arger_desc_enum_sh(value_out, name, NULL, info, enum_descs, enum_descs_count);
}

/**
 * @brief Create a string list argument descriptor.
 */
CmdArgerDesc cmd_arger_desc_string_list(char **values_out, uint32_t *count_out, uint32_t max_count,
                                        const char *name, const char *info) {
  return cmd_arger_desc_string_list_sh(values_out, count_out, max_count, name, NULL, info);
}

/**
 * @brief Create a boolean flag descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_flag_sh(CmdArgerBool *value_out, const char *name,
                                    const char *short_name, const char *info) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = value_out,
      .kind = CmdArgerDescKind_flag,
  };
}

/**
 * @brief Create a string argument descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_string_sh(char **value_out, const char *name, const char *short_name,
                                      const char *info) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = value_out,
      .kind = CmdArgerDescKind_string,
  };
}

/**
 * @brief Create an integer argument descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_integer_sh(int64_t *value_out, const char *name, const char *short_name,
                                       const char *info) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = value_out,
      .kind = CmdArgerDescKind_integer,
  };
}

/**
 * @brief Create a floating point argument descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_float_sh(double *value_out, const char *name, const char *short_name,
                                     const char *info) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = value_out,
      .kind = CmdArgerDescKind_float,
  };
}

/**
 * @brief Create an enum argument descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_enum_sh(int64_t *value_out, const char *name, const char *short_name,
                                    const char *info, CmdArgerEnumDesc *enum_descs,
                                    uint32_t enum_descs_count) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = value_out,
      .kind = CmdArgerDescKind_enum,
      .spec.enums.descs = enum_descs,
      .spec.enums.count = enum_descs_count,
  };
}

/**
 * @brief Create a string list argument descriptor with a short name.
 */
CmdArgerDesc cmd_arger_desc_string_list_sh(char **values_out, uint32_t *count_out,
                                           uint32_t max_count, const char *name,
                                           const char *short_name, const char *info) {
  return (CmdArgerDesc){
      .name = name,
      .short_name = short_name,
      .info = info,
      .value_out = values_out,
      .kind = CmdArgerDescKind_string_list,
      .spec.list.count_out = count_out,
      .spec.list.max_count = max_count,
  };
}

// ============================================================================
// Modifier Functions
// ============================================================================

/**
 * @brief Adds an environment variable fallback to a descriptor.
 */
CmdArgerDesc cmd_arger_with_env(CmdArgerDesc desc, const char *env_var) {
  desc.env_var = env_var;
  return desc;
}

/**
 * @brief Assigns a descriptor to a specific help group.
 */
CmdArgerDesc cmd_arger_with_group(CmdArgerDesc desc, const char *group) {
  desc.group = group;
  return desc;
}

/**
 * @brief Restricts a string argument to a specific set of choices.
 */
CmdArgerDesc cmd_arger_with_choices(CmdArgerDesc desc, const char **choices,
                                    uint32_t choices_count) {
  desc.spec.choices.items = choices;
  desc.spec.choices.count = choices_count;
  return desc;
}

/**
 * @brief Adds a custom validator function to a descriptor.
 */
CmdArgerDesc cmd_arger_with_validator(CmdArgerDesc desc, CmdArgerValidator validator) {
  desc.validator = validator;
  return desc;
}

/**
 * @brief Makes an optional argument required.
 */
CmdArgerDesc cmd_arger_required(CmdArgerDesc desc) {
  desc.is_required = cmd_arger_true;
  return desc;
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Reads the entire content of a file into a heap-allocated buffer.
 * @param path The filesystem path to the file.
 * @return Null-terminated string buffer, or NULL if reading failed.
 */
static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buffer = malloc(length + 1);
  if (buffer) {
    fread(buffer, 1, length, f);
    buffer[length] = '\0';
  }
  fclose(f);
  return buffer;
}

/**
 * @brief Internal printf wrapper that automatically colorizes using mustache tags.
 */
static void cmd_arger_printf(CmdArgerBool colors, const char *fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  char color_buf[4096];
  mustache_colorize(color_buf, buf, colors);
  printf("%s", color_buf);
}

/**
 * @brief Checks environment variables for default values of arguments.
 */
static void cmd_arger_apply_env_vars(CmdArgerDesc *args, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    if (!args[i].env_var)
      continue;

    (void)dotenv_sync_environment(args[i].env_var);
    char *env_val = getenv(args[i].env_var);
    if (!env_val)
      continue;

    switch (args[i].kind) {
    case CmdArgerDescKind_flag: {
      if (tstr_casecmp(env_val, "1") == 0 || tstr_casecmp(env_val, "true") == 0 ||
          tstr_casecmp(env_val, "yes") == 0 || tstr_casecmp(env_val, "on") == 0) {
        *(CmdArgerBool *)args[i].value_out = cmd_arger_true;
      } else {
        *(CmdArgerBool *)args[i].value_out = cmd_arger_false;
      }
      break;
    }
    case CmdArgerDescKind_string: {
      *(char **)args[i].value_out = env_val;
      break;
    }
    case CmdArgerDescKind_integer: {
      char *end;
      long v = strtol(env_val, &end, 10);
      if (*end == '\0') {
        *(int64_t *)args[i].value_out = v;
      }
      break;
    }
    case CmdArgerDescKind_float: {
      char *end;
      double v = strtod(env_val, &end);
      if (*end == '\0') {
        *(double *)args[i].value_out = v;
      }
      break;
    }
    default:
      break;
    }
  }
}

/**
 * @brief Dynamic array for storing command line arguments.
 */
typedef struct {
  char **argv;
  uint8_t *owned;
  int argc;
  int capacity;
} ArgList;

typedef struct ExpandedArgsCleanup_s {
  char **argv;
  uint8_t *owned;
  int argc;
  struct ExpandedArgsCleanup_s *next;
} ExpandedArgsCleanup;

static ExpandedArgsCleanup *g_expanded_args_cleanup;
static int g_expanded_args_atexit_registered;

static void free_expanded_args_cleanup(void) {
  ExpandedArgsCleanup *node = g_expanded_args_cleanup;
  while (node) {
    ExpandedArgsCleanup *next = node->next;
    if (node->owned) {
      for (int i = 0; i < node->argc; i++) {
        if (node->owned[i]) {
          free(node->argv[i]);
        }
      }
    }
    free(node->owned);
    free(node->argv);
    free(node);
    node = next;
  }
  g_expanded_args_cleanup = NULL;
}

static void register_expanded_args_cleanup(char **argv, uint8_t *owned, int argc) {
  ExpandedArgsCleanup *node = (ExpandedArgsCleanup *)malloc(sizeof(*node));
  if (!node) return;
  node->argv = argv;
  node->owned = owned;
  node->argc = argc;
  node->next = g_expanded_args_cleanup;
  g_expanded_args_cleanup = node;
  if (!g_expanded_args_atexit_registered) {
    atexit(free_expanded_args_cleanup);
    g_expanded_args_atexit_registered = 1;
  }
}

/**
 * @brief Adds an argument to the ArgList.
 */
static void arg_list_add(ArgList *list, char *arg, uint8_t owned) {
  if (list->argc >= list->capacity) {
    list->capacity *= 2;
    list->argv = realloc(list->argv, sizeof(char *) * list->capacity);
    list->owned = realloc(list->owned, sizeof(uint8_t) * list->capacity);
  }
  list->argv[list->argc++] = arg;
  list->owned[list->argc - 1] = owned;
}

/**
 * @brief Expands response files (@file) into individual arguments.
 */
static void expand_args(int *argc_out, char ***argv_out, int argc_in, char **argv_in) {
  ArgList list;
  list.capacity = argc_in + 16;
  list.argv = malloc(sizeof(char *) * list.capacity);
  list.owned = malloc(sizeof(uint8_t) * list.capacity);
  list.argc = 0;
  if (!list.argv || !list.owned) {
    free(list.argv);
    free(list.owned);
    *argc_out = argc_in;
    *argv_out = argv_in;
    return;
  }

  for (int i = 0; i < argc_in; i++) {
    char *arg = argv_in[i];
    if (arg[0] == '@' && arg[1] != '\0') {
      char *content = read_file(arg + 1);
      if (content) {
        const char *p = content;
        const char *token_start;
        size_t len;

        while ((len = lex_next_arg(&p, &token_start)) > 0) {
          tstr token = tstr_dup_len(token_start, len);
          if (token) {
            char *token_cstr = tstr_to_cstr(token);
            tstr_free(token);
            if (token_cstr) {
              arg_list_add(&list, token_cstr, 1);
            }
          }
        }
        free(content);
      } else {
        arg_list_add(&list, arg, 0);
      }
    } else {
      arg_list_add(&list, arg, 0);
    }
  }

  if (list.argc >= list.capacity) {
    list.argv = realloc(list.argv, sizeof(char *) * (list.capacity + 1));
    list.owned = realloc(list.owned, sizeof(uint8_t) * (list.capacity + 1));
  }
  list.argv[list.argc] = NULL;
  list.owned[list.argc] = 0;

  *argc_out = list.argc;
  *argv_out = list.argv;
  register_expanded_args_cleanup(list.argv, list.owned, list.argc);
}

// ============================================================================
// Parser Implementation
// ============================================================================

/**
 * @brief Finds a descriptor in an array by name or short name.
 */
static CmdArgerDesc *find_desc(CmdArgerDesc *descs, uint32_t count, cmd_token_kind_t kind,
                               const char *key_start, size_t key_len) {
  if (!descs)
    return NULL;
  for (uint32_t i = 0; i < count; i++) {
    if (kind == CMD_TOKEN_SHORT_OPTION) {
      if (descs[i].short_name && strncmp(descs[i].short_name, key_start, key_len) == 0 &&
          descs[i].short_name[key_len] == '\0') {
        return &descs[i];
      }
    } else {
      if (strncmp(descs[i].name, key_start, key_len) == 0 && descs[i].name[key_len] == '\0') {
        return &descs[i];
      }
    }
  }
  return NULL;
}

/**
 * @brief Parses and validates a raw string value into the appropriate type for a descriptor.
 */
static CmdArgerBool apply_value(CmdArgerDesc *desc, const char *value_str, CmdArgerBool colors) {
  if (desc->validator) {
    const char *err_msg = NULL;
    if (!desc->validator(value_str, &err_msg)) {
      cmd_arger_printf(colors,
                       "{{error}}error:{{reset}} invalid value '%s' for option '%s': %s\n\n",
                       value_str, desc->name, err_msg ? err_msg : "validation failed");
      return cmd_arger_false;
    }
  }

  switch (desc->kind) {
  case CmdArgerDescKind_string: {
    if (desc->spec.choices.items && desc->spec.choices.count > 0) {
      int match = 0;
      for (uint32_t i = 0; i < desc->spec.choices.count; i++) {
        if (strcmp(value_str, desc->spec.choices.items[i]) == 0) {
          match = 1;
          break;
        }
      }
      if (!match) {
        cmd_arger_printf(colors,
                         "{{error}}error:{{reset}} invalid value '%s' for option '%s'. Allowed: ",
                         value_str, desc->name);
        for (uint32_t i = 0; i < desc->spec.choices.count; i++) {
          printf("%s%s", desc->spec.choices.items[i],
                 (i < desc->spec.choices.count - 1) ? ", " : "");
        }
        printf("\n\n");
        return cmd_arger_false;
      }
    }
    *(char **)desc->value_out = (char *)value_str;
    break;
  }
  case CmdArgerDescKind_integer: {
    char *end;
    long v = strtol(value_str, &end, 10);
    if (*end != '\0') {
      cmd_arger_printf(colors, "{{error}}error:{{reset}} expected integer for '%s', got '%s'\n\n",
                       desc->name, value_str);
      return cmd_arger_false;
    }
    *(int64_t *)desc->value_out = (int64_t)v;
    break;
  }
  case CmdArgerDescKind_float: {
    char *end;
    double v = strtod(value_str, &end);
    if (*end != '\0') {
      cmd_arger_printf(colors, "{{error}}error:{{reset}} expected float for '%s', got '%s'\n\n",
                       desc->name, value_str);
      return cmd_arger_false;
    }
    *(double *)desc->value_out = v;
    break;
  }
  case CmdArgerDescKind_enum: {
    int found = 0;
    for (uint32_t i = 0; i < desc->spec.enums.count; i++) {
      if (tstr_casecmp(value_str, desc->spec.enums.descs[i].name) == 0) {
        *(int64_t *)desc->value_out = desc->spec.enums.descs[i].value;
        found = 1;
        break;
      }
    }
    if (!found) {
      cmd_arger_printf(colors,
                       "{{error}}error:{{reset}} invalid choice '%s' for option '%s'. Allowed: ",
                       value_str, desc->name);
      for (uint32_t i = 0; i < desc->spec.enums.count; i++) {
        printf("%s%s", desc->spec.enums.descs[i].name,
               (i < desc->spec.enums.count - 1) ? ", " : "");
      }
      printf("\n\n");
      return cmd_arger_false;
    }
    break;
  }
  case CmdArgerDescKind_string_list: {
    uint32_t count = *desc->spec.list.count_out;
    if (count < desc->spec.list.max_count) {
      ((char **)desc->value_out)[count] = (char *)value_str;
      *desc->spec.list.count_out = count + 1;
    }
    break;
  }
  default:
    break;
  }
  return cmd_arger_true;
}

/**
 * @brief Internal unified parsing logic shared between parse and parse_subcommand.
 */
static void cmd_arger_parse_internal(CmdArgerDesc *global_optional_args,
                                     uint32_t global_optional_args_count,
                                     CmdArgerDesc *required_args, uint32_t required_args_count,
                                     CmdArgerSubCommand *subcommands, uint32_t subcommands_count,
                                     int *selected_subcommand_idx, int argc, char **argv,
                                     const char *app_name_and_version, CmdArgerBool colors) {
  // Load .env if it exists, don't overwrite existing environment variables
  dotenv_load_default(false);

  int has_response_file = 0;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '@') {
      has_response_file = 1;
      break;
    }
  }

  char **active_argv = argv;
  int active_argc = argc;

  if (has_response_file) {
    expand_args(&active_argc, &active_argv, argc, argv);
  }

  int arg_idx = 1;

  if (global_optional_args) {
    cmd_arger_apply_env_vars(global_optional_args, global_optional_args_count);
  }

  int current_subcommand = -1;
  if (selected_subcommand_idx)
    *selected_subcommand_idx = -1;

  CmdArgerDesc *active_optional = global_optional_args;
  uint32_t active_optional_cnt = global_optional_args_count;

  CmdArgerDesc *active_required = required_args;
  uint32_t active_required_cnt = required_args_count;
  int required_args_idx = 0;

  if (subcommands_count > 0) {
    active_required = NULL;
    active_required_cnt = 0;
  }

  uint8_t *seen_global = NULL;
  if (global_optional_args_count > 0) {
    seen_global = (uint8_t *)calloc(global_optional_args_count, sizeof(uint8_t));
  }
  uint8_t *seen_subcommand = NULL;

  int stop_options = 0;
  while (arg_idx < active_argc) {
    const char *current_str = active_argv[arg_idx];
    const char *key_start = NULL;
    size_t key_len = 0;
    const char *val_start = NULL;

    cmd_token_kind_t kind = lex_token(current_str, &key_start, &key_len, &val_start);

    if (!stop_options && kind == CMD_TOKEN_DASH_DASH) {
      stop_options = 1;
      goto next_arg;
    }

    if (!stop_options && kind == CMD_TOKEN_HELP)
      goto PRINT_HELP;

    CmdArgerDesc *desc = NULL;
    const char *value_str = NULL;

    if (!stop_options && (kind == CMD_TOKEN_OPTION || kind == CMD_TOKEN_OPTION_WITH_VALUE ||
                          kind == CMD_TOKEN_SHORT_OPTION)) {
      if (kind == CMD_TOKEN_SHORT_OPTION && key_len > 1) {
        for (size_t k = 0; k < key_len; k++) {
          desc = find_desc(active_optional, active_optional_cnt, kind, key_start + k, 1);
          if (!desc && current_subcommand != -1) {
            desc =
                find_desc(global_optional_args, global_optional_args_count, kind, key_start + k, 1);
            if (desc) {
              seen_global[desc - global_optional_args] = 1;
            }
          } else if (desc) {
            seen_subcommand[desc - active_optional] = 1;
          }

          if (!desc) {
            cmd_arger_printf(
                colors,
                "{{error}}error:{{reset}} unsupported short option '-%c' in bundle '%s'\n\n",
                key_start[k], current_str);
            goto PRINT_HELP;
          }

          if (desc->kind == CmdArgerDescKind_flag) {
            *(CmdArgerBool *)desc->value_out = cmd_arger_true;
          } else {
            if (k < key_len - 1) {
              cmd_arger_printf(colors,
                               "{{error}}error:{{reset}} short option '-%c' in bundle '%s' "
                               "requires a value and must be at the end\n\n",
                               key_start[k], current_str);
              goto PRINT_HELP;
            }
            if (++arg_idx >= active_argc) {
              cmd_arger_printf(colors, "{{error}}error:{{reset}} option '-%c' requires a value\n\n",
                               key_start[k]);
              goto PRINT_HELP;
            }
            value_str = active_argv[arg_idx];
            if (!apply_value(desc, value_str, colors))
              goto PRINT_HELP;
          }
        }
        goto next_arg;
      }

      desc = find_desc(active_optional, active_optional_cnt, kind, key_start, key_len);
      if (desc) {
        if (current_subcommand != -1)
          seen_subcommand[desc - active_optional] = 1;
        else
          seen_global[desc - active_optional] = 1;
      } else if (current_subcommand != -1) {
        desc =
            find_desc(global_optional_args, global_optional_args_count, kind, key_start, key_len);
        if (desc)
          seen_global[desc - global_optional_args] = 1;
      }

      if (!desc) {
        const char *prefix = (kind == CMD_TOKEN_SHORT_OPTION) ? "-" : "--";
        cmd_arger_printf(colors, "{{error}}error:{{reset}} unsupported option '%s%.*s'\n\n", prefix,
                         (int)key_len, key_start);
        goto PRINT_HELP;
      }

      if (desc->kind == CmdArgerDescKind_flag) {
        if (kind == CMD_TOKEN_OPTION_WITH_VALUE) {
          const char *prefix = (kind == CMD_TOKEN_SHORT_OPTION) ? "-" : "--";
          cmd_arger_printf(colors, "{{error}}error:{{reset}} flag '%s%s' cannot take a value\n\n",
                           prefix, desc->name);
          goto PRINT_HELP;
        }
        *(CmdArgerBool *)desc->value_out = cmd_arger_true;
      } else {
        if (kind == CMD_TOKEN_OPTION_WITH_VALUE) {
          value_str = val_start;
        } else {
          if (++arg_idx >= active_argc) {
            const char *prefix = (kind == CMD_TOKEN_SHORT_OPTION) ? "-" : "--";
            cmd_arger_printf(colors,
                             "{{error}}error:{{reset}} option '%s%.*s' requires a value\n\n",
                             prefix, (int)key_len, key_start);
            goto PRINT_HELP;
          }
          value_str = active_argv[arg_idx];
        }
      }
    } else {
      if (current_subcommand == -1 && subcommands_count > 0) {
        for (uint32_t i = 0; i < subcommands_count; i++) {
          if (strcmp(subcommands[i].name, current_str) == 0) {
            current_subcommand = (int)i;
            if (selected_subcommand_idx)
              *selected_subcommand_idx = (int)i;
            active_optional = subcommands[i].optional_args;
            active_optional_cnt = subcommands[i].optional_args_count;
            active_required = subcommands[i].required_args;
            active_required_cnt = subcommands[i].required_args_count;
            required_args_idx = 0;
            if (active_optional_cnt > 0) {
              seen_subcommand = (uint8_t *)calloc(active_optional_cnt, sizeof(uint8_t));
            }
            if (active_optional)
              cmd_arger_apply_env_vars(active_optional, active_optional_cnt);
            goto next_arg;
          }
        }
        cmd_arger_printf(colors, "{{error}}error:{{reset}} unknown command '%s'\n\n", current_str);
        goto PRINT_HELP;
      } else {
        if (required_args_idx >= active_required_cnt) {
          cmd_arger_printf(colors,
                           "{{error}}error:{{reset}} unexpected positional argument '%s'\n\n",
                           current_str);
          goto PRINT_HELP;
        }

        desc = &active_required[required_args_idx];
        if (desc->kind == CmdArgerDescKind_string_list) {
          uint32_t count = *desc->spec.list.count_out;
          if (count >= desc->spec.list.max_count) {
            required_args_idx++;
            if (required_args_idx >= active_required_cnt) {
              cmd_arger_printf(colors,
                               "{{error}}error:{{reset}} unexpected positional argument '%s'\n\n",
                               current_str);
              goto PRINT_HELP;
            }
            desc = &active_required[required_args_idx++];
          }
          // Note: if it is a list, we DON'T increment required_args_idx yet
          // so the next positional arg also goes to this list
        } else {
          required_args_idx++;
        }
        value_str = current_str;
      }
    }

    if (desc && value_str && !apply_value(desc, value_str, colors))
      goto PRINT_HELP;

  next_arg:
    arg_idx++;
  }

  if (required_args_idx < active_required_cnt) {
    // If we're at a list that has at least one item, it counts as fulfilled
    if (active_required[required_args_idx].kind == CmdArgerDescKind_string_list &&
        *active_required[required_args_idx].spec.list.count_out > 0) {
      required_args_idx++;
    }

    if (required_args_idx < active_required_cnt) {
      cmd_arger_printf(colors, "{{error}}error:{{reset}} missing required argument '%s'\n\n",
                       active_required[required_args_idx].name);
      goto PRINT_HELP;
    }
  }

  for (uint32_t i = 0; i < global_optional_args_count; i++) {
    if (global_optional_args[i].is_required && !seen_global[i]) {
      cmd_arger_printf(colors, "{{error}}error:{{reset}} missing required option '--%s'\n\n",
                       global_optional_args[i].name);
      goto PRINT_HELP;
    }
  }
  if (current_subcommand != -1) {
    for (uint32_t i = 0; i < active_optional_cnt; i++) {
      if (active_optional[i].is_required && !seen_subcommand[i]) {
        cmd_arger_printf(colors, "{{error}}error:{{reset}} missing required option '--%s'\n\n",
                         active_optional[i].name);
        goto PRINT_HELP;
      }
    }
  }

  if (subcommands_count > 0 && current_subcommand == -1) {
    cmd_arger_printf(colors, "{{error}}error:{{reset}} missing subcommand\n\n");
    goto PRINT_HELP;
  }

  if (seen_global)
    free(seen_global);
  if (seen_subcommand)
    free(seen_subcommand);
  return;

PRINT_HELP:
  if (seen_global)
    free(seen_global);
  if (seen_subcommand)
    free(seen_subcommand);
  if (current_subcommand != -1) {
    cmd_arger_show_subcommand_help_and_exit(
        global_optional_args, global_optional_args_count, active_optional, active_optional_cnt,
        active_required, active_required_cnt, subcommands, subcommands_count,
        subcommands[current_subcommand].name, argv[0], app_name_and_version, colors);
  } else {
    cmd_arger_show_subcommand_help_and_exit(global_optional_args, global_optional_args_count, NULL,
                                            0, NULL, 0, subcommands, subcommands_count, NULL,
                                            argv[0], app_name_and_version, colors);
  }
}

/**
 * @brief Public API to parse standard arguments.
 */
void cmd_arger_parse(CmdArgerDesc *optional_args, uint32_t optional_args_count,
                     CmdArgerDesc *required_args, uint32_t required_args_count, int argc,
                     char **argv, const char *app_name_and_version, CmdArgerBool colors) {
  cmd_arger_parse_internal(optional_args, optional_args_count, required_args, required_args_count,
                           NULL, 0, NULL, argc, argv, app_name_and_version, colors);
}

/**
 * @brief Public API to parse arguments with subcommands.
 */
void cmd_arger_parse_subcommand(CmdArgerDesc *global_optional_args,
                                uint32_t global_optional_args_count,
                                CmdArgerSubCommand *subcommands, uint32_t subcommands_count,
                                int *selected_subcommand_idx, int argc, char **argv,
                                const char *app_name_and_version, CmdArgerBool colors) {
  cmd_arger_parse_internal(global_optional_args, global_optional_args_count, NULL, 0, subcommands,
                           subcommands_count, selected_subcommand_idx, argc, argv,
                           app_name_and_version, colors);
}

// ============================================================================
// Help Display Functions
// ============================================================================

/**
 * @brief Public API to manually trigger help display.
 */
void cmd_arger_show_help_and_exit(CmdArgerDesc *optional_args, uint32_t optional_args_count,
                                  CmdArgerDesc *required_args, uint32_t required_args_count,
                                  const char *exe_name, const char *app_name_and_version, CmdArgerBool colors) {
  cmd_arger_show_subcommand_help_and_exit(optional_args, optional_args_count, NULL, 0,
                                          required_args, required_args_count, NULL, 0, NULL,
                                          exe_name, app_name_and_version, colors);
}

/**
 * @brief Prints a detailed description row for an option, including short name and defaults.
 */
static void print_option_desc(CmdArgerDesc *desc, CmdArgerBool colors) {
  if (desc->short_name) {
    cmd_arger_printf(colors, "\t{{yellow}}-%s, --%s{{reset}}: %s", desc->short_name, desc->name,
                     desc->info);
  } else {
    cmd_arger_printf(colors, "\t{{yellow}}--%s{{reset}}: %s", desc->name, desc->info);
  }

  if (desc->kind == CmdArgerDescKind_string && desc->spec.choices.items &&
      desc->spec.choices.count > 0) {
    printf(" [");
    for (uint32_t i = 0; i < desc->spec.choices.count; i++) {
      printf("%s%s", desc->spec.choices.items[i],
             (i < desc->spec.choices.count - 1) ? "|" : "");
    }
    printf("]");
  } else if (desc->kind == CmdArgerDescKind_enum && desc->spec.enums.descs &&
             desc->spec.enums.count > 0) {
    printf(" [");
    for (uint32_t i = 0; i < desc->spec.enums.count; i++) {
      printf("%s%s", desc->spec.enums.descs[i].name,
             (i < desc->spec.enums.count - 1) ? "|" : "");
    }
    printf("]");
  }

  if (!desc->is_required && desc->value_out) {
    switch (desc->kind) {
    case CmdArgerDescKind_string: {
      char *s = *(char **)desc->value_out;
      if (s)
        cmd_arger_printf(colors, " {{grey}}[default: \"%s\"]{{reset}}", s);
      break;
    }
    case CmdArgerDescKind_integer: {
      cmd_arger_printf(colors, " {{grey}}[default: %lld]{{reset}}",
                       (long long)*(int64_t *)desc->value_out);
      break;
    }
    case CmdArgerDescKind_float: {
      cmd_arger_printf(colors, " {{grey}}[default: %.2f]{{reset}}", *(double *)desc->value_out);
      break;
    }
    case CmdArgerDescKind_flag: {
      if (*(CmdArgerBool *)desc->value_out)
        cmd_arger_printf(colors, " {{grey}}[default: true]{{reset}}");
      break;
    }
    default:
      break;
    }
  }

  if (desc->is_required) {
    cmd_arger_printf(colors, " {{red}}(required){{reset}}");
  }
  printf("\n");
}

/**
 * @brief Core engine for printing the help screen, supporting groups and subcommands.
 */
void cmd_arger_show_subcommand_help_and_exit(
    CmdArgerDesc *global_optional_args, uint32_t global_optional_args_count,
    CmdArgerDesc *active_optional_args, uint32_t active_optional_args_count,
    CmdArgerDesc *active_required_args, uint32_t active_required_args_count,
    CmdArgerSubCommand *all_subcommands, uint32_t all_subcommands_count,
    const char *active_subcommand_name, const char *exe_name, const char *app_name_and_version, CmdArgerBool colors) {
  const char *base_name = strrchr(exe_name, '/');
  if (!base_name)
    base_name = strrchr(exe_name, '\\');
  if (base_name)
    base_name++;
  else
    base_name = exe_name;

  cmd_arger_printf(colors, "{{bold}}------ %s help ------\n{{reset}}", app_name_and_version);
  cmd_arger_printf(colors, "{{bold}}usage:{{reset}} %s", base_name);

  if (active_subcommand_name) {
    printf(" %s", active_subcommand_name);
  } else if (all_subcommands_count > 0) {
    printf(" <subcommand>");
  }

  if (global_optional_args_count > 0 || (active_optional_args && active_optional_args_count > 0)) {
    printf(" [OPTIONS...]");
  }

  if (active_required_args) {
    for (uint32_t i = 0; i < active_required_args_count; i++)
      printf(" %s", active_required_args[i].name);
  }
  printf("\n\n");

  if (all_subcommands_count > 0 && !active_subcommand_name) {
    printf("SUBCOMMANDS:\n");
    for (uint32_t i = 0; i < all_subcommands_count; i++) {
      cmd_arger_printf(colors, "\t{{yellow}}%s{{reset}}: %s\n", all_subcommands[i].name,
                       all_subcommands[i].info);
    }
    printf("\n");
  }

  if (active_required_args && active_required_args_count > 0) {
    printf("ARGUMENTS:\n");
    for (uint32_t i = 0; i < active_required_args_count; i++) {
      cmd_arger_printf(colors, "\t{{red}}%s{{reset}}: %s\n", active_required_args[i].name,
                       active_required_args[i].info);
    }
    printf("\n");
  }

  if (global_optional_args_count == 0 && active_optional_args_count == 0) {
    exit(1);
  }

  uint32_t total_opts = global_optional_args_count + active_optional_args_count;
  CmdArgerDesc **all_opts = (CmdArgerDesc **)malloc(sizeof(CmdArgerDesc *) * total_opts);
  uint32_t idx = 0;

  for (uint32_t i = 0; i < global_optional_args_count; i++)
    all_opts[idx++] = &global_optional_args[i];
  for (uint32_t i = 0; i < active_optional_args_count; i++)
    all_opts[idx++] = &active_optional_args[i];

  printf("OPTIONS:\n");
  cmd_arger_printf(colors, "\t{{yellow}}--help{{reset}}: this help screen\n");

  for (uint32_t i = 0; i < total_opts; i++) {
    if (!all_opts[i]->group) {
      print_option_desc(all_opts[i], colors);
    }
  }
  printf("\n");

  const char *processed_groups[64];
  int processed_groups_count = 0;

  for (uint32_t i = 0; i < total_opts; i++) {
    const char *g = all_opts[i]->group;
    if (!g)
      continue;

    int already = 0;
    for (int j = 0; j < processed_groups_count; j++) {
      if (strcmp(processed_groups[j], g) == 0) {
        already = 1;
        break;
      }
    }
    if (already)
      continue;

    if (processed_groups_count < 64) {
      processed_groups[processed_groups_count++] = g;
      printf("%s:\n", g);
      for (uint32_t k = 0; k < total_opts; k++) {
        if (all_opts[k]->group && strcmp(all_opts[k]->group, g) == 0) {
          print_option_desc(all_opts[k], colors);
        }
      }
      printf("\n");
    }
  }

  free(all_opts);
  exit(1);
}
