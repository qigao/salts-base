#include "cyaml_ypath_lexer.h"
#include "tinytest.h"

#include <stdint.h>

suite("cyaml YPATH re2c lexer") {
    it("distinguishes path slash from filter division") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "/");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_SLASH);

        ypath_lex_init(&lexer, "/");
        lexer.in_filter = true;
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_DIV);
    }

    it("recognizes idiv and matches only inside filters") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "idiv matches");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDENT);

        ypath_lex_init(&lexer, "idiv matches");
        lexer.in_filter = true;
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDIV);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_MATCHES);
    }

    it("keeps keyword prefixes as identifiers") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "true_value falsehood nullable true false null");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_TRUE);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_FALSE);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_NULL);
    }

    it("saturates out of range integers") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "999999999999999999999999 -999999999999999999999999");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_INT);
        check_true(lexer.tok.val.i == INT64_MAX);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_INT);
        check_true(lexer.tok.val.i == INT64_MIN);
    }

    it("reports an exponent beyond the supported range") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "1e309");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_ERROR);
        check_equal(lexer.error, "exponent overflow");
    }

    it("returns borrowed string contents without quotes") {
        const char* source = "\"hello\\\" world\" 'it\\'s'";
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, source);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_STRING);
        check_true(lexer.tok.start == source + 1);
        check_equal(lexer.tok.len, 13);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_STRING);
        check_equal(lexer.tok.len, 5);
    }

    it("tokenizes single | and & as bitwise operators") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "|");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_BOR);
        ypath_lex_init(&lexer, "&");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_BAND);
        ypath_lex_init(&lexer, "^");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_CARET);
        ypath_lex_init(&lexer, "<< >> ~ ,");
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_LSHIFT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_RSHIFT);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_TILDE);
        ypath_lex_next(&lexer);
        check_equal(lexer.tok.type, YPATH_TOK_COMMA);
    }
}
