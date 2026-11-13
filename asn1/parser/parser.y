%include {
#include "mib_internal.h"
}
%name Asn1Parse
%token_prefix TK_
%token_type {AstNode*}
%default_type {AstNode*}
%extra_argument {mib_parse_state *state}
%stack_size 128
%token_destructor { free_tree($$); }
%default_destructor { free_tree($$); }
%syntax_error { state->error = 1; }
%parse_failure { state->error = 1; }
%stack_overflow { state->error = 1; }

main(R) ::= module(M). { state->root = M; R = NULL; }
module(R) ::= IDENTIFIER(N) KW_DEFINITIONS KW_ASSIGN KW_BEGIN assignments(L) KW_END. {
    R = mib_node(state, NODE_MODULE, N ? N->value : NULL, L, NULL);
    free_tree(N);
}
assignments(R) ::= . { R = NULL; }
assignments(R) ::= assignments(L) assignment(A). {
    R = L;
    if (!L) R = A;
    else { AstNode *tail = L; while (tail->right) tail = tail->right; tail->right = A; }
}
assignment(R) ::= IDENTIFIER(N) KW_ASSIGN type_def(T). {
    R = mib_node(state, NODE_ASSIGNMENT, N ? N->value : NULL, T, NULL);
    free_tree(N);
}
assignment(R) ::= IDENTIFIER(N) KW_OBJECT KW_IDENTIFIER KW_ASSIGN LBRACE oid_arcs(A) RBRACE. {
    AstNode *oid = mib_node(state, NODE_OBJECT_IDENTIFIER, NULL, A, NULL);
    R = mib_node(state, NODE_ASSIGNMENT, N ? N->value : NULL, oid, NULL);
    free_tree(N);
}
oid_arcs(R) ::= oid_arc(A). { R = A; }
oid_arcs(R) ::= oid_arcs(L) oid_arc(A). {
    R = L;
    if (!L) R = A;
    else { AstNode *tail = L; while (tail->right) tail = tail->right; tail->right = A; }
}
oid_arc(R) ::= NUMBER(N). { R = N; }
oid_arc(R) ::= IDENTIFIER(N). { R = N; }
oid_arc(R) ::= IDENTIFIER(N) LPAREN NUMBER(V) RPAREN. { R = V; free_tree(N); }

type_def(R) ::= type_name(T). { R = T; }
type_def(R) ::= KW_SEQUENCE LBRACE fields(L) RBRACE. {
    R = mib_node(state, NODE_SEQUENCE, "SEQUENCE", L, NULL);
}
type_def(R) ::= KW_SEQUENCE KW_OF type_def(T). {
    R = mib_node(state, NODE_SEQUENCE_OF, "SEQUENCE OF", T, NULL);
}
type_def(R) ::= KW_SET LBRACE fields(L) RBRACE. {
    R = mib_node(state, NODE_SET, "SET", L, NULL);
}
type_def(R) ::= KW_SET KW_OF type_def(T). {
    R = mib_node(state, NODE_SET_OF, "SET OF", T, NULL);
}
type_def(R) ::= KW_CHOICE LBRACE fields(L) RBRACE. {
    R = mib_node(state, NODE_CHOICE, "CHOICE", L, NULL);
}
fields(R) ::= field(F). { R = F; }
fields(R) ::= fields(L) COMMA field(F). {
    R = L;
    if (!L) R = F;
    else { AstNode *tail = L; while (tail->right) tail = tail->right; tail->right = F; }
}
field(R) ::= IDENTIFIER(N) type_def(T). {
    R = mib_node(state, NODE_TYPE_DEF, N ? N->value : NULL, T, NULL);
    free_tree(N);
}
type_name(R) ::= KW_INTEGER. { R = mib_node(state, NODE_INTEGER, "INTEGER", NULL, NULL); }
type_name(R) ::= KW_BOOLEAN. { R = mib_node(state, NODE_BOOLEAN, "BOOLEAN", NULL, NULL); }
type_name(R) ::= KW_OCTET KW_STRING. { R = mib_node(state, NODE_OCTET_STRING, "OCTET STRING", NULL, NULL); }
type_name(R) ::= KW_BIT KW_STRING. { R = mib_node(state, NODE_BIT_STRING, "BIT STRING", NULL, NULL); }
type_name(R) ::= KW_NULL. { R = mib_node(state, NODE_NULL, "NULL", NULL, NULL); }
type_name(R) ::= IDENTIFIER(N). { R = N; }
