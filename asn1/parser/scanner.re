#include "mib_internal.h"
#include "asn1_schema_grammar_gen.h"
#include <stdlib.h>
#include <string.h>

void *Asn1ParseAlloc(void *(*allocate)(size_t));
void Asn1ParseFree(void *parser, void (*release)(void *));
void Asn1Parse(void *parser, int token, AstNode *value, mib_parse_state *state);

AstNode *mib_node(mib_parse_state *state, ast_node_type_t type,
                  const char *value, AstNode *left, AstNode *right) {
    AstNode *node = NULL;
    if (!state->error && state->nodes < MIB_MAX_AST_NODES)
        node = new_node(type, value, left, right);
    if (!node) {
        state->error = 1;
        free_tree(left);
        free_tree(right);
    } else {
        ++state->nodes;
    }
    return node;
}

int mib_parse_schema(const char *input, mib_parse_state *state) {
    size_t length = 0u;
    void *parser;
    const char *YYCURSOR, *YYLIMIT, *YYMARKER, *start;
    if (!state) return -1;
    memset(state, 0, sizeof(*state));
    if (!input) return -1;
    while (length <= MIB_MAX_INPUT_BYTES && input[length]) ++length;
    if (length > MIB_MAX_INPUT_BYTES) return -1;
    parser = Asn1ParseAlloc(malloc);
    if (!parser) return -1;
    YYCURSOR = input;
    YYLIMIT = input + length;
    while (!state->error) {
        int token = 0;
        AstNode *value = NULL;
        // Both newline and paired ASN.1 comment terminators are bounded.
        if (YYLIMIT - YYCURSOR >= 2 && YYCURSOR[0] == '-' && YYCURSOR[1] == '-') {
            YYCURSOR += 2;
            while (YYCURSOR < YYLIMIT && *YYCURSOR != '\n' && *YYCURSOR != '\r') {
                if (YYLIMIT - YYCURSOR >= 2 && YYCURSOR[0] == '-' && YYCURSOR[1] == '-') {
                    YYCURSOR += 2;
                    break;
                }
                ++YYCURSOR;
            }
            continue;
        }
        start = YYCURSOR;
        /*!re2c
            re2c:define:YYCTYPE = "unsigned char";
            re2c:yyfill:enable = 0;
            re2c:eof = 0;
            [ \t\r\n]+ { continue; }
            "DEFINITIONS" { token = TK_KW_DEFINITIONS; goto emit; }
            "BEGIN"       { token = TK_KW_BEGIN; goto emit; }
            "END"         { token = TK_KW_END; goto emit; }
            "OBJECT"      { token = TK_KW_OBJECT; goto emit; }
            "IDENTIFIER"  { token = TK_KW_IDENTIFIER; goto emit; }
            "SEQUENCE"    { token = TK_KW_SEQUENCE; goto emit; }
            "SET"         { token = TK_KW_SET; goto emit; }
            "CHOICE"      { token = TK_KW_CHOICE; goto emit; }
            "OF"          { token = TK_KW_OF; goto emit; }
            "INTEGER"     { token = TK_KW_INTEGER; goto emit; }
            "BOOLEAN"     { token = TK_KW_BOOLEAN; goto emit; }
            "OCTET"       { token = TK_KW_OCTET; goto emit; }
            "BIT"         { token = TK_KW_BIT; goto emit; }
            "STRING"      { token = TK_KW_STRING; goto emit; }
            "NULL"        { token = TK_KW_NULL; goto emit; }
            "::=" { token = TK_KW_ASSIGN; goto emit; }
            "{" { token = TK_LBRACE; goto emit; }
            "}" { token = TK_RBRACE; goto emit; }
            "(" { token = TK_LPAREN; goto emit; }
            ")" { token = TK_RPAREN; goto emit; }
            "," { token = TK_COMMA; goto emit; }
            [a-zA-Z] ([a-zA-Z0-9] | "-" [a-zA-Z0-9])* { token = TK_IDENTIFIER; goto text; }
            [0-9]+ { token = TK_NUMBER; goto text; }
            $ { Asn1Parse(parser, 0, NULL, state); break; }
            * { state->error = 1; break; }
        */
text:
        {
            const size_t size = (size_t)(YYCURSOR - start);
            char text[MIB_MAX_IDENTIFIER_BYTES + 1u];
            if (size > MIB_MAX_IDENTIFIER_BYTES) { state->error = 1; break; }
            memcpy(text, start, size);
            text[size] = '\0';
            value = mib_node(state, token == TK_NUMBER ? NODE_OID_ARC : NODE_Identifier,
                             text, NULL, NULL);
            if (!value) break;
        }
emit:
        Asn1Parse(parser, token, value, state);
    }
    Asn1ParseFree(parser, free);
    if (state->error || !state->root) {
        free_tree(state->root);
        state->root = NULL;
        return -1;
    }
    return 0;
}
