#ifndef MIB_COMPILER_H
#define MIB_COMPILER_H


#include "ast.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MIB Compiler API
enum {
    MIB_MAX_INPUT_BYTES = 1024 * 1024,
    MIB_MAX_AST_NODES = 4096,
    MIB_MAX_IDENTIFIER_BYTES = 127,
    MIB_MAX_OID_ARCS = 128
};

// The context owns module_name, root and output_dir. Destroy it when quiescent;
// independent contexts may be used concurrently, with distinct output files.
typedef struct {
    char *module_name;
    AstNode *root;
    char *output_dir;
    int generate_c_code;
    int generate_runtime_tables;
} mib_compile_context_t;

// Compile the supported ASN.1/OID subset into an existing output directory.
// Inputs are borrowed for this synchronous call. Returns 0 on success, -1 for
// invalid/unsupported input, resource limits, allocation or I/O failure.
// Output files can be partially written on I/O failure; no rollback is promised.
// Example: mib_compile_string("Demo DEFINITIONS ::= BEGIN "
//     "root OBJECT IDENTIFIER ::= { 1 3 6 1 } END", ".");
 int mib_compile_file(const char *mib_file, const char *output_dir);
 int mib_compile_string(const char *mib_content, const char *output_dir);
 mib_compile_context_t *mib_compile_create_context(void);
 void mib_compile_destroy_context(mib_compile_context_t *ctx);

// Code generation
// Borrow ctx and filename; return 0 on success or -1 on invalid context/I/O.
 int mib_generate_c_header(mib_compile_context_t *ctx, const char *filename);
 int mib_generate_c_source(mib_compile_context_t *ctx, const char *filename);
 int mib_generate_runtime_table(mib_compile_context_t *ctx, const char *filename);

// OID management
typedef struct {
    uint32_t *components;
    size_t count;
    char *name;
    char *description;
} mib_oid_entry_t;

typedef struct {
    mib_oid_entry_t *entries;
    size_t count;
    size_t capacity;
} mib_oid_table_t;

// Immutable generated runtime descriptors borrow static generated storage.
typedef struct {
    const char *name;
    const uint32_t *components;
    size_t count;
} mib_oid_view_t;

typedef struct {
    const char *name;
    const mib_oid_view_t *entries;
    size_t count;
} mib_module_t;

// Returns a new owned table (including all strings/arcs), or NULL on malformed
// AST, unresolved references, duplicate definitions, limits or allocation error.
// An empty module succeeds with a zero-entry table. Destroy with the function below.
 mib_oid_table_t *mib_extract_oids(AstNode *root);
 void mib_oid_table_destroy(mib_oid_table_t *table);

#ifdef __cplusplus
}
#endif

#endif // MIB_COMPILER_H
