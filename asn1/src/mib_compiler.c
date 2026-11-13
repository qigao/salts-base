#include "mib_internal.h"
#include <asn1_value.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MIB_PATH_BYTES = 4096 };

static char *copy_string(const char *text) {
    const size_t size = strlen(text);
    char *copy;
    if (size == SIZE_MAX) return NULL;
    copy = (char *)malloc(size + 1u);
    if (copy) memcpy(copy, text, size + 1u);
    return copy;
}

static int identifier(const char *name) {
    size_t length = 0u;
    if (!name || !((*name >= 'A' && *name <= 'Z') || (*name >= 'a' && *name <= 'z')))
        return 0;
    for (; name[length]; ++length) {
        const unsigned char c = (unsigned char)name[length];
        if (length >= MIB_MAX_IDENTIFIER_BYTES ||
            !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
        if (c == '-' && (name[length + 1u] == '-' || !name[length + 1u])) return 0;
    }
    return 1;
}

static void c_identifier(char *out, const char *name) {
    do { *out++ = *name == '-' ? '_' : *name; } while (*name++);
}

static int oid_arc(const char *text, uint32_t *out) {
    uint32_t value = 0u;
    if (!text || !*text) return -1;
    do {
        uint32_t digit;
        if (*text < '0' || *text > '9') return -1;
        digit = (uint32_t)(*text++ - '0');
        if (value > (UINT32_MAX - digit) / 10u) return -1;
        value = value * 10u + digit;
    } while (*text);
    *out = value;
    return 0;
}

void mib_oid_table_destroy(mib_oid_table_t *table) {
    if (!table) return;
    for (size_t i = 0u; i < table->count; ++i) {
        free(table->entries[i].components);
        free(table->entries[i].name);
        free(table->entries[i].description);
    }
    free(table->entries);
    free(table);
}

mib_oid_table_t *mib_extract_oids(AstNode *root) {
    mib_oid_table_t *table;
    size_t definitions = 0u, capacity = 0u;
    if (!root || root->type != NODE_MODULE || !identifier(root->value)) return NULL;
    // The parser owns the tree. This pass never mutates it; the returned table
    // owns independent copies. Definition and arc scans have explicit bounds.
    for (AstNode *item = root->left; item; item = item->right) {
        if (++definitions > MIB_MAX_AST_NODES || item->type != NODE_ASSIGNMENT ||
            !identifier(item->value) || !item->left) return NULL;
        if (item->left->type == NODE_OBJECT_IDENTIFIER) ++capacity;
    }
    table = (mib_oid_table_t *)calloc(1u, sizeof(*table));
    if (!table) return NULL;
    if (capacity) {
        table->entries = (mib_oid_entry_t *)calloc(capacity, sizeof(*table->entries));
        if (!table->entries) { free(table); return NULL; }
    }
    table->capacity = capacity;
    for (AstNode *item = root->left; item; item = item->right) {
        uint32_t components[MIB_MAX_OID_ARCS];
        size_t count = 0u, arc_nodes = 0u;
        mib_oid_entry_t *entry;
        for (AstNode *prior = root->left; prior != item; prior = prior->right)
            if (strcmp(prior->value, item->value) == 0) goto fail;
        if (item->left->type != NODE_OBJECT_IDENTIFIER) continue;
        for (AstNode *arc = item->left->left; arc; arc = arc->right) {
            if (++arc_nodes > MIB_MAX_OID_ARCS) goto fail;
            if (arc->type == NODE_Identifier && count == 0u) {
                const mib_oid_entry_t *prefix = NULL;
                if (!identifier(arc->value)) goto fail;
                for (size_t i = 0u; i < table->count; ++i)
                    if (strcmp(table->entries[i].name, arc->value) == 0)
                        prefix = &table->entries[i];
                if (!prefix) goto fail;
                count = prefix->count;
                memcpy(components, prefix->components, count * sizeof(*components));
            } else {
                if (arc->type != NODE_OID_ARC || count == MIB_MAX_OID_ARCS ||
                    oid_arc(arc->value, &components[count]) != 0) goto fail;
                ++count;
            }
        }
        // Share binary ASN.1's root-arc and integer-encoding validation.
        {
            asn1_value *oid = NULL;
            if (asn1_value_create_oid(components, count, &oid) != SALTS_ASN1_OK) goto fail;
            asn1_value_free(oid);
        }
        entry = &table->entries[table->count++];
        entry->name = copy_string(item->value);
        entry->components = (uint32_t *)malloc(count * sizeof(*components));
        if (!entry->name || !entry->components) goto fail;
        memcpy(entry->components, components, count * sizeof(*components));
        entry->count = count;
    }
    return table;
fail:
    mib_oid_table_destroy(table);
    return NULL;
}

mib_compile_context_t *mib_compile_create_context(void) {
    mib_compile_context_t *ctx = (mib_compile_context_t *)calloc(1u, sizeof(*ctx));
    if (ctx) {
        ctx->generate_c_code = 1;
        ctx->generate_runtime_tables = 1;
    }
    return ctx;
}

void mib_compile_destroy_context(mib_compile_context_t *ctx) {
    if (!ctx) return;
    free(ctx->module_name);
    free(ctx->output_dir);
    free_tree(ctx->root);
    free(ctx);
}

typedef enum { MIB_HEADER, MIB_SOURCE, MIB_RUNTIME } mib_output_kind;

static void write_arcs(FILE *file, const mib_oid_entry_t *entry) {
    for (size_t i = 0u; i < entry->count; ++i)
        fprintf(file, "%sUINT32_C(%" PRIu32 ")", i ? ", " : "", entry->components[i]);
}

static int generate(mib_compile_context_t *ctx, const char *filename, mib_output_kind kind) {
    mib_oid_table_t *table;
    char module[MIB_MAX_IDENTIFIER_BYTES + 1u];
    FILE *file;
    int failed;
    if (!ctx || !identifier(ctx->module_name) || !filename || !*filename ||
        !ctx->root || !ctx->root->value || strcmp(ctx->root->value, ctx->module_name)) return -1;
    table = mib_extract_oids(ctx->root);
    if (!table) return -1;
    file = fopen(filename, "wb");
    if (!file) { mib_oid_table_destroy(table); return -1; }
    c_identifier(module, ctx->module_name);
    fprintf(file, "/* Generated from ASN.1 module %s. */\n", ctx->module_name);
    if (kind == MIB_HEADER) {
        fprintf(file, "#ifndef MIB_%s_H\n#define MIB_%s_H\n\n#include <mib_compiler.h>\n"
                      "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", module, module);
    } else if (kind == MIB_SOURCE) {
        fprintf(file, "#include \"%s.h\"\n\n", ctx->module_name);
    } else {
        fprintf(file, "#include <mib_compiler.h>\n\n");
    }
    for (size_t i = 0u; i < table->count; ++i) {
        const mib_oid_entry_t *entry = &table->entries[i];
        char name[MIB_MAX_IDENTIFIER_BYTES + 1u];
        c_identifier(name, entry->name);
        if (kind == MIB_HEADER) {
            fprintf(file, "extern const uint32_t %s_OID[%zu];\n#define %s_OID_LEN %zu\n",
                    name, entry->count, name, entry->count);
        } else {
            if (kind == MIB_SOURCE) fprintf(file, "const uint32_t %s_OID[] = {", name);
            else fprintf(file, "static const uint32_t mib_runtime_%zu[] = {", i);
            write_arcs(file, entry);
            fprintf(file, "};\n");
        }
    }
    if (kind == MIB_HEADER) {
        if (ctx->generate_runtime_tables)
            fprintf(file, "\nextern const mib_module_t %s_module;\n", module);
        fprintf(file, "\n#ifdef __cplusplus\n}\n#endif\n#endif\n");
    } else if (kind == MIB_RUNTIME) {
        if (table->count) {
            fprintf(file, "\nstatic const mib_oid_view_t mib_runtime_entries[] = {\n");
            for (size_t i = 0u; i < table->count; ++i)
                fprintf(file, "    {\"%s\", mib_runtime_%zu, %zu},\n",
                        table->entries[i].name, i, table->entries[i].count);
            fprintf(file, "};\n");
        }
        fprintf(file, "\nconst mib_module_t %s_module = {\"%s\", %s, %zu};\n",
                module, ctx->module_name, table->count ? "mib_runtime_entries" : "NULL", table->count);
    }
    failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    mib_oid_table_destroy(table);
    return failed ? -1 : 0;
}

int mib_generate_c_header(mib_compile_context_t *ctx, const char *filename) {
    return generate(ctx, filename, MIB_HEADER);
}
int mib_generate_c_source(mib_compile_context_t *ctx, const char *filename) {
    return generate(ctx, filename, MIB_SOURCE);
}
int mib_generate_runtime_table(mib_compile_context_t *ctx, const char *filename) {
    return generate(ctx, filename, MIB_RUNTIME);
}

static int generate_output(mib_compile_context_t *ctx, const char *suffix,
                            int (*writer)(mib_compile_context_t *, const char *)) {
    char path[MIB_PATH_BYTES];
    const int size = snprintf(path, sizeof(path), "%s/%s%s", ctx->output_dir, ctx->module_name, suffix);
    if (size < 0 || (size_t)size >= sizeof(path)) return -1;
    return writer(ctx, path);
}

static int compile_string(const char *input, const char *output_dir, int c, int runtime) {
    mib_parse_state state;
    mib_compile_context_t *ctx;
    mib_oid_table_t *table;
    int result = -1;
    if (!output_dir || !*output_dir || strlen(output_dir) >= MIB_PATH_BYTES ||
        mib_parse_schema(input, &state) != 0) return -1;
    ctx = mib_compile_create_context();
    if (!ctx) { free_tree(state.root); return -1; }
    ctx->root = state.root;
    ctx->module_name = copy_string(ctx->root->value);
    ctx->output_dir = copy_string(output_dir);
    ctx->generate_c_code = c;
    ctx->generate_runtime_tables = runtime;
    if (!ctx->module_name || !ctx->output_dir) goto done;
    // Validate the complete input before opening any output file, even if all
    // output kinds were disabled by the command line.
    table = mib_extract_oids(ctx->root);
    if (!table) goto done;
    mib_oid_table_destroy(table);
    if (c && (generate_output(ctx, ".h", mib_generate_c_header) != 0 ||
              generate_output(ctx, ".c", mib_generate_c_source) != 0)) goto done;
    if (runtime && generate_output(ctx, "_runtime.c", mib_generate_runtime_table) != 0) goto done;
    result = 0;
done:
    mib_compile_destroy_context(ctx);
    return result;
}

int mib_compile_string(const char *input, const char *output_dir) {
    return compile_string(input, output_dir, 1, 1);
}

int mib_compile_file_options(const char *input, const char *output_dir, int c, int runtime) {
    FILE *file;
    char *content;
    size_t size;
    int result, failed;
    if (!input || !output_dir) return -1;
    file = fopen(input, "rb");
    if (!file) return -1;
    // The extra byte detects an oversized file without unbounded allocation,
    // relying on seeking, or confusing a short read with successful input.
    content = (char *)malloc((size_t)MIB_MAX_INPUT_BYTES + 1u);
    if (!content) { fclose(file); return -1; }
    size = fread(content, 1u, (size_t)MIB_MAX_INPUT_BYTES + 1u, file);
    failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed || size > MIB_MAX_INPUT_BYTES || memchr(content, '\0', size)) {
        free(content);
        return -1;
    }
    content[size] = '\0';
    result = compile_string(content, output_dir, c, runtime);
    free(content);
    return result;
}

int mib_compile_file(const char *input, const char *output_dir) {
    return mib_compile_file_options(input, output_dir, 1, 1);
}
