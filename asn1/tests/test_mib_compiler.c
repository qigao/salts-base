#include "mib_internal.h"
#include <tinytest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

spec("MIB compiler") {
    static char *directory, *buffer;
    static mib_parse_state state;
    static mib_oid_table_t *table;
    static char path[4096];

    before_each() {
        directory = tt_make_temp_dir("asn1-mib");
        buffer = NULL;
        memset(&state, 0, sizeof(state));
        table = NULL;
        check_not_null(directory);
    }
    after_each() {
        free_tree(state.root);
        mib_oid_table_destroy(table);
        free(buffer);
        if (directory) { tt_remove_tree(directory); free(directory); }
    }

    it("extracts named numeric arcs and earlier OID prefixes into owned storage") {
        const char *input = "Demo DEFINITIONS ::= BEGIN "
            "internet OBJECT IDENTIFIER ::= { iso(1) 3 6 1 } "
            "node OBJECT IDENTIFIER ::= { internet 4 1 32473 7 } END";
        const uint32_t expected[] = {1u, 3u, 6u, 1u, 4u, 1u, 32473u, 7u};
        check_equal(mib_parse_schema(input, &state), 0);
        table = mib_extract_oids(state.root);
        check_not_null(table);
        free_tree(state.root);
        state.root = NULL;
        check_equal(table->count, (size_t)2u);
        check_equal(table->entries[1].name, "node");
        check_equal(table->entries[1].count, (size_t)8u);
        check_equal(table->entries[1].components, expected, sizeof(expected));
    }

    it("parses the existing schema types and both comment terminators") {
        check_equal(mib_parse_schema("Demo DEFINITIONS ::= BEGIN -- comment -- "
            "Packet ::= SEQUENCE { id INTEGER, items SEQUENCE OF OCTET STRING, "
            "bits BIT STRING, yes BOOLEAN, no NULL, choice CHOICE { x INTEGER, y BOOLEAN } } "
            "Names ::= SET OF Packet Flags ::= SET { flag BOOLEAN } END -- eof", &state), 0);
        check_not_null(ast_find_node_by_type(state.root, NODE_CHOICE));
        check_not_null(ast_find_node_by_type(state.root, NODE_SET_OF));
        check_not_null(ast_find_node_by_value(state.root, "Packet"));
        table = mib_extract_oids(state.root);
        check_not_null(table);
        check_equal(table->count, (size_t)0u);
    }

    it("rejects malformed and unsupported input without retaining partial trees") {
        const char *inputs[] = {
            "", "Demo DEFINITIONS ::= BEGIN", "Demo DEFINITIONS ::= BEGIN END @",
            "Demo DEFINITIONS ::= BEGIN x OBJECT IDENTIFIER ::= { 1 3 } END extra",
            "Demo DEFINITIONS ::= BEGIN IMPORTS x FROM Other; END",
            "Demo DEFINITIONS ::= BEGIN x OBJECT-TYPE SYNTAX INTEGER END",
            "Demo DEFINITIONS ::= BEGIN x OBJECT IDENTIFIER ::= { 1 3 END"
        };
        for (size_t i = 0u; i < sizeof(inputs) / sizeof(*inputs); ++i) {
            check_equal(mib_parse_schema(inputs[i], &state), -1);
            check_null(state.root);
        }
        check_equal(mib_parse_schema("Good DEFINITIONS ::= BEGIN END", &state), 0);
    }

    it("rejects invalid OIDs, unresolved prefixes, overflow and duplicate symbols") {
        const char *definitions[] = {
            "x OBJECT IDENTIFIER ::= { 3 1 }", "x OBJECT IDENTIFIER ::= { 1 40 }",
            "x OBJECT IDENTIFIER ::= { 1 }", "x OBJECT IDENTIFIER ::= { 1 3 4294967296 }",
            "x OBJECT IDENTIFIER ::= { missing 7 }",
            "x OBJECT IDENTIFIER ::= { later 7 } later OBJECT IDENTIFIER ::= { 1 3 }",
            "x OBJECT IDENTIFIER ::= { 1 3 } y OBJECT IDENTIFIER ::= { 1 x }",
            "x OBJECT IDENTIFIER ::= { 1 3 } x OBJECT IDENTIFIER ::= { 1 4 }",
            "x ::= INTEGER x OBJECT IDENTIFIER ::= { 1 3 }"
        };
        for (size_t i = 0u; i < sizeof(definitions) / sizeof(*definitions); ++i) {
            char input[512];
            snprintf(input, sizeof(input), "Bad DEFINITIONS ::= BEGIN %s END", definitions[i]);
            check_equal(mib_compile_string(input, directory), -1);
        }
        snprintf(path, sizeof(path), "%s/Bad.h", directory);
        buffer = tt_read_file(path, NULL);
        check_null(buffer);
    }

    it("accepts the full uint32 arc range through the binary ASN.1 validator") {
        check_equal(mib_parse_schema("Demo DEFINITIONS ::= BEGIN "
            "x OBJECT IDENTIFIER ::= { 2 4294967295 4294967295 } END", &state), 0);
        table = mib_extract_oids(state.root);
        check_not_null(table);
        check_equal(table->entries[0].components[1], UINT32_MAX);
        check_equal(table->entries[0].components[2], UINT32_MAX);
    }

    it("enforces input, identifier, node and resolved OID limits") {
        buffer = (char *)malloc((size_t)MIB_MAX_INPUT_BYTES + 2u);
        check_not_null(buffer);
        memset(buffer, ' ', (size_t)MIB_MAX_INPUT_BYTES + 1u);
        buffer[MIB_MAX_INPUT_BYTES + 1u] = '\0';
        check_equal(mib_compile_string(buffer, directory), -1);
        memset(buffer, 'A', MIB_MAX_IDENTIFIER_BYTES + 1u);
        strcpy(buffer + MIB_MAX_IDENTIFIER_BYTES + 1u, " DEFINITIONS ::= BEGIN END");
        check_equal(mib_compile_string(buffer, directory), -1);
        strcpy(buffer, "Demo DEFINITIONS ::= BEGIN x OBJECT IDENTIFIER ::= { 1 3");
        for (size_t i = 2u; i <= MIB_MAX_OID_ARCS; ++i) strcat(buffer, " 1");
        strcat(buffer, " } END");
        check_equal(mib_compile_string(buffer, directory), -1);
        strcpy(buffer, "Demo DEFINITIONS ::= BEGIN ");
        for (size_t i = 0u; i < MIB_MAX_AST_NODES; ++i) strcat(buffer, "T ::= INTEGER ");
        strcat(buffer, "END");
        check_equal(mib_parse_schema(buffer, &state), -1);
        check_null(state.root);
    }

    it("rejects embedded NULs and oversized files") {
        const char input[] = "Demo DEFINITIONS ::= BEGIN END\0junk";
        snprintf(path, sizeof(path), "%s/input.mib", directory);
        check_equal(tt_write_file(path, input, sizeof(input) - 1u), 0);
        check_equal(mib_compile_file(path, directory), -1);
        buffer = (char *)malloc((size_t)MIB_MAX_INPUT_BYTES + 1u);
        check_not_null(buffer);
        memset(buffer, ' ', (size_t)MIB_MAX_INPUT_BYTES + 1u);
        check_equal(tt_write_file(path, buffer, (size_t)MIB_MAX_INPUT_BYTES + 1u), 0);
        check_equal(mib_compile_file(path, directory), -1);
    }

    it("propagates input and output I/O failures") {
        const char *input = "Fail DEFINITIONS ::= BEGIN x OBJECT IDENTIFIER ::= { 1 3 } END";
        snprintf(path, sizeof(path), "%s/missing.mib", directory);
        check_equal(mib_compile_file(path, directory), -1);
        snprintf(path, sizeof(path), "%s/missing/output", directory);
        check_equal(mib_compile_string(input, path), -1);
        snprintf(path, sizeof(path), "%s/Fail.c", directory);
        check_equal(tt_make_dir(path), 0);
        check_equal(mib_compile_string(input, directory), -1);
        check_equal(mib_generate_c_header(NULL, path), -1);
        check_equal(mib_compile_string(NULL, directory), -1);
        check_equal(mib_compile_string(input, NULL), -1);
    }
}
