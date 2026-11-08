#ifndef CYAML_YPATH_INTERNAL_H
#define CYAML_YPATH_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include "query_vm.h"

#define YPATH_MAX_STEPS 64
#define YPATH_MAX_DEPTH 16
#define YPATH_POOL_EXPR_CAP 128
#define YPATH_POOL_STEP_CAP 256
#define YPATH_VM_INSN_CAP 256

#define YPATH_SLICE_HAS_START 0x01
#define YPATH_SLICE_HAS_END 0x02
#define YPATH_SLICE_HAS_STEP 0x04

typedef enum {
    YPATH_STEP_IDENTITY,
    YPATH_STEP_PARENT,
    YPATH_STEP_WILDCARD,
    YPATH_STEP_RECURSIVE,
    YPATH_STEP_NAME,
    YPATH_STEP_ALIAS,
    YPATH_STEP_INDEX,
    YPATH_STEP_SLICE,
    YPATH_STEP_FILTER
} ypath_step_type_t;

typedef enum {
    YPATH_EXPR_INT,
    YPATH_EXPR_FLOAT,
    YPATH_EXPR_STRING,
    YPATH_EXPR_BOOL,
    YPATH_EXPR_NULL,
    YPATH_EXPR_PATH,
    YPATH_EXPR_UNARY,
    YPATH_EXPR_BINARY,
    YPATH_EXPR_COND
} ypath_expr_type_t;

typedef enum {
    YPATH_OP_OR,
    YPATH_OP_AND,
    YPATH_OP_EQ,
    YPATH_OP_NE,
    YPATH_OP_LT,
    YPATH_OP_LE,
    YPATH_OP_GT,
    YPATH_OP_GE,
    YPATH_OP_ADD,
    YPATH_OP_SUB,
    YPATH_OP_MUL,
    YPATH_OP_DIV,
    YPATH_OP_NEG,
    YPATH_OP_NOT,
    YPATH_OP_BAND,
    YPATH_OP_BOR,
    YPATH_OP_BXOR,
    YPATH_OP_LSHIFT,
    YPATH_OP_RSHIFT,
    YPATH_OP_BNOT,
    YPATH_OP_IDIV,
    YPATH_OP_MATCHES
} ypath_op_t;

typedef struct ypath_expr ypath_expr_t;
typedef struct ypath_step ypath_step_t;

struct ypath_step {
    ypath_step_type_t type;
    union {
        struct {
            const char* s;
            uint32_t len;
        } name;
        int64_t idx;
        struct {
            int64_t start;
            int64_t end;
            int64_t step;
            uint8_t flags;
        } slice;
        ypath_expr_t* filter;
        struct {
            ypath_expr_t* expr;
            uint32_t vm_offset;
            uint32_t vm_len;
            uint32_t vm_register_count;
        } filter_vm;
    } v;
};

struct ypath_expr {
    ypath_expr_type_t type;
    union {
        int64_t i;
        double f;
        struct {
            const char* s;
            uint32_t len;
        } str;
        bool b;
        struct {
            ypath_step_t* steps;
            uint32_t count;
        } path;
        struct {
            ypath_op_t op;
            ypath_expr_t* arg;
        } unary;
        struct {
            ypath_op_t op;
            ypath_expr_t* left;
            ypath_expr_t* right;
        } binary;
        struct {
            ypath_expr_t* cond;
            ypath_expr_t* then_expr;
            ypath_expr_t* else_expr;
        } cond;
    } v;
};

typedef struct {
    ypath_step_t steps[YPATH_MAX_STEPS];
    uint32_t count;
    bool absolute;
} ypath_path_t;

typedef struct {
    ypath_expr_t exprs[YPATH_POOL_EXPR_CAP];
    ypath_step_t steps[YPATH_POOL_STEP_CAP];
    uint32_t expr_count;
    uint32_t step_count;
} ypath_ast_storage_t;

//! Parsed YPATH tree. String slices borrow from source, which must outlive this object.
typedef struct {
    const char* source;
    ypath_path_t path;
    ypath_ast_storage_t storage;
    qvm_instruction_t vm[YPATH_VM_INSN_CAP];
    uint32_t vm_count;
    const char* error;
    uint32_t error_pos;
} ypath_ast_t;

//! Parse source into an owned tree of nodes with borrowed string slices.
bool cyaml_ypath_parse(const char* source, ypath_ast_t* ast);
bool cyaml_ypath_parse_ex(const char* source, ypath_ast_t* ast,
    const qvm_limits_t* limits, qvm_diagnostic_t* diagnostic);

#endif // CYAML_YPATH_INTERNAL_H
