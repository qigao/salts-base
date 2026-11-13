#ifndef SALTS_MIB_INTERNAL_H
#define SALTS_MIB_INTERNAL_H

#include <mib_compiler.h>

typedef struct {
    AstNode *root;
    size_t nodes;
    int error;
} mib_parse_state;

// Semantic reductions consume left/right even if allocation fails.
AstNode *mib_node(mib_parse_state *state, ast_node_type_t type,
                  const char *value, AstNode *left, AstNode *right);
int mib_parse_schema(const char *input, mib_parse_state *state);
int mib_compile_file_options(const char *input, const char *output_dir,
                             int generate_c, int generate_runtime);

#endif
