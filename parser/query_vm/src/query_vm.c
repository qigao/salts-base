#include "query_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const qvm_opcode_names[QVM_OP_COUNT_VALUE] = {
    "LOAD_PATH",       "LOAD_CONST",      "LOAD_KEY",
    "LOAD_INDEX",      "LOAD_INVALID",    "EXISTS",
    "LENGTH",          "COUNT",           "CMP",
    "CMP_LEAF",        "CMP_LEAF_NUMBER", "CMP_LEAF_STRING",
    "NOT_EXISTS",      "MATCH",           "CONTAINS",
    "CONTAINS_CI",     "MATCH_FULL",      "SEARCH",
    "CMP_LENGTH_LEAF", "CMP_COUNT_LEAF",  "ADD",
    "SUB",             "MUL",             "DIV",
    "MOD",             "UNION",           "NEG",
    "NOT",             "JMP_FALSE",       "JMP_TRUE",
    "JMP",             "TRUE",            "FALSE",
    "BAND",            "BOR",             "BXOR",
    "LSHIFT",          "RSHIFT",          "BNOT",
    "CAT",             "SELECT",
};


static int qvm_fail(qvm_diagnostic_t *diagnostic, qvm_status_t status,
                    uint32_t instruction, uint8_t opcode, uint32_t operand,
                    const char *message) {
  if (diagnostic) {
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = status;
    diagnostic->instruction = instruction;
    diagnostic->opcode = opcode;
    diagnostic->operand = operand;
    diagnostic->message = message;
  }
  return status;
}

static void qvm_clear_diagnostic(qvm_diagnostic_t *diagnostic) {
  if (!diagnostic) return;
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->status = QVM_STATUS_OK;
  diagnostic->instruction = QVM_NO_INSTRUCTION;
  diagnostic->opcode = QVM_NO_OPCODE;
  diagnostic->operand = QVM_NO_OPERAND;
}

qvm_limits_t qvm_default_limits(void) {
  qvm_limits_t limits = {
      QVM_DEFAULT_MAX_INSTRUCTIONS,
      QVM_DEFAULT_MAX_OPERANDS,
      QVM_DEFAULT_MAX_REGEXES,
      QVM_DEFAULT_MAX_STEPS,
  };
  return limits;
}

static const qvm_limits_t *qvm_resolve_limits(const qvm_limits_t *limits,
                                              qvm_limits_t *defaults) {
  if (limits) return limits;
  *defaults = qvm_default_limits();
  return defaults;
}

const char *qvm_opcode_name(qvm_opcode_t opcode) {
  if ((unsigned)opcode >= QVM_OP_COUNT_VALUE) return "UNKNOWN";
  return qvm_opcode_names[opcode];
}

static int qvm_writes_register(uint8_t op) {
  return op != QVM_OP_JMP_FALSE && op != QVM_OP_JMP_TRUE && op != QVM_OP_JMP;
}

static int qvm_is_binary(uint8_t op) {
  return op == QVM_OP_CMP || op == QVM_OP_MATCH || op == QVM_OP_CONTAINS ||
         op == QVM_OP_CONTAINS_CI || op == QVM_OP_MATCH_FULL ||
         op == QVM_OP_SEARCH || op == QVM_OP_ADD || op == QVM_OP_SUB ||
         op == QVM_OP_MUL || op == QVM_OP_DIV || op == QVM_OP_MOD ||
         op == QVM_OP_UNION || op == QVM_OP_BAND || op == QVM_OP_BOR ||
         op == QVM_OP_BXOR || op == QVM_OP_LSHIFT || op == QVM_OP_RSHIFT ||
         op == QVM_OP_CAT;
}

static int qvm_is_leaf(uint8_t op) {
  return op == QVM_OP_CMP_LEAF || op == QVM_OP_CMP_LEAF_NUMBER ||
         op == QVM_OP_CMP_LEAF_STRING || op == QVM_OP_CMP_LENGTH_LEAF ||
         op == QVM_OP_CMP_COUNT_LEAF;
}

static int qvm_reads_uninitialized(const qvm_instruction_t *insn,
                                   uint64_t initialized) {
  if (qvm_is_binary(insn->op))
    return !(initialized & (UINT64_C(1) << insn->src1)) ||
           !(initialized & (UINT64_C(1) << insn->src2));
  if (insn->op == QVM_OP_NOT || insn->op == QVM_OP_NEG ||
      insn->op == QVM_OP_BNOT || insn->op == QVM_OP_JMP_FALSE ||
      insn->op == QVM_OP_JMP_TRUE)
    return !(initialized & (UINT64_C(1) << insn->src1));
  if (insn->op == QVM_OP_SELECT)
    return !(initialized & (UINT64_C(1) << insn->src1)) ||
           !(initialized & (UINT64_C(1) << insn->src2)) ||
           !(initialized & (UINT64_C(1) << insn->arg));
  return 0;
}

static void qvm_merge_state(uint32_t successor, uint64_t state,
                            uint64_t *states, uint8_t *reachable) {
  if (reachable[successor])
    states[successor] &= state;
  else {
    reachable[successor] = 1;
    states[successor] = state;
  }
}

int qvm_verify_slice_ex(const qvm_instruction_t *instructions,
                        uint32_t instruction_count, uint32_t offset,
                        uint32_t len, uint32_t register_count,
                        uint32_t operand_count, uint32_t regex_count,
                        const qvm_limits_t *limits,
                        qvm_diagnostic_t *diagnostic) {
  uint32_t end;
  uint64_t *states;
  uint8_t *reachable;
  qvm_limits_t default_limits;
  limits = qvm_resolve_limits(limits, &default_limits);
  qvm_clear_diagnostic(diagnostic);
  if (!instructions || len == 0 || register_count == 0 ||
      register_count > QVM_MAX_REGISTERS || offset > instruction_count ||
      len > instruction_count - offset)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_ARGUMENT, offset,
                    QVM_NO_OPCODE, QVM_NO_OPERAND,
                    "invalid QVM program bounds");
  if (len > limits->max_instructions)
    return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset,
                    QVM_NO_OPCODE, len,
                    "QVM instruction limit exceeded");
  if (operand_count > limits->max_operands)
    return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset,
                    QVM_NO_OPCODE, operand_count,
                    "QVM operand limit exceeded");
  if (regex_count > limits->max_regexes)
    return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset,
                    QVM_NO_OPCODE, regex_count,
                    "QVM regex limit exceeded");
  end = offset + len;
  for (uint32_t pc = offset; pc < end; ++pc) {
    const qvm_instruction_t *insn = &instructions[pc];
    if (insn->op >= QVM_OP_COUNT_VALUE)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      QVM_NO_OPERAND, "unknown QVM opcode");
    if (insn->reserved != 0)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->reserved,
                      "reserved instruction bits are nonzero");
    if (qvm_writes_register(insn->op) && insn->dst >= register_count)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->dst, "destination register is out of range");

    if (qvm_is_binary(insn->op) &&
        (insn->src1 >= register_count || insn->src2 >= register_count))
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1 >= register_count ? insn->src1 : insn->src2,
                      "source register is out of range");
    if ((insn->op == QVM_OP_NOT || insn->op == QVM_OP_NEG ||
         insn->op == QVM_OP_BNOT) &&
        insn->src1 >= register_count)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1,
                      "unary source register is out of range");
    if (insn->op == QVM_OP_SELECT &&
        (insn->src1 >= register_count || insn->src2 >= register_count ||
         insn->arg >= register_count))
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1 >= register_count ? insn->src1 : insn->src2,
                      "select source register is out of range");

    if ((insn->op == QVM_OP_LOAD_PATH || insn->op == QVM_OP_LOAD_CONST ||
         insn->op == QVM_OP_EXISTS || insn->op == QVM_OP_COUNT ||
         insn->op == QVM_OP_NOT_EXISTS) && insn->src1 >= operand_count)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1, "operand is out of range");
    if (insn->op == QVM_OP_LENGTH && insn->src1 != QVM_NO_OPERAND &&
        insn->src1 >= operand_count)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1, "length operand is out of range");
    if (qvm_is_leaf(insn->op)) {
      if (insn->src2 >= operand_count ||
          ((insn->op != QVM_OP_CMP_LENGTH_LEAF) && insn->src1 >= operand_count) ||
          (insn->op == QVM_OP_CMP_LENGTH_LEAF &&
           insn->src1 != QVM_NO_OPERAND && insn->src1 >= operand_count))
        return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                        insn->src2 >= operand_count ? insn->src2 : insn->src1,
                        "leaf operand is out of range");
    }

    if ((insn->op == QVM_OP_MATCH || insn->op == QVM_OP_MATCH_FULL ||
         insn->op == QVM_OP_SEARCH) && insn->arg != QVM_NO_OPERAND &&
        insn->arg >= regex_count)
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->arg, "regex operand is out of range");

    if (insn->op == QVM_OP_JMP_FALSE || insn->op == QVM_OP_JMP_TRUE) {
      if (insn->src1 >= register_count)
        return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                        insn->src1,
                        "jump source register is out of range");
      if (insn->arg <= pc || insn->arg > end)
        return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                        insn->arg,
                        "jump target is not a forward in-range target");
    } else if (insn->op == QVM_OP_JMP &&
               (insn->arg <= pc || insn->arg > end)) {
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->arg,
                      "jump target is not a forward in-range target");
    }
  }

  if ((size_t)len >= SIZE_MAX ||
      ((size_t)len + 1) > SIZE_MAX / sizeof(*states) ||
      ((size_t)len + 1) > SIZE_MAX / sizeof(*reachable))
    return qvm_fail(diagnostic, QVM_STATUS_NO_MEMORY, offset, QVM_NO_OPCODE,
                    len, "QVM verifier state size overflows");
  states = calloc((size_t)len + 1, sizeof(*states));
  reachable = calloc((size_t)len + 1, sizeof(*reachable));
  if (!states || !reachable) {
    free(states);
    free(reachable);
    return qvm_fail(diagnostic, QVM_STATUS_NO_MEMORY, offset, QVM_NO_OPCODE,
                    len, "unable to allocate QVM verifier state");
  }
  reachable[0] = 1;
  for (uint32_t pc = offset; pc < end; ++pc) {
    const qvm_instruction_t *insn = &instructions[pc];
    uint32_t relative = pc - offset;
    uint64_t state;
    if (!reachable[relative]) continue;
    state = states[relative];
    if (qvm_reads_uninitialized(insn, state)) {
      free(states);
      free(reachable);
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      insn->src1,
                      "source register is not initialized on every path");
    }
    if (qvm_writes_register(insn->op))
      state |= UINT64_C(1) << insn->dst;
    if (insn->op == QVM_OP_JMP) {
      qvm_merge_state(insn->arg - offset, state, states, reachable);
    } else if (insn->op == QVM_OP_JMP_FALSE ||
               insn->op == QVM_OP_JMP_TRUE) {
      qvm_merge_state(relative + 1, state, states, reachable);
      qvm_merge_state(insn->arg - offset, state, states, reachable);
    } else {
      qvm_merge_state(relative + 1, state, states, reachable);
    }
  }
  if (!reachable[len] || !(states[len] & UINT64_C(1))) {
    free(states);
    free(reachable);
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, end,
                    QVM_NO_OPCODE, 0,
                    "result register is not initialized on every exit");
  }
  free(states);
  free(reachable);
  return QVM_STATUS_OK;
}

int qvm_verify_slice(const qvm_instruction_t *instructions,
                     uint32_t instruction_count, uint32_t offset, uint32_t len,
                     uint32_t register_count, uint32_t operand_count,
                     uint32_t regex_count, qvm_verify_error_t *error) {
  return qvm_verify_slice_ex(instructions, instruction_count, offset, len,
                             register_count, operand_count, regex_count, NULL,
                             error);
}

int qvm_disassemble_slice(const qvm_instruction_t *instructions,
                          uint32_t instruction_count, uint32_t offset,
                          uint32_t len, char *buffer, size_t capacity,
                          size_t *required, qvm_diagnostic_t *diagnostic) {
  char line[128];
  size_t total = 0;
  qvm_limits_t limits = qvm_default_limits();
  qvm_clear_diagnostic(diagnostic);
  if (required) *required = 0;
  if (!instructions || len == 0 || offset > instruction_count ||
      len > instruction_count - offset || (!buffer && capacity != 0))
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_ARGUMENT, offset,
                    QVM_NO_OPCODE, QVM_NO_OPERAND,
                    "invalid QVM disassembly bounds");
  if (len > limits.max_instructions)
    return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset,
                    QVM_NO_OPCODE, len,
                    "QVM instruction limit exceeded");
  for (uint32_t pc = offset; pc < offset + len; ++pc) {
    const qvm_instruction_t *insn = &instructions[pc];
    int written = snprintf(line, sizeof(line),
                           "%04u %-20s dst=%u arg=%u src1=%u src2=%u\n", pc,
                           qvm_opcode_name((qvm_opcode_t)insn->op), insn->dst,
                           insn->arg, insn->src1, insn->src2);
    size_t line_len;
    if (written < 0 || (size_t)written >= sizeof(line))
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      QVM_NO_OPERAND,
                      "unable to format QVM instruction");
    line_len = (size_t)written;
    if (total > SIZE_MAX - line_len)
      return qvm_fail(diagnostic, QVM_STATUS_NO_MEMORY, pc, insn->op,
                      QVM_NO_OPERAND,
                      "QVM disassembly size overflows");
    if (buffer && total < capacity - (capacity != 0)) {
      size_t available = capacity - 1 - total;
      size_t copied = line_len < available ? line_len : available;
      memcpy(buffer + total, line, copied);
    }
    total += line_len;
  }
  if (required) *required = total;
  if (buffer && capacity != 0)
    buffer[total < capacity ? total : capacity - 1] = '\0';
  if (capacity == 0 || total >= capacity)
    return qvm_fail(diagnostic, QVM_STATUS_BUFFER_TOO_SMALL, offset,
                    QVM_NO_OPCODE,
                    total > UINT32_MAX ? UINT32_MAX : (uint32_t)total,
                    "QVM disassembly buffer is too small");
  return QVM_STATUS_OK;
}

static int qvm_require_common_ops(const qvm_exec_ops_t *ops) {
  return ops && ops->resolve && ops->truthy && ops->binary &&
         ops->make_invalid && ops->make_bool && ops->make_number &&
         ops->make_string;
}

static int qvm_validate_exec_instruction(const qvm_instruction_t *insn,
                                         uint32_t pc, uint32_t end,
                                         qvm_diagnostic_t *diagnostic) {
  if (insn->op >= QVM_OP_COUNT_VALUE)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    QVM_NO_OPERAND, "unknown QVM opcode");
  if (insn->reserved != 0)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->reserved, "reserved instruction bits are nonzero");
  if (qvm_writes_register(insn->op) && insn->dst >= QVM_MAX_REGISTERS)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->dst, "destination register is out of range");
  if (qvm_is_binary(insn->op) &&
      (insn->src1 >= QVM_MAX_REGISTERS || insn->src2 >= QVM_MAX_REGISTERS))
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->src1 >= QVM_MAX_REGISTERS ? insn->src1 : insn->src2,
                    "source register is out of range");
  if ((insn->op == QVM_OP_NOT || insn->op == QVM_OP_NEG ||
       insn->op == QVM_OP_BNOT || insn->op == QVM_OP_JMP_FALSE ||
       insn->op == QVM_OP_JMP_TRUE) &&
      insn->src1 >= QVM_MAX_REGISTERS)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->src1, "source register is out of range");
  if (insn->op == QVM_OP_SELECT &&
      (insn->src1 >= QVM_MAX_REGISTERS || insn->src2 >= QVM_MAX_REGISTERS ||
       insn->arg >= QVM_MAX_REGISTERS))
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->src1 >= QVM_MAX_REGISTERS ? insn->src1 : insn->src2,
                    "select source register is out of range");
  if ((insn->op == QVM_OP_JMP_FALSE || insn->op == QVM_OP_JMP_TRUE ||
       insn->op == QVM_OP_JMP) && (insn->arg <= pc || insn->arg > end))
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                    insn->arg, "jump target is not a forward in-range target");
  return QVM_STATUS_OK;
}

static int qvm_exec_leaf(const qvm_exec_ops_t *ops, void *ctx,
                         const qvm_instruction_t *insn, qvm_value_t *out) {
  if (!ops->leaf) return QVM_STATUS_UNSUPPORTED;
  return ops->leaf(ctx, (qvm_opcode_t)insn->op, insn->arg,
                   insn->src1, insn->src2, out)
             ? QVM_STATUS_OK
             : QVM_STATUS_BACKEND_ERROR;
}

static int qvm_store_exists(const qvm_exec_ops_t *ops, void *ctx,
                            uint32_t operand, int invert, qvm_value_t *out) {
  int exists;
  if (!ops->exists || !ops->exists(ctx, operand, &exists))
    return QVM_STATUS_BACKEND_ERROR;
  ops->make_bool(ctx, invert ? !exists : exists, out);
  return QVM_STATUS_OK;
}

int qvm_execute_ex(const qvm_instruction_t *instructions,
                   uint32_t instruction_count, uint32_t offset, uint32_t len,
                   const qvm_exec_ops_t *ops, void *ctx,
                   const qvm_exec_input_t *input, qvm_value_t *out,
                   const qvm_limits_t *limits,
                   qvm_diagnostic_t *diagnostic) {
  qvm_value_t registers[QVM_MAX_REGISTERS];
  uint32_t pc = offset;
  uint32_t end;
  uint32_t steps = 0;
  int status;
  qvm_limits_t default_limits;
  limits = qvm_resolve_limits(limits, &default_limits);
  qvm_clear_diagnostic(diagnostic);
  if (!instructions || !out || !qvm_require_common_ops(ops) || len == 0 ||
      offset > instruction_count || len > instruction_count - offset)
    return qvm_fail(diagnostic, QVM_STATUS_INVALID_ARGUMENT, offset,
                    QVM_NO_OPCODE, QVM_NO_OPERAND,
                    "invalid QVM execution arguments");
  if (len > limits->max_instructions)
    return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset,
                    QVM_NO_OPCODE, len,
                    "QVM instruction limit exceeded");
  end = offset + len;
  memset(registers, 0, sizeof(registers));

  if (len == 1) {
    const qvm_instruction_t *insn = &instructions[offset];
    status = qvm_validate_exec_instruction(insn, offset, end, diagnostic);
    if (status != QVM_STATUS_OK) return status;
    if (limits->max_steps == 0)
      return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, offset, insn->op,
                      0, "QVM execution step limit exceeded");
    if (qvm_is_leaf(insn->op)) {
      status = qvm_exec_leaf(ops, ctx, insn, &registers[0]);
      if (status != QVM_STATUS_OK)
        return qvm_fail(diagnostic, (qvm_status_t)status, offset, insn->op,
                        insn->src1, "QVM leaf backend failed");
      *out = registers[0];
      return QVM_STATUS_OK;
    }
    if (insn->op == QVM_OP_EXISTS || insn->op == QVM_OP_NOT_EXISTS) {
      status = qvm_store_exists(ops, ctx, insn->src1,
                                insn->op == QVM_OP_NOT_EXISTS, &registers[0]);
      if (status != QVM_STATUS_OK)
        return qvm_fail(diagnostic, (qvm_status_t)status, offset, insn->op,
                        insn->src1, "QVM exists backend failed");
      *out = registers[0];
      return QVM_STATUS_OK;
    }
    if (insn->op == QVM_OP_TRUE || insn->op == QVM_OP_FALSE) {
      ops->make_bool(ctx, insn->op == QVM_OP_TRUE, &registers[0]);
      *out = registers[0];
      return QVM_STATUS_OK;
    }
  }

  while (pc < end) {
    const qvm_instruction_t *insn = &instructions[pc];
    qvm_value_t *value;
    status = qvm_validate_exec_instruction(insn, pc, end, diagnostic);
    if (status != QVM_STATUS_OK) return status;
    if (steps >= limits->max_steps)
      return qvm_fail(diagnostic, QVM_STATUS_RESOURCE_LIMIT, pc, insn->op,
                      steps, "QVM execution step limit exceeded");
    ++steps;
    switch (insn->op) {
    case QVM_OP_LOAD_PATH:
    case QVM_OP_LOAD_CONST:
      value = &registers[insn->dst];
      memset(value, 0, sizeof(*value));
      if (!ops->resolve(ctx, insn->src1, value)) ops->make_invalid(ctx, value);
      break;
    case QVM_OP_LOAD_KEY:
      value = &registers[insn->dst];
      if (input && input->key)
        ops->make_string(ctx, input->key, input->key_len, value);
      else
        ops->make_invalid(ctx, value);
      break;
    case QVM_OP_LOAD_INDEX:
      value = &registers[insn->dst];
      if (input && input->array_index >= 0)
        ops->make_number(ctx, (double)input->array_index, value);
      else
        ops->make_invalid(ctx, value);
      break;
    case QVM_OP_LOAD_INVALID:
      ops->make_invalid(ctx, &registers[insn->dst]);
      break;
    case QVM_OP_EXISTS:
    case QVM_OP_NOT_EXISTS:
      status = qvm_store_exists(ops, ctx, insn->src1,
                                insn->op == QVM_OP_NOT_EXISTS,
                                &registers[insn->dst]);
      if (status != QVM_STATUS_OK)
        return qvm_fail(diagnostic, (qvm_status_t)status, pc, insn->op,
                        insn->src1, "QVM exists backend failed");
      break;
    case QVM_OP_LENGTH:
      if (!ops->length || !ops->length(ctx, insn->src1, &registers[insn->dst]))
        return qvm_fail(diagnostic, QVM_STATUS_BACKEND_ERROR, pc, insn->op,
                        insn->src1, "QVM length backend failed");
      break;
    case QVM_OP_COUNT:
      if (!ops->count || !ops->count(ctx, insn->src1, &registers[insn->dst]))
        return qvm_fail(diagnostic, QVM_STATUS_BACKEND_ERROR, pc, insn->op,
                        insn->src1, "QVM count backend failed");
      break;
    case QVM_OP_CMP_LEAF:
    case QVM_OP_CMP_LEAF_NUMBER:
    case QVM_OP_CMP_LEAF_STRING:
    case QVM_OP_CMP_LENGTH_LEAF:
    case QVM_OP_CMP_COUNT_LEAF:
      status = qvm_exec_leaf(ops, ctx, insn, &registers[insn->dst]);
      if (status != QVM_STATUS_OK)
        return qvm_fail(diagnostic, (qvm_status_t)status, pc, insn->op,
                        insn->src1, "QVM leaf backend failed");
      break;
    case QVM_OP_CMP:
    case QVM_OP_MATCH:
    case QVM_OP_CONTAINS:
    case QVM_OP_CONTAINS_CI:
    case QVM_OP_MATCH_FULL:
    case QVM_OP_SEARCH:
    case QVM_OP_ADD:
    case QVM_OP_SUB:
    case QVM_OP_MUL:
    case QVM_OP_DIV:
    case QVM_OP_MOD:
    case QVM_OP_UNION:
    case QVM_OP_BAND:
    case QVM_OP_BOR:
    case QVM_OP_BXOR:
    case QVM_OP_LSHIFT:
    case QVM_OP_RSHIFT:
    case QVM_OP_CAT:
      if (!ops->binary(ctx, (qvm_opcode_t)insn->op, insn->arg,
                       &registers[insn->src1], &registers[insn->src2],
                       &registers[insn->dst]))
        return qvm_fail(diagnostic, QVM_STATUS_BACKEND_ERROR, pc, insn->op,
                        insn->arg, "QVM binary backend failed");
      break;
    case QVM_OP_NEG:
    case QVM_OP_BNOT:
      if (!ops->unary || !ops->unary(ctx, (qvm_opcode_t)insn->op,
                                     &registers[insn->src1],
                                     &registers[insn->dst]))
        return qvm_fail(diagnostic, QVM_STATUS_BACKEND_ERROR, pc, insn->op,
                        insn->src1, "QVM unary backend failed");
      break;
    case QVM_OP_SELECT:
      if (ops->truthy(ctx, &registers[insn->src1]))
        registers[insn->dst] = registers[insn->src2];
      else
        registers[insn->dst] = registers[insn->arg];
      break;
    case QVM_OP_NOT:
      ops->make_bool(ctx, !ops->truthy(ctx, &registers[insn->src1]),
                     &registers[insn->dst]);
      break;
    case QVM_OP_JMP_FALSE:
      if (!ops->truthy(ctx, &registers[insn->src1])) {
        pc = insn->arg;
        continue;
      }
      break;
    case QVM_OP_JMP_TRUE:
      if (ops->truthy(ctx, &registers[insn->src1])) {
        pc = insn->arg;
        continue;
      }
      break;
    case QVM_OP_JMP:
      pc = insn->arg;
      continue;
    case QVM_OP_TRUE:
      ops->make_bool(ctx, 1, &registers[insn->dst]);
      break;
    case QVM_OP_FALSE:
      ops->make_bool(ctx, 0, &registers[insn->dst]);
      break;
    default:
      return qvm_fail(diagnostic, QVM_STATUS_INVALID_PROGRAM, pc, insn->op,
                      QVM_NO_OPERAND, "unknown QVM opcode");
    }
    ++pc;
  }
  *out = registers[0];
  return QVM_STATUS_OK;
}

int qvm_execute(const qvm_instruction_t *instructions,
                uint32_t instruction_count, uint32_t offset, uint32_t len,
                const qvm_exec_ops_t *ops, void *ctx,
                const qvm_exec_input_t *input, qvm_value_t *out) {
  return qvm_execute_ex(instructions, instruction_count, offset, len, ops, ctx,
                        input, out, NULL, NULL);
}
