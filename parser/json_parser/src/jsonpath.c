/**
 * @file jsonpath.c
 * @brief JSONPath parser and matcher for json_value_t trees
 */

#include "json_parser.h"
#include "json_types.h"
#include "json_unicode.h"
#include "jsonpath_grammar_gen.h"
#include "jsonpath_types.h"
#include "jsonpath_contains.h"
#include "jsonpath_utf8.h"
#include "query_vm.h"
#include "re.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSONPATH_ERROR_LEN 256
#define JSONPATH_NO_INDEX UINT32_MAX
#define JSONPATH_NO_REGEX UINT32_MAX
#define JSONPATH_MAX_REGEXES 4096U
#define JSONPATH_MAX_INSTRUCTIONS (1024U * 1024U)
#define JSONPATH_MAX_CONSTANT_BYTES (64U * 1024U * 1024U)
#define JSONPATH_STREAM_MAX_SEGMENTS 64U
#define JSONPATH_STREAM_MAX_ALTERNATIVES 64U
#define JSONPATH_STREAM_MAX_DEPTH 256U

typedef struct jsonpath_instruction_s {
  int type;
  int num;
  int num2;
  int num3;
  uint32_t slice_mask;
  uint32_t is_selector;
  uint32_t expr_vm_offset;
  uint32_t expr_vm_len;
  uint32_t down;
  uint32_t sibling;
  uint32_t string_offset;
  uint32_t string_len;
  uint32_t regex_index;
  size_t key_hash;
  double number;
} jsonpath_instruction_t;

struct json_path_program_s {
  jsonpath_instruction_t *instructions;
  char *constants;
  re_t *regexes;
  qvm_instruction_t *expr_vm;
  uint32_t instruction_count;
  uint32_t entry;
  uint32_t constant_bytes;
  uint32_t regex_count;
  uint32_t expr_vm_count;
  qvm_limits_t qvm_limits;
  int qvm_diagnostics;
};

struct json_path_result_s {
  json_value_t **items;
  size_t count;
  size_t capacity;
  int error;
  int count_only; /* count terminal matches without storing nodes */
  qvm_status_t qvm_status;
  qvm_diagnostic_t qvm_diagnostic;
};

static void jsonpath_qvm_diagnostic_clear(qvm_diagnostic_t *diagnostic) {
  if (!diagnostic) return;
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->status = QVM_STATUS_OK;
  diagnostic->instruction = QVM_NO_INSTRUCTION;
  diagnostic->opcode = QVM_NO_OPCODE;
  diagnostic->operand = QVM_NO_OPERAND;
}

typedef qvm_value_t jsonpath_runtime_value_t;

typedef enum {
  JSONPATH_STREAM_KEY,
  JSONPATH_STREAM_INDEX,
  JSONPATH_STREAM_WILDCARD,
  JSONPATH_STREAM_FILTER
} jsonpath_stream_segment_kind_t;

/* Streamable filter predicate: compare the candidate value itself against a
 * scalar constant (`[?@ <op> <const>]` or the reversed form). The candidate
 * must be a scalar because a single SAX pass can only decide scalar values
 * without buffering; member predicates such as `[?@.key <op> <const>]` stay on
 * the DOM API (rejected at stream creation). */
typedef struct {
  int op; /* EQ/NE/LT/LE/GT/GE */
  jsonpath_runtime_value_t constant;
} jsonpath_stream_filter_t;

typedef struct {
  jsonpath_stream_segment_kind_t kind;
  const jsonpath_instruction_t *instruction;
  jsonpath_stream_filter_t filter;
} jsonpath_stream_segment_t;

typedef struct {
  jsonpath_stream_segment_t segments[JSONPATH_STREAM_MAX_SEGMENTS];
  size_t count;
} jsonpath_stream_alternative_t;

typedef struct {
  unsigned long long active;
  unsigned long long selected;
  bool forward;
  bool object;
  size_t path_depth;
  size_t index;
} jsonpath_stream_frame_t;

struct json_path_stream_s {
  const json_path_program_t *program;
  json_path_stream_handler_t handler;
  void *ctx;
  json_sax_parser_t *parser;
  jsonpath_stream_alternative_t alternatives[JSONPATH_STREAM_MAX_ALTERNATIVES];
  size_t alternative_count;
  jsonpath_stream_frame_t frames[JSONPATH_STREAM_MAX_DEPTH];
  size_t depth;
  unsigned long long pending_active;
  bool pending_valid;
  /* State for a scalar filter candidate being decided at its closing event. */
  unsigned long long pending_filter_bits;
  unsigned long long pending_filter_pass;
  bool pending_filter_valid;
  json_type_t pending_filter_type;
  int pending_filter_held_kind;   /* JSON_NULL/BOOL/NUMBER/STRING */
  bool pending_filter_held_bool;
  char pending_filter_held_number[64];
  size_t pending_filter_held_number_len;
  char pending_filter_held_string[64];
  char *pending_filter_held_string_dyn;
  size_t pending_filter_held_string_len;
  bool root_started;
  bool failed;
  size_t matches;
  char error[JSONPATH_ERROR_LEN];
};

static char g_jsonpath_error[JSONPATH_ERROR_LEN] = {0};

void *JsonPathParseAlloc(void *(*mallocProc)(size_t));
void JsonPathParseFree(void *parser, void (*freeProc)(void *));
void JsonPathParse(void *parser, int tokenType, jsonpath_opcode_t *token,
                   jsonpath_parse_ctx_t *ctx);

static char *jsonpath_strdup_len(const char *str, size_t len) {
  char *out = (char *)malloc(len + 1);
  if (!out) return NULL;
  memcpy(out, str, len);
  out[len] = '\0';
  return out;
}

jsonpath_opcode_t *jsonpath_append_op(jsonpath_opcode_t *a, jsonpath_opcode_t *b) {
  jsonpath_opcode_t *tail = a;

  if (!a) return b;

  while (tail->sibling)
    tail = tail->sibling;

  tail->sibling = b;
  return a;
}

jsonpath_opcode_t *jsonpath_alloc_op(jsonpath_parse_ctx_t *ctx, int type, int num, double number,
                                     const char *str, ...) {
  jsonpath_opcode_t *child;
  jsonpath_opcode_t *op = (jsonpath_opcode_t *)calloc(1, sizeof(*op));
  if (!op) {
    if (ctx) ctx->error_code = -2;
    return NULL;
  }

  op->type = type;
  op->num = num;
  op->number = number;

  if (str) {
    op->str = jsonpath_strdup_len(str, strlen(str));
    if (!op->str) {
      free(op);
      if (ctx) ctx->error_code = -2;
      return NULL;
    }
  }

  va_list ap;
  va_start(ap, str);
  while ((child = va_arg(ap, jsonpath_opcode_t *)) != NULL) {
    if (!op->down) op->down = child;
    else jsonpath_append_op(op->down, child);
  }
  va_end(ap);

  op->next = ctx->pool;
  ctx->pool = op;
  return op;
}

static void jsonpath_free_ctx(jsonpath_parse_ctx_t *ctx) {
  jsonpath_opcode_t *op = ctx ? ctx->pool : NULL;
  while (op) {
    jsonpath_opcode_t *next = op->next;
    free(op->str);
    free(op);
    op = next;
  }
  free(ctx);
}

static int jsonpath_append_utf8(char *out, size_t *pos, int code) {
  if (code <= 0) return 0;
  if (code <= 0x7F) {
    out[(*pos)++] = (char)code;
  } else if (code <= 0x7FF) {
    out[(*pos)++] = (char)(0xC0 | (code >> 6));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  } else if (code <= 0xFFFF) {
    out[(*pos)++] = (char)(0xE0 | (code >> 12));
    out[(*pos)++] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  } else if (code <= 0x10FFFF) {
    out[(*pos)++] = (char)(0xF0 | (code >> 18));
    out[(*pos)++] = (char)(0x80 | ((code >> 12) & 0x3F));
    out[(*pos)++] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  }
  return 0;
}

static int jsonpath_parse_string(const char *buf, char quote, char **out_str,
                                 jsonpath_parse_ctx_t *ctx) {
  size_t cap = strlen(buf) + 1;
  size_t input_len = cap - 1;
  char *out = (char *)malloc(cap);
  size_t pos = 0;
  size_t i = 1;

  if (!out) return -2;

  while (buf[i]) {
    unsigned char c = (unsigned char)buf[i++];

    if (c == (unsigned char)quote) {
      out[pos] = '\0';
      *out_str = out;
      return (int)i;
    }

    if (c != '\\') {
      out[pos++] = (char)c;
      continue;
    }

    size_t escape = i - 1;
    c = (unsigned char)buf[i++];
    if (!c) {
      free(out);
      return -1;
    }

    switch (c) {
    case '"':
    case '\'':
    case '\\':
    case '/':
      out[pos++] = (char)c;
      break;
    case 'b':
      out[pos++] = '\b';
      break;
    case 'f':
      out[pos++] = '\f';
      break;
    case 'n':
      out[pos++] = '\n';
      break;
    case 'r':
      out[pos++] = '\r';
      break;
    case 't':
      out[pos++] = '\t';
      break;
    case 'u': {
      uint32_t codepoint;
      size_t next = escape;
      if (!json_unicode_decode_escape(buf, input_len, &next, &codepoint)) {
        free(out);
        ctx->error_pos = ctx->off + (int)escape;
        return -3;
      }
      pos += json_unicode_append_utf8(out + pos, codepoint);
      i = next;
      break;
    }
    case 'x': {
      int h0 = json_unicode_hex(buf[i]);
      int h1 = json_unicode_hex(buf[i + 1]);
      if (h0 < 0 || h1 < 0) {
        free(out);
        ctx->error_pos = ctx->off + (int)i;
        return -3;
      }
      jsonpath_append_utf8(out, &pos, (h0 << 4) | h1);
      i += 2;
      break;
    }
    default:
      if (quote == '/') {
        out[pos++] = '\\';
      }
      out[pos++] = (char)c;
      break;
    }
  }

  free(out);
  return -1;
}

static int jsonpath_lex(const char *input, jsonpath_parse_ctx_t *ctx, jsonpath_opcode_t **out_op) {
  jsonpath_opcode_t *op = NULL;
  char *str = NULL;
  char *end = NULL;
  int type = 0;
  int consumed = 1;

  *out_op = NULL;

  switch (*input) {
  case ' ':
  case '\t':
  case '\r':
  case '\n':
    return 1;
  case '&':
    if (input[1] == '&') {
      type = JSONPATH_TOKEN_AND;
      consumed = 2;
    }
    break;
  case '|':
    if (input[1] == '|') {
      type = JSONPATH_TOKEN_OR;
      consumed = 2;
    }
    break;
  case '<':
    type = input[1] == '=' ? JSONPATH_TOKEN_LE : JSONPATH_TOKEN_LT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '>':
    type = input[1] == '=' ? JSONPATH_TOKEN_GE : JSONPATH_TOKEN_GT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '!':
    type = input[1] == '=' ? JSONPATH_TOKEN_NE : JSONPATH_TOKEN_NOT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '=':
    type = JSONPATH_TOKEN_EQ;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '~':
    type = JSONPATH_TOKEN_MATCH;
    break;
  case ',':
    type = JSONPATH_TOKEN_UNION;
    break;
  case '.':
    if (input[1] == '.') {
      type = JSONPATH_TOKEN_DESCENDANT;
      consumed = 2;
    } else {
      type = JSONPATH_TOKEN_DOT;
      consumed = 1;
    }
    break;
  case '[':
    type = JSONPATH_TOKEN_BROPEN;
    break;
  case ':':
    type = JSONPATH_TOKEN_COLON;
    break;
  case '?':
    type = JSONPATH_TOKEN_QMARK;
    break;
  case ']':
    type = JSONPATH_TOKEN_BRCLOSE;
    break;
  case '(':
    type = JSONPATH_TOKEN_POPEN;
    break;
  case ')':
    type = JSONPATH_TOKEN_PCLOSE;
    break;
  case '$':
    type = JSONPATH_TOKEN_ROOT;
    break;
  case '@':
    type = JSONPATH_TOKEN_THIS;
    break;
  case '*':
    type = JSONPATH_TOKEN_WILDCARD;
    break;
  case '\'':
  case '"':
    consumed = jsonpath_parse_string(input, *input, &str, ctx);
    if (consumed < 0) return consumed;
    type = JSONPATH_TOKEN_STRING;
    break;
  case '/':
    consumed = jsonpath_parse_string(input, '/', &str, ctx);
    if (consumed < 0) return consumed;
    while (isalpha((unsigned char)input[consumed]))
      consumed++;
    type = JSONPATH_TOKEN_REGEXP;
    break;
  default:
    if (*input == '-' || isdigit((unsigned char)*input)) {
      double number = strtod(input, &end);
      if (end == input) return -3;
      consumed = (int)(end - input);
      op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_NUMBER, (int)number, number, NULL, NULL);
      if (!op) return -2;
      *out_op = op;
      return consumed;
    }

    if (*input == '_' || isalpha((unsigned char)*input)) {
      const char *start = input;
      while (*input == '_' || isalnum((unsigned char)*input))
        input++;

      consumed = (int)(input - start);
      if (consumed == 8 && memcmp(start, "contains", 8) == 0) {
        /* Function-style operator: contains(@.x, 'needle'). Plain member
         * names like $.contains keep their LABEL meaning. */
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_CONTAINS, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 11 && memcmp(start, "contains_ci", 11) == 0) {
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_CONTAINS_CI, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 6 && memcmp(start, "length", 6) == 0) {
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_LENGTH, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 5 && memcmp(start, "count", 5) == 0) {
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_COUNT, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 5 && memcmp(start, "match", 5) == 0) {
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_MATCHFUNC, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 6 && memcmp(start, "search", 6) == 0) {
        const char *peek = input;
        while (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')
          ++peek;
        if (*peek == '(') {
          op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_SEARCHFUNC, 0, 0.0, NULL, NULL);
          if (!op) return -2;
          *out_op = op;
          return consumed;
        }
      }
      if (consumed == 4 && memcmp(start, "true", 4) == 0) {
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_BOOL, 1, 1.0, NULL, NULL);
      } else if (consumed == 5 && memcmp(start, "false", 5) == 0) {
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_BOOL, 0, 0.0, NULL, NULL);
      } else {
        str = jsonpath_strdup_len(start, (size_t)consumed);
        if (!str) return -2;
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_LABEL, 0, 0.0, str, NULL);
        free(str);
      }
      if (!op) return -2;
      *out_op = op;
      return consumed;
    }
    break;
  }

  if (!type) return -4;

  op = jsonpath_alloc_op(ctx, type, 0, 0.0, str, NULL);
  free(str);
  if (!op) return -2;

  *out_op = op;
  return consumed;
}

static jsonpath_parse_ctx_t *jsonpath_parse(const char *expr) {
  jsonpath_parse_ctx_t *ctx;
  void *parser;
  const char *ptr;
  int consumed;

  if (!expr || !*expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Empty JSONPath expression");
    return NULL;
  }

  ctx = (jsonpath_parse_ctx_t *)calloc(1, sizeof(*ctx));
  if (!ctx) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }

  parser = JsonPathParseAlloc(malloc);
  if (!parser) {
    jsonpath_free_ctx(ctx);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }

  ptr = expr;
  while (*ptr && !ctx->error_code) {
    jsonpath_opcode_t *op = NULL;
    consumed = jsonpath_lex(ptr, ctx, &op);
    if (consumed < 0) {
      ctx->error_code = consumed;
      ctx->error_pos = ctx->error_pos ? ctx->error_pos : ctx->off;
      break;
    }

    if (op) JsonPathParse(parser, op->type, op, ctx);

    ptr += consumed;
    ctx->off += consumed;
  }

  if (!ctx->error_code) JsonPathParse(parser, 0, NULL, ctx);

  JsonPathParseFree(parser, free);

  if (ctx->error_code || !ctx->path) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid JSONPath expression at offset %d",
             ctx->error_pos);
    jsonpath_free_ctx(ctx);
    return NULL;
  }

  return ctx;
}

static const jsonpath_instruction_t *jsonpath_program_instruction(
    const json_path_program_t *program, uint32_t index);
static const char *jsonpath_program_string(const json_path_program_t *program,
                                           const jsonpath_instruction_t *instruction);

/* ---------------------------------------------------------------------------
 * Filter expression VM
 *
 * Selector/filter expressions (comparisons, boolean logic, regex/contains,
 * paths, length()/count()) are lowered to a flat register bytecode: every
 * intermediate is written to a fixed slot (slot 0 is the result) instead of a
 * value stack, and AND/OR short-circuit by jumping on the raw slot truthiness
 * without materializing a boolean. Each candidate element is evaluated with a
 * single instruction loop and no per-access bounds checks (the compiler bounds
 * slot indices). The bytecode layout is private to json_path_program_t; the
 * public program API is unchanged.
 * ------------------------------------------------------------------------- */
#define JSONPATH_VM_LOAD_PATH QVM_OP_LOAD_PATH
#define JSONPATH_VM_LOAD_CONST QVM_OP_LOAD_CONST
#define JSONPATH_VM_LOAD_KEY QVM_OP_LOAD_KEY
#define JSONPATH_VM_LOAD_INDEX QVM_OP_LOAD_INDEX
#define JSONPATH_VM_LOAD_INVALID QVM_OP_LOAD_INVALID
#define JSONPATH_VM_EXISTS QVM_OP_EXISTS
#define JSONPATH_VM_LENGTH QVM_OP_LENGTH
#define JSONPATH_VM_COUNT QVM_OP_COUNT
#define JSONPATH_VM_CMP QVM_OP_CMP
#define JSONPATH_VM_CMP_LEAF QVM_OP_CMP_LEAF
#define JSONPATH_VM_CMP_LEAF_NUM QVM_OP_CMP_LEAF_NUMBER
#define JSONPATH_VM_CMP_LEAF_STR QVM_OP_CMP_LEAF_STRING
#define JSONPATH_VM_NOT_EXISTS QVM_OP_NOT_EXISTS
#define JSONPATH_VM_MATCH QVM_OP_MATCH
#define JSONPATH_VM_CONTAINS QVM_OP_CONTAINS
#define JSONPATH_VM_CONTAINS_CI QVM_OP_CONTAINS_CI
#define JSONPATH_VM_MATCH_FUNC QVM_OP_MATCH_FULL
#define JSONPATH_VM_SEARCH_FUNC QVM_OP_SEARCH
#define JSONPATH_VM_CMP_LEN_LEAF QVM_OP_CMP_LENGTH_LEAF
#define JSONPATH_VM_CMP_COUNT_LEAF QVM_OP_CMP_COUNT_LEAF
#define JSONPATH_VM_NOT QVM_OP_NOT
#define JSONPATH_VM_JMPF QVM_OP_JMP_FALSE
#define JSONPATH_VM_JMPT QVM_OP_JMP_TRUE
#define JSONPATH_VM_JMP QVM_OP_JMP
#define JSONPATH_VM_TRUE QVM_OP_TRUE
#define JSONPATH_VM_FALSE QVM_OP_FALSE

#define JSONPATH_RT_INVALID (-1)
/* Register file slots per evaluation; slot 0 is the result. A binary op uses
 * one temp slot at depth d, so max slots = 1 + max binary nesting depth.
 * Deeper expressions (pathological) fall back to the recursive evaluator. */
#define JSONPATH_VM_MAX_SLOTS QVM_MAX_REGISTERS

typedef qvm_instruction_t jsonpath_vm_insn_t;

typedef struct {
  jsonpath_vm_insn_t *insns;
  size_t count;
  size_t capacity;
  int failed;    /* OOM / hard compile error */
  int overflow;  /* register depth exceeded JSONPATH_VM_MAX_SLOTS (recoverable) */
} jsonpath_vm_builder_t;

static void jsonpath_vm_emit(jsonpath_vm_builder_t *builder, uint8_t op, uint16_t dst,
                             uint32_t arg, uint32_t src1, uint32_t src2) {
  jsonpath_vm_insn_t *insns;
  size_t new_capacity;
  if (builder->failed) return;
  if (builder->count == builder->capacity) {
    new_capacity = builder->capacity ? builder->capacity * 2U : 32U;
    if (new_capacity > JSONPATH_MAX_INSTRUCTIONS) {
      builder->failed = 1;
      return;
    }
    insns = (jsonpath_vm_insn_t *)realloc(builder->insns, new_capacity * sizeof(*insns));
    if (!insns) {
      builder->failed = 1;
      return;
    }
    builder->insns = insns;
    builder->capacity = new_capacity;
  }
  builder->insns[builder->count].op = op;
  builder->insns[builder->count].reserved = 0;
  builder->insns[builder->count].dst = dst;
  builder->insns[builder->count].arg = arg;
  builder->insns[builder->count].src1 = src1;
  builder->insns[builder->count].src2 = src2;
  ++builder->count;
}

/* Flip a comparison so `literal op @.member` is evaluated as
 * `@.member reversed_op literal` with the same truth value. */
static int jsonpath_vm_reverse_op(int op) {
  switch (op) {
  case JSONPATH_TOKEN_LT: return JSONPATH_TOKEN_GT;
  case JSONPATH_TOKEN_LE: return JSONPATH_TOKEN_GE;
  case JSONPATH_TOKEN_GT: return JSONPATH_TOKEN_LT;
  case JSONPATH_TOKEN_GE: return JSONPATH_TOKEN_LE;
  default: return op; /* EQ and NE are symmetric */
  }
}

/* Emit the leaf compare best suited to the literal operand type: NUMBER and
 * STRING get specialized instructions that skip the value-type dispatch; any
 * other literal (BOOL) keeps the general leaf. */
static void jsonpath_vm_emit_cmp_leaf(jsonpath_vm_builder_t *builder, uint16_t dst,
                                      int opcode, uint32_t path_index,
                                      uint32_t literal_index, int literal_type) {
  uint8_t vm_op;
  switch (literal_type) {
  case JSONPATH_TOKEN_NUMBER: vm_op = JSONPATH_VM_CMP_LEAF_NUM; break;
  case JSONPATH_TOKEN_STRING: vm_op = JSONPATH_VM_CMP_LEAF_STR; break;
  default: vm_op = JSONPATH_VM_CMP_LEAF; break;
  }
  jsonpath_vm_emit(builder, vm_op, dst, (uint32_t)opcode, path_index, literal_index);
}

static void jsonpath_vm_compile_node(const json_path_program_t *program,
                                     jsonpath_vm_builder_t *builder, uint32_t index,
                                     int boolean_ctx, uint16_t dst, int depth) {
  const jsonpath_instruction_t *instruction;
  const jsonpath_instruction_t *left;
  const jsonpath_instruction_t *right;
  uint32_t left_index;
  uint32_t right_index;
  uint32_t child;
  uint32_t first_jump;
  uint32_t last_jump;
  uint16_t temp;

  if (builder->failed || builder->overflow) return;
  instruction = jsonpath_program_instruction(program, index);
  if (!instruction) {
    builder->failed = 1;
    return;
  }
  switch (instruction->type) {
  case JSONPATH_TOKEN_EQ:
  case JSONPATH_TOKEN_NE:
  case JSONPATH_TOKEN_LT:
  case JSONPATH_TOKEN_LE:
  case JSONPATH_TOKEN_GT:
  case JSONPATH_TOKEN_GE:
    left_index = instruction->down;
    left = jsonpath_program_instruction(program, left_index);
    right_index = left ? left->sibling : JSONPATH_NO_INDEX;
    right = jsonpath_program_instruction(program, right_index);
    {
      /* Common shape `@.member op literal` becomes a single leaf compare so
       * the hot filter loop resolves both operands directly; `literal op
       * @.member` is flipped onto the same leaf with the reversed operator. */
      const int left_is_path = left && (left->type == JSONPATH_TOKEN_THIS ||
                                        left->type == JSONPATH_TOKEN_ROOT);
      const int right_is_literal = right && (right->type == JSONPATH_TOKEN_BOOL ||
                                             right->type == JSONPATH_TOKEN_NUMBER ||
                                             right->type == JSONPATH_TOKEN_STRING);
      const int right_is_path = right && (right->type == JSONPATH_TOKEN_THIS ||
                                          right->type == JSONPATH_TOKEN_ROOT);
      const int left_is_literal = left && (left->type == JSONPATH_TOKEN_BOOL ||
                                           left->type == JSONPATH_TOKEN_NUMBER ||
                                           left->type == JSONPATH_TOKEN_STRING);
      if (left_is_path && right_is_literal) {
        jsonpath_vm_emit_cmp_leaf(builder, dst, (int)instruction->type,
                                  left_index, right_index, right->type);
      } else if (right_is_path && left_is_literal) {
        jsonpath_vm_emit_cmp_leaf(builder, dst, jsonpath_vm_reverse_op((int)instruction->type),
                                  right_index, left_index, left->type);
      } else if (left && (left->type == JSONPATH_TOKEN_LENGTH ||
                          left->type == JSONPATH_TOKEN_COUNT) &&
                 right && right->type == JSONPATH_TOKEN_NUMBER) {
        /* `length(@.x) op literal` / `count(@.x) op literal` fold into a single
         * leaf: compute the size and compare with the literal number. */
        jsonpath_vm_emit(builder,
                         left->type == JSONPATH_TOKEN_LENGTH
                             ? JSONPATH_VM_CMP_LEN_LEAF
                             : JSONPATH_VM_CMP_COUNT_LEAF,
                         dst, (uint8_t)instruction->type, left->down, right_index);
      } else if (right && (right->type == JSONPATH_TOKEN_LENGTH ||
                           right->type == JSONPATH_TOKEN_COUNT) &&
                 left && left->type == JSONPATH_TOKEN_NUMBER) {
        jsonpath_vm_emit(builder,
                         right->type == JSONPATH_TOKEN_LENGTH
                             ? JSONPATH_VM_CMP_LEN_LEAF
                             : JSONPATH_VM_CMP_COUNT_LEAF,
                         dst, (uint8_t)jsonpath_vm_reverse_op((int)instruction->type),
                         right->down, left_index);
      } else {
        if (depth + 1 >= JSONPATH_VM_MAX_SLOTS) {
          builder->overflow = 1;
          return;
        }
        temp = (uint16_t)(1U + (unsigned int)depth);
        jsonpath_vm_compile_node(program, builder, left_index, 0, dst, depth);
        jsonpath_vm_compile_node(program, builder, right_index, 0, temp, depth + 1);
        jsonpath_vm_emit(builder, JSONPATH_VM_CMP, dst, (uint8_t)instruction->type, dst, temp);
      }
    }
    break;
  case JSONPATH_TOKEN_MATCH:
    left_index = instruction->down;
    left = jsonpath_program_instruction(program, left_index);
    right_index = left ? left->sibling : JSONPATH_NO_INDEX;
    if (depth + 1 >= JSONPATH_VM_MAX_SLOTS) {
      builder->overflow = 1;
      return;
    }
    temp = (uint16_t)(1U + (unsigned int)depth);
    jsonpath_vm_compile_node(program, builder, left_index, 0, dst, depth);
    jsonpath_vm_compile_node(program, builder, right_index, 0, temp, depth + 1);
    jsonpath_vm_emit(builder, JSONPATH_VM_MATCH, dst, instruction->regex_index, dst, temp);
    break;
  case JSONPATH_TOKEN_CONTAINS:
  case JSONPATH_TOKEN_CONTAINS_CI:
  case JSONPATH_TOKEN_MATCHFUNC:
  case JSONPATH_TOKEN_SEARCHFUNC:
    left_index = instruction->down;
    left = jsonpath_program_instruction(program, left_index);
    right_index = left ? left->sibling : JSONPATH_NO_INDEX;
    if (depth + 1 >= JSONPATH_VM_MAX_SLOTS) {
      builder->overflow = 1;
      return;
    }
    temp = (uint16_t)(1U + (unsigned int)depth);
    jsonpath_vm_compile_node(program, builder, left_index, 0, dst, depth);
    jsonpath_vm_compile_node(program, builder, right_index, 0, temp, depth + 1);
    if (instruction->type == JSONPATH_TOKEN_MATCHFUNC)
      jsonpath_vm_emit(builder, JSONPATH_VM_MATCH_FUNC, dst, instruction->regex_index, dst, temp);
    else if (instruction->type == JSONPATH_TOKEN_SEARCHFUNC)
      jsonpath_vm_emit(builder, JSONPATH_VM_SEARCH_FUNC, dst, instruction->regex_index, dst, temp);
    else if (instruction->type == JSONPATH_TOKEN_CONTAINS_CI)
      jsonpath_vm_emit(builder, JSONPATH_VM_CONTAINS_CI, dst, 0, dst, temp);
    else
      jsonpath_vm_emit(builder, JSONPATH_VM_CONTAINS, dst, 0, dst, temp);
    break;
  case JSONPATH_TOKEN_NOT: {
    const jsonpath_instruction_t *not_child =
        jsonpath_program_instruction(program, instruction->down);
    if (not_child && (not_child->type == JSONPATH_TOKEN_THIS ||
                      not_child->type == JSONPATH_TOKEN_ROOT)) {
      /* `[?!@.x]` becomes a single non-existence test. */
      jsonpath_vm_emit(builder, JSONPATH_VM_NOT_EXISTS, dst, 0, instruction->down, 0);
    } else {
      jsonpath_vm_compile_node(program, builder, instruction->down, 1, dst, depth);
      jsonpath_vm_emit(builder, JSONPATH_VM_NOT, dst, 0, dst, 0);
    }
    break;
  }
  case JSONPATH_TOKEN_AND:
    /* a && b && c: compile each child into the shared result slot and jump to
     * the end on the first falsy value; the slot holds the last evaluated
     * value, whose truthiness is the result (no boolean materialization). */
    child = instruction->down;
    first_jump = 0;
    last_jump = 0;
    while (child != JSONPATH_NO_INDEX) {
      uint32_t next;
      uint32_t jump;
      jsonpath_vm_compile_node(program, builder, child, 1, dst, depth);
      next = jsonpath_program_instruction(program, child)->sibling;
      if (next != JSONPATH_NO_INDEX) {
        jump = (uint32_t)builder->count;
        jsonpath_vm_emit(builder, JSONPATH_VM_JMPF, 0, 0, dst, 0);
        if (first_jump == 0) first_jump = jump;
        last_jump = jump;
      }
      child = next;
    }
    if (first_jump != 0 && last_jump != 0) {
      for (uint32_t pos = first_jump; pos <= last_jump; ++pos) {
        if (builder->insns[pos].op == JSONPATH_VM_JMPF)
          builder->insns[pos].arg = (uint32_t)builder->count;
      }
    }
    break;
  case JSONPATH_TOKEN_OR:
  case JSONPATH_TOKEN_UNION:
    /* a || b || c: jump to the end on the first truthy value. */
    child = instruction->down;
    first_jump = 0;
    last_jump = 0;
    while (child != JSONPATH_NO_INDEX) {
      uint32_t next;
      uint32_t jump;
      jsonpath_vm_compile_node(program, builder, child, 1, dst, depth);
      next = jsonpath_program_instruction(program, child)->sibling;
      if (next != JSONPATH_NO_INDEX) {
        jump = (uint32_t)builder->count;
        jsonpath_vm_emit(builder, JSONPATH_VM_JMPT, 0, 0, dst, 0);
        if (first_jump == 0) first_jump = jump;
        last_jump = jump;
      }
      child = next;
    }
    if (first_jump != 0 && last_jump != 0) {
      for (uint32_t pos = first_jump; pos <= last_jump; ++pos) {
        if (builder->insns[pos].op == JSONPATH_VM_JMPT)
          builder->insns[pos].arg = (uint32_t)builder->count;
      }
    }
    break;
  case JSONPATH_TOKEN_THIS:
  case JSONPATH_TOKEN_ROOT:
    jsonpath_vm_emit(builder, boolean_ctx ? JSONPATH_VM_EXISTS : JSONPATH_VM_LOAD_PATH,
                     dst, 0, index, 0);
    break;
  case JSONPATH_TOKEN_LENGTH:
    jsonpath_vm_emit(builder, JSONPATH_VM_LENGTH, dst, 0, instruction->down, 0);
    break;
  case JSONPATH_TOKEN_COUNT:
    jsonpath_vm_emit(builder, JSONPATH_VM_COUNT, dst, 0, instruction->down, 0);
    break;
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_NUMBER:
    if (boolean_ctx) {
      if (depth + 1 >= JSONPATH_VM_MAX_SLOTS) {
        builder->overflow = 1;
        return;
      }
      temp = (uint16_t)(1U + (unsigned int)depth);
      jsonpath_vm_emit(builder, instruction->type == JSONPATH_TOKEN_STRING
                                    ? JSONPATH_VM_LOAD_KEY
                                    : JSONPATH_VM_LOAD_INDEX,
                       temp, 0, 0, 0);
      jsonpath_vm_emit(builder, JSONPATH_VM_LOAD_CONST, dst, 0, index, 0);
      jsonpath_vm_emit(builder, JSONPATH_VM_CMP, dst, (uint8_t)JSONPATH_TOKEN_EQ, temp, dst);
    } else {
      jsonpath_vm_emit(builder, JSONPATH_VM_LOAD_CONST, dst, 0, index, 0);
    }
    break;
  case JSONPATH_TOKEN_BOOL:
  case JSONPATH_TOKEN_REGEXP:
    jsonpath_vm_emit(builder, JSONPATH_VM_LOAD_CONST, dst, 0, index, 0);
    break;
  case JSONPATH_TOKEN_WILDCARD:
    jsonpath_vm_emit(builder, boolean_ctx ? JSONPATH_VM_TRUE : JSONPATH_VM_LOAD_INVALID,
                     dst, 0, 0, 0);
    break;
  default:
    /* Not a selector expression (label/index/slice/descendant paths are
     * evaluated by the path walker, not the expression VM). */
    jsonpath_vm_emit(builder, JSONPATH_VM_LOAD_INVALID, dst, 0, 0, 0);
    break;
  }
}

static int jsonpath_vm_compilable_type(int type) {
  switch (type) {
  case JSONPATH_TOKEN_EQ:
  case JSONPATH_TOKEN_NE:
  case JSONPATH_TOKEN_LT:
  case JSONPATH_TOKEN_LE:
  case JSONPATH_TOKEN_GT:
  case JSONPATH_TOKEN_GE:
  case JSONPATH_TOKEN_MATCH:
  case JSONPATH_TOKEN_CONTAINS:
  case JSONPATH_TOKEN_CONTAINS_CI:
  case JSONPATH_TOKEN_MATCHFUNC:
  case JSONPATH_TOKEN_SEARCHFUNC:
  case JSONPATH_TOKEN_NOT:
  case JSONPATH_TOKEN_AND:
  case JSONPATH_TOKEN_OR:
  case JSONPATH_TOKEN_UNION:
  case JSONPATH_TOKEN_THIS:
  case JSONPATH_TOKEN_ROOT:
  case JSONPATH_TOKEN_LENGTH:
  case JSONPATH_TOKEN_COUNT:
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_NUMBER:
  case JSONPATH_TOKEN_BOOL:
  case JSONPATH_TOKEN_REGEXP:
  case JSONPATH_TOKEN_WILDCARD:
    return 1;
  default:
    return 0;
  }
}

/* Compile one selector root (a bracket/union child) into the filter VM pool.
 * Returns 0 on hard failure (OOM); slot overflow rolls the pool back and keeps
 * the instruction on the recursive fallback (expr_vm_offset = NO_INDEX). */
static int jsonpath_vm_compile_root(const json_path_program_t *program,
                                    jsonpath_vm_builder_t *builder, uint32_t index) {
  jsonpath_instruction_t *instruction =
      (jsonpath_instruction_t *)jsonpath_program_instruction(program, index);
  uint32_t root_start;
  if (!instruction) return 0;
  root_start = (uint32_t)builder->count;
  instruction->expr_vm_offset = (uint32_t)builder->count;
  jsonpath_vm_compile_node(program, builder, index, 1, 0, 0);
  if (builder->overflow) {
    builder->count = root_start;
    builder->overflow = 0;
    instruction->expr_vm_offset = JSONPATH_NO_INDEX;
    instruction->expr_vm_len = 0;
    return 1;
  }
  if (builder->failed) return 0;
  instruction->expr_vm_len = (uint32_t)builder->count - instruction->expr_vm_offset;
  if (instruction->expr_vm_len == 0) {
    builder->failed = 1;
    return 0;
  }
  return 1;
}

static json_path_program_t *jsonpath_program_fail(jsonpath_parse_ctx_t *ctx,
                                                  const char *message) {
  if (message) snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "%s", message);
  jsonpath_free_ctx(ctx);
  return NULL;
}

static int jsonpath_program_discover(jsonpath_opcode_t *op,
                                     jsonpath_opcode_t **ordered,
                                     size_t *ordered_count,
                                     jsonpath_opcode_t **stack,
                                     size_t *stack_count) {
  if (!op || !ordered || !ordered_count || !stack || !stack_count) return 1;
  if (op->compiled_index_plus_one != 0) return 1;
  if (*ordered_count >= JSONPATH_MAX_INSTRUCTIONS ||
      *ordered_count >= (size_t)UINT32_MAX)
    return 0;
  op->compiled_index_plus_one = ++*ordered_count;
  ordered[*ordered_count - 1U] = op;
  if (*stack_count >= JSONPATH_MAX_INSTRUCTIONS) return 0;
  stack[(*stack_count)++] = op;
  return 1;
}

/* O(tokens + constant bytes) time and O(tokens + constant bytes) owned space. */
static json_path_program_t *jsonpath_program_lower(jsonpath_parse_ctx_t *ctx,
                                                   const qvm_limits_t *limits,
                                                   qvm_diagnostic_t *diagnostic) {
  jsonpath_opcode_t *op;
  jsonpath_opcode_t **scratch;
  jsonpath_opcode_t **ordered;
  jsonpath_opcode_t **stack;
  json_path_program_t *program;
  size_t pool_count = 0;
  size_t ordered_count = 0;
  size_t stack_count = 0;
  size_t constants = 0;
  size_t i;
  char *constant_ptr;
  const char *vm_error = "Out of memory";

  jsonpath_qvm_diagnostic_clear(diagnostic);

  if (!ctx || !ctx->path) return jsonpath_program_fail(ctx, "Invalid JSONPath program");

  for (op = ctx->pool; op; op = op->next) {
    if (pool_count == SIZE_MAX) return jsonpath_program_fail(ctx, "JSONPath is too large");
    ++pool_count;
  }
  if (pool_count == 0 || pool_count > JSONPATH_MAX_INSTRUCTIONS ||
      pool_count > SIZE_MAX / (2U * sizeof(*scratch)))
    return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");

  scratch = (jsonpath_opcode_t **)malloc(pool_count * 2U * sizeof(*scratch));
  if (!scratch) return jsonpath_program_fail(ctx, "Out of memory");
  ordered = scratch;
  stack = scratch + pool_count;
  for (op = ctx->pool; op; op = op->next) op->compiled_index_plus_one = 0;

  if (!jsonpath_program_discover(ctx->path, ordered, &ordered_count, stack,
                                 &stack_count)) {
    free(scratch);
    return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
  }
  while (stack_count > 0) {
    op = stack[--stack_count];
    if (op->down && !jsonpath_program_discover(op->down, ordered, &ordered_count,
                                               stack, &stack_count)) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
    }
    if (op->sibling &&
        !jsonpath_program_discover(op->sibling, ordered, &ordered_count, stack,
                                   &stack_count)) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
    }
  }

  for (i = 0; i < ordered_count; ++i) {
    if (!ordered[i]->str) continue;
    size_t len = strlen(ordered[i]->str);
    if (len == SIZE_MAX || constants > SIZE_MAX - len - 1U ||
        constants + len + 1U > JSONPATH_MAX_CONSTANT_BYTES ||
        constants + len + 1U > UINT32_MAX) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath constants exceed limits");
    }
    constants += len + 1U;
  }

  program = (json_path_program_t *)calloc(1, sizeof(*program));
  if (!program) {
    free(scratch);
    return jsonpath_program_fail(ctx, "Out of memory");
  }
  program->instructions = (jsonpath_instruction_t *)calloc(
      ordered_count, sizeof(*program->instructions));
  if (!program->instructions) {
    free(scratch);
    free(program);
    return jsonpath_program_fail(ctx, "Out of memory");
  }
  if (constants > 0) {
    program->constants = (char *)malloc(constants);
    if (!program->constants) {
      free(scratch);
      free(program->instructions);
      free(program);
      return jsonpath_program_fail(ctx, "Out of memory");
    }
  }

  program->instruction_count = (uint32_t)ordered_count;
  program->qvm_limits = limits ? *limits : qvm_default_limits();
  program->qvm_diagnostics = limits != NULL;
  program->constant_bytes = (uint32_t)constants;
  program->entry = ctx->path->down
                       ? (uint32_t)(ctx->path->down->compiled_index_plus_one - 1U)
                       : JSONPATH_NO_INDEX;
  constant_ptr = program->constants;
  for (i = 0; i < ordered_count; ++i) {
    jsonpath_instruction_t *instruction = &program->instructions[i];
    op = ordered[i];
    instruction->type = op->type;
    instruction->num = op->num;
    instruction->num2 = op->num2;
    instruction->num3 = op->num3;
    instruction->slice_mask = op->slice_mask;
    instruction->is_selector = op->is_selector != 0;
    instruction->number = op->number;
    instruction->down = op->down
                            ? (uint32_t)(op->down->compiled_index_plus_one - 1U)
                            : JSONPATH_NO_INDEX;
    instruction->sibling = op->sibling
                               ? (uint32_t)(op->sibling->compiled_index_plus_one - 1U)
                               : JSONPATH_NO_INDEX;
    instruction->string_offset = UINT32_MAX;
    instruction->string_len = 0;
    instruction->expr_vm_offset = JSONPATH_NO_INDEX;
    instruction->expr_vm_len = 0;
    if (op->str) {
      size_t len = strlen(op->str);
      instruction->string_offset = (uint32_t)(constant_ptr - program->constants);
      instruction->string_len = (uint32_t)len;
      memcpy(constant_ptr, op->str, len + 1U);
      constant_ptr += len + 1U;
      if (op->type == JSONPATH_TOKEN_LABEL || op->type == JSONPATH_TOKEN_STRING)
        instruction->key_hash = json_object_key_hash(op->str, len);
    }
  }

  /* Wire descendant selectors: for `..sel rest`, the selector's sibling must
   * point at the continuation so applying the selector to each descendant
   * naturally resumes with the remaining segments. */
  for (i = 0; i < ordered_count; ++i) {
    jsonpath_instruction_t *instruction = &program->instructions[i];
    if (instruction->type != JSONPATH_TOKEN_DESCENDANT) continue;
    if (instruction->down != JSONPATH_NO_INDEX)
      program->instructions[instruction->down].sibling = instruction->sibling;
  }

  /* Pre-compile literal regex patterns so repeated ~ (MATCH) predicates reuse
   * an owned handle instead of re-validating the pattern on every evaluation.
   * Patterns that fail to compile or exceed the pool cap fall back to
   * re_match_n() at execution time with identical no-match semantics. */
  for (i = 0; i < ordered_count; ++i)
    program->instructions[i].regex_index = JSONPATH_NO_REGEX;
  {
    size_t match_count = 0;
    for (i = 0; i < ordered_count; ++i) {
      const jsonpath_instruction_t *instruction = &program->instructions[i];
      const jsonpath_instruction_t *left;
      const jsonpath_instruction_t *right;
      if (instruction->type != JSONPATH_TOKEN_MATCH) continue;
      left = jsonpath_program_instruction(program, instruction->down);
      right = jsonpath_program_instruction(program, instruction->sibling);
      if (left && right &&
          (left->type == JSONPATH_TOKEN_REGEXP ||
           right->type == JSONPATH_TOKEN_REGEXP ||
           right->type == JSONPATH_TOKEN_STRING))
        ++match_count;
    }
    if (match_count > 0 && match_count <= JSONPATH_MAX_REGEXES) {
      program->regexes = (re_t *)calloc(match_count, sizeof(re_t));
      if (program->regexes) {
        for (i = 0; i < ordered_count; ++i) {
          jsonpath_instruction_t *instruction = &program->instructions[i];
          const jsonpath_instruction_t *left;
          const jsonpath_instruction_t *right;
          const char *pattern;
          if (instruction->type != JSONPATH_TOKEN_MATCH &&
              instruction->type != JSONPATH_TOKEN_MATCHFUNC &&
              instruction->type != JSONPATH_TOKEN_SEARCHFUNC)
            continue;
          left = jsonpath_program_instruction(program, instruction->down);
          right = left ? jsonpath_program_instruction(program, left->sibling) : NULL;
          pattern = NULL;
          if (left && left->type == JSONPATH_TOKEN_REGEXP) {
            pattern = jsonpath_program_string(program, left);
          } else if (right &&
                     (right->type == JSONPATH_TOKEN_REGEXP ||
                      right->type == JSONPATH_TOKEN_STRING)) {
            pattern = jsonpath_program_string(program, right);
          }
          if (!pattern || left == NULL || right == NULL) continue;
          if (re_compile_n(pattern, strlen(pattern), NULL,
                           &program->regexes[program->regex_count]) == RE_STATUS_OK)
            instruction->regex_index = program->regex_count++;
        }
      }
    }
  }

  /* Lower every selector/filter expression to the flat filter VM. Each
   * expression-root instruction owns a contiguous bytecode range; nested
   * expression nodes also get a root-mode program that is unused but keeps
   * the executor uniform (every selector runs jsonpath_program_expr). */
  {
    jsonpath_vm_builder_t builder;
    memset(&builder, 0, sizeof(builder));
    /* Only selector roots get a VM program: bracket filters, single selectors
     * and union children. Nested expression operands are embedded in their
     * parent program via LOAD_PATH/LOAD_CONST, so no redundant per-node
     * root-mode programs are emitted. */
    for (i = 0; i < ordered_count; ++i) {
      jsonpath_instruction_t *instruction = &program->instructions[i];
      if (!instruction->is_selector) continue;
      if (instruction->type == JSONPATH_TOKEN_UNION) {
        uint32_t child = instruction->down;
        while (child != JSONPATH_NO_INDEX) {
          const jsonpath_instruction_t *selector =
              jsonpath_program_instruction(program, child);
          if (selector && jsonpath_vm_compilable_type(selector->type) &&
              !jsonpath_vm_compile_root(program, &builder, child)) {
            builder.failed = 1;
            break;
          }
          child = selector ? selector->sibling : JSONPATH_NO_INDEX;
        }
        if (builder.failed) break;
      } else if (jsonpath_vm_compilable_type(instruction->type)) {
        if (!jsonpath_vm_compile_root(program, &builder, (uint32_t)i)) {
          builder.failed = 1;
          break;
        }
      }
    }
    if (!builder.failed) {
      for (i = 0; i < ordered_count; ++i) {
        const jsonpath_instruction_t *instruction = &program->instructions[i];
        qvm_diagnostic_t verify_error;
        if (instruction->expr_vm_offset == JSONPATH_NO_INDEX ||
            instruction->expr_vm_len == 0)
          continue;
        if (qvm_verify_slice_ex(builder.insns, (uint32_t)builder.count,
                                instruction->expr_vm_offset,
                                instruction->expr_vm_len,
                                JSONPATH_VM_MAX_SLOTS,
                                program->instruction_count,
                                program->regex_count, &program->qvm_limits,
                                &verify_error) != QVM_STATUS_OK) {
          builder.failed = 1;
          vm_error = verify_error.message ? verify_error.message : "Invalid JSONPath bytecode";
          if (diagnostic) *diagnostic = verify_error;
          break;
        }
      }
    }
    if (builder.failed) {
      free(builder.insns);
      for (i = 0; i < program->regex_count; ++i) re_destroy(program->regexes[i]);
      free(program->regexes);
      free(program->constants);
      free(program->instructions);
      free(program);
      free(scratch);
      return jsonpath_program_fail(ctx, vm_error);
    }
    if (builder.count > 0) {
      if (builder.count > UINT32_MAX) {
        free(builder.insns);
        for (i = 0; i < program->regex_count; ++i) re_destroy(program->regexes[i]);
        free(program->regexes);
        free(program->constants);
        free(program->instructions);
        free(program);
        free(scratch);
        return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
      }
      program->expr_vm = builder.insns;
      program->expr_vm_count = (uint32_t)builder.count;
    } else {
      free(builder.insns);
    }
  }

  free(scratch);
  jsonpath_free_ctx(ctx);
  return program;
}

json_path_program_t *json_path_compile_ex(const char *expr,
                                          const qvm_limits_t *limits,
                                          qvm_diagnostic_t *diagnostic) {
  jsonpath_parse_ctx_t *ctx;
  json_path_program_t *program;
  if (!expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  ctx = jsonpath_parse(expr);
  if (!ctx) return NULL;
  program = jsonpath_program_lower(ctx, limits, diagnostic);
  if (program) g_jsonpath_error[0] = '\0';
  return program;
}

json_path_program_t *json_path_compile(const char *expr) {
  return json_path_compile_ex(expr, NULL, NULL);
}

void json_path_program_free(json_path_program_t *program) {
  uint32_t i;
  if (!program) return;
  for (i = 0; i < program->regex_count; ++i) re_destroy(program->regexes[i]);
  free(program->regexes);
  free(program->expr_vm);
  free(program->constants);
  free(program->instructions);
  free(program);
}

static int jsonpath_result_push(json_path_result_t *result, const json_value_t *value) {
  json_value_t **items;
  size_t new_capacity;

  if (!result || !value) return 1;

  if (result->count_only) {
    ++result->count;
    return 1;
  }

  if (result->count == result->capacity) {
    if (result->capacity > SIZE_MAX / 2U) {
      result->error = 1;
      return 0;
    }
    new_capacity = result->capacity ? result->capacity * 2U : 8U;
    if (new_capacity > SIZE_MAX / sizeof(*items)) {
      result->error = 1;
      return 0;
    }
    items = (json_value_t **)realloc(result->items, new_capacity * sizeof(*items));
    if (!items) {
      result->error = 1;
      return 0;
    }
    result->items = items;
    result->capacity = new_capacity;
  }

  result->items[result->count++] = (json_value_t *)value;
  return 1;
}


static const jsonpath_instruction_t *jsonpath_program_instruction(
    const json_path_program_t *program, uint32_t index) {
  if (!program || index == JSONPATH_NO_INDEX || index >= program->instruction_count)
    return NULL;
  return &program->instructions[index];
}

static const char *jsonpath_program_string(const json_path_program_t *program,
                                           const jsonpath_instruction_t *instruction) {
  if (!program || !instruction || instruction->string_offset == UINT32_MAX ||
      instruction->string_offset >= program->constant_bytes)
    return NULL;
  return program->constants + instruction->string_offset;
}

static int jsonpath_program_compare_values(const jsonpath_runtime_value_t *left,
                                           const jsonpath_runtime_value_t *right,
                                           int opcode);

static int jsonpath_stream_fail(json_path_stream_t *stream, const char *message) {
  if (stream && !stream->failed) {
    snprintf(stream->error, sizeof(stream->error), "%s", message);
    stream->failed = true;
  }
  return -1;
}

/* Reverse a comparison so `constant op @` is evaluated as `@ reversed_op
 * constant` with the same truth value. */
static int jsonpath_stream_filter_reverse_op(int op) {
  return jsonpath_vm_reverse_op(op);
}

/* Compile `[?@ <op> <const>]` (or `[?<const> <op> @]`) into a scalar
 * predicate. The left/right operands must be the bare current node `@` and a
 * scalar literal; member and nested paths return false (DOM API). */
static bool jsonpath_stream_filter_compile(const json_path_program_t *program,
                                           const jsonpath_instruction_t *instruction,
                                           jsonpath_stream_filter_t *filter) {
  const jsonpath_instruction_t *left;
  const jsonpath_instruction_t *right;
  const jsonpath_instruction_t *literal;
  int op;
  if (!program || !instruction || !filter) return false;
  op = instruction->type;
  if (op != JSONPATH_TOKEN_EQ && op != JSONPATH_TOKEN_NE && op != JSONPATH_TOKEN_LT &&
      op != JSONPATH_TOKEN_LE && op != JSONPATH_TOKEN_GT && op != JSONPATH_TOKEN_GE)
    return false;
  left = jsonpath_program_instruction(program, instruction->down);
  right = left ? jsonpath_program_instruction(program, left->sibling) : NULL;
  if (!left || !right) return false;
  if (left->type == JSONPATH_TOKEN_THIS && left->down == JSONPATH_NO_INDEX &&
      (right->type == JSONPATH_TOKEN_BOOL || right->type == JSONPATH_TOKEN_NUMBER ||
       right->type == JSONPATH_TOKEN_STRING)) {
    literal = right;
  } else if (right->type == JSONPATH_TOKEN_THIS && right->down == JSONPATH_NO_INDEX &&
             (left->type == JSONPATH_TOKEN_BOOL || left->type == JSONPATH_TOKEN_NUMBER ||
              left->type == JSONPATH_TOKEN_STRING)) {
    literal = left;
    op = jsonpath_stream_filter_reverse_op(op);
  } else {
    return false;
  }
  memset(filter, 0, sizeof(*filter));
  filter->op = op;
  filter->constant.type = literal->type;
  switch (literal->type) {
  case JSONPATH_TOKEN_BOOL: filter->constant.num = literal->num; break;
  case JSONPATH_TOKEN_NUMBER: filter->constant.number = literal->number; break;
  case JSONPATH_TOKEN_STRING: filter->constant.str = jsonpath_program_string(program, literal); break;
  default: break;
  }
  return true;
}

static bool jsonpath_stream_instruction_to_segment(
    const json_path_program_t *program, const jsonpath_instruction_t *instruction,
    jsonpath_stream_segment_t *segment) {
  if (!program || !instruction || !segment) return false;
  memset(segment, 0, sizeof(*segment));
  segment->instruction = instruction;
  switch (instruction->type) {
  case JSONPATH_TOKEN_LABEL:
  case JSONPATH_TOKEN_STRING:
    segment->kind = JSONPATH_STREAM_KEY;
    return true;
  case JSONPATH_TOKEN_NUMBER:
    if (instruction->num < 0) return false;
    segment->kind = JSONPATH_STREAM_INDEX;
    return true;
  case JSONPATH_TOKEN_WILDCARD:
    segment->kind = JSONPATH_STREAM_WILDCARD;
    return true;
  default:
    if (jsonpath_stream_filter_compile(program, instruction, &segment->filter)) {
      segment->kind = JSONPATH_STREAM_FILTER;
      return true;
    }
    return false;
  }
}

static bool jsonpath_stream_collect(json_path_stream_t *stream, uint32_t index,
                                    const jsonpath_stream_alternative_t *prefix) {
  const jsonpath_instruction_t *instruction;
  jsonpath_stream_alternative_t next;

  if (index == JSONPATH_NO_INDEX) {
    if (stream->alternative_count >= JSONPATH_STREAM_MAX_ALTERNATIVES) return false;
    stream->alternatives[stream->alternative_count++] = *prefix;
    return true;
  }
  instruction = jsonpath_program_instruction(stream->program, index);
  if (!instruction) return false;

  if (instruction->type == JSONPATH_TOKEN_UNION) {
    uint32_t child = instruction->down;
    if (child == JSONPATH_NO_INDEX) return false;
    while (child != JSONPATH_NO_INDEX) {
      const jsonpath_instruction_t *alternative =
          jsonpath_program_instruction(stream->program, child);
      if (!alternative || prefix->count >= JSONPATH_STREAM_MAX_SEGMENTS) return false;
      next = *prefix;
      if (!jsonpath_stream_instruction_to_segment(
              stream->program, alternative, &next.segments[next.count++]))
        return false;
      if (!jsonpath_stream_collect(stream, instruction->sibling, &next)) return false;
      child = alternative->sibling;
    }
    return true;
  }

  if (prefix->count >= JSONPATH_STREAM_MAX_SEGMENTS) return false;
  next = *prefix;
  if (!jsonpath_stream_instruction_to_segment(stream->program, instruction,
                                          &next.segments[next.count++]))
    return false;
  return jsonpath_stream_collect(stream, instruction->sibling, &next);
}

static unsigned long long jsonpath_stream_all_alternatives(
    const json_path_stream_t *stream) {
  if (!stream || stream->alternative_count == 0) return 0;
  if (stream->alternative_count == JSONPATH_STREAM_MAX_ALTERNATIVES)
    return ULLONG_MAX;
  return (1ULL << stream->alternative_count) - 1ULL;
}

static bool jsonpath_stream_segment_matches(
    const json_path_stream_t *stream, const jsonpath_stream_segment_t *segment,
    bool object, const char *key, size_t key_len, size_t key_hash, size_t index) {
  const jsonpath_instruction_t *instruction;
  const char *expected;
  if (!stream || !segment || !(instruction = segment->instruction)) return false;
  switch (segment->kind) {
  case JSONPATH_STREAM_WILDCARD: return true;
  case JSONPATH_STREAM_INDEX:
    return !object && (size_t)instruction->num == index;
  case JSONPATH_STREAM_KEY:
    expected = jsonpath_program_string(stream->program, instruction);
    return object && expected && instruction->key_hash == key_hash &&
           instruction->string_len == key_len &&
           memcmp(expected, key, key_len) == 0;
  }
  return false;
}

static unsigned long long jsonpath_stream_child_active(
    const json_path_stream_t *stream, unsigned long long parent_active,
    size_t segment_index, bool object, const char *key, size_t key_len,
    size_t key_hash, size_t index) {
  unsigned long long active = 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    const jsonpath_stream_alternative_t *alternative = &stream->alternatives[i];
    if (!(parent_active & bit) || segment_index >= alternative->count) continue;
    if (jsonpath_stream_segment_matches(stream, &alternative->segments[segment_index],
                                        object, key, key_len, key_hash, index))
      active |= bit;
  }
  return active;
}

static unsigned long long jsonpath_stream_selected(
    const json_path_stream_t *stream, unsigned long long active, size_t path_depth) {
  unsigned long long selected = 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    if ((active & bit) && stream->alternatives[i].count == path_depth) selected |= bit;
  }
  return selected;
}

/* Alternatives of parent whose next segment is a FILTER: the current child is
 * a filter candidate. */
static unsigned long long jsonpath_stream_filter_bits(
    const json_path_stream_t *stream, const jsonpath_stream_frame_t *parent) {
  unsigned long long bits = 0;
  if (!stream || !parent) return 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    const jsonpath_stream_alternative_t *alt = &stream->alternatives[i];
    if (!(parent->active & bit) || parent->path_depth >= alt->count) continue;
    if (alt->segments[parent->path_depth].kind == JSONPATH_STREAM_FILTER)
      bits |= bit;
  }
  return bits;
}

/* Evaluate every pending filter predicate against a scalar candidate and
 * return the subset of alternatives that pass (union fan-out keeps a node once
 * per passing alternative). */
static unsigned long long jsonpath_stream_filter_pass_bits(
    const json_path_stream_t *stream, unsigned long long bits,
    const jsonpath_runtime_value_t *candidate) {
  unsigned long long pass = 0;
  if (!stream || !candidate) return 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    const jsonpath_stream_alternative_t *alt;
    const jsonpath_stream_segment_t *segment;
    if (!(bits & bit)) continue;
    alt = &stream->alternatives[i];
    if (alt->count == 0) continue;
    segment = &alt->segments[alt->count - 1U];
    if (segment->kind == JSONPATH_STREAM_FILTER &&
        jsonpath_program_compare_values(candidate, &segment->filter.constant,
                                        segment->filter.op))
      pass |= bit;
  }
  return pass;
}

static int jsonpath_stream_callback_failed(json_path_stream_t *stream) {
  return jsonpath_stream_fail(stream, "JSONPath stream callback failed");
}

static int jsonpath_stream_match_start(json_path_stream_t *stream,
                                       unsigned long long selected,
                                       json_type_t type) {
  if (!selected || !stream->handler.on_match_start) return 0;
  return stream->handler.on_match_start(stream->ctx, type) == 0
             ? 0
             : jsonpath_stream_callback_failed(stream);
}

static unsigned int jsonpath_stream_popcount(unsigned long long value) {
  unsigned int count = 0;
  while (value) {
    value &= value - 1U;
    ++count;
  }
  return count;
}

/* Hold a candidate value byte slice (raw number token or string) until the
 * filter decision is known at the closing event. Short values stay inline;
 * longer ones get a transient allocation freed after the decision. */
static int jsonpath_stream_filter_hold(json_path_stream_t *stream, const char *value,
                                       size_t len) {
  free(stream->pending_filter_held_string_dyn);
  stream->pending_filter_held_string_dyn = NULL;
  stream->pending_filter_held_string_len = len;
  /* The held copy is NUL-terminated so scalar predicates can reuse the same
   * strcmp-based comparison as the DOM path. */
  if (len + 1U <= sizeof(stream->pending_filter_held_string)) {
    memcpy(stream->pending_filter_held_string, value, len);
    stream->pending_filter_held_string[len] = '\0';
  } else {
    char *copy = (char *)malloc(len + 1U);
    if (!copy) return -1;
    memcpy(copy, value, len);
    copy[len] = '\0';
    stream->pending_filter_held_string_dyn = copy;
  }
  return 0;
}

/* Decide a scalar filter candidate at its closing event: emit the match only
 * when at least one pending alternative passed, then count each passing
 * alternative (union fan-out). The value itself is delivered once. */
static int jsonpath_stream_filter_finish(json_path_stream_t *stream, json_type_t type) {
  unsigned long long pass = stream->pending_filter_pass;
  const int held_kind = stream->pending_filter_held_kind;
  const char *held_text;
  size_t held_len;
  stream->pending_filter_valid = false;
  if (!pass) return 0;
  if (stream->handler.on_match_start &&
      stream->handler.on_match_start(stream->ctx, type) != 0)
    return jsonpath_stream_callback_failed(stream);
  held_text = stream->pending_filter_held_string_len < sizeof(stream->pending_filter_held_string)
                  ? stream->pending_filter_held_string
                  : stream->pending_filter_held_string_dyn;
  held_len = stream->pending_filter_held_string_len;
  switch (held_kind) {
  case JSON_NULL:
    if (stream->handler.events.on_null &&
        stream->handler.events.on_null(stream->ctx) != 0)
      return jsonpath_stream_callback_failed(stream);
    break;
  case JSON_BOOL:
    if (stream->handler.events.on_bool &&
        stream->handler.events.on_bool(stream->ctx, stream->pending_filter_held_bool) != 0)
      return jsonpath_stream_callback_failed(stream);
    break;
  case JSON_NUMBER:
    if (stream->handler.events.on_number &&
        stream->handler.events.on_number(stream->ctx, held_text, held_len) != 0)
      return jsonpath_stream_callback_failed(stream);
    break;
  case JSON_STRING:
    if (stream->handler.events.on_string &&
        stream->handler.events.on_string(stream->ctx, held_text, held_len) != 0)
      return jsonpath_stream_callback_failed(stream);
    break;
  default:
    break;
  }
  free(stream->pending_filter_held_string_dyn);
  stream->pending_filter_held_string_dyn = NULL;
  if (stream->handler.on_match_end &&
      stream->handler.on_match_end(stream->ctx, type) != 0)
    return jsonpath_stream_callback_failed(stream);
  stream->matches += jsonpath_stream_popcount(pass);
  return 0;
}

static int jsonpath_stream_match_end(json_path_stream_t *stream,
                                     unsigned long long selected,
                                     json_type_t type) {
  if (stream->pending_filter_valid)
    return jsonpath_stream_filter_finish(stream, type);
  if (!selected) return 0;
  if (stream->handler.on_match_end &&
      stream->handler.on_match_end(stream->ctx, type) != 0)
    return jsonpath_stream_callback_failed(stream);
  /* RFC 9535 2.5.1.2: a union keeps a node as many times as selectors match
   * it. Each selected bit is one alternative that reached this value, so a
   * fan-out such as ['a','a'] or [0,0] counts once per alternative. The value
   * itself is still delivered to the callbacks a single time. */
  stream->matches += jsonpath_stream_popcount(selected);
  return 0;
}

static int jsonpath_stream_prepare_value(json_path_stream_t *stream,
                                         unsigned long long *active,
                                         unsigned long long *selected,
                                         bool *forward, size_t *path_depth) {
  jsonpath_stream_frame_t *parent;
  if (!stream || !active || !selected || !forward || !path_depth)
    return jsonpath_stream_fail(stream, "Invalid JSONPath stream state");

  if (stream->depth == 0) {
    if (stream->root_started)
      return jsonpath_stream_fail(stream, "Unexpected second JSON root value");
    stream->root_started = true;
    stream->pending_filter_valid = false;
    *active = jsonpath_stream_all_alternatives(stream);
    *path_depth = 0;
    *selected = jsonpath_stream_selected(stream, *active, *path_depth);
    *forward = *selected != 0;
    return 0;
  }

  parent = &stream->frames[stream->depth - 1U];
  *path_depth = parent->path_depth + 1U;
  if (parent->object) {
    if (!stream->pending_valid)
      return jsonpath_stream_fail(stream, "Object value has no matching key state");
    *active = stream->pending_active;
    stream->pending_valid = false;
  } else {
    unsigned long long filter_bits;
    size_t index;
    if (parent->index == SIZE_MAX)
      return jsonpath_stream_fail(stream, "JSON array index overflow");
    index = parent->index++;
    filter_bits = jsonpath_stream_filter_bits(stream, parent);
    if (filter_bits != 0) {
      /* Every child of a filtered container is a candidate whose decision is
       * deferred to its closing event; key/index matching does not apply. */
      stream->pending_filter_valid = true;
      stream->pending_filter_bits = filter_bits;
      stream->pending_filter_pass = 0;
      *active = 0;
    } else {
      stream->pending_filter_valid = false;
      *active = jsonpath_stream_child_active(
          stream, parent->active, parent->path_depth, false, NULL, 0, 0, index);
    }
  }
  *selected = jsonpath_stream_selected(stream, *active, *path_depth);
  *forward = parent->forward || *selected != 0;
  return 0;
}

static int jsonpath_stream_container_start(json_path_stream_t *stream,
                                           json_type_t type) {
  unsigned long long active;
  unsigned long long selected;
  bool forward;
  size_t path_depth;
  jsonpath_stream_frame_t *frame;
  int callback_result = 0;

  if (stream->depth >= JSONPATH_STREAM_MAX_DEPTH)
    return jsonpath_stream_fail(stream, "JSONPath stream depth exceeded");
  if (jsonpath_stream_prepare_value(stream, &active, &selected, &forward,
                                    &path_depth) != 0)
    return -1;
  if (stream->pending_filter_valid) {
    /* A container candidate never satisfies a scalar predicate: drop the
     * whole subtree (the frame still tracks depth so children stay inert). */
    stream->pending_filter_valid = false;
    forward = false;
    active = 0;
    selected = 0;
  }
  if (jsonpath_stream_match_start(stream, selected, type) != 0) return -1;
  if (forward) {
    if (type == JSON_OBJECT && stream->handler.events.on_object_start)
      callback_result = stream->handler.events.on_object_start(stream->ctx);
    else if (type == JSON_ARRAY && stream->handler.events.on_array_start)
      callback_result = stream->handler.events.on_array_start(stream->ctx);
  }
  if (callback_result != 0) return jsonpath_stream_callback_failed(stream);

  frame = &stream->frames[stream->depth++];
  memset(frame, 0, sizeof(*frame));
  frame->active = active;
  frame->selected = selected;
  frame->forward = forward;
  frame->object = type == JSON_OBJECT;
  frame->path_depth = path_depth;
  return 0;
}

static int jsonpath_stream_container_end(json_path_stream_t *stream,
                                         json_type_t type) {
  jsonpath_stream_frame_t frame;
  int callback_result = 0;
  if (!stream || stream->depth == 0)
    return jsonpath_stream_fail(stream, "Unexpected JSON container end");
  frame = stream->frames[stream->depth - 1U];
  if (frame.object != (type == JSON_OBJECT))
    return jsonpath_stream_fail(stream, "Mismatched JSON container end");
  if (frame.forward) {
    if (type == JSON_OBJECT && stream->handler.events.on_object_end)
      callback_result = stream->handler.events.on_object_end(stream->ctx);
    else if (type == JSON_ARRAY && stream->handler.events.on_array_end)
      callback_result = stream->handler.events.on_array_end(stream->ctx);
  }
  if (callback_result != 0) return jsonpath_stream_callback_failed(stream);
  if (jsonpath_stream_match_end(stream, frame.selected, type) != 0) return -1;
  --stream->depth;
  return 0;
}

static int jsonpath_stream_scalar_start(json_path_stream_t *stream,
                                        json_type_t type,
                                        unsigned long long *selected,
                                        bool *forward) {
  unsigned long long active;
  size_t path_depth;
  if (jsonpath_stream_prepare_value(stream, &active, selected, forward,
                                    &path_depth) != 0)
    return -1;
  return jsonpath_stream_match_start(stream, *selected, type);
}

static int jsonpath_stream_on_null(void *ctx) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_NULL, &selected, &forward) != 0)
    return -1;
  if (stream->pending_filter_valid) {
    jsonpath_runtime_value_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type = JSONPATH_RT_INVALID;
    stream->pending_filter_held_kind = JSON_NULL;
    stream->pending_filter_pass = jsonpath_stream_filter_pass_bits(
        stream, stream->pending_filter_bits, &candidate);
    return jsonpath_stream_match_end(stream, selected, JSON_NULL);
  }
  if (forward && stream->handler.events.on_null &&
      stream->handler.events.on_null(stream->ctx) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_NULL);
}

static int jsonpath_stream_on_bool(void *ctx, bool value) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_BOOL, &selected, &forward) != 0)
    return -1;
  if (stream->pending_filter_valid) {
    jsonpath_runtime_value_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type = JSONPATH_TOKEN_BOOL;
    candidate.num = value ? 1 : 0;
    stream->pending_filter_held_kind = JSON_BOOL;
    stream->pending_filter_held_bool = value;
    stream->pending_filter_pass = jsonpath_stream_filter_pass_bits(
        stream, stream->pending_filter_bits, &candidate);
    return jsonpath_stream_match_end(stream, selected, JSON_BOOL);
  }
  if (forward && stream->handler.events.on_bool &&
      stream->handler.events.on_bool(stream->ctx, value) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_BOOL);
}

static int jsonpath_stream_on_number(void *ctx, const char *value, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_NUMBER, &selected, &forward) != 0)
    return -1;
  if (stream->pending_filter_valid) {
    jsonpath_runtime_value_t candidate;
    char numbuf[64];
    size_t numlen = len < sizeof(numbuf) - 1U ? len : sizeof(numbuf) - 1U;
    memcpy(numbuf, value, numlen);
    numbuf[numlen] = '\0';
    memset(&candidate, 0, sizeof(candidate));
    candidate.type = JSONPATH_TOKEN_NUMBER;
    candidate.number = strtod(numbuf, NULL);
    if (jsonpath_stream_filter_hold(stream, value, len) != 0)
      return jsonpath_stream_callback_failed(stream);
    stream->pending_filter_held_kind = JSON_NUMBER;
    stream->pending_filter_pass = jsonpath_stream_filter_pass_bits(
        stream, stream->pending_filter_bits, &candidate);
    return jsonpath_stream_match_end(stream, selected, JSON_NUMBER);
  }
  if (forward && stream->handler.events.on_number &&
      stream->handler.events.on_number(stream->ctx, value, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_NUMBER);
}

static int jsonpath_stream_on_string(void *ctx, const char *value, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_STRING, &selected, &forward) != 0)
    return -1;
  if (stream->pending_filter_valid) {
    jsonpath_runtime_value_t candidate;
    if (jsonpath_stream_filter_hold(stream, value, len) != 0)
      return jsonpath_stream_callback_failed(stream);
    memset(&candidate, 0, sizeof(candidate));
    candidate.type = JSONPATH_TOKEN_STRING;
    candidate.str = stream->pending_filter_held_string_len + 1U <=
                            sizeof(stream->pending_filter_held_string)
                        ? stream->pending_filter_held_string
                        : stream->pending_filter_held_string_dyn;
    stream->pending_filter_held_kind = JSON_STRING;
    stream->pending_filter_pass = jsonpath_stream_filter_pass_bits(
        stream, stream->pending_filter_bits, &candidate);
    return jsonpath_stream_match_end(stream, selected, JSON_STRING);
  }
  if (forward && stream->handler.events.on_string &&
      stream->handler.events.on_string(stream->ctx, value, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_STRING);
}

static int jsonpath_stream_on_object_start(void *ctx) {
  return jsonpath_stream_container_start((json_path_stream_t *)ctx, JSON_OBJECT);
}

static int jsonpath_stream_on_array_start(void *ctx) {
  return jsonpath_stream_container_start((json_path_stream_t *)ctx, JSON_ARRAY);
}

static int jsonpath_stream_on_object_key(void *ctx, const char *key, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  jsonpath_stream_frame_t *frame;
  if (!stream || stream->depth == 0)
    return jsonpath_stream_fail(stream, "Object key outside an object");
  frame = &stream->frames[stream->depth - 1U];
  if (!frame->object)
    return jsonpath_stream_fail(stream, "Object key inside an array");
  if (frame->forward && stream->handler.events.on_object_key &&
      stream->handler.events.on_object_key(stream->ctx, key, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  {
    unsigned long long filter_bits = jsonpath_stream_filter_bits(stream, frame);
    if (filter_bits != 0) {
      stream->pending_filter_valid = true;
      stream->pending_filter_bits = filter_bits;
      stream->pending_filter_pass = 0;
      stream->pending_active = 0;
    } else {
      stream->pending_filter_valid = false;
      stream->pending_active = jsonpath_stream_child_active(
          stream, frame->active, frame->path_depth, true, key, len,
          json_object_key_hash(key, len), 0);
    }
  }
  stream->pending_valid = true;
  return 0;
}

static int jsonpath_stream_on_object_end(void *ctx) {
  return jsonpath_stream_container_end((json_path_stream_t *)ctx, JSON_OBJECT);
}

static int jsonpath_stream_on_array_end(void *ctx) {
  return jsonpath_stream_container_end((json_path_stream_t *)ctx, JSON_ARRAY);
}

json_path_stream_t *json_path_stream_create(
    const json_path_program_t *program,
    const json_path_stream_handler_t *handler, void *ctx) {
  const jsonpath_stream_alternative_t empty = {0};
  const json_sax_handler_raw_t sax_handler = {
      .on_null = jsonpath_stream_on_null,
      .on_bool = jsonpath_stream_on_bool,
      .on_number = jsonpath_stream_on_number,
      .on_string = jsonpath_stream_on_string,
      .on_object_start = jsonpath_stream_on_object_start,
      .on_object_key = jsonpath_stream_on_object_key,
      .on_object_end = jsonpath_stream_on_object_end,
      .on_array_start = jsonpath_stream_on_array_start,
      .on_array_end = jsonpath_stream_on_array_end,
  };
  json_path_stream_t *stream;

  if (!program || !handler) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  stream = (json_path_stream_t *)calloc(1, sizeof(*stream));
  if (!stream) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  stream->program = program;
  stream->handler = *handler;
  stream->ctx = ctx;
  if (!jsonpath_stream_collect(stream, program->entry, &empty) ||
      stream->alternative_count == 0) {
    free(stream);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
             "JSONPath expression is not streamable; use the DOM query API");
    return NULL;
  }
  /* A streamable filter must be the terminal segment of every alternative:
   * the single-pass matcher can only decide a candidate at its closing event.
   * Mixed filter/key alternatives or filters followed by more segments are
   * rejected here instead of silently mis-selecting. */
  {
    int any_filter = 0;
    for (size_t a = 0; a < stream->alternative_count; ++a) {
      const jsonpath_stream_alternative_t *alt = &stream->alternatives[a];
      for (size_t s = 0; s < alt->count; ++s) {
        if (alt->segments[s].kind == JSONPATH_STREAM_FILTER) {
          any_filter = 1;
          if (s != alt->count - 1U) {
            free(stream);
            snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
                     "JSONPath filter must be the final segment to stream; "
                     "not streamable; use the DOM query API");
            return NULL;
          }
        }
      }
    }
    if (any_filter) {
      for (size_t a = 0; a < stream->alternative_count; ++a) {
        const jsonpath_stream_alternative_t *alt = &stream->alternatives[a];
        if (alt->count == 0 || alt->segments[alt->count - 1U].kind != JSONPATH_STREAM_FILTER) {
          free(stream);
          snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
                   "JSONPath expression mixes filter and non-filter selectors; "
                   "not streamable; use the DOM query API");
          return NULL;
        }
      }
    }
  }
  stream->parser = json_sax_parser_create_raw(&sax_handler, stream);
  if (!stream->parser) {
    free(stream);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
             "Unable to create JSONPath stream parser");
    return NULL;
  }
  g_jsonpath_error[0] = '\0';
  return stream;
}

int json_path_stream_feed(json_path_stream_t *stream, const char *data, size_t len) {
  int result;
  if (!stream || (!data && len > 0)) {
    if (stream) jsonpath_stream_fail(stream, "Invalid arguments");
    return -1;
  }
  if (stream->failed) return -1;
  result = json_sax_parser_feed(stream->parser, data, len);
  if (result != 0 && !stream->failed) {
    const char *error = json_sax_parser_error(stream->parser);
    jsonpath_stream_fail(stream, error && *error ? error : "JSON stream parse failed");
  }
  return result;
}

int json_path_stream_finish(json_path_stream_t *stream) {
  int result;
  if (!stream) return -1;
  if (stream->failed) return -1;
  result = json_sax_parser_finish(stream->parser);
  if (result != 0 && !stream->failed) {
    const char *error = json_sax_parser_error(stream->parser);
    jsonpath_stream_fail(stream, error && *error ? error : "JSON stream finish failed");
  }
  return result;
}

size_t json_path_stream_match_count(const json_path_stream_t *stream) {
  return stream ? stream->matches : 0;
}

const char *json_path_stream_error(const json_path_stream_t *stream) {
  if (!stream) return g_jsonpath_error[0] ? g_jsonpath_error : NULL;
  return stream->error[0] ? stream->error : NULL;
}

void json_path_stream_destroy(json_path_stream_t *stream) {
  if (!stream) return;
  free(stream->pending_filter_held_string_dyn);
  json_sax_parser_destroy(stream->parser);
  free(stream);
}

static const json_value_t *jsonpath_program_match_next(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only);

static int jsonpath_program_value_to_runtime(const json_value_t *value,
                                             jsonpath_runtime_value_t *out) {
  if (!value || !out) return 0;
  memset(out, 0, sizeof(*out));
  switch (json_type(value)) {
  case JSON_BOOL:
    out->type = JSONPATH_TOKEN_BOOL;
    out->num = json_bool(value) ? 1 : 0;
    return 1;
  case JSON_NUMBER:
    out->type = JSONPATH_TOKEN_NUMBER;
    out->number = json_number(value);
    return 1;
  case JSON_STRING:
    out->type = JSONPATH_TOKEN_STRING;
    out->str = json_string(value);
    return 1;
  default:
    return 0;
  }
}

static size_t jsonpath_program_count_path(const json_path_program_t *program, uint32_t index,
                                          const json_value_t *root, const json_value_t *cur);

/* UTF-8 code-point count for string length(): SIMDe 16-byte scan with a
 * scalar tail, matching the scalar variant for every byte pattern. */
static size_t jsonpath_utf8_length(const char *str, size_t len) {
  return jsonpath_utf8_length_simde(str, len);
}

static int jsonpath_vm_truthy(const jsonpath_runtime_value_t *value) {
  if (!value) return 0;
  switch (value->type) {
  case JSONPATH_RT_INVALID: return 0;
  case JSONPATH_TOKEN_BOOL: return value->num != 0;
  case JSONPATH_TOKEN_NUMBER: return value->number != 0.0;
  case JSONPATH_TOKEN_STRING:
    return value->str != NULL && value->str[0] != '\0';
  default: return 0;
  }
}

static const json_value_t *jsonpath_program_eval_path(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  if (!instruction) return NULL;
  if (instruction->type == JSONPATH_TOKEN_ROOT)
    return jsonpath_program_match_next(program, instruction->down, root, root, NULL, 1);
  if (instruction->type == JSONPATH_TOKEN_THIS)
    return jsonpath_program_match_next(program, instruction->down, root, cur, NULL, 1);
  return NULL;
}

static int jsonpath_program_resolve(const json_path_program_t *program, uint32_t index,
                                    const json_value_t *root, const json_value_t *cur,
                                    jsonpath_runtime_value_t *out) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const json_value_t *value;
  if (!instruction || !out) return 0;
  if (instruction->type == JSONPATH_TOKEN_ROOT || instruction->type == JSONPATH_TOKEN_THIS) {
    value = jsonpath_program_eval_path(program, index, root, cur);
    return jsonpath_program_value_to_runtime(value, out);
  }
  memset(out, 0, sizeof(*out));
  out->type = instruction->type;
  switch (instruction->type) {
  case JSONPATH_TOKEN_BOOL: out->num = instruction->num; break;
  case JSONPATH_TOKEN_NUMBER: out->number = instruction->number; break;
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_REGEXP: out->str = jsonpath_program_string(program, instruction); break;
  default: break;
  }
  return 1;
}

static int jsonpath_program_apply_cmp(int cmp, int opcode);

static int jsonpath_program_compare_values(const jsonpath_runtime_value_t *left,
                                           const jsonpath_runtime_value_t *right,
                                           int opcode) {
  int cmp = 0;
  if (!left || !right || left->type != right->type) return 0;
  switch (left->type) {
  case JSONPATH_TOKEN_BOOL:
    cmp = left->num - right->num;
    break;
  case JSONPATH_TOKEN_NUMBER:
    if (left->number < right->number) cmp = -1;
    else if (left->number > right->number) cmp = 1;
    break;
  case JSONPATH_TOKEN_STRING:
    cmp = strcmp(left->str ? left->str : "", right->str ? right->str : "");
    break;
  default:
    return 0;
  }
  return jsonpath_program_apply_cmp(cmp, opcode);
}

/* Map a three-way comparison result to a boolean under opcode. */
static int jsonpath_program_apply_cmp(int cmp, int opcode) {
  switch (opcode) {
  case JSONPATH_TOKEN_EQ: return cmp == 0;
  case JSONPATH_TOKEN_NE: return cmp != 0;
  case JSONPATH_TOKEN_LT: return cmp < 0;
  case JSONPATH_TOKEN_LE: return cmp <= 0;
  case JSONPATH_TOKEN_GT: return cmp > 0;
  case JSONPATH_TOKEN_GE: return cmp >= 0;
  default: return 0;
  }
}

static int jsonpath_program_compare(const json_path_program_t *program, uint32_t index,
                                    const json_value_t *root, const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const jsonpath_instruction_t *left_instruction;
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  if (!instruction || instruction->down == JSONPATH_NO_INDEX ||
      !(left_instruction = jsonpath_program_instruction(program, instruction->down)) ||
      left_instruction->sibling == JSONPATH_NO_INDEX ||
      !jsonpath_program_resolve(program, instruction->down, root, cur, &left) ||
      !jsonpath_program_resolve(program, left_instruction->sibling, root, cur, &right))
    return 0;
  return jsonpath_program_compare_values(&left, &right, instruction->type);
}

static void jsonpath_program_value_to_string(const jsonpath_runtime_value_t *value,
                                             char *buf, size_t len) {
  if (!value || !buf || len == 0) return;
  switch (value->type) {
  case JSONPATH_TOKEN_BOOL: snprintf(buf, len, "%s", value->num ? "true" : "false"); break;
  case JSONPATH_TOKEN_NUMBER: snprintf(buf, len, "%.17g", value->number); break;
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_REGEXP: snprintf(buf, len, "%s", value->str ? value->str : ""); break;
  default: buf[0] = '\0'; break;
  }
}

static int jsonpath_program_regex_values(const json_path_program_t *program,
                                           uint32_t regex_index,
                                           const jsonpath_runtime_value_t *left,
                                           const jsonpath_runtime_value_t *right,
                                           int full_match) {
  char left_buf[64];
  char right_buf[64];
  const char *haystack;
  const char *pattern;
  re_match_result_t match;
  re_status_t status;
  if (!left || !right) return 0;
  jsonpath_program_value_to_string(left, left_buf, sizeof(left_buf));
  jsonpath_program_value_to_string(right, right_buf, sizeof(right_buf));
  haystack = left->str && left->type == JSONPATH_TOKEN_STRING ? left->str : left_buf;
  pattern = right->str && (right->type == JSONPATH_TOKEN_STRING ||
                           right->type == JSONPATH_TOKEN_REGEXP)
                ? right->str
                : right_buf;
  if (left->type == JSONPATH_TOKEN_REGEXP) {
    haystack = right->str && right->type == JSONPATH_TOKEN_STRING ? right->str : right_buf;
    pattern = left->str ? left->str : "";
  }
  if (!haystack || !pattern) return 0;
  /* Prefer the compile-time regex handle; fall back to one-shot validation
   * when the pattern is a runtime value or the pool cap was exceeded. */
  if (regex_index != JSONPATH_NO_REGEX) {
    re_t compiled = program->regexes[regex_index];
    if (!compiled) return 0;
    status = re_matchn(compiled, haystack, strlen(haystack), NULL, &match);
  } else {
    status = re_match_n(pattern, strlen(pattern), haystack, strlen(haystack), NULL, &match);
  }
  if (status != RE_STATUS_OK) return 0;
  /* RFC 9535 match(): the regex must cover the entire string; search() only
   * requires the pattern to appear somewhere. */
  if (full_match) {
    const size_t haystack_len = strlen(haystack);
    return match.index == 0 && match.length == haystack_len;
  }
  return 1;
}

static int jsonpath_program_match_values(const json_path_program_t *program,
                                         uint32_t regex_index,
                                         const jsonpath_runtime_value_t *left,
                                         const jsonpath_runtime_value_t *right) {
  return jsonpath_program_regex_values(program, regex_index, left, right, 0);
}

static int jsonpath_program_match_like(const json_path_program_t *program, uint32_t index,
                                       const json_value_t *root, const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const jsonpath_instruction_t *left_instruction;
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  if (!instruction || instruction->down == JSONPATH_NO_INDEX ||
      !(left_instruction = jsonpath_program_instruction(program, instruction->down)) ||
      left_instruction->sibling == JSONPATH_NO_INDEX ||
      !jsonpath_program_resolve(program, instruction->down, root, cur, &left) ||
      !jsonpath_program_resolve(program, left_instruction->sibling, root, cur, &right))
    return 0;
  return jsonpath_program_match_values(program, instruction->regex_index, &left, &right);
}

static int jsonpath_program_contains_values(const jsonpath_runtime_value_t *left,
                                              const jsonpath_runtime_value_t *right) {
  char left_buf[64];
  char right_buf[64];
  const char *haystack;
  const char *needle;
  if (!left || !right) return 0;
  jsonpath_program_value_to_string(left, left_buf, sizeof(left_buf));
  jsonpath_program_value_to_string(right, right_buf, sizeof(right_buf));
  haystack = left->str && left->type == JSONPATH_TOKEN_STRING ? left->str : left_buf;
  needle = right->str && right->type == JSONPATH_TOKEN_STRING ? right->str : right_buf;
  if (!haystack) haystack = "";
  if (!needle) needle = "";
  return jsonpath_contains_simde(haystack, strlen(haystack), needle, strlen(needle));
}

/* ASCII case-insensitive substring containment (non-standard contains_ci()).
 * Byte-oriented; only A-Z/a-z folding applies, matching the re engine's
 * byte-level model. An empty needle is contained (mirrors contains()). */
static int jsonpath_program_contains_ci_values(const jsonpath_runtime_value_t *left,
                                               const jsonpath_runtime_value_t *right) {
  char left_buf[64];
  char right_buf[64];
  const char *haystack;
  const char *needle;
  size_t haystack_len;
  size_t needle_len;
  size_t i;
  if (!left || !right) return 0;
  jsonpath_program_value_to_string(left, left_buf, sizeof(left_buf));
  jsonpath_program_value_to_string(right, right_buf, sizeof(right_buf));
  haystack = left->str && left->type == JSONPATH_TOKEN_STRING ? left->str : left_buf;
  needle = right->str && right->type == JSONPATH_TOKEN_STRING ? right->str : right_buf;
  if (!haystack) haystack = "";
  if (!needle) needle = "";
  haystack_len = strlen(haystack);
  needle_len = strlen(needle);
  if (needle_len == 0) return 1;
  if (needle_len > haystack_len) return 0;
  for (i = 0; i + needle_len <= haystack_len; ++i) {
    size_t j;
    for (j = 0; j < needle_len; ++j) {
      unsigned char h = (unsigned char)haystack[i + j];
      unsigned char n = (unsigned char)needle[j];
      if (h >= 'A' && h <= 'Z') h = (unsigned char)(h + 32);
      if (n >= 'A' && n <= 'Z') n = (unsigned char)(n + 32);
      if (h != n) break;
    }
    if (j == needle_len) return 1;
  }
  return 0;
}

static int jsonpath_program_match_contains(const json_path_program_t *program,
                                           uint32_t index, const json_value_t *root,
                                           const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const jsonpath_instruction_t *left_instruction;
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  if (!instruction || instruction->down == JSONPATH_NO_INDEX ||
      !(left_instruction = jsonpath_program_instruction(program, instruction->down)) ||
      left_instruction->sibling == JSONPATH_NO_INDEX ||
      !jsonpath_program_resolve(program, instruction->down, root, cur, &left) ||
      !jsonpath_program_resolve(program, left_instruction->sibling, root, cur, &right))
    return 0;
  return jsonpath_program_contains_values(&left, &right);
}

static int jsonpath_program_expr_recursive(const json_path_program_t *program, uint32_t index,
                                           const json_value_t *root, const json_value_t *cur,
                                           int array_index, const char *key,
                                           json_path_result_t *execution);

/* Leaf executor helpers shared by the dispatch loop and the single-instruction
 * fast path (slot 0 result, no loop/switch overhead for the common shapes). */
static void jsonpath_vm_exec_cmp_leaf(const json_path_program_t *program,
                                      const jsonpath_vm_insn_t *insn,
                                      const json_value_t *root, const json_value_t *cur,
                                      jsonpath_runtime_value_t *value) {
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  memset(&left, 0, sizeof(left));
  memset(&right, 0, sizeof(right));
  if (!jsonpath_program_resolve(program, insn->src1, root, cur, &left))
    left.type = JSONPATH_RT_INVALID;
  if (!jsonpath_program_resolve(program, insn->src2, root, cur, &right))
    right.type = JSONPATH_RT_INVALID;
  memset(value, 0, sizeof(*value));
  value->type = JSONPATH_TOKEN_BOOL;
  value->num = jsonpath_program_compare_values(&left, &right, (int)insn->arg) ? 1 : 0;
}

static void jsonpath_vm_exec_cmp_leaf_num(const json_path_program_t *program,
                                          const jsonpath_vm_insn_t *insn,
                                          const json_value_t *root, const json_value_t *cur,
                                          jsonpath_runtime_value_t *value) {
  jsonpath_runtime_value_t left;
  const jsonpath_instruction_t *literal = jsonpath_program_instruction(program, insn->src2);
  int cmp;
  memset(&left, 0, sizeof(left));
  memset(value, 0, sizeof(*value));
  value->type = JSONPATH_TOKEN_BOOL;
  value->num = 0;
  if (!literal || !jsonpath_program_resolve(program, insn->src1, root, cur, &left) ||
      left.type != JSONPATH_TOKEN_NUMBER)
    return;
  cmp = left.number < literal->number ? -1 : (left.number > literal->number ? 1 : 0);
  value->num = jsonpath_program_apply_cmp(cmp, (int)insn->arg) ? 1 : 0;
}

static void jsonpath_vm_exec_cmp_leaf_str(const json_path_program_t *program,
                                          const jsonpath_vm_insn_t *insn,
                                          const json_value_t *root, const json_value_t *cur,
                                          jsonpath_runtime_value_t *value) {
  jsonpath_runtime_value_t left;
  const jsonpath_instruction_t *literal = jsonpath_program_instruction(program, insn->src2);
  const char *rhs = literal ? jsonpath_program_string(program, literal) : NULL;
  int cmp;
  memset(&left, 0, sizeof(left));
  memset(value, 0, sizeof(*value));
  value->type = JSONPATH_TOKEN_BOOL;
  value->num = 0;
  if (!rhs || !jsonpath_program_resolve(program, insn->src1, root, cur, &left) ||
      left.type != JSONPATH_TOKEN_STRING)
    return;
  cmp = strcmp(left.str ? left.str : "", rhs);
  value->num = jsonpath_program_apply_cmp(cmp, (int)insn->arg) ? 1 : 0;
}

typedef struct jsonpath_qvm_context_s {
  const json_path_program_t *program;
  const json_value_t *root;
  const json_value_t *cur;
} jsonpath_qvm_context_t;

static int jsonpath_qvm_resolve(void *opaque, uint32_t operand,
                                qvm_value_t *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  return jsonpath_program_resolve(ctx->program, operand, ctx->root, ctx->cur,
                                  out);
}

static int jsonpath_qvm_truthy(void *opaque, const qvm_value_t *value) {
  (void)opaque;
  return jsonpath_vm_truthy(value);
}

static int jsonpath_qvm_binary(void *opaque, qvm_opcode_t op, uint32_t arg,
                               const qvm_value_t *left,
                               const qvm_value_t *right, qvm_value_t *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  int result;
  switch (op) {
  case QVM_OP_CMP:
    result = jsonpath_program_compare_values(left, right, (int)arg);
    break;
  case QVM_OP_MATCH:
    result = jsonpath_program_match_values(ctx->program, arg, left, right);
    break;
  case QVM_OP_CONTAINS:
    result = jsonpath_program_contains_values(left, right);
    break;
  case QVM_OP_CONTAINS_CI:
    result = jsonpath_program_contains_ci_values(left, right);
    break;
  case QVM_OP_MATCH_FULL:
    result = jsonpath_program_regex_values(ctx->program, arg, left, right, 1);
    break;
  case QVM_OP_SEARCH:
    result = jsonpath_program_regex_values(ctx->program, arg, left, right, 0);
    break;
  default:
    return 0;
  }
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_BOOL;
  out->num = result ? 1 : 0;
  return 1;
}

static int jsonpath_qvm_exists(void *opaque, uint32_t operand, int *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  *out = jsonpath_program_eval_path(ctx->program, operand, ctx->root,
                                    ctx->cur) != NULL;
  return 1;
}

static int jsonpath_qvm_length(void *opaque, uint32_t operand,
                               qvm_value_t *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  const json_value_t *node =
      operand == QVM_NO_OPERAND
          ? ctx->cur
          : jsonpath_program_eval_path(ctx->program, operand, ctx->root,
                                       ctx->cur);
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_RT_INVALID;
  if (!node) return 1;
  if (json_type(node) == JSON_ARRAY) {
    out->type = JSONPATH_TOKEN_NUMBER;
    out->number = (double)json_array_size(node);
  } else if (json_type(node) == JSON_OBJECT) {
    out->type = JSONPATH_TOKEN_NUMBER;
    out->number = (double)json_object_size(node);
  } else if (json_type(node) == JSON_STRING) {
    out->type = JSONPATH_TOKEN_NUMBER;
    out->number =
        (double)jsonpath_utf8_length(json_string(node), json_string_len(node));
  }
  return 1;
}

static int jsonpath_qvm_count(void *opaque, uint32_t operand,
                              qvm_value_t *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_NUMBER;
  out->number = (double)jsonpath_program_count_path(
      ctx->program, operand, ctx->root, ctx->cur);
  return 1;
}

static int jsonpath_qvm_leaf(void *opaque, qvm_opcode_t op, uint32_t arg,
                             uint32_t src1, uint32_t src2, qvm_value_t *out) {
  const jsonpath_qvm_context_t *ctx = (const jsonpath_qvm_context_t *)opaque;
  qvm_instruction_t insn = {(uint8_t)op, 0, 0, arg, src1, src2};
  const jsonpath_instruction_t *literal;
  qvm_value_t measured;
  double actual;
  int cmp;
  switch (op) {
  case QVM_OP_CMP_LEAF:
    jsonpath_vm_exec_cmp_leaf(ctx->program, &insn, ctx->root, ctx->cur, out);
    return 1;
  case QVM_OP_CMP_LEAF_NUMBER:
    jsonpath_vm_exec_cmp_leaf_num(ctx->program, &insn, ctx->root, ctx->cur,
                                  out);
    return 1;
  case QVM_OP_CMP_LEAF_STRING:
    jsonpath_vm_exec_cmp_leaf_str(ctx->program, &insn, ctx->root, ctx->cur,
                                  out);
    return 1;
  case QVM_OP_CMP_LENGTH_LEAF:
    if (!jsonpath_qvm_length(opaque, src1, &measured)) return 0;
    break;
  case QVM_OP_CMP_COUNT_LEAF:
    if (!jsonpath_qvm_count(opaque, src1, &measured)) return 0;
    break;
  default:
    return 0;
  }
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_BOOL;
  literal = jsonpath_program_instruction(ctx->program, src2);
  if (!literal || measured.type != JSONPATH_TOKEN_NUMBER) return 1;
  actual = measured.number;
  cmp = actual < literal->number ? -1 : (actual > literal->number ? 1 : 0);
  out->num = jsonpath_program_apply_cmp(cmp, (int)arg) ? 1 : 0;
  return 1;
}

static void jsonpath_qvm_make_invalid(void *opaque, qvm_value_t *out) {
  (void)opaque;
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_RT_INVALID;
}

static void jsonpath_qvm_make_bool(void *opaque, int value, qvm_value_t *out) {
  (void)opaque;
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_BOOL;
  out->num = value ? 1 : 0;
}

static void jsonpath_qvm_make_number(void *opaque, double value,
                                     qvm_value_t *out) {
  (void)opaque;
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_NUMBER;
  out->number = value;
}

static void jsonpath_qvm_make_string(void *opaque, const char *value,
                                     size_t len, qvm_value_t *out) {
  (void)opaque;
  memset(out, 0, sizeof(*out));
  out->type = JSONPATH_TOKEN_STRING;
  out->str = value;
  out->length = len;
}

static const qvm_exec_ops_t jsonpath_qvm_ops = {
    jsonpath_qvm_resolve,      jsonpath_qvm_truthy,
    jsonpath_qvm_binary,       NULL,
    jsonpath_qvm_exists,       jsonpath_qvm_length,
    jsonpath_qvm_count,        jsonpath_qvm_leaf,
    jsonpath_qvm_make_invalid, jsonpath_qvm_make_bool,
    jsonpath_qvm_make_number,  jsonpath_qvm_make_string};

static int jsonpath_vm_run(const json_path_program_t *program, uint32_t offset, uint32_t len,
                           const json_value_t *root, const json_value_t *cur,
                           int array_index, const char *key,
                           json_path_result_t *execution) {
  jsonpath_qvm_context_t context = {program, root, cur};
  qvm_exec_input_t input = {array_index, key, key ? strlen(key) : 0};
  qvm_value_t result;
  if (!program || !program->expr_vm || offset >= program->expr_vm_count)
    return 0;
  qvm_diagnostic_t diagnostic = {0};
  int status = qvm_execute_ex(program->expr_vm, program->expr_vm_count, offset, len,
                              &jsonpath_qvm_ops, &context, &input, &result,
                              &program->qvm_limits,
                              program->qvm_diagnostics ? &diagnostic : NULL);
  if (status != QVM_STATUS_OK) {
    if (execution) {
      execution->qvm_status = (qvm_status_t)status;
      if (program->qvm_diagnostics)
        execution->qvm_diagnostic = diagnostic;
      else {
        jsonpath_qvm_diagnostic_clear(&execution->qvm_diagnostic);
        execution->qvm_diagnostic.status = (qvm_status_t)status;
        execution->qvm_diagnostic.message = "JSONPath query VM execution failed";
      }
    }
    return 0;
  }
  return jsonpath_vm_truthy(&result);
}

static int jsonpath_program_expr(const json_path_program_t *program, uint32_t index,
                                 const json_value_t *root, const json_value_t *cur,
                                 int array_index, const char *key,
                                 json_path_result_t *execution) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  if (!instruction) return 0;
  if (program->expr_vm && instruction->expr_vm_offset != JSONPATH_NO_INDEX &&
      instruction->expr_vm_len > 0) {
    return jsonpath_vm_run(program, instruction->expr_vm_offset, instruction->expr_vm_len,
                           root, cur, array_index, key, execution);
  }
  return jsonpath_program_expr_recursive(program, index, root, cur, array_index, key,
                                         execution);
}

static int jsonpath_program_expr_recursive(const json_path_program_t *program, uint32_t index,
                                 const json_value_t *root, const json_value_t *cur,
                                 int array_index, const char *key,
                                 json_path_result_t *execution) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  uint32_t child;
  if (!instruction) return 0;
  switch (instruction->type) {
  case JSONPATH_TOKEN_WILDCARD: return 1;
  case JSONPATH_TOKEN_EQ:
  case JSONPATH_TOKEN_NE:
  case JSONPATH_TOKEN_LT:
  case JSONPATH_TOKEN_LE:
  case JSONPATH_TOKEN_GT:
  case JSONPATH_TOKEN_GE:
    return jsonpath_program_compare(program, index, root, cur);
  case JSONPATH_TOKEN_MATCH:
    return jsonpath_program_match_like(program, index, root, cur);
  case JSONPATH_TOKEN_CONTAINS:
    return jsonpath_program_match_contains(program, index, root, cur);
  case JSONPATH_TOKEN_ROOT:
  case JSONPATH_TOKEN_THIS:
    return jsonpath_program_eval_path(program, index, root, cur) != NULL;
  case JSONPATH_TOKEN_NOT:
    return instruction->down != JSONPATH_NO_INDEX &&
           !jsonpath_program_expr(program, instruction->down, root, cur, array_index, key,
                                  execution);
  case JSONPATH_TOKEN_AND:
    for (child = instruction->down; child != JSONPATH_NO_INDEX;) {
      if (!jsonpath_program_expr(program, child, root, cur, array_index, key, execution)) return 0;
      child = jsonpath_program_instruction(program, child)->sibling;
    }
    return 1;
  case JSONPATH_TOKEN_OR:
  case JSONPATH_TOKEN_UNION:
    for (child = instruction->down; child != JSONPATH_NO_INDEX;) {
      if (jsonpath_program_expr(program, child, root, cur, array_index, key, execution)) return 1;
      child = jsonpath_program_instruction(program, child)->sibling;
    }
    return 0;
  case JSONPATH_TOKEN_STRING: {
    const char *text = jsonpath_program_string(program, instruction);
    return key && text && instruction->string_len == strlen(key) &&
           memcmp(text, key, instruction->string_len) == 0;
  }
  case JSONPATH_TOKEN_NUMBER: return array_index == instruction->num;
  default: return 0;
  }
}

static const json_value_t *jsonpath_program_match_expr(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const json_value_t *matched = NULL;
  uint32_t selector;
  int is_union;
  if (!instruction || !cur) return NULL;

  /* RFC 9535 2.5.1.2: a bracketed selection evaluates each selector
   * independently and concatenates the per-selector nodelists in the order the
   * selectors appear, keeping a node matched by several selectors as many
   * times as it matches. For a single instruction the same loop runs once. */
  is_union = instruction->type == JSONPATH_TOKEN_UNION;
  selector = is_union ? instruction->down : index;
  while (selector != JSONPATH_NO_INDEX) {
    size_t count;
    size_t i;
    if (json_type(cur) == JSON_OBJECT) {
      count = json_object_size(cur);
      for (i = 0; i < count; ++i) {
        const char *key = json_object_key(cur, i);
        const json_value_t *value = json_object_value(cur, i);
        if (jsonpath_program_expr(program, selector, root, value, -1, key, result)) {
          matched = jsonpath_program_match_next(program, instruction->sibling, root, value,
                                                result, first_only);
          if (matched && first_only) return matched;
        }
      }
    } else if (json_type(cur) == JSON_ARRAY) {
      count = json_array_size(cur);
      for (i = 0; i < count; ++i) {
        const json_value_t *value = json_array_get(cur, i);
        if (jsonpath_program_expr(program, selector, root, value, (int)i, NULL, result)) {
          matched = jsonpath_program_match_next(program, instruction->sibling, root, value,
                                                result, first_only);
          if (matched && first_only) return matched;
        }
      }
    }
    if (!is_union) break;
    selector = jsonpath_program_instruction(program, selector)->sibling;
  }
  return matched;
}

static const json_value_t *jsonpath_program_match_descendant(
    const json_path_program_t *program, uint32_t selector, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only) {
  const json_value_t *matched;
  if (!cur) return NULL;
  /* RFC 9535 2.5.2: the descendant segment visits the input node itself and
   * every descendant, applying the selector to each visited node in document
   * order (depth-first preorder). The selector resumes with the continuation
   * through its rewired sibling. Each recursion step applies the selector to
   * the node it receives exactly once, so no visited node is matched twice. */
  matched = jsonpath_program_match_next(program, selector, root, cur, result, first_only);
  if (matched && first_only) return matched;
  if (json_type(cur) == JSON_OBJECT) {
    const size_t count = json_object_size(cur);
    size_t i;
    for (i = 0; i < count; ++i) {
      const json_value_t *child = json_object_value(cur, i);
      matched = jsonpath_program_match_descendant(program, selector, root, child,
                                                  result, first_only);
      if (matched && first_only) return matched;
    }
  } else if (json_type(cur) == JSON_ARRAY) {
    const size_t count = json_array_size(cur);
    size_t i;
    for (i = 0; i < count; ++i) {
      const json_value_t *child = json_array_get(cur, i);
      matched = jsonpath_program_match_descendant(program, selector, root, child,
                                                  result, first_only);
      if (matched && first_only) return matched;
    }
  }
  return NULL;
}

static const json_value_t *jsonpath_program_match_next(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only) {
  const jsonpath_instruction_t *instruction;
  const json_value_t *next;
  int array_index;
  size_t len;
  if (!cur) return NULL;
  if (index == JSONPATH_NO_INDEX) {
    jsonpath_result_push(result, cur);
    return cur;
  }
  instruction = jsonpath_program_instruction(program, index);
  if (!instruction) return NULL;
  switch (instruction->type) {
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_LABEL: {
    const char *key = jsonpath_program_string(program, instruction);
    if (!key) return NULL;
    next = json_object_get_hashed_v(
        cur, vstr_from_buf(key, instruction->string_len), instruction->key_hash);
    return next ? jsonpath_program_match_next(program, instruction->sibling, root, next,
                                              result, first_only)
                : NULL;
  }
  case JSONPATH_TOKEN_NUMBER:
    if (json_type(cur) != JSON_ARRAY) return NULL;
    array_index = instruction->num;
    len = json_array_size(cur);
    if (array_index < 0) array_index += (int)len;
    if (array_index < 0 || (size_t)array_index >= len) return NULL;
    next = json_array_get(cur, (size_t)array_index);
    return jsonpath_program_match_next(program, instruction->sibling, root, next, result,
                                       first_only);
  case JSONPATH_TOKEN_SLICE:
    if (json_type(cur) != JSON_ARRAY) return NULL;
    {
      /* RFC 9535 2.3.4: slice [start:end:step]. Omitted components use the
       * defaults below; step 0 yields an empty list. Indices iterate in
       * ascending order for step > 0 and descending order for step < 0,
       * resuming the remaining segments for every selected element. */
      len = json_array_size(cur);
      const long long step = (instruction->slice_mask & JSONPATH_SLICE_HAS_STEP)
                                 ? (long long)instruction->num3
                                 : 1;
      long long start;
      long long end;
      long long lower;
      long long upper;
      long long i;
      const json_value_t *matched = NULL;
      if (step == 0) return NULL;
      if (step >= 0) {
        start = (instruction->slice_mask & JSONPATH_SLICE_HAS_START)
                    ? (long long)instruction->num
                    : 0;
        end = (instruction->slice_mask & JSONPATH_SLICE_HAS_END)
                  ? (long long)instruction->num2
                  : (long long)len;
      } else {
        start = (instruction->slice_mask & JSONPATH_SLICE_HAS_START)
                    ? (long long)instruction->num
                    : (long long)len - 1;
        end = (instruction->slice_mask & JSONPATH_SLICE_HAS_END)
                  ? (long long)instruction->num2
                  : -(long long)len - 1;
      }
      start = start >= 0 ? start : (long long)len + start;
      end = end >= 0 ? end : (long long)len + end;
      if (step >= 0) {
        lower = start < 0 ? 0 : (start > (long long)len ? (long long)len : start);
        upper = end < 0 ? 0 : (end > (long long)len ? (long long)len : end);
      } else {
        lower = end < -1 ? -1 : (end > (long long)len - 1 ? (long long)len - 1 : end);
        upper = start < -1 ? -1 : (start > (long long)len - 1 ? (long long)len - 1 : start);
      }
      if (lower >= upper) return NULL;
      for (i = step > 0 ? lower : upper;; i += step) {
        if (step > 0 && i >= upper) break;
        if (step < 0 && i <= lower) break;
        next = json_array_get(cur, (size_t)i);
        matched = jsonpath_program_match_next(program, instruction->sibling, root, next,
                                              result, first_only);
        if (matched && first_only) return matched;
      }
      return matched;
    }
  case JSONPATH_TOKEN_DESCENDANT:
    return jsonpath_program_match_descendant(program, instruction->down, root, cur,
                                             result, first_only);
  default:
    return jsonpath_program_match_expr(program, index, root, cur, result, first_only);
  }
}

/* Number of nodes selected by a THIS/ROOT path expression (RFC 9535 count()).
 * Reuses the proven path walker into a transient result; the allocation is
 * bounded by the selected nodelist size. */
static size_t jsonpath_program_count_path(const json_path_program_t *program, uint32_t index,
                                          const json_value_t *root, const json_value_t *cur) {
  const jsonpath_instruction_t *instruction = jsonpath_program_instruction(program, index);
  json_path_result_t result;
  if (!instruction) return 0;
  if (instruction->type != JSONPATH_TOKEN_ROOT &&
      instruction->type != JSONPATH_TOKEN_THIS)
    return 0;
  /* Reuse the proven path walker in count-only mode: terminal matches bump a
   * counter without allocating a node array (RFC 9535 count()). */
  memset(&result, 0, sizeof(result));
  result.count_only = 1;
  jsonpath_program_match_next(program, instruction->down,
                              instruction->type == JSONPATH_TOKEN_ROOT ? root : cur,
                              instruction->type == JSONPATH_TOKEN_ROOT ? root : cur,
                              &result, 0);
  return result.count;
}


json_path_result_t *json_path_query_compiled_ex(const json_value_t *root,
                                                const json_path_program_t *program,
                                                qvm_diagnostic_t *diagnostic) {
  json_path_result_t *result;
  if (!root || !program) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  jsonpath_qvm_diagnostic_clear(diagnostic);
  result = (json_path_result_t *)calloc(1, sizeof(*result));
  if (!result) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  jsonpath_program_match_next(program, program->entry, root, root, result, 0);
  if (result->qvm_status != QVM_STATUS_OK) {
    const char *message = result->qvm_diagnostic.message;
    if (diagnostic) *diagnostic = result->qvm_diagnostic;
    json_path_result_free(result);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "%s",
             message ? message : "JSONPath query VM execution failed");
    return NULL;
  }
  if (result->error) {
    json_path_result_free(result);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  g_jsonpath_error[0] = '\0';
  return result;
}

json_path_result_t *json_path_query_compiled(const json_value_t *root,
                                             const json_path_program_t *program) {
  return json_path_query_compiled_ex(root, program, NULL);
}

json_value_t *json_path_get_compiled_ex(const json_value_t *root,
                                        const json_path_program_t *program,
                                        qvm_diagnostic_t *diagnostic) {
  const json_value_t *value;
  json_path_result_t execution;
  if (!root || !program) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  memset(&execution, 0, sizeof(execution));
  execution.count_only = 1;
  jsonpath_qvm_diagnostic_clear(diagnostic);
  value = jsonpath_program_match_next(program, program->entry, root, root, &execution, 1);
  if (execution.qvm_status != QVM_STATUS_OK) {
    if (diagnostic) *diagnostic = execution.qvm_diagnostic;
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "%s",
             execution.qvm_diagnostic.message ? execution.qvm_diagnostic.message
                                              : "JSONPath query VM execution failed");
    return NULL;
  }
  g_jsonpath_error[0] = '\0';
  return (json_value_t *)value;
}

json_value_t *json_path_get_compiled(const json_value_t *root,
                                     const json_path_program_t *program) {
  return json_path_get_compiled_ex(root, program, NULL);
}

json_path_result_t *json_path_query(const json_value_t *root, const char *expr) {
  json_path_program_t *program;
  json_path_result_t *result;

  if (!root || !expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }

  program = json_path_compile(expr);
  if (!program) return NULL;
  result = json_path_query_compiled(root, program);
  json_path_program_free(program);
  return result;
}

json_value_t *json_path_get(const json_value_t *root, const char *expr) {
  json_path_program_t *program;
  json_value_t *value;
  if (!root || !expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  program = json_path_compile(expr);
  if (!program) return NULL;
  value = json_path_get_compiled(root, program);
  json_path_program_free(program);
  return value;
}

size_t json_path_result_size(const json_path_result_t *result) {
  return result ? result->count : 0;
}

json_value_t *json_path_result_get(const json_path_result_t *result, size_t index) {
  if (!result || index >= result->count) return NULL;
  return result->items[index];
}

void json_path_result_free(json_path_result_t *result) {
  if (!result) return;
  free(result->items);
  free(result);
}

const char *json_path_get_error(void) { return g_jsonpath_error[0] ? g_jsonpath_error : NULL; }
