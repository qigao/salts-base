#include "query_vm.h"
#include "tinytest.h"

#include <string.h>

enum { TEST_INVALID, TEST_BOOL, TEST_NUMBER, TEST_STRING };

typedef struct {
  qvm_value_t operands[4];
  int resolve_count;
  int leaf_count;
  char concat[64];
} test_backend_t;

static int test_resolve(void *ctx, uint32_t operand, qvm_value_t *out) {
  test_backend_t *backend = ctx;
  backend->resolve_count++;
  if (operand >= 4) return 0;
  *out = backend->operands[operand];
  return 1;
}

static int test_truthy(void *ctx, const qvm_value_t *value) {
  (void)ctx;
  if (value->type == TEST_BOOL) return value->boolean != 0;
  if (value->type == TEST_NUMBER) return value->number != 0.0;
  if (value->type == TEST_STRING) return value->length != 0;
  return 0;
}

static int test_binary(void *ctx, qvm_opcode_t op, uint32_t arg,
                       const qvm_value_t *left, const qvm_value_t *right,
                       qvm_value_t *out) {
  test_backend_t *backend = ctx;
  memset(out, 0, sizeof(*out));
  if (op == QVM_OP_CMP && left->type == TEST_NUMBER && right->type == TEST_NUMBER) {
    out->type = TEST_BOOL;
    if (arg == 0) out->boolean = left->number == right->number;
    else if (arg == 1) out->boolean = left->number < right->number;
    else return 0;
    return 1;
  }
  if (op == QVM_OP_ADD && left->type == TEST_NUMBER && right->type == TEST_NUMBER) {
    out->type = TEST_NUMBER;
    out->number = left->number + right->number;
    return 1;
  }
  if ((op == QVM_OP_BAND || op == QVM_OP_BOR || op == QVM_OP_BXOR ||
       op == QVM_OP_LSHIFT || op == QVM_OP_RSHIFT) &&
      left->type == TEST_NUMBER && right->type == TEST_NUMBER) {
    const int64_t l = (int64_t)left->number;
    const int64_t r = (int64_t)right->number;
    int64_t result;
    switch (op) {
    case QVM_OP_BAND: result = l & r; break;
    case QVM_OP_BOR: result = l | r; break;
    case QVM_OP_BXOR: result = l ^ r; break;
    case QVM_OP_LSHIFT: result = l << (r & 63); break;
    default: result = (int64_t)((uint64_t)l >> (r & 63)); break;
    }
    out->type = TEST_NUMBER;
    out->number = (double)result;
    return 1;
  }
  if (op == QVM_OP_CAT && left->type == TEST_STRING && right->type == TEST_STRING) {
    const size_t llen = left->length;
    const size_t rlen = right->length;
    if (llen + rlen >= sizeof(backend->concat)) return 0;
    memcpy(backend->concat, left->str, llen);
    memcpy(backend->concat + llen, right->str, rlen);
    backend->concat[llen + rlen] = '\0';
    out->type = TEST_STRING;
    out->str = backend->concat;
    out->length = llen + rlen;
    return 1;
  }
  return 0;
}

static int test_unary(void *ctx, qvm_opcode_t op,
                      const qvm_value_t *input, qvm_value_t *out) {
  (void)ctx;
  if (input->type != TEST_NUMBER) return 0;
  memset(out, 0, sizeof(*out));
  out->type = TEST_NUMBER;
  if (op == QVM_OP_NEG) out->number = -input->number;
  else if (op == QVM_OP_BNOT) out->number = (double)~(int64_t)input->number;
  else return 0;
  return 1;
}

static int test_exists(void *ctx, uint32_t operand, int *out) {
  test_backend_t *backend = ctx;
  if (operand >= 4) return 0;
  *out = backend->operands[operand].type != TEST_INVALID;
  return 1;
}

static int test_length(void *ctx, uint32_t operand, qvm_value_t *out) {
  test_backend_t *backend = ctx;
  if (operand >= 4) return 0;
  memset(out, 0, sizeof(*out));
  out->type = TEST_NUMBER;
  out->number = (double)backend->operands[operand].length;
  return 1;
}

static int test_count(void *ctx, uint32_t operand, qvm_value_t *out) {
  return test_length(ctx, operand, out);
}

static int test_leaf(void *ctx, qvm_opcode_t op, uint32_t arg,
                     uint32_t src1, uint32_t src2, qvm_value_t *out) {
  test_backend_t *backend = ctx;
  backend->leaf_count++;
  if (op != QVM_OP_CMP_LEAF_NUMBER || src1 >= 4 || src2 >= 4) return 0;
  return test_binary(ctx, QVM_OP_CMP, arg, &backend->operands[src1],
                     &backend->operands[src2], out);
}

static void test_make_invalid(void *ctx, qvm_value_t *out) {
  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->type = TEST_INVALID;
}

static void test_make_bool(void *ctx, int value, qvm_value_t *out) {
  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->type = TEST_BOOL;
  out->boolean = value != 0;
}

static void test_make_number(void *ctx, double value, qvm_value_t *out) {
  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->type = TEST_NUMBER;
  out->number = value;
}

static void test_make_string(void *ctx, const char *value, size_t len,
                             qvm_value_t *out) {
  (void)ctx;
  memset(out, 0, sizeof(*out));
  out->type = TEST_STRING;
  out->str = value;
  out->length = len;
}

static qvm_exec_ops_t test_ops(void) {
  qvm_exec_ops_t ops = {
      .resolve = test_resolve,
      .truthy = test_truthy,
      .binary = test_binary,
      .unary = test_unary,
      .exists = test_exists,
      .length = test_length,
      .count = test_count,
      .leaf = test_leaf,
      .make_invalid = test_make_invalid,
      .make_bool = test_make_bool,
      .make_number = test_make_number,
      .make_string = test_make_string,
  };
  return ops;
}

suite("query vm") {
  group("verification") {
    it("accepts bounded forward control flow") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_JMP_FALSE, 0, 0, 3, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 0, 0, 1, 0},
      };
      qvm_verify_error_t error;
      check_equal(qvm_verify_slice(program, 3, 0, 3, 2, 2, 0, &error),
                   QVM_STATUS_OK);
    }

    it("rejects backward jumps and invalid registers") {
      qvm_instruction_t backward[] = {
          {QVM_OP_TRUE, 0, 0, 0, 0, 0},
          {QVM_OP_JMP, 0, 0, 0, 0, 0},
      };
      qvm_instruction_t bad_register[] = {
          {QVM_OP_LOAD_CONST, 0, QVM_MAX_REGISTERS, 0, 0, 0},
      };
      check_equal(qvm_verify_slice(backward, 2, 0, 2, 1, 1, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(qvm_verify_slice(bad_register, 1, 0, 1, QVM_MAX_REGISTERS,
                                    1, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
    }

    it("rejects uninitialized registers across control-flow joins") {
      qvm_instruction_t direct[] = {
          {QVM_OP_NOT, 0, 0, 0, 1, 0},
      };
      qvm_instruction_t branch[] = {
          {QVM_OP_TRUE, 0, 1, 0, 0, 0},
          {QVM_OP_JMP_TRUE, 0, 0, 3, 1, 0},
          {QVM_OP_TRUE, 0, 0, 0, 0, 0},
          {QVM_OP_NOT, 0, 0, 0, 0, 0},
      };
      check_equal(qvm_verify_slice(direct, 1, 0, 1, 2, 0, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(qvm_verify_slice(branch, 4, 0, 4, 2, 0, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
    }

    it("rejects programs that exceed configured resource limits") {
      qvm_instruction_t program[] = {
          {QVM_OP_TRUE, 0, 0, 0, 0, 0},
      };
      qvm_limits_t limits = qvm_default_limits();
      qvm_diagnostic_t diagnostic;
      limits.max_instructions = 0;
      check_equal(qvm_verify_slice_ex(program, 1, 0, 1, 1, 0, 0, &limits,
                                       &diagnostic),
                   QVM_STATUS_RESOURCE_LIMIT);
      check_equal(diagnostic.status, QVM_STATUS_RESOURCE_LIMIT);
      check_equal(diagnostic.instruction, 0);
      check_equal(diagnostic.opcode, QVM_NO_OPCODE);
      check_equal(diagnostic.operand, 1);
    }

    it("reports the failing instruction and operand") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 2, 0, 0, 0},
      };
      qvm_diagnostic_t diagnostic;
      check_equal(qvm_verify_slice(program, 1, 0, 1, 1, 1, 0,
                                    &diagnostic),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(diagnostic.instruction, 0);
      check_equal(diagnostic.opcode, QVM_OP_LOAD_CONST);
      check_equal(diagnostic.operand, 2);
      check_equal(diagnostic.message,
                   "destination register is out of range");
    }

    it("rejects out-of-range sources for the new opcodes") {
      qvm_instruction_t select_bad[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_SELECT, 0, 0, 1, 0, QVM_MAX_REGISTERS},
      };
      qvm_instruction_t bnot_bad[] = {
          {QVM_OP_BNOT, 0, 0, 0, QVM_MAX_REGISTERS, 0},
      };
      qvm_instruction_t band_bad[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_BAND, 0, 0, 0, 0, QVM_MAX_REGISTERS},
      };
      qvm_verify_error_t error;
      check_equal(qvm_verify_slice(select_bad, 3, 0, 3, 2, 1, 0, &error),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(error.operand, QVM_MAX_REGISTERS);
      check_equal(qvm_verify_slice(bnot_bad, 1, 0, 1, 1, 0, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(qvm_verify_slice(band_bad, 3, 0, 3, 2, 1, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
    }

    it("rejects select with uninitialized sources") {
      qvm_instruction_t program[] = {
          {QVM_OP_TRUE, 0, 1, 0, 0, 0},
          {QVM_OP_TRUE, 0, 2, 0, 0, 0},
          {QVM_OP_SELECT, 0, 0, 3, 1, 2},
      };
      check_equal(qvm_verify_slice(program, 3, 0, 3, 4, 0, 0, NULL),
                   QVM_STATUS_INVALID_PROGRAM);
    }
  }

  group("execution") {
    it("short-circuits without resolving the skipped operand") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_JMP_FALSE, 0, 0, 3, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 0, 0, 1, 0},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_BOOL;
      backend.operands[0].boolean = 0;
      backend.operands[1].type = TEST_BOOL;
      backend.operands[1].boolean = 1;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal(out.type, TEST_BOOL);
      check_false(out.boolean);
      check_equal(backend.resolve_count, 1);
    }

    it("uses the single-instruction numeric leaf path") {
      qvm_instruction_t program[] = {
          {QVM_OP_CMP_LEAF_NUMBER, 0, 0, 1, 0, 1},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_NUMBER;
      backend.operands[0].number = 1.0;
      backend.operands[1].type = TEST_NUMBER;
      backend.operands[1].number = 2.0;
      check_equal(qvm_execute(program, 1, 0, 1, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_true(out.boolean);
      check_equal(backend.leaf_count, 1);
      check_equal(backend.resolve_count, 0);
    }

    it("stops when the execution step budget is exhausted") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 0, 0, 1, 0},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_limits_t limits = qvm_default_limits();
      qvm_diagnostic_t diagnostic;
      qvm_value_t out;
      limits.max_steps = 1;
      backend.operands[0].type = TEST_BOOL;
      backend.operands[1].type = TEST_BOOL;
      check_equal(qvm_execute_ex(program, 2, 0, 2, &ops, &backend, NULL,
                                  &out, &limits, &diagnostic),
                   QVM_STATUS_RESOURCE_LIMIT);
      check_equal(diagnostic.instruction, 1);
      check_equal(diagnostic.opcode, QVM_OP_LOAD_CONST);
      check_equal(diagnostic.operand, 1);
      check_equal(backend.resolve_count, 1);
    }

    it("reports backend failures with bytecode context") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 0, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 1, 0, 1, 0},
          {QVM_OP_MOD, 0, 0, 0, 0, 1},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_diagnostic_t diagnostic;
      qvm_value_t out;
      backend.operands[0].type = TEST_NUMBER;
      backend.operands[1].type = TEST_NUMBER;
      check_equal(qvm_execute_ex(program, 3, 0, 3, &ops, &backend, NULL,
                                  &out, NULL, &diagnostic),
                   QVM_STATUS_BACKEND_ERROR);
      check_equal(diagnostic.instruction, 2);
      check_equal(diagnostic.opcode, QVM_OP_MOD);
      check_equal(diagnostic.message, "QVM binary backend failed");
    }

    it("rejects malformed bytecode before register access") {
      qvm_instruction_t program[] = {
          {QVM_OP_NOT, 0, 0, 0, QVM_MAX_REGISTERS, 0},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_diagnostic_t diagnostic;
      qvm_value_t out;
      check_equal(qvm_execute_ex(program, 1, 0, 1, &ops, &backend, NULL,
                                  &out, NULL, &diagnostic),
                   QVM_STATUS_INVALID_PROGRAM);
      check_equal(diagnostic.instruction, 0);
      check_equal(diagnostic.opcode, QVM_OP_NOT);
      check_equal(diagnostic.operand, QVM_MAX_REGISTERS);
    }

    it("selects between two registers by truthiness") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 2, 0, 1, 0},
          {QVM_OP_LOAD_CONST, 0, 3, 0, 2, 0},
          {QVM_OP_SELECT, 0, 0, 3, 1, 2},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_BOOL;
      backend.operands[0].boolean = 1;
      backend.operands[1].type = TEST_NUMBER;
      backend.operands[1].number = 11.0;
      backend.operands[2].type = TEST_NUMBER;
      backend.operands[2].number = 22.0;
      check_equal(qvm_execute(program, 4, 0, 4, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal(out.type, TEST_NUMBER);
      check_equal((int)out.number, 11);

      backend.operands[0].boolean = 0;
      check_equal(qvm_execute(program, 4, 0, 4, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 22);
    }

    it("executes bitwise and shift operations through the binary backend") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 2, 0, 1, 0},
          {QVM_OP_BAND, 0, 0, 0, 1, 2},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_NUMBER;
      backend.operands[0].number = 6.0;
      backend.operands[1].type = TEST_NUMBER;
      backend.operands[1].number = 3.0;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 2);

      program[2].op = QVM_OP_BOR;
      backend.operands[0].number = 4.0;
      backend.operands[1].number = 1.0;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 5);

      program[2].op = QVM_OP_BXOR;
      backend.operands[0].number = 6.0;
      backend.operands[1].number = 3.0;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 5);

      program[2].op = QVM_OP_LSHIFT;
      backend.operands[0].number = 1.0;
      backend.operands[1].number = 4.0;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 16);

      program[2].op = QVM_OP_RSHIFT;
      backend.operands[0].number = 16.0;
      backend.operands[1].number = 2.0;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, 4);
    }

    it("executes bitwise not through the unary backend") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_BNOT, 0, 0, 0, 1, 0},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_NUMBER;
      backend.operands[0].number = 0.0;
      check_equal(qvm_execute(program, 2, 0, 2, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal((int)out.number, -1);
    }

    it("concatenates strings through the binary backend") {
      qvm_instruction_t program[] = {
          {QVM_OP_LOAD_CONST, 0, 1, 0, 0, 0},
          {QVM_OP_LOAD_CONST, 0, 2, 0, 1, 0},
          {QVM_OP_CAT, 0, 0, 0, 1, 2},
      };
      test_backend_t backend = {0};
      qvm_exec_ops_t ops = test_ops();
      qvm_value_t out;
      backend.operands[0].type = TEST_STRING;
      backend.operands[0].str = "foo";
      backend.operands[0].length = 3;
      backend.operands[1].type = TEST_STRING;
      backend.operands[1].str = "bar";
      backend.operands[1].length = 3;
      check_equal(qvm_execute(program, 3, 0, 3, &ops, &backend, NULL, &out),
                   QVM_STATUS_OK);
      check_equal(out.type, TEST_STRING);
      check_equal(out.length, 6);
      check_equal(out.str, "foobar");
    }
  }

  group("diagnostics") {
    it("disassembles a bytecode slice into a caller-owned buffer") {
      qvm_instruction_t program[] = {
          {QVM_OP_TRUE, 0, 0, 0, 0, 0},
          {QVM_OP_NOT, 0, 0, 0, 0, 0},
      };
      char buffer[256];
      size_t required = 0;
      check_equal(qvm_disassemble_slice(program, 2, 0, 2, NULL, 0,
                                         &required, NULL),
                   QVM_STATUS_BUFFER_TOO_SMALL);
      check_greater(required, 0);
      check_equal(qvm_disassemble_slice(program, 2, 0, 2, buffer,
                                         sizeof(buffer), &required, NULL),
                   QVM_STATUS_OK);
      check_contains(buffer, "0000 TRUE");
      check_contains(buffer, "0001 NOT");
      check_equal(qvm_opcode_name(QVM_OP_NOT), "NOT");
      check_equal(qvm_opcode_name(QVM_OP_BAND), "BAND");
      check_equal(qvm_opcode_name(QVM_OP_SELECT), "SELECT");
      check_equal(qvm_opcode_name((qvm_opcode_t)QVM_OP_COUNT_VALUE),
                   "UNKNOWN");
    }
  }
}
