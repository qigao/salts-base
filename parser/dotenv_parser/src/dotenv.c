#include "dotenv.h"
#include "dotenv_environment_internal.h"
#include "dotenv_lexer.h"
#include "tstr.h"
#include <fmt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static tstr concat(tstr buffer, const char *string)
{
    if (!string) return buffer;
    tstr updated = tstr_cat(buffer, string);
    return updated ? updated : buffer;
}

static tstr resolve_nested(const char *value)
{
    if (!value) return NULL;

    // Simple check for ${}
    if (!strstr(value, "${")) return tstr_dup(value);

    tstr result = NULL;
    const char *ptr = value;
    const char *start;

    while ((start = strstr(ptr, "${")) != NULL) {
        // Concat everything before ${
        if (start > ptr) {
            size_t len = start - ptr;
            tstr updated = tstr_cat_len(result, ptr, len);
            if (updated) {
                result = updated;
            }
        }

        const char *end = strstr(start, "}");
        if (!end) break; // Unterminated ${

        size_t name_len = end - (start + 2);
        tstr name = tstr_dup_len(start + 2, name_len);
        if (!name) break;

        char *env_val = NULL;
        if (dotenv_environment_get_copy(name, &env_val) > 0) {
            result = concat(result, env_val);
        }
        free(env_val);
        tstr_free(name);
        ptr = end + 1;
    }

    if (*ptr) {
        result = concat(result, ptr);
    }

    return result ? result : tstr_new();
}

int dotenv_load(const char *path, bool overwrite)
{
    if (!path) return -1;

    char full_path[1024];
    FILE *file = fopen(path, "rb");

    if (!file) {
        // Try appending /.env if path is a directory (or just doesn't exist as is)
        fmt(full_path, sizeof(full_path), "{}/.env", path);
        file = fopen(full_path, "rb");
    }

    if (!file) return -1;

    // Read whole file into memory
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return -1;
    }

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return -1;
    }

    size_t read_len = fread(buffer, 1, size, file);
    buffer[read_len] = '\0';
    fclose(file);

    dotenv_lexer_t lexer;
    dotenv_lexer_init(&lexer, buffer, read_len);

    dotenv_token_t token;
    tstr current_key = NULL;

    while (dotenv_lexer_next(&lexer, &token) > 0) {
        if (token.type == DOTENV_TOKEN_KEY) {
            tstr_free(current_key);
            current_key = tstr_dup_len(token.value, token.length);
        } else if (token.type == DOTENV_TOKEN_VALUE) {
            if (current_key) {
                tstr raw_val = tstr_dup_len(token.value, token.length);
                if (!raw_val) {
                    tstr_free(current_key);
                    current_key = NULL;
                    continue;
                }

                tstr final_val = resolve_nested(raw_val);
                dotenv_environment_set(current_key, final_val, overwrite ? 1 : 0);

                tstr_free(raw_val);
                tstr_free(final_val);
                tstr_free(current_key);
                current_key = NULL;
            }
        } else if (token.type == DOTENV_TOKEN_EOF) {
            break;
        }
    }

    tstr_free(current_key);
    free(buffer);

    return 0;
}

int dotenv_load_default(bool overwrite)
{
    return dotenv_load(".env", overwrite);
}
