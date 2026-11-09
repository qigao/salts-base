#include <cserde/token.h>
#include "tinytest.h"

spec("CSerde canonical tokens") {
    it("accepts every canonical token kind") {
        int kind;

        for (kind = CSERDE_NULL; kind <= CSERDE_MAP_END; ++kind)
            check_true(cserde_token_kind_valid((cserde_token_kind)kind));
        check_false(cserde_token_kind_valid((cserde_token_kind)-1));
        check_false(cserde_token_kind_valid(
            (cserde_token_kind)(CSERDE_MAP_END + 1)));
    }

    it("accepts exactly the two view lifetimes") {
        check_true(cserde_view_lifetime_valid(CSERDE_VIEW_TRANSIENT));
        check_true(cserde_view_lifetime_valid(CSERDE_VIEW_STABLE));
        check_false(cserde_view_lifetime_valid((cserde_view_lifetime)-1));
        check_false(cserde_view_lifetime_valid((cserde_view_lifetime)2));
    }

    it("accepts zero length borrowed slices with null data") {
        cserde_token string_token = {
            .kind = CSERDE_STRING,
            .value.slice = { NULL, 0u, CSERDE_VIEW_TRANSIENT }
        };
        cserde_token bytes_token = {
            .kind = CSERDE_BYTES,
            .value.slice = { NULL, 0u, CSERDE_VIEW_STABLE }
        };

        check_true(cserde_token_valid(&string_token));
        check_true(cserde_token_valid(&bytes_token));
    }

    it("accepts nonempty slices with backing data") {
        static const unsigned char data[] = { 0x61u, 0x62u };
        cserde_token string_token = {
            .kind = CSERDE_STRING,
            .value.slice = { data, 2u, CSERDE_VIEW_STABLE }
        };
        cserde_token bytes_token = {
            .kind = CSERDE_BYTES,
            .value.slice = { data, 2u, CSERDE_VIEW_TRANSIENT }
        };

        check_true(cserde_token_valid(&string_token));
        check_true(cserde_token_valid(&bytes_token));
    }

    it("rejects nonempty slices without backing data") {
        cserde_token token = {
            .kind = CSERDE_STRING,
            .value.slice = { NULL, 1u, CSERDE_VIEW_STABLE }
        };

        check_false(cserde_token_valid(&token));
    }

    it("rejects invalid slice lifetimes") {
        static const unsigned char text[] = "x";
        cserde_token token = {
            .kind = CSERDE_BYTES,
            .value.slice = { text, 1u, (cserde_view_lifetime)99 }
        };

        check_false(cserde_token_valid(&token));
    }

    it("rejects null token") {
        check_false(cserde_token_valid(NULL));
    }

    it("shallow validates all nonslice token payloads") {
        int kind;

        for (kind = CSERDE_NULL; kind <= CSERDE_MAP_END; ++kind) {
            cserde_token token = { .kind = (cserde_token_kind)kind };
            if (kind == CSERDE_STRING || kind == CSERDE_BYTES)
                continue;
            check_true(cserde_token_valid(&token));
        }
    }
}
