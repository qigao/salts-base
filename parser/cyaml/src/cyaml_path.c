#include "cyaml_internal.h"
#include "cyaml_ypath_internal.h"
#include "cyaml_ypath_expr_parser.h"
#include "cyaml_ypath_lexer.h"
#include "cyaml_ypath_grammar_gen.h"
#include "re.h"
#include <math.h>
#include <string.h>

// #region Constants

#define YPATH_STACK_INIT_CAP 32
#define YPATH_NODEBUF_INIT 16
#define YPATH_NUM_BUF_SIZE 64

// #endregion

// #region Value Types

typedef enum {
    YPATH_VAL_NULL,
    YPATH_VAL_BOOL,
    YPATH_VAL_INT,
    YPATH_VAL_FLOAT,
    YPATH_VAL_STR,
    YPATH_VAL_NODES
} ypath_val_type_t;

typedef struct {
    cyaml_node_t** nodes;
    uint32_t count;
    uint32_t cap;
} ypath_nodebuf_t;

typedef struct {
    ypath_val_type_t type;
    union {
        bool b;
        int64_t i;
        double f;
        struct {
            const char* s;
            uint32_t len;
        } str;
        ypath_nodebuf_t nodes;
    } v;
} ypath_val_t;

// #endregion

// #region Parser

typedef struct {
    ypath_lexer_t lex;
    ypath_ast_storage_t* pool;
    const char* error;
    uint32_t error_pos;
} ypath_parser_t;

#define YPATH_PARSE_SIGNED_INT(p, out, flag, flags, on_err) \
    do {                                                    \
        if ((p)->lex.tok.type == YPATH_TOK_INT) {           \
            (out) = (p)->lex.tok.val.i;                     \
            (flags) |= (flag);                              \
            ypath_lex_next(&(p)->lex);                      \
        } else if ((p)->lex.tok.type == YPATH_TOK_MINUS) {  \
            ypath_lex_next(&(p)->lex);                      \
            if ((p)->lex.tok.type != YPATH_TOK_INT) {       \
                ypath_parse_err(p, "expected integer");     \
                on_err;                                     \
            }                                               \
            (out) = -(p)->lex.tok.val.i;                    \
            (flags) |= (flag);                              \
            ypath_lex_next(&(p)->lex);                      \
        }                                                   \
    } while (0)

static inline void ypath_parse_err(ypath_parser_t* p, const char* msg)
{
    if (!p->error) {
        p->error = msg;
        p->error_pos = (uint32_t)(p->lex.tok.start - p->lex.src);
    }
}

static inline ypath_expr_t* ypath_pool_expr(ypath_ast_storage_t* p, ypath_expr_type_t type)
{
    if (p->expr_count >= YPATH_POOL_EXPR_CAP)
        return NULL;
    ypath_expr_t* e = &p->exprs[p->expr_count++];
    memset(e, 0, sizeof(*e));
    e->type = type;
    return e;
}

static inline ypath_step_t* ypath_pool_step(ypath_ast_storage_t* p)
{
    if (p->step_count >= YPATH_POOL_STEP_CAP)
        return NULL;
    ypath_step_t* s = &p->steps[p->step_count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static bool ypath_expect(ypath_parser_t* p, ypath_tok_t t)
{
    if (p->lex.tok.type == t) {
        ypath_lex_next(&p->lex);
        return true;
    }
    ypath_parse_err(p, "unexpected token");
    return false;
}

static ypath_expr_t* ypath_parse_expr(ypath_parser_t* p);

static ypath_step_t* ypath_add_step(ypath_parser_t* p, ypath_path_t* path)
{
    if (path->count >= YPATH_MAX_STEPS) {
        ypath_parse_err(p, "too many steps");
        return NULL;
    }
    ypath_step_t* s = &path->steps[path->count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static bool ypath_parse_bracket(ypath_parser_t* p, ypath_path_t* path)
{
    ypath_lex_next(&p->lex);

    if (p->lex.tok.type == YPATH_TOK_QUESTION) {
        p->lex.in_filter = true;
        ypath_lex_next(&p->lex);
        ypath_expr_t* f = ypath_parse_expr(p);
        p->lex.in_filter = false;
        if (!f)
            return false;
        ypath_step_t* s = ypath_add_step(p, path);
        if (!s)
            return false;
        s->type = YPATH_STEP_FILTER;
        s->v.filter = f;
    } else if (p->lex.tok.type == YPATH_TOK_INT || p->lex.tok.type == YPATH_TOK_COLON || p->lex.tok.type == YPATH_TOK_MINUS) {
        int64_t start = 0, end = 0, step_val = 1;
        uint8_t flags = 0;
        bool is_slice = false;

        YPATH_PARSE_SIGNED_INT(p, start, YPATH_SLICE_HAS_START, flags, return false);

        if (p->lex.tok.type == YPATH_TOK_COLON) {
            is_slice = true;
            ypath_lex_next(&p->lex);
            YPATH_PARSE_SIGNED_INT(p, end, YPATH_SLICE_HAS_END, flags, return false);
            if (p->lex.tok.type == YPATH_TOK_COLON) {
                ypath_lex_next(&p->lex);
                YPATH_PARSE_SIGNED_INT(p, step_val, YPATH_SLICE_HAS_STEP, flags, return false);
            }
        }

        ypath_step_t* s = ypath_add_step(p, path);
        if (!s)
            return false;
        if (is_slice) {
            s->type = YPATH_STEP_SLICE;
            s->v.slice.start = start;
            s->v.slice.end = end;
            s->v.slice.step = step_val;
            s->v.slice.flags = flags;
        } else {
            s->type = YPATH_STEP_INDEX;
            s->v.idx = start;
        }
    } else if (p->lex.tok.type == YPATH_TOK_STAR) {
        ypath_lex_next(&p->lex);
        ypath_step_t* s = ypath_add_step(p, path);
        if (!s)
            return false;
        s->type = YPATH_STEP_WILDCARD;
    } else {
        ypath_parse_err(p, "expected index, slice, or filter");
        return false;
    }

    return ypath_expect(p, YPATH_TOK_RBRACKET);
}

static bool ypath_parse_step(ypath_parser_t* p, ypath_path_t* path)
{
    ypath_step_t* s;
    switch (p->lex.tok.type) {
    case YPATH_TOK_DOT:
        ypath_lex_next(&p->lex);
        if (!(s = ypath_add_step(p, path)))
            return false;
        s->type = YPATH_STEP_IDENTITY;
        break;
    case YPATH_TOK_DOTDOT:
        ypath_lex_next(&p->lex);
        if (!(s = ypath_add_step(p, path)))
            return false;
        s->type = YPATH_STEP_PARENT;
        break;
    case YPATH_TOK_STARSTAR:
        ypath_lex_next(&p->lex);
        if (!(s = ypath_add_step(p, path)))
            return false;
        s->type = YPATH_STEP_RECURSIVE;
        break;
    case YPATH_TOK_STAR:
        ypath_lex_next(&p->lex);
        if (p->lex.tok.type == YPATH_TOK_IDENT) {
            if (!(s = ypath_add_step(p, path)))
                return false;
            s->type = YPATH_STEP_ALIAS;
            s->v.name.s = p->lex.tok.start;
            s->v.name.len = p->lex.tok.len;
            ypath_lex_next(&p->lex);
        } else {
            if (!(s = ypath_add_step(p, path)))
                return false;
            s->type = YPATH_STEP_WILDCARD;
        }
        break;
    case YPATH_TOK_IDENT:
    case YPATH_TOK_STRING:
    case YPATH_TOK_INT:
        if (!(s = ypath_add_step(p, path)))
            return false;
        s->type = YPATH_STEP_NAME;
        s->v.name.s = p->lex.tok.start;
        s->v.name.len = p->lex.tok.len;
        ypath_lex_next(&p->lex);
        break;
    case YPATH_TOK_LBRACKET:
        if (!ypath_parse_bracket(p, path))
            return false;
        break;
    default:
        ypath_parse_err(p, "expected step");
        return false;
    }

    while (p->lex.tok.type == YPATH_TOK_LBRACKET)
        if (!ypath_parse_bracket(p, path))
            return false;
    return true;
}

static bool ypath_parse_path_steps(ypath_parser_t* p, ypath_path_t* path)
{
    if (p->lex.tok.type == YPATH_TOK_EOF || p->lex.tok.type == YPATH_TOK_RPAREN || p->lex.tok.type == YPATH_TOK_RBRACKET)
        return true;
    if (!ypath_parse_step(p, path))
        return false;
    while (p->lex.tok.type == YPATH_TOK_SLASH) {
        ypath_lex_next(&p->lex);
        if (!ypath_parse_step(p, path))
            return false;
    }
    return true;
}

static ypath_expr_t* ypath_parse_primary(ypath_parser_t* p)
{
    ypath_expr_t* e;
    switch (p->lex.tok.type) {
    case YPATH_TOK_INT:
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_INT)))
            return NULL;
        e->v.i = p->lex.tok.val.i;
        ypath_lex_next(&p->lex);
        return e;
    case YPATH_TOK_FLOAT:
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_FLOAT)))
            return NULL;
        e->v.f = p->lex.tok.val.f;
        ypath_lex_next(&p->lex);
        return e;
    case YPATH_TOK_STRING:
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_STRING)))
            return NULL;
        e->v.str.s = p->lex.tok.start;
        e->v.str.len = p->lex.tok.len;
        ypath_lex_next(&p->lex);
        return e;
    case YPATH_TOK_TRUE:
    case YPATH_TOK_FALSE:
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_BOOL)))
            return NULL;
        e->v.b = (p->lex.tok.type == YPATH_TOK_TRUE);
        ypath_lex_next(&p->lex);
        return e;
    case YPATH_TOK_NULL:
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_NULL)))
            return NULL;
        ypath_lex_next(&p->lex);
        return e;
    case YPATH_TOK_AT: {
        ypath_lex_next(&p->lex);
        if (!(e = ypath_pool_expr(p->pool, YPATH_EXPR_PATH)))
            return NULL;
        ypath_step_t* first = &p->pool->steps[p->pool->step_count];
        uint32_t start_count = p->pool->step_count;
        while (p->lex.tok.type == YPATH_TOK_SLASH || p->lex.tok.type == YPATH_TOK_DOT || p->lex.tok.type == YPATH_TOK_LBRACKET) {
            if (p->lex.tok.type == YPATH_TOK_SLASH || p->lex.tok.type == YPATH_TOK_DOT)
                ypath_lex_next(&p->lex);
            if (p->lex.tok.type == YPATH_TOK_LBRACKET) {
                ypath_path_t tmp = { .count = 0 };
                if (!ypath_parse_bracket(p, &tmp))
                    return NULL;
                ypath_step_t* s = ypath_pool_step(p->pool);
                if (!s)
                    return NULL;
                *s = tmp.steps[0];
            } else {
                ypath_step_t* s = ypath_pool_step(p->pool);
                if (!s)
                    return NULL;
                switch (p->lex.tok.type) {
                case YPATH_TOK_IDENT:
                case YPATH_TOK_STRING:
                    s->type = YPATH_STEP_NAME;
                    s->v.name.s = p->lex.tok.start;
                    s->v.name.len = p->lex.tok.len;
                    ypath_lex_next(&p->lex);
                    break;
                default:
                    ypath_parse_err(p, "expected path step");
                    return NULL;
                }
            }
        }
        e->v.path.steps = first;
        e->v.path.count = p->pool->step_count - start_count;
        return e;
    }
    default:
        ypath_parse_err(p, "expected expression");
        return NULL;
    }
}

ypath_expr_t* cyaml_ypath_expr_make_unary(
    ypath_expr_parse_ctx_t* ctx, ypath_op_t op, ypath_expr_t* arg)
{
    ypath_expr_t* expr = ypath_pool_expr(ctx->storage, YPATH_EXPR_UNARY);
    if (!expr) {
        cyaml_ypath_expr_set_error(ctx, "expression too complex");
        return NULL;
    }
    expr->v.unary.op = op;
    expr->v.unary.arg = arg;
    return expr;
}

ypath_expr_t* cyaml_ypath_expr_make_binary(
    ypath_expr_parse_ctx_t* ctx, ypath_op_t op, ypath_expr_t* left, ypath_expr_t* right)
{
    ypath_expr_t* expr = ypath_pool_expr(ctx->storage, YPATH_EXPR_BINARY);
    if (!expr) {
        cyaml_ypath_expr_set_error(ctx, "expression too complex");
        return NULL;
    }
    expr->v.binary.op = op;
    expr->v.binary.left = left;
    expr->v.binary.right = right;
    return expr;
}

ypath_expr_t* cyaml_ypath_expr_make_cond(
    ypath_expr_parse_ctx_t* ctx, ypath_expr_t* cond, ypath_expr_t* then_expr,
    ypath_expr_t* else_expr)
{
    ypath_expr_t* expr = ypath_pool_expr(ctx->storage, YPATH_EXPR_COND);
    if (!expr) {
        cyaml_ypath_expr_set_error(ctx, "expression too complex");
        return NULL;
    }
    expr->v.cond.cond = cond;
    expr->v.cond.then_expr = then_expr;
    expr->v.cond.else_expr = else_expr;
    return expr;
}

void cyaml_ypath_expr_set_error(ypath_expr_parse_ctx_t* ctx, const char* error)
{
    if (!ctx->error) {
        ctx->error = error;
        ctx->error_pos = ctx->token_pos;
    }
}

static int ypath_expr_token_id(ypath_tok_t token)
{
    switch (token) {
    case YPATH_TOK_OR: return YPATH_EXPR_TOKEN_OR;
    case YPATH_TOK_AND: return YPATH_EXPR_TOKEN_AND;
    case YPATH_TOK_EQ: return YPATH_EXPR_TOKEN_EQ;
    case YPATH_TOK_NE: return YPATH_EXPR_TOKEN_NE;
    case YPATH_TOK_LT: return YPATH_EXPR_TOKEN_LT;
    case YPATH_TOK_LE: return YPATH_EXPR_TOKEN_LE;
    case YPATH_TOK_GT: return YPATH_EXPR_TOKEN_GT;
    case YPATH_TOK_GE: return YPATH_EXPR_TOKEN_GE;
    case YPATH_TOK_PLUS: return YPATH_EXPR_TOKEN_PLUS;
    case YPATH_TOK_MINUS: return YPATH_EXPR_TOKEN_MINUS;
    case YPATH_TOK_STAR: return YPATH_EXPR_TOKEN_MUL;
    case YPATH_TOK_DIV: return YPATH_EXPR_TOKEN_DIV;
    case YPATH_TOK_BANG: return YPATH_EXPR_TOKEN_BANG;
    case YPATH_TOK_LPAREN: return YPATH_EXPR_TOKEN_LPAREN;
    case YPATH_TOK_RPAREN: return YPATH_EXPR_TOKEN_RPAREN;
    case YPATH_TOK_BAND: return YPATH_EXPR_TOKEN_BAND;
    case YPATH_TOK_BOR: return YPATH_EXPR_TOKEN_BOR;
    case YPATH_TOK_CARET: return YPATH_EXPR_TOKEN_CARET;
    case YPATH_TOK_LSHIFT: return YPATH_EXPR_TOKEN_LSHIFT;
    case YPATH_TOK_RSHIFT: return YPATH_EXPR_TOKEN_RSHIFT;
    case YPATH_TOK_TILDE: return YPATH_EXPR_TOKEN_TILDE;
    case YPATH_TOK_QUESTION: return YPATH_EXPR_TOKEN_QUESTION;
    case YPATH_TOK_COLON: return YPATH_EXPR_TOKEN_COLON;
    case YPATH_TOK_COMMA: return YPATH_EXPR_TOKEN_COMMA;
    case YPATH_TOK_IDIV: return YPATH_EXPR_TOKEN_IDIV;
    case YPATH_TOK_MATCHES: return YPATH_EXPR_TOKEN_MATCHES;
    default: return 0;
    }
}

static ypath_expr_t* ypath_parse_expr(ypath_parser_t* p)
{
    ypath_expr_parse_ctx_t ctx = { .storage = p->pool };
    void* lemon = YPathExprParseAlloc(malloc);
    if (!lemon) {
        ypath_parse_err(p, "out of memory");
        return NULL;
    }

    while (p->lex.tok.type != YPATH_TOK_RBRACKET && p->lex.tok.type != YPATH_TOK_EOF) {
        ypath_expr_token_t token = { .start = p->lex.tok.start };
        ctx.token_pos = (uint32_t)(p->lex.tok.start - p->lex.src);

        int token_id = ypath_expr_token_id(p->lex.tok.type);
        if (token_id != 0) {
            YPathExprParse(lemon, token_id, token, &ctx);
            ypath_lex_next(&p->lex);
        } else {
            token.expr = ypath_parse_primary(p);
            if (!token.expr)
                break;
            YPathExprParse(lemon, YPATH_EXPR_TOKEN_PRIMARY, token, &ctx);
        }

        if (ctx.error)
            break;
    }

    if (!ctx.error && !p->error) {
        ypath_expr_token_t end_token = { .start = p->lex.tok.start };
        ctx.token_pos = (uint32_t)(p->lex.tok.start - p->lex.src);
        YPathExprParse(lemon, 0, end_token, &ctx);
    }
    YPathExprParseFree(lemon, free);

    if (ctx.error) {
        if (!p->error) {
            p->error = ctx.error;
            p->error_pos = ctx.error_pos;
        }
        return NULL;
    }
    return p->error ? NULL : ctx.root;
}

static bool ypath_parse_path(ypath_parser_t* p, ypath_path_t* path)
{
    memset(path, 0, sizeof(*path));
    if (p->lex.tok.type == YPATH_TOK_SLASH) {
        path->absolute = true;
        ypath_lex_next(&p->lex);
    }
    return ypath_parse_path_steps(p, path);
}

static bool ypath_compile_path(ypath_ast_t* ast, ypath_step_t* steps,
                               uint32_t count, const qvm_limits_t* limits,
                               qvm_diagnostic_t* diagnostic);

static bool ypath_expr_index(const ypath_ast_t* ast, const ypath_expr_t* expr,
                             uint32_t* out)
{
    if (!ast || !expr || expr < ast->storage.exprs ||
        expr >= ast->storage.exprs + YPATH_POOL_EXPR_CAP)
        return false;
    *out = (uint32_t)(expr - ast->storage.exprs);
    return *out < ast->storage.expr_count;
}

static bool ypath_vm_emit(ypath_ast_t* ast, qvm_opcode_t op, uint16_t dst,
                          uint32_t arg, uint32_t src1, uint32_t src2,
                          uint32_t* out)
{
    if (ast->vm_count >= YPATH_VM_INSN_CAP)
        return false;
    ast->vm[ast->vm_count] = (qvm_instruction_t){
        (uint8_t)op, 0, dst, arg, src1, src2};
    if (out)
        *out = ast->vm_count;
    ast->vm_count++;
    return true;
}

static bool ypath_compile_expr(ypath_ast_t* ast, const ypath_expr_t* expr,
                               uint16_t dst, uint16_t* next)
{
    uint32_t operand;
    uint32_t jump;
    uint32_t end_jump;
    uint16_t left_reg;
    uint16_t right_reg;
    if (!ypath_expr_index(ast, expr, &operand))
        return false;
    switch (expr->type) {
    case YPATH_EXPR_INT:
    case YPATH_EXPR_FLOAT:
    case YPATH_EXPR_STRING:
    case YPATH_EXPR_BOOL:
    case YPATH_EXPR_NULL:
    case YPATH_EXPR_PATH:
        return ypath_vm_emit(ast, QVM_OP_LOAD_CONST, dst, QVM_NO_OPERAND,
                             operand, QVM_NO_OPERAND, NULL);
    case YPATH_EXPR_UNARY:
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        left_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.unary.arg, left_reg, next))
            return false;
        return ypath_vm_emit(ast,
                             expr->v.unary.op == YPATH_OP_NEG  ? QVM_OP_NEG
                             : expr->v.unary.op == YPATH_OP_BNOT ? QVM_OP_BNOT
                                                                 : QVM_OP_NOT,
                             dst, QVM_NO_OPERAND, left_reg, QVM_NO_OPERAND,
                             NULL);
    case YPATH_EXPR_BINARY:
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        left_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.binary.left, left_reg, next))
            return false;
        if (expr->v.binary.op == YPATH_OP_AND ||
            expr->v.binary.op == YPATH_OP_OR) {
            if (!ypath_vm_emit(ast,
                               expr->v.binary.op == YPATH_OP_AND
                                   ? QVM_OP_JMP_FALSE
                                   : QVM_OP_JMP_TRUE,
                               0, QVM_NO_OPERAND, left_reg, QVM_NO_OPERAND,
                               &jump))
                return false;
            if (*next >= QVM_MAX_REGISTERS)
                return false;
            right_reg = (*next)++;
            if (!ypath_compile_expr(ast, expr->v.binary.right, right_reg,
                                    next))
                return false;
            if (!ypath_vm_emit(ast, QVM_OP_CMP, dst,
                               (uint32_t)expr->v.binary.op, left_reg, right_reg,
                               NULL))
                return false;
            if (!ypath_vm_emit(ast, QVM_OP_JMP,
                               0, QVM_NO_OPERAND, QVM_NO_OPERAND,
                               QVM_NO_OPERAND, &end_jump))
                return false;
            ast->vm[jump].arg = ast->vm_count;
            if (!ypath_vm_emit(ast,
                               expr->v.binary.op == YPATH_OP_AND ? QVM_OP_FALSE
                                                                  : QVM_OP_TRUE,
                               dst, QVM_NO_OPERAND, QVM_NO_OPERAND,
                               QVM_NO_OPERAND, NULL))
                return false;
            ast->vm[end_jump].arg = ast->vm_count;
            return true;
        }
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        right_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.binary.right, right_reg, next))
            return false;
        return ypath_vm_emit(
            ast,
            expr->v.binary.op == YPATH_OP_ADD     ? QVM_OP_ADD
            : expr->v.binary.op == YPATH_OP_SUB   ? QVM_OP_SUB
            : expr->v.binary.op == YPATH_OP_MUL   ? QVM_OP_MUL
            : expr->v.binary.op == YPATH_OP_DIV   ? QVM_OP_DIV
            : expr->v.binary.op == YPATH_OP_IDIV  ? QVM_OP_DIV
            : expr->v.binary.op == YPATH_OP_BAND  ? QVM_OP_BAND
            : expr->v.binary.op == YPATH_OP_BOR   ? QVM_OP_BOR
            : expr->v.binary.op == YPATH_OP_BXOR  ? QVM_OP_BXOR
            : expr->v.binary.op == YPATH_OP_LSHIFT ? QVM_OP_LSHIFT
            : expr->v.binary.op == YPATH_OP_RSHIFT ? QVM_OP_RSHIFT
            : expr->v.binary.op == YPATH_OP_MATCHES ? QVM_OP_CMP
                                                    : QVM_OP_CMP,
            dst, (uint32_t)expr->v.binary.op, left_reg, right_reg, NULL);
    case YPATH_EXPR_COND: {
        uint16_t cond_reg;
        uint16_t then_reg;
        uint16_t else_reg;
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        cond_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.cond.cond, cond_reg, next))
            return false;
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        then_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.cond.then_expr, then_reg, next))
            return false;
        if (*next >= QVM_MAX_REGISTERS)
            return false;
        else_reg = (*next)++;
        if (!ypath_compile_expr(ast, expr->v.cond.else_expr, else_reg, next))
            return false;
        return ypath_vm_emit(ast, QVM_OP_SELECT, dst, else_reg, cond_reg,
                             then_reg, NULL);
    }
    }
    return false;
}

static bool ypath_compile_nested_filters(ypath_ast_t* ast,
                                         const ypath_expr_t* expr,
                                         const qvm_limits_t* limits,
                                         qvm_diagnostic_t* diagnostic)
{
    switch (expr->type) {
    case YPATH_EXPR_PATH:
        return ypath_compile_path(ast, expr->v.path.steps, expr->v.path.count,
                                  limits, diagnostic);
    case YPATH_EXPR_UNARY:
        return ypath_compile_nested_filters(ast, expr->v.unary.arg, limits,
                                             diagnostic);
    case YPATH_EXPR_BINARY:
        return ypath_compile_nested_filters(ast, expr->v.binary.left, limits,
                                             diagnostic) &&
               ypath_compile_nested_filters(ast, expr->v.binary.right, limits,
                                             diagnostic);
    case YPATH_EXPR_COND:
        return ypath_compile_nested_filters(ast, expr->v.cond.cond, limits,
                                             diagnostic) &&
               ypath_compile_nested_filters(ast, expr->v.cond.then_expr, limits,
                                             diagnostic) &&
               ypath_compile_nested_filters(ast, expr->v.cond.else_expr, limits,
                                             diagnostic);
    default:
        return true;
    }
}

static bool ypath_compile_path(ypath_ast_t* ast, ypath_step_t* steps,
                               uint32_t count, const qvm_limits_t* limits,
                               qvm_diagnostic_t* diagnostic)
{
    for (uint32_t i = 0; i < count; ++i) {
        ypath_step_t* step = &steps[i];
        uint16_t register_count = 1;
        qvm_verify_error_t error;
        uint32_t offset;
        if (step->type != YPATH_STEP_FILTER)
            continue;
        if (!ypath_compile_nested_filters(ast, step->v.filter, limits,
                                          diagnostic))
            return false;
        offset = ast->vm_count;
        if (!ypath_compile_expr(ast, step->v.filter, 0, &register_count))
            return false;
        step->v.filter_vm.expr = step->v.filter;
        step->v.filter_vm.vm_offset = offset;
        step->v.filter_vm.vm_len = ast->vm_count - offset;
        step->v.filter_vm.vm_register_count = register_count;
        if (qvm_verify_slice_ex(ast->vm, ast->vm_count, offset,
                                step->v.filter_vm.vm_len, register_count,
                                ast->storage.expr_count, 0, limits, &error) !=
            QVM_STATUS_OK)
        {
            if (diagnostic)
                *diagnostic = error;
            return false;
        }
    }
    return true;
}

bool cyaml_ypath_parse_ex(const char* source, ypath_ast_t* ast,
                          const qvm_limits_t* limits,
                          qvm_diagnostic_t* diagnostic)
{
    if (!ast)
        return false;

    memset(ast, 0, sizeof(*ast));
    ast->source = source;
    if (!source) {
        ast->error = "null argument";
        return false;
    }

    ypath_parser_t parser = { .pool = &ast->storage };
    ypath_lex_init(&parser.lex, source);
    ypath_lex_next(&parser.lex);

    if (!ypath_parse_path(&parser, &ast->path) || parser.error) {
        ast->error = parser.error ? parser.error : "parse error";
        ast->error_pos = parser.error_pos;
        return false;
    }

    if (parser.lex.tok.type != YPATH_TOK_EOF) {
        ast->error = "unexpected token";
        ast->error_pos = (uint32_t)(parser.lex.tok.start - source);
        return false;
    }

    if (diagnostic) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = QVM_STATUS_OK;
        diagnostic->instruction = QVM_NO_INSTRUCTION;
        diagnostic->opcode = QVM_NO_OPCODE;
        diagnostic->operand = QVM_NO_OPERAND;
    }
    if (!ypath_compile_path(ast, ast->path.steps, ast->path.count, limits,
                            diagnostic)) {
        ast->error = "YPATH expression exceeds query VM limits";
        ast->error_pos = 0;
        return false;
    }

    return true;
}

bool cyaml_ypath_parse(const char* source, ypath_ast_t* ast)
{
    return cyaml_ypath_parse_ex(source, ast, NULL, NULL);
}

// #endregion

// #region Evaluator

#define YPATH_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) \
                                                               : (v))
#define YPATH_STR_EQ(s1, l1, s2, l2) ((l1) == (l2) && memcmp((s1), (s2), (l1)) == 0)

typedef struct {
    const cyaml_doc_t* doc;
    const cyaml_node_t* root;
    const cyaml_node_t* current;
    const char* src;
    const ypath_ast_t* ast;
    qvm_limits_t limits;
    qvm_status_t qvm_status;
    qvm_diagnostic_t qvm_diagnostic;
    bool qvm_diagnostics;
} ypath_ctx_t;

typedef struct {
    cyaml_node_t* node;
    uint32_t child_idx;
    uint8_t phase;
} ypath_trav_frame_t;

typedef struct {
    struct {
        const ypath_step_t* steps;
        uint32_t step_count, step_idx, node_idx, child_idx;
        ypath_nodebuf_t in, out;
        const cyaml_node_t* saved_current;
    } p;
} ypath_frame_t;

#define YPATH_TRAV_INIT(stk, cnt, cap, on_fail)                            \
    ypath_trav_frame_t* stk = malloc(YPATH_STACK_INIT_CAP * sizeof(*stk)); \
    if (!stk) {                                                            \
        on_fail;                                                           \
    }                                                                      \
    size_t cnt = 0, cap = YPATH_STACK_INIT_CAP

#define YPATH_TRAV_PUSH(stk, cnt, cap, nd, on_fail)                        \
    do {                                                                   \
        if ((cnt) >= (cap)) {                                              \
            size_t nc = (cap) * 2;                                         \
            ypath_trav_frame_t* tmp = realloc((stk), nc * sizeof(*(stk))); \
            if (!tmp) {                                                    \
                on_fail;                                                   \
            }                                                              \
            (stk) = tmp;                                                   \
            (cap) = nc;                                                    \
        }                                                                  \
        (stk)[(cnt)] = (ypath_trav_frame_t) { (nd), 0, 0 };                \
        (cnt)++;                                                           \
    } while (0)

#define YPATH_TRAV_SEQ(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->seq.count) {                 \
        (cnt)--;                                              \
    } else {                                                  \
        cyaml_node_t* c = (cur)->seq.items[(f)->child_idx++]; \
        if (c)                                                \
            YPATH_TRAV_PUSH(stk, cnt, cap, c, on_fail);       \
    }

#define YPATH_TRAV_MAP_VAL(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->map.count) {                     \
        (cnt)--;                                                  \
    } else {                                                      \
        cyaml_node_t* c = (cur)->map.pairs[(f)->child_idx++].val; \
        if (c)                                                    \
            YPATH_TRAV_PUSH(stk, cnt, cap, c, on_fail);           \
    }

#define YPATH_TRAV_MAP_ALL(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->map.count) {                     \
        (cnt)--;                                                  \
    } else {                                                      \
        cyaml_node_t* k = (cur)->map.pairs[(f)->child_idx].key;   \
        cyaml_node_t* v = (cur)->map.pairs[(f)->child_idx++].val; \
        if (k)                                                    \
            YPATH_TRAV_PUSH(stk, cnt, cap, k, on_fail);           \
        if (v)                                                    \
            YPATH_TRAV_PUSH(stk, cnt, cap, v, on_fail);           \
    }

#define YPATH_PUSH_FRAME(stk, sp, cap, frame)                         \
    do {                                                              \
        if ((sp) >= (int)(cap)) {                                     \
            size_t nc = (cap) * 2;                                    \
            ypath_frame_t* tmp = realloc((stk), nc * sizeof(*(stk))); \
            if (!tmp)                                                 \
                goto cleanup;                                         \
            (stk) = tmp;                                              \
            (cap) = nc;                                               \
        }                                                             \
        (stk)[(sp)++] = (frame);                                      \
    } while (0)

#define YPATH_PUSH_VAL(vals, vp, cap, v)                              \
    do {                                                              \
        if ((vp) >= (int)(cap)) {                                     \
            size_t nc = (cap) * 2;                                    \
            ypath_val_t* tmp = realloc((vals), nc * sizeof(*(vals))); \
            if (!tmp)                                                 \
                goto cleanup;                                         \
            (vals) = tmp;                                             \
            (cap) = nc;                                               \
        }                                                             \
        (vals)[(vp)++] = (v);                                         \
    } while (0)

static inline void ypath_nodebuf_free(ypath_nodebuf_t* b) { free(b->nodes); }

static bool ypath_nodebuf_add(ypath_nodebuf_t* b, cyaml_node_t* n)
{
    for (uint32_t i = 0; i < b->count; i++)
        if (b->nodes[i] == n)
            return true;
    if (b->count >= b->cap) {
        uint32_t new_cap = b->cap ? b->cap * 2 : YPATH_NODEBUF_INIT;
        cyaml_node_t** new_nodes = realloc(b->nodes, new_cap * sizeof(*new_nodes));
        if (!new_nodes)
            return false;
        b->nodes = new_nodes;
        b->cap = new_cap;
    }
    b->nodes[b->count++] = n;
    return true;
}

static inline int64_t ypath_norm_slice_idx(int64_t idx, int64_t len)
{
    idx = YPATH_CLAMP(idx, -len, len);
    return idx < 0 ? idx + len : idx;
}

static void ypath_collect_all(cyaml_node_t* n, ypath_nodebuf_t* b)
{
    if (!n)
        return;

    YPATH_TRAV_INIT(stack, cnt, cap, return);
    YPATH_TRAV_PUSH(stack, cnt, cap, n, { free(stack); return; });

    while (cnt > 0) {
        ypath_trav_frame_t* f = &stack[cnt - 1];
        cyaml_node_t* cur = f->node;

        if (f->phase == 0) {
            ypath_nodebuf_add(b, cur);
            f->phase = 1;
        }

        if (cur->type == CYAML_SEQ)
            YPATH_TRAV_SEQ(f, cur, stack, cnt, cap, { free(stack); return; })
        else if (cur->type == CYAML_MAP)
            YPATH_TRAV_MAP_VAL(f, cur, stack, cnt, cap, { free(stack); return; })
        else
            cnt--;
    }

    free(stack);
}

static cyaml_node_t* ypath_find_parent(const cyaml_node_t* root, const cyaml_node_t* child)
{
    if (!root || root == child)
        return NULL;

    YPATH_TRAV_INIT(stack, cnt, cap, return NULL);
    cyaml_node_t* result = NULL;
    YPATH_TRAV_PUSH(stack, cnt, cap, (cyaml_node_t*)root, { free(stack); return NULL; });

    while (cnt > 0) {
        ypath_trav_frame_t* f = &stack[cnt - 1];
        cyaml_node_t* cur = f->node;

        if (cur->type == CYAML_SEQ) {
            if (f->child_idx >= cur->seq.count) {
                cnt--;
            } else {
                cyaml_node_t* c = cur->seq.items[f->child_idx++];
                if (c == child) {
                    result = cur;
                    break;
                }
                if (c)
                    YPATH_TRAV_PUSH(stack, cnt, cap, c, { free(stack); return NULL; });
            }
        } else if (cur->type == CYAML_MAP) {
            if (f->child_idx >= cur->map.count) {
                cnt--;
            } else {
                cyaml_node_t* k = cur->map.pairs[f->child_idx].key;
                cyaml_node_t* v = cur->map.pairs[f->child_idx++].val;
                if (k == child || v == child) {
                    result = cur;
                    break;
                }
                if (k)
                    YPATH_TRAV_PUSH(stack, cnt, cap, k, { free(stack); return NULL; });
                if (v)
                    YPATH_TRAV_PUSH(stack, cnt, cap, v, { free(stack); return NULL; });
            }
        } else {
            cnt--;
        }
    }

    free(stack);
    return result;
}

static cyaml_node_t* ypath_find_anchor(const cyaml_node_t* n, const char* name, uint32_t len, const char* src)
{
    if (!n)
        return NULL;

    YPATH_TRAV_INIT(stack, cnt, cap, return NULL);
    cyaml_node_t* result = NULL;
    YPATH_TRAV_PUSH(stack, cnt, cap, (cyaml_node_t*)n, { free(stack); return NULL; });

    while (cnt > 0) {
        ypath_trav_frame_t* f = &stack[cnt - 1];
        cyaml_node_t* cur = f->node;

        if (f->phase == 0) {
            if (cur->anchor.len == len && memcmp(src + cur->anchor.off, name, len) == 0) {
                result = cur;
                break;
            }
            f->phase = 1;
        }

        if (cur->type == CYAML_SEQ)
            YPATH_TRAV_SEQ(f, cur, stack, cnt, cap, { free(stack); return NULL; })
        else if (cur->type == CYAML_MAP)
            YPATH_TRAV_MAP_ALL(f, cur, stack, cnt, cap, { free(stack); return NULL; })
        else
            cnt--;
    }

    free(stack);
    return result;
}

static cyaml_node_t* ypath_resolve_alias(cyaml_node_t* n, const cyaml_node_t* root, const char* src)
{
    if (!n || n->type != CYAML_ALIAS)
        return n;
    if (n->alias.target)
        return n->alias.target;
    if (n->anchor.len == 0)
        return NULL;
    return ypath_find_anchor(root, src + n->anchor.off, n->anchor.len, src);
}

static bool ypath_str_truthy(const char* s, uint32_t len)
{
    if (len == 0)
        return false;
    if (YPATH_STR_EQ(s, len, S_NULL, L_NULL) || YPATH_STR_EQ(s, len, CYAML_S_FALSE, L_FALSE) || YPATH_STR_EQ(s, len, S_TILDE, L_TILDE) || YPATH_STR_EQ(s, len, "0", 1))
        return false;
    return true;
}

static bool ypath_str_null(const char* s, uint32_t len)
{
    return len == 0 || YPATH_STR_EQ(s, len, S_NULL, L_NULL) || YPATH_STR_EQ(s, len, S_TILDE, L_TILDE);
}

static bool ypath_val_truthy(const ypath_val_t* v, const char* src)
{
    switch (v->type) {
    case YPATH_VAL_NULL:
        return false;
    case YPATH_VAL_BOOL:
        return v->v.b;
    case YPATH_VAL_INT:
        return v->v.i != 0;
    case YPATH_VAL_FLOAT:
        return v->v.f != 0.0;
    case YPATH_VAL_STR:
        return ypath_str_truthy(v->v.str.s, v->v.str.len);
    case YPATH_VAL_NODES:
        if (v->v.nodes.count == 1 && v->v.nodes.nodes[0]->type == CYAML_SCALAR) {
            cyaml_node_t* n = v->v.nodes.nodes[0];
            return ypath_str_truthy(src + n->span.off, n->span.len);
        }
        return v->v.nodes.count > 0;
    }
    return false;
}

static double ypath_val_float(const ypath_val_t* v, const char* src)
{
    double f;
    char buf[YPATH_NUM_BUF_SIZE];
    uint32_t len;

    switch (v->type) {
    case YPATH_VAL_INT:
        return (double)v->v.i;
    case YPATH_VAL_FLOAT:
        return v->v.f;
    case YPATH_VAL_STR:
        len = v->v.str.len < YPATH_NUM_BUF_SIZE - 1 ? v->v.str.len : YPATH_NUM_BUF_SIZE - 1;
        memcpy(buf, v->v.str.s, len);
        buf[len] = 0;
        return cyaml_str_to_f64(buf, NULL, &f) ? f : 0.0;
    case YPATH_VAL_NODES:
        if (v->v.nodes.count == 1 && v->v.nodes.nodes[0]->type == CYAML_SCALAR) {
            cyaml_node_t* n = v->v.nodes.nodes[0];
            len = n->span.len < YPATH_NUM_BUF_SIZE - 1 ? n->span.len : YPATH_NUM_BUF_SIZE - 1;
            memcpy(buf, src + n->span.off, len);
            buf[len] = 0;
            return cyaml_str_to_f64(buf, NULL, &f) ? f : 0.0;
        }
        return 0.0;
    default:
        return 0.0;
    }
}

static int64_t ypath_val_int(const ypath_val_t* v, const char* src)
{
    switch (v->type) {
    case YPATH_VAL_INT:
        return v->v.i;
    case YPATH_VAL_FLOAT:
        return (int64_t)v->v.f;
    case YPATH_VAL_STR: {
        char buf[YPATH_NUM_BUF_SIZE];
        double f = 0.0;
        uint32_t len = v->v.str.len < YPATH_NUM_BUF_SIZE - 1 ? v->v.str.len
                                                            : YPATH_NUM_BUF_SIZE - 1;
        memcpy(buf, v->v.str.s, len);
        buf[len] = 0;
        cyaml_str_to_f64(buf, NULL, &f);
        return (int64_t)f;
    }
    case YPATH_VAL_NODES:
        return (int64_t)ypath_val_float(v, src);
    default:
        return 0;
    }
}

static bool ypath_val_string(const ypath_val_t* v, const char* src,
                             const char** out_s, uint32_t* out_len)
{
    switch (v->type) {
    case YPATH_VAL_STR:
        *out_s = v->v.str.s;
        *out_len = v->v.str.len;
        return true;
    case YPATH_VAL_NODES:
        if (v->v.nodes.count == 1 && v->v.nodes.nodes[0]->type == CYAML_SCALAR) {
            cyaml_node_t* n = v->v.nodes.nodes[0];
            *out_s = src + n->span.off;
            *out_len = n->span.len;
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool ypath_val_eq(const ypath_val_t* a, const ypath_val_t* b, const char* src)
{
    ypath_val_t ta = *a, tb = *b;

    if (a->type == YPATH_VAL_NODES && a->v.nodes.count == 1) {
        cyaml_node_t* n = a->v.nodes.nodes[0];
        if (n->type == CYAML_SCALAR) {
            ta.type = YPATH_VAL_STR;
            ta.v.str.s = src + n->span.off;
            ta.v.str.len = n->span.len;
        } else if (n->type == CYAML_NULL) {
            ta.type = YPATH_VAL_NULL;
        }
    }
    if (b->type == YPATH_VAL_NODES && b->v.nodes.count == 1) {
        cyaml_node_t* n = b->v.nodes.nodes[0];
        if (n->type == CYAML_SCALAR) {
            tb.type = YPATH_VAL_STR;
            tb.v.str.s = src + n->span.off;
            tb.v.str.len = n->span.len;
        } else if (n->type == CYAML_NULL) {
            tb.type = YPATH_VAL_NULL;
        }
    }

    if (ta.type == YPATH_VAL_NULL && tb.type == YPATH_VAL_NULL)
        return true;
    if (ta.type == YPATH_VAL_NULL && tb.type == YPATH_VAL_STR)
        return ypath_str_null(tb.v.str.s, tb.v.str.len);
    if (tb.type == YPATH_VAL_NULL && ta.type == YPATH_VAL_STR)
        return ypath_str_null(ta.v.str.s, ta.v.str.len);
    if (ta.type == YPATH_VAL_NULL || tb.type == YPATH_VAL_NULL)
        return false;

    if (ta.type == YPATH_VAL_STR && tb.type == YPATH_VAL_STR)
        return YPATH_STR_EQ(ta.v.str.s, ta.v.str.len, tb.v.str.s, tb.v.str.len);

    if ((ta.type == YPATH_VAL_INT || ta.type == YPATH_VAL_FLOAT) && (tb.type == YPATH_VAL_INT || tb.type == YPATH_VAL_FLOAT))
        return ypath_val_float(&ta, src) == ypath_val_float(&tb, src);

    if (ta.type == YPATH_VAL_BOOL && tb.type == YPATH_VAL_BOOL)
        return ta.v.b == tb.v.b;

    if (ta.type == YPATH_VAL_STR && tb.type == YPATH_VAL_BOOL) {
        if (YPATH_STR_EQ(ta.v.str.s, ta.v.str.len, S_TRUE, L_TRUE))
            return tb.v.b;
        if (YPATH_STR_EQ(ta.v.str.s, ta.v.str.len, CYAML_S_FALSE, L_FALSE))
            return !tb.v.b;
        return false;
    }
    if (tb.type == YPATH_VAL_STR && ta.type == YPATH_VAL_BOOL) {
        if (YPATH_STR_EQ(tb.v.str.s, tb.v.str.len, S_TRUE, L_TRUE))
            return ta.v.b;
        if (YPATH_STR_EQ(tb.v.str.s, tb.v.str.len, CYAML_S_FALSE, L_FALSE))
            return !ta.v.b;
        return false;
    }

    if ((ta.type == YPATH_VAL_STR || tb.type == YPATH_VAL_STR) && (ta.type == YPATH_VAL_INT || ta.type == YPATH_VAL_FLOAT || tb.type == YPATH_VAL_INT || tb.type == YPATH_VAL_FLOAT))
        return ypath_val_float(&ta, src) == ypath_val_float(&tb, src);

    return false;
}

static inline void ypath_val_free(ypath_val_t* v)
{
    if (v->type == YPATH_VAL_NODES)
        ypath_nodebuf_free(&v->v.nodes);
}

static ypath_val_t ypath_eval(ypath_ctx_t* ctx, cyaml_node_t* start,
                              const ypath_step_t* steps, uint32_t count);

typedef struct {
    ypath_ctx_t* path;
    ypath_nodebuf_t owned[YPATH_VM_INSN_CAP];
    uint32_t owned_count;
} ypath_qvm_context_t;

static ypath_val_t ypath_qvm_value(const qvm_value_t* value)
{
    ypath_val_t out = { .type = (ypath_val_type_t)value->type };
    switch (out.type) {
    case YPATH_VAL_BOOL:
        out.v.b = value->boolean != 0;
        break;
    case YPATH_VAL_INT:
        out.v.i = value->integer;
        break;
    case YPATH_VAL_FLOAT:
        out.v.f = value->number;
        break;
    case YPATH_VAL_STR:
        out.v.str.s = value->str;
        out.v.str.len = (uint32_t)value->length;
        break;
    case YPATH_VAL_NODES:
        out.v.nodes.nodes = (cyaml_node_t**)value->opaque;
        out.v.nodes.count = (uint32_t)value->length;
        out.v.nodes.cap = out.v.nodes.count;
        break;
    case YPATH_VAL_NULL:
        break;
    }
    return out;
}

static int ypath_qvm_resolve(void* opaque, uint32_t operand, qvm_value_t* out)
{
    ypath_qvm_context_t* qctx = opaque;
    const ypath_ast_t* ast = qctx->path->ast;
    const ypath_expr_t* expr;
    ypath_val_t nodes;
    if (!ast || operand >= ast->storage.expr_count)
        return 0;
    expr = &ast->storage.exprs[operand];
    memset(out, 0, sizeof(*out));
    out->type = expr->type == YPATH_EXPR_INT      ? YPATH_VAL_INT
                : expr->type == YPATH_EXPR_FLOAT ? YPATH_VAL_FLOAT
                : expr->type == YPATH_EXPR_STRING ? YPATH_VAL_STR
                : expr->type == YPATH_EXPR_BOOL   ? YPATH_VAL_BOOL
                                                  : YPATH_VAL_NULL;
    switch (expr->type) {
    case YPATH_EXPR_INT:
        out->integer = expr->v.i;
        return 1;
    case YPATH_EXPR_FLOAT:
        out->number = expr->v.f;
        return 1;
    case YPATH_EXPR_STRING:
        out->str = expr->v.str.s;
        out->length = expr->v.str.len;
        return 1;
    case YPATH_EXPR_BOOL:
        out->boolean = expr->v.b;
        return 1;
    case YPATH_EXPR_NULL:
        return 1;
    case YPATH_EXPR_PATH:
        if (!qctx->path->current || qctx->owned_count >= YPATH_VM_INSN_CAP)
            return 0;
        nodes = ypath_eval(qctx->path, (cyaml_node_t*)qctx->path->current,
                           expr->v.path.steps, expr->v.path.count);
        if (nodes.type != YPATH_VAL_NODES)
            return 0;
        qctx->owned[qctx->owned_count] = nodes.v.nodes;
        out->type = YPATH_VAL_NODES;
        out->opaque = nodes.v.nodes.nodes;
        out->length = nodes.v.nodes.count;
        qctx->owned_count++;
        return 1;
    default:
        return 0;
    }
}

static int ypath_qvm_truthy(void* opaque, const qvm_value_t* value)
{
    ypath_qvm_context_t* qctx = opaque;
    ypath_val_t converted = ypath_qvm_value(value);
    return ypath_val_truthy(&converted, qctx->path->src);
}

static int ypath_qvm_binary(void* opaque, qvm_opcode_t op, uint32_t arg,
                            const qvm_value_t* left,
                            const qvm_value_t* right, qvm_value_t* out)
{
    ypath_qvm_context_t* qctx = opaque;
    ypath_val_t lhs = ypath_qvm_value(left);
    ypath_val_t rhs = ypath_qvm_value(right);
    ypath_op_t operation = (ypath_op_t)arg;
    memset(out, 0, sizeof(*out));
    if (op == QVM_OP_ADD || op == QVM_OP_SUB || op == QVM_OP_MUL ||
        op == QVM_OP_DIV) {
        if (op == QVM_OP_DIV && operation == YPATH_OP_IDIV) {
            int64_t right_int = ypath_val_int(&rhs, qctx->path->src);
            out->type = YPATH_VAL_INT;
            out->integer = right_int != 0
                               ? ypath_val_int(&lhs, qctx->path->src) / right_int
                               : 0;
            return 1;
        }
        double left_number = ypath_val_float(&lhs, qctx->path->src);
        double right_number = ypath_val_float(&rhs, qctx->path->src);
        out->type = YPATH_VAL_FLOAT;
        out->number = op == QVM_OP_ADD   ? left_number + right_number
                      : op == QVM_OP_SUB ? left_number - right_number
                      : op == QVM_OP_MUL ? left_number * right_number
                      : right_number != 0.0 ? left_number / right_number
                                            : 0.0;
        return 1;
    }
    if (op == QVM_OP_BAND || op == QVM_OP_BOR || op == QVM_OP_BXOR ||
        op == QVM_OP_LSHIFT || op == QVM_OP_RSHIFT) {
        const int64_t left_int = ypath_val_int(&lhs, qctx->path->src);
        const int64_t right_int = ypath_val_int(&rhs, qctx->path->src);
        int64_t result;
        switch (op) {
        case QVM_OP_BAND: result = left_int & right_int; break;
        case QVM_OP_BOR: result = left_int | right_int; break;
        case QVM_OP_BXOR: result = left_int ^ right_int; break;
        case QVM_OP_LSHIFT: result = left_int << (right_int & 63); break;
        default: result = (int64_t)((uint64_t)left_int >> (right_int & 63)); break;
        }
        out->type = YPATH_VAL_INT;
        out->integer = result;
        return 1;
    }
    if (op != QVM_OP_CMP)
        return 0;
    out->type = YPATH_VAL_BOOL;
    switch (operation) {
    case YPATH_OP_OR:
        out->boolean = ypath_val_truthy(&lhs, qctx->path->src) ||
                       ypath_val_truthy(&rhs, qctx->path->src);
        break;
    case YPATH_OP_AND:
        out->boolean = ypath_val_truthy(&lhs, qctx->path->src) &&
                       ypath_val_truthy(&rhs, qctx->path->src);
        break;
    case YPATH_OP_EQ:
    case YPATH_OP_NE:
        out->boolean = ypath_val_eq(&lhs, &rhs, qctx->path->src);
        if (operation == YPATH_OP_NE)
            out->boolean = !out->boolean;
        break;
    case YPATH_OP_LT:
        out->boolean = ypath_val_float(&lhs, qctx->path->src) <
                       ypath_val_float(&rhs, qctx->path->src);
        break;
    case YPATH_OP_LE:
        out->boolean = ypath_val_float(&lhs, qctx->path->src) <=
                       ypath_val_float(&rhs, qctx->path->src);
        break;
    case YPATH_OP_GT:
        out->boolean = ypath_val_float(&lhs, qctx->path->src) >
                       ypath_val_float(&rhs, qctx->path->src);
        break;
    case YPATH_OP_GE:
        out->boolean = ypath_val_float(&lhs, qctx->path->src) >=
                       ypath_val_float(&rhs, qctx->path->src);
        break;
    case YPATH_OP_MATCHES: {
        const char* text;
        const char* pattern;
        uint32_t text_len;
        uint32_t pattern_len;
        re_match_result_t match;
        out->boolean = false;
        if (ypath_val_string(&lhs, qctx->path->src, &text, &text_len) &&
            ypath_val_string(&rhs, qctx->path->src, &pattern, &pattern_len) &&
            re_match_n(pattern, pattern_len, text, text_len, NULL, &match) ==
                RE_STATUS_OK) {
            out->boolean = true;
        }
        break;
    }
    default:
        return 0;
    }
    return 1;
}

static int ypath_qvm_unary(void* opaque, qvm_opcode_t op,
                           const qvm_value_t* input, qvm_value_t* out)
{
    ypath_qvm_context_t* qctx = opaque;
    ypath_val_t value = ypath_qvm_value(input);
    memset(out, 0, sizeof(*out));
    if (op == QVM_OP_NEG) {
        out->type = YPATH_VAL_FLOAT;
        out->number = -ypath_val_float(&value, qctx->path->src);
        return 1;
    }
    if (op == QVM_OP_BNOT) {
        out->type = YPATH_VAL_INT;
        out->integer = ~ypath_val_int(&value, qctx->path->src);
        return 1;
    }
    return 0;
}

static void ypath_qvm_make_invalid(void* opaque, qvm_value_t* out)
{
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = YPATH_VAL_NULL;
}

static void ypath_qvm_make_bool(void* opaque, int value, qvm_value_t* out)
{
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = YPATH_VAL_BOOL;
    out->boolean = value != 0;
}

static void ypath_qvm_make_number(void* opaque, double value, qvm_value_t* out)
{
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = YPATH_VAL_FLOAT;
    out->number = value;
}

static void ypath_qvm_make_string(void* opaque, const char* value, size_t len,
                                  qvm_value_t* out)
{
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = YPATH_VAL_STR;
    out->str = value;
    out->length = len;
}

static const qvm_exec_ops_t ypath_qvm_ops = {
    ypath_qvm_resolve,      ypath_qvm_truthy,     ypath_qvm_binary,
    ypath_qvm_unary,        NULL,                 NULL,
    NULL,                   NULL,                 ypath_qvm_make_invalid,
    ypath_qvm_make_bool,    ypath_qvm_make_number, ypath_qvm_make_string};

static bool ypath_qvm_filter(ypath_ctx_t* ctx, const ypath_step_t* step)
{
    ypath_qvm_context_t qctx = { .path = ctx };
    qvm_value_t result;
    int status = qvm_execute_ex(ctx->ast->vm, ctx->ast->vm_count,
                                step->v.filter_vm.vm_offset,
                                step->v.filter_vm.vm_len, &ypath_qvm_ops, &qctx,
                                NULL, &result, &ctx->limits,
                                ctx->qvm_diagnostics ? &ctx->qvm_diagnostic : NULL);
    if (status != QVM_STATUS_OK) {
        ctx->qvm_status = (qvm_status_t)status;
        if (!ctx->qvm_diagnostics) {
            memset(&ctx->qvm_diagnostic, 0, sizeof(ctx->qvm_diagnostic));
            ctx->qvm_diagnostic.status = (qvm_status_t)status;
            ctx->qvm_diagnostic.message = "YPATH query VM execution failed";
        }
    }
    bool matched = status == QVM_STATUS_OK && ypath_qvm_truthy(&qctx, &result);
    for (uint32_t i = 0; i < qctx.owned_count; ++i)
        ypath_nodebuf_free(&qctx.owned[i]);
    return matched;
}

static ypath_val_t ypath_eval(ypath_ctx_t* ctx, cyaml_node_t* start, const ypath_step_t* steps, uint32_t count)
{
    size_t stack_cap = YPATH_STACK_INIT_CAP, vals_cap = YPATH_STACK_INIT_CAP;
    ypath_frame_t* stack = malloc(stack_cap * sizeof(*stack));
    ypath_val_t* vals = malloc(vals_cap * sizeof(*vals));
    if (!stack || !vals) {
        free(stack);
        free(vals);
        return (ypath_val_t) { .type = YPATH_VAL_NULL };
    }
    int sp = 0, vp = 0;

    ypath_frame_t init = { 0 };
    init.p.steps = steps;
    init.p.step_count = count;
    init.p.step_idx = 0;
    init.p.node_idx = 0;
    init.p.child_idx = 0;
    init.p.in = (ypath_nodebuf_t) { 0 };
    init.p.out = (ypath_nodebuf_t) { 0 };
    init.p.saved_current = ctx->current;
    ypath_nodebuf_add(&init.p.in, start);
    YPATH_PUSH_FRAME(stack, sp, stack_cap, init);

    while (sp > 0) {
        ypath_frame_t* f = &stack[sp - 1];

        while (f->p.step_idx < f->p.step_count) {
                const ypath_step_t* s = &f->p.steps[f->p.step_idx];

                while (f->p.node_idx < f->p.in.count) {
                    cyaml_node_t* n = ypath_resolve_alias(f->p.in.nodes[f->p.node_idx], ctx->root, ctx->src);
                    if (!n) {
                        f->p.node_idx++;
                        continue;
                    }

                    if (s->type == YPATH_STEP_FILTER) {
                        uint32_t cc = (n->type == CYAML_SEQ) ? n->seq.count : (n->type == CYAML_MAP) ? n->map.count
                                                                                                     : 1;

                        while (f->p.child_idx < cc) {
                            cyaml_node_t* child;
                            if (n->type == CYAML_SEQ)
                                child = n->seq.items[f->p.child_idx];
                            else if (n->type == CYAML_MAP)
                                child = n->map.pairs[f->p.child_idx].val;
                            else
                                child = n;

                            f->p.saved_current = ctx->current;
                            ctx->current = child;
                            if (ypath_qvm_filter(ctx, s))
                                ypath_nodebuf_add(&f->p.out, child);
                            ctx->current = f->p.saved_current;
                            f->p.child_idx++;
                            continue;
                        }
                        f->p.child_idx = 0;
                        f->p.node_idx++;
                        continue;
                    }

                    switch (s->type) {
                    case YPATH_STEP_IDENTITY:
                        ypath_nodebuf_add(&f->p.out, n);
                        break;
                    case YPATH_STEP_PARENT: {
                        cyaml_node_t* p = ypath_find_parent(ctx->root, n);
                        if (p)
                            ypath_nodebuf_add(&f->p.out, p);
                        break;
                    }
                    case YPATH_STEP_WILDCARD:
                        if (n->type == CYAML_SEQ)
                            for (uint32_t i = 0; i < n->seq.count; i++)
                                ypath_nodebuf_add(&f->p.out, n->seq.items[i]);
                        else if (n->type == CYAML_MAP)
                            for (uint32_t i = 0; i < n->map.count; i++)
                                ypath_nodebuf_add(&f->p.out, n->map.pairs[i].val);
                        break;
                    case YPATH_STEP_RECURSIVE:
                        ypath_collect_all(n, &f->p.out);
                        break;
                    case YPATH_STEP_NAME:
                        if (n->type == CYAML_MAP && ctx->src)
                            for (uint32_t i = 0; i < n->map.count; i++) {
                                cyaml_node_t* k = n->map.pairs[i].key;
                                if (k && k->type == CYAML_SCALAR && YPATH_STR_EQ(ctx->src + k->span.off, k->span.len, s->v.name.s, s->v.name.len)) {
                                    ypath_nodebuf_add(&f->p.out, n->map.pairs[i].val);
                                    break;
                                }
                            }
                        break;
                    case YPATH_STEP_ALIAS:
                        if (ctx->src) {
                            cyaml_node_t* found = ypath_find_anchor(ctx->root, s->v.name.s, s->v.name.len, ctx->src);
                            if (found)
                                ypath_nodebuf_add(&f->p.out, found);
                        }
                        break;
                    case YPATH_STEP_INDEX:
                        if (n->type == CYAML_SEQ) {
                            int64_t idx = s->v.idx;
                            if (idx < 0)
                                idx += (int64_t)n->seq.count;
                            if (idx >= 0 && idx < (int64_t)n->seq.count)
                                ypath_nodebuf_add(&f->p.out, n->seq.items[idx]);
                        }
                        break;
                    case YPATH_STEP_SLICE:
                        if (n->type == CYAML_SEQ) {
                            int64_t len = (int64_t)n->seq.count;
                            int64_t ss = (s->v.slice.flags & YPATH_SLICE_HAS_START) ? s->v.slice.start : 0;
                            int64_t se = (s->v.slice.flags & YPATH_SLICE_HAS_END) ? s->v.slice.end : len;
                            int64_t st = (s->v.slice.flags & YPATH_SLICE_HAS_STEP) ? s->v.slice.step : 1;
                            if (st == 0)
                                st = 1;
                            if (st > 0) {
                                ss = ypath_norm_slice_idx(ss, len);
                                se = ypath_norm_slice_idx(se, len);
                                for (int64_t i = ss; i < se; i += st)
                                    ypath_nodebuf_add(&f->p.out, n->seq.items[i]);
                            } else {
                                ss = (s->v.slice.flags & YPATH_SLICE_HAS_START) ? YPATH_CLAMP(s->v.slice.start, -len, len - 1) : len - 1;
                                se = (s->v.slice.flags & YPATH_SLICE_HAS_END) ? YPATH_CLAMP(s->v.slice.end, -len - 1, len) : -len - 1;
                                if (ss < 0)
                                    ss += len;
                                if (se < -1)
                                    se += len;
                                for (int64_t i = ss; i > se && i >= 0; i += st)
                                    ypath_nodebuf_add(&f->p.out, n->seq.items[i]);
                            }
                        }
                        break;
                    case YPATH_STEP_FILTER:
                        break;
                    }
                    f->p.node_idx++;
                }

                ypath_nodebuf_t tmp = f->p.in;
                f->p.in = f->p.out;
                f->p.out = tmp;
                f->p.out.count = 0;
                f->p.step_idx++;
                f->p.node_idx = 0;
            }

            ctx->current = f->p.saved_current;
            ypath_nodebuf_free(&f->p.out);
            ypath_val_t result = { .type = YPATH_VAL_NODES, .v.nodes = f->p.in };
            sp--;
            YPATH_PUSH_VAL(vals, vp, vals_cap, result);
    }

    ypath_val_t result = vp > 0 ? vals[0] : (ypath_val_t) { .type = YPATH_VAL_NULL };
    free(stack);
    free(vals);
    return result;

cleanup:
    while (vp > 0)
        ypath_val_free(&vals[--vp]);
    for (int i = 0; i < sp; i++) {
        ypath_nodebuf_free(&stack[i].p.in);
        ypath_nodebuf_free(&stack[i].p.out);
    }
    free(stack);
    free(vals);
    return (ypath_val_t) { .type = YPATH_VAL_NULL };
}

// #endregion

// #region Public API

CYAML_API cyaml_path_result_t cyaml_path_query_ex(const cyaml_doc_t* doc,
    const cyaml_node_t* context, const char* path,
    const qvm_limits_t* limits, qvm_diagnostic_t* diagnostic)
{
    cyaml_path_result_t result = { 0 };

    if (!doc || !path) {
        result.error = "null argument";
        return result;
    }
    if (!context)
        context = doc->root;
    if (!context) {
        result.error = "no context node";
        return result;
    }

    ypath_ast_t ast;
    if (!cyaml_ypath_parse_ex(path, &ast, limits, diagnostic)) {
        result.error = ast.error;
        result.error_pos = ast.error_pos;
        return result;
    }

    ypath_ctx_t ctx = { .doc = doc,
                        .root = doc->root,
                        .current = context,
                        .src = cyaml_src(doc),
                        .ast = &ast,
                        .limits = limits ? *limits : qvm_default_limits(),
                        .qvm_status = QVM_STATUS_OK,
                        .qvm_diagnostics = limits != NULL || diagnostic != NULL };
    cyaml_node_t* start = ast.path.absolute ? (cyaml_node_t*)doc->root : (cyaml_node_t*)context;
    ypath_val_t val = ypath_eval(&ctx, start, ast.path.steps, ast.path.count);

    if (ctx.qvm_status != QVM_STATUS_OK) {
        result.error = ctx.qvm_diagnostic.message
                           ? ctx.qvm_diagnostic.message
                           : "YPATH query VM execution failed";
        if (diagnostic)
            *diagnostic = ctx.qvm_diagnostic;
        ypath_val_free(&val);
        return result;
    }

    if (val.type == YPATH_VAL_NODES && val.v.nodes.count > 0) {
        result.nodes = malloc(val.v.nodes.count * sizeof(cyaml_node_t*));
        if (result.nodes) {
            memcpy(result.nodes, val.v.nodes.nodes, val.v.nodes.count * sizeof(cyaml_node_t*));
            result.count = val.v.nodes.count;
        }
    }
    ypath_val_free(&val);
    return result;
}

CYAML_API cyaml_path_result_t cyaml_path_query(const cyaml_doc_t* doc,
    const cyaml_node_t* context, const char* path)
{
    return cyaml_path_query_ex(doc, context, path, NULL, NULL);
}

CYAML_API void cyaml_path_result_free(cyaml_path_result_t* result)
{
    if (result) {
        free(result->nodes);
        result->nodes = NULL;
        result->count = 0;
    }
}

CYAML_API cyaml_node_t* cyaml_path_first(const cyaml_doc_t* doc, const cyaml_node_t* context, const char* path)
{
    cyaml_path_result_t r = cyaml_path_query(doc, context, path);
    cyaml_node_t* n = r.count > 0 ? r.nodes[0] : NULL;
    cyaml_path_result_free(&r);
    return n;
}

// #endregion

// #region Debug

#ifdef CYAML_DEBUG

static const char* ypath_tok_name(ypath_tok_t t)
{
    static const char* names[] = {
        "EOF", "SLASH", "DOT", "DOTDOT", "STAR", "STARSTAR", "LBRACKET", "RBRACKET",
        "LPAREN", "RPAREN", "COLON", "QUESTION", "AT", "OR", "AND", "EQ", "NE",
        "LT", "LE", "GT", "GE", "PLUS", "MINUS", "DIV", "BANG", "IDENT", "INT",
        "FLOAT", "STRING", "TRUE", "FALSE", "NULL", "ERROR"
    };
    return t < sizeof(names) / sizeof(names[0]) ? names[t] : "?";
}

static void ypath_print_expr(const ypath_expr_t* e, int ind);

static void ypath_print_steps(const ypath_step_t* steps, uint32_t n, int ind)
{
    static const char* snames[] = { "IDENTITY", "PARENT", "WILDCARD", "RECURSIVE", "NAME", "ALIAS", "INDEX", "SLICE", "FILTER" };
    for (uint32_t i = 0; i < n; i++) {
        const ypath_step_t* s = &steps[i];
        for (int j = 0; j < ind; j++)
            printf("  ");
        printf("%s", snames[s->type]);
        if (s->type == YPATH_STEP_NAME || s->type == YPATH_STEP_ALIAS)
            printf(" '%.*s'", (int)s->v.name.len, s->v.name.s);
        else if (s->type == YPATH_STEP_INDEX)
            printf(" [%lld]", (long long)s->v.idx);
        else if (s->type == YPATH_STEP_SLICE)
            printf(" [%lld:%lld:%lld]", (long long)s->v.slice.start, (long long)s->v.slice.end, (long long)s->v.slice.step);
        printf("\n");
        if (s->type == YPATH_STEP_FILTER)
            ypath_print_expr(s->v.filter, ind + 1);
    }
}

static void ypath_print_expr(const ypath_expr_t* e, int ind)
{
    for (int i = 0; i < ind; i++)
        printf("  ");
    if (!e) {
        printf("(null)\n");
        return;
    }
    static const char* ops[] = { "OR", "AND", "EQ", "NE", "LT", "LE", "GT", "GE", "ADD", "SUB", "MUL", "DIV", "NEG", "NOT" };
    switch (e->type) {
    case YPATH_EXPR_INT:
        printf("INT %lld\n", (long long)e->v.i);
        break;
    case YPATH_EXPR_FLOAT:
        printf("FLOAT %f\n", e->v.f);
        break;
    case YPATH_EXPR_STRING:
        printf("STRING '%.*s'\n", (int)e->v.str.len, e->v.str.s);
        break;
    case YPATH_EXPR_BOOL:
        printf("BOOL %s\n", e->v.b ? S_TRUE : CYAML_S_FALSE);
        break;
    case YPATH_EXPR_NULL:
        printf("NULL\n");
        break;
    case YPATH_EXPR_PATH:
        printf("PATH (%u steps)\n", e->v.path.count);
        ypath_print_steps(e->v.path.steps, e->v.path.count, ind + 1);
        break;
    case YPATH_EXPR_UNARY:
        printf("UNARY %s\n", ops[e->v.unary.op]);
        ypath_print_expr(e->v.unary.arg, ind + 1);
        break;
    case YPATH_EXPR_BINARY:
        printf("BINARY %s\n", ops[e->v.binary.op]);
        ypath_print_expr(e->v.binary.left, ind + 1);
        ypath_print_expr(e->v.binary.right, ind + 1);
        break;
    }
}

CYAML_API void cyaml_path_debug(const char* path)
{
    if (!path) {
        printf("(null path)\n");
        return;
    }
    printf("=== Tokens ===\nInput: %s\n", path);

    ypath_lexer_t l;
    ypath_lex_init(&l, path);
    ypath_lex_next(&l);
    while (l.tok.type != YPATH_TOK_EOF && l.tok.type != YPATH_TOK_ERROR) {
        printf("  %-12s '%.*s'", ypath_tok_name(l.tok.type), (int)l.tok.len, l.tok.start);
        if (l.tok.type == YPATH_TOK_INT)
            printf(" (val=%lld)", (long long)l.tok.val.i);
        if (l.tok.type == YPATH_TOK_FLOAT)
            printf(" (val=%f)", l.tok.val.f);
        printf("\n");
        ypath_lex_next(&l);
    }
    if (l.tok.type == YPATH_TOK_ERROR)
        printf("  ERROR: %s\n", l.error);
    printf("  EOF\n\n=== AST ===\n");

    ypath_ast_t ast;
    if (!cyaml_ypath_parse(path, &ast)) {
        printf("Parse error: %s at pos %u\n", ast.error ? ast.error : "unknown", ast.error_pos);
        return;
    }
    printf("Path: absolute=%s, steps=%u\n", ast.path.absolute ? "yes" : "no", ast.path.count);
    ypath_print_steps(ast.path.steps, ast.path.count, 1);
}

#else

CYAML_API void cyaml_path_debug(const char* path) { (void)path; }

#endif

// #endregion
