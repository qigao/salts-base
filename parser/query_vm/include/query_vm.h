/**
 * @file query_vm.h
 * @brief Format-neutral register VM for JSONPath, YPath, and XPath expressions
 */

#ifndef SALTS_QUERY_VM_H
#define SALTS_QUERY_VM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QVM_MAX_REGISTERS 64U
#define QVM_NO_OPERAND UINT32_MAX
#define QVM_NO_INSTRUCTION UINT32_MAX
#define QVM_NO_OPCODE UINT8_MAX
#define QVM_DEFAULT_MAX_INSTRUCTIONS 1048576U
#define QVM_DEFAULT_MAX_OPERANDS 1048576U
#define QVM_DEFAULT_MAX_REGEXES 65536U
#define QVM_DEFAULT_MAX_STEPS 1048576U

typedef enum qvm_opcode_e {
  QVM_OP_LOAD_PATH,
  QVM_OP_LOAD_CONST,
  QVM_OP_LOAD_KEY,
  QVM_OP_LOAD_INDEX,
  QVM_OP_LOAD_INVALID,
  QVM_OP_EXISTS,
  QVM_OP_LENGTH,
  QVM_OP_COUNT,
  QVM_OP_CMP,
  QVM_OP_CMP_LEAF,
  QVM_OP_CMP_LEAF_NUMBER,
  QVM_OP_CMP_LEAF_STRING,
  QVM_OP_NOT_EXISTS,
  QVM_OP_MATCH,
  QVM_OP_CONTAINS,
  QVM_OP_CONTAINS_CI,
  QVM_OP_MATCH_FULL,
  QVM_OP_SEARCH,
  QVM_OP_CMP_LENGTH_LEAF,
  QVM_OP_CMP_COUNT_LEAF,
  QVM_OP_ADD,
  QVM_OP_SUB,
  QVM_OP_MUL,
  QVM_OP_DIV,
  QVM_OP_MOD,
  QVM_OP_UNION,
  QVM_OP_NEG,
  QVM_OP_NOT,
  QVM_OP_JMP_FALSE,
  QVM_OP_JMP_TRUE,
  QVM_OP_JMP,
  QVM_OP_TRUE,
  QVM_OP_FALSE,
  QVM_OP_BAND,   /* bitwise AND (binary) */
  QVM_OP_BOR,    /* bitwise OR  (binary) */
  QVM_OP_BXOR,   /* bitwise XOR (binary) */
  QVM_OP_LSHIFT, /* left shift  (binary) */
  QVM_OP_RSHIFT, /* right shift (binary) */
  QVM_OP_BNOT,   /* bitwise NOT (unary) */
  QVM_OP_CAT,    /* string concatenation (binary) */
  QVM_OP_SELECT, /* dst = truthy(src1) ? src2 : arg */
  QVM_OP_COUNT_VALUE
} qvm_opcode_t;

typedef struct qvm_instruction_s {
  uint8_t op;
  uint8_t reserved;
  uint16_t dst;
  uint32_t arg;
  uint32_t src1;
  uint32_t src2;
} qvm_instruction_t;

typedef struct qvm_value_s {
  int type;
  union {
    int num;
    int boolean;
    int64_t integer;
    uint64_t uinteger;
    double number;
    const char *str;
    void *opaque;
  };
  size_t length;
} qvm_value_t;

typedef enum qvm_status_e {
  QVM_STATUS_OK = 0,
  QVM_STATUS_INVALID_ARGUMENT = -1,
  QVM_STATUS_INVALID_PROGRAM = -2,
  QVM_STATUS_UNSUPPORTED = -3,
  QVM_STATUS_BACKEND_ERROR = -4,
  QVM_STATUS_NO_MEMORY = -5,
  QVM_STATUS_RESOURCE_LIMIT = -6,
  QVM_STATUS_BUFFER_TOO_SMALL = -7
} qvm_status_t;

typedef struct qvm_limits_s {
  uint32_t max_instructions;
  uint32_t max_operands;
  uint32_t max_regexes;
  uint32_t max_steps;
} qvm_limits_t;

typedef struct qvm_diagnostic_s {
  qvm_status_t status;
  uint32_t instruction;
  uint8_t opcode;
  uint8_t reserved[3];
  uint32_t operand;
  const char *message;
} qvm_diagnostic_t;

typedef qvm_diagnostic_t qvm_verify_error_t;

typedef struct qvm_exec_input_s {
  int array_index;
  const char *key;
  size_t key_len;
} qvm_exec_input_t;

typedef struct qvm_exec_ops_s {
  int (*resolve)(void *ctx, uint32_t operand, qvm_value_t *out);
  int (*truthy)(void *ctx, const qvm_value_t *value);
  int (*binary)(void *ctx, qvm_opcode_t op, uint32_t arg,
                const qvm_value_t *left, const qvm_value_t *right,
                qvm_value_t *out);
  int (*unary)(void *ctx, qvm_opcode_t op, const qvm_value_t *input,
               qvm_value_t *out);
  int (*exists)(void *ctx, uint32_t operand, int *out);
  int (*length)(void *ctx, uint32_t operand, qvm_value_t *out);
  int (*count)(void *ctx, uint32_t operand, qvm_value_t *out);
  int (*leaf)(void *ctx, qvm_opcode_t op, uint32_t arg,
              uint32_t src1, uint32_t src2, qvm_value_t *out);
  void (*make_invalid)(void *ctx, qvm_value_t *out);
  void (*make_bool)(void *ctx, int value, qvm_value_t *out);
  void (*make_number)(void *ctx, double value, qvm_value_t *out);
  void (*make_string)(void *ctx, const char *value, size_t len,
                      qvm_value_t *out);
} qvm_exec_ops_t;

qvm_limits_t qvm_default_limits(void);

const char *qvm_opcode_name(qvm_opcode_t opcode);

int qvm_disassemble_slice(const qvm_instruction_t *instructions,
                          uint32_t instruction_count, uint32_t offset,
                          uint32_t len, char *buffer, size_t capacity,
                          size_t *required, qvm_diagnostic_t *diagnostic);

int qvm_verify_slice_ex(const qvm_instruction_t *instructions,
                        uint32_t instruction_count, uint32_t offset,
                        uint32_t len, uint32_t register_count,
                        uint32_t operand_count, uint32_t regex_count,
                        const qvm_limits_t *limits,
                        qvm_diagnostic_t *diagnostic);

int qvm_verify_slice(const qvm_instruction_t *instructions,
                     uint32_t instruction_count, uint32_t offset, uint32_t len,
                     uint32_t register_count, uint32_t operand_count,
                     uint32_t regex_count, qvm_verify_error_t *error);

int qvm_execute_ex(const qvm_instruction_t *instructions,
                   uint32_t instruction_count, uint32_t offset, uint32_t len,
                   const qvm_exec_ops_t *ops, void *ctx,
                   const qvm_exec_input_t *input, qvm_value_t *out,
                   const qvm_limits_t *limits,
                   qvm_diagnostic_t *diagnostic);

int qvm_execute(const qvm_instruction_t *instructions,
                uint32_t instruction_count, uint32_t offset, uint32_t len,
                const qvm_exec_ops_t *ops, void *ctx,
                const qvm_exec_input_t *input, qvm_value_t *out);

#ifdef __cplusplus
}
#endif

#endif
