#include <stdlib.h>
#include <string.h>
#include "toml.h"
#include "tinytest.h"

spec("toml_parser") {
  describe("Basic Types") {
    it("should parse strings, integers, floats, booleans, and timestamps correctly") {
        char errbuf[200];
        const char* conf =
            "str = \"hello\"\n"
            "int = 123\n"
            "float = 3.14\n"
            "bool = true\n"
            "ts = 2023-10-27T12:00:00Z\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_value_t v_str = toml_table_string(tbl, "str");
        check(v_str.ok);
        check_equal(v_str.u.s, "hello");
        free(v_str.u.s);

        toml_value_t v_int = toml_table_int(tbl, "int");
        check(v_int.ok);
        check_equal(v_int.u.i, 123);

        toml_value_t v_float = toml_table_double(tbl, "float");
        check(v_float.ok);
        check_within(v_float.u.d, 3.14, 0.001);

        toml_value_t v_bool = toml_table_bool(tbl, "bool");
        check(v_bool.ok);
        check(v_bool.u.b);

        toml_value_t v_ts = toml_table_timestamp(tbl, "ts");
        check(v_ts.ok);
        check_equal(v_ts.u.ts.year, 2023);
        check_equal(v_ts.u.ts.month, 10);
        check_equal(v_ts.u.ts.day, 27);

        toml_free(tbl);
    }
  }

  describe("Table Structures") {
    it("should parse nested tables correctly") {
        char errbuf[200];
        const char* conf =
            "[server.http]\n"
            "port = 8080\n"
            "host = \"localhost\"\n"
            "[server.grpc]\n"
            "port = 9090\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_table_t* server = toml_table_table(tbl, "server");
        check_not_null(server);

        toml_table_t* http = toml_table_table(server, "http");
        check_not_null(http);
        check_equal(toml_table_int(http, "port").u.i, 8080);

        toml_table_t* grpc = toml_table_table(server, "grpc");
        check_not_null(grpc);
        check_equal(toml_table_int(grpc, "port").u.i, 9090);

        toml_free(tbl);
    }

    it("should parse arrays of tables correctly") {
        char errbuf[200];
        const char* conf =
            "[[user]]\n"
            "name = \"alice\"\n"
            "[[user]]\n"
            "name = \"bob\"\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_array_t* users = toml_table_array(tbl, "user");
        check_not_null(users);
        check_equal(toml_array_len(users), 2);

        toml_table_t* u0 = toml_array_table(users, 0);
        toml_value_t name0 = toml_table_string(u0, "name");
        check(name0.ok);
        check_equal(name0.u.s, "alice");
        free(name0.u.s);

        toml_table_t* u1 = toml_array_table(users, 1);
        toml_value_t name1 = toml_table_string(u1, "name");
        check(name1.ok);
        check_equal(name1.u.s, "bob");
        free(name1.u.s);

        toml_free(tbl);
    }

    it("should parse inline tables correctly") {
        char errbuf[200];
        const char* conf = "pt = { x = 1, y = 2, sub = { id = \"A\" } }";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_table_t* pt = toml_table_table(tbl, "pt");
        check_not_null(pt);
        check_equal(toml_table_int(pt, "x").u.i, 1);

        toml_table_t* sub = toml_table_table(pt, "sub");
        check_not_null(sub);
        toml_value_t id = toml_table_string(sub, "id");
        check(id.ok);
        check_equal(id.u.s, "A");
        free(id.u.s);

        toml_free(tbl);
    }
  }

  describe("Arrays") {
    it("should parse mixed arrays correctly") {
        char errbuf[200];
        const char* conf = "data = [ [1, 2], { val = 3 }, \"four\" ]";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_array_t* data = toml_table_array(tbl, "data");
        check_not_null(data);
        check_equal(toml_array_len(data), 3);

        toml_array_t* sub_arr = toml_array_array(data, 0);
        check_not_null(sub_arr);
        check_equal(toml_array_int(sub_arr, 0).u.i, 1);

        toml_table_t* sub_tbl = toml_array_table(data, 1);
        check_not_null(sub_tbl);
        check_equal(toml_table_int(sub_tbl, "val").u.i, 3);

        toml_value_t text = toml_array_string(data, 2);
        check(text.ok);
        check_equal(text.u.s, "four");
        free(text.u.s);

        toml_free(tbl);
    }

    it("should handle heterogeneous arrays") {
        char errbuf[200];
        const char* conf = "mixed = [1, \"two\", { three = 3 }, [4]]";
        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_array_t* mixed = toml_table_array(tbl, "mixed");
        check_equal(toml_array_len(mixed), 4);
        check_equal(toml_array_int(mixed, 0).u.i, 1);
        toml_value_t text = toml_array_string(mixed, 1);
        check(text.ok);
        check_equal(text.u.s, "two");
        free(text.u.s);

        toml_free(tbl);
    }
  }

  describe("Error Handling") {
    it("should report syntax errors correctly") {
        char errbuf[200];
        toml_table_t* tbl = toml_parse("key = ", errbuf, sizeof(errbuf));
        check_null(tbl);
        check_equal(errbuf, "at 1:7: missing '='");

        tbl = toml_parse("k = 'abc", errbuf, sizeof(errbuf));
        check_null(tbl);
        check_equal(errbuf, "at 1:8: unterminated quote (')");
    }

    it("should report duplicate definition errors correctly") {
        char errbuf[200];
        toml_table_t* tbl = toml_parse("a = 1\na = 2", errbuf, sizeof(errbuf));
        check_null(tbl);
        check_equal(errbuf, "at 2:1: key already defined");

        tbl = toml_parse("[a]\n[a]", errbuf, sizeof(errbuf));
        check_null(tbl);
        check_equal(errbuf, "at 2:2: key already defined");
    }
  }

  describe("Complex Queries and Data") {
    it("should support path-like queries for nested data") {
        char errbuf[200];
        const char* conf =
            "[a.b.c]\n"
            "val = 42\n"
            "[[a.b.d]]\n"
            "id = 1\n"
            "[[a.b.d]]\n"
            "id = 2\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_table_t* a = toml_table_table(tbl, "a");
        check_not_null(a);
        toml_table_t* b = toml_table_table(a, "b");
        check_not_null(b);
        toml_table_t* c = toml_table_table(b, "c");
        check_not_null(c);
        check_equal(toml_table_int(c, "val").u.i, 42);

        toml_array_t* d = toml_table_array(b, "d");
        check_not_null(d);
        check_equal(toml_array_len(d), 2);

        toml_free(tbl);
    }

    it("should parse various datetime formats correctly") {
        char errbuf[200];
        const char* conf =
            "d1 = 1979-05-27T07:32:00Z\n"
            "d2 = 1979-05-27T00:32:00-07:00\n"
            "d3 = 1979-05-27T00:32:00.999999-07:00\n"
            "d4 = 1979-05-27\n"
            "d5 = 07:32:00\n"
            "d6 = 00:32:00.999999\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        check_equal(toml_table_timestamp(tbl, "d1").u.ts.kind, 'd');
        check_equal(toml_table_timestamp(tbl, "d2").u.ts.kind, 'd');
        check_equal(toml_table_timestamp(tbl, "d3").u.ts.kind, 'd');
        check_equal(toml_table_timestamp(tbl, "d4").u.ts.kind, 'D');
        check_equal(toml_table_timestamp(tbl, "d5").u.ts.kind, 't');
        check_equal(toml_table_timestamp(tbl, "d6").u.ts.kind, 't');

        toml_free(tbl);
    }

    it("should successfully parse multiline strings") {
        char errbuf[200];
        const char* conf =
            "lines = \"\"\"\nThe quick brown \\\nfox jumps over \\\nthe lazy dog.\"\"\"\n"
            "literal = '''\nNo \\ escaping\nhere.'''\n";

        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_value_t lines = toml_table_string(tbl, "lines");
        check(lines.ok);
        check_not_null(lines.u.s);
        free(lines.u.s);

        toml_free(tbl);
    }

    it("should handle escaped characters within strings correctly") {
        char errbuf[200];
        const char* conf = "quoted = \"I'm a \\\"quote\\\"\"";
        toml_table_t* tbl = toml_parse((char*)conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);

        toml_value_t q = toml_table_string(tbl, "quoted");
        check(q.ok);
        check_not_null(q.u.s);
        free(q.u.s);
        toml_free(tbl);
    }

    it("should preserve a long plain string while skipping comments") {
        enum { VALUE_BYTES = 4096 };
        const char prefix[] = "  # ignored comment\nvalue = \"";
        const char suffix[] = "\"\n";
        char *conf = (char *)malloc(sizeof(prefix) + VALUE_BYTES + sizeof(suffix));
        char errbuf[200];
        size_t offset = 0;

        check_not_null(conf);
        memcpy(conf + offset, prefix, sizeof(prefix) - 1);
        offset += sizeof(prefix) - 1;
        memset(conf + offset, 'x', VALUE_BYTES);
        offset += VALUE_BYTES;
        memcpy(conf + offset, suffix, sizeof(suffix));

        toml_table_t *tbl = toml_parse(conf, errbuf, sizeof(errbuf));
        check_not_null(tbl);
        toml_value_t value = toml_table_string(tbl, "value");
        check(value.ok);
        check_equal(strlen(value.u.s), VALUE_BYTES);
        check_equal(value.u.s, conf + sizeof(prefix) - 1, VALUE_BYTES);

        free(value.u.s);
        toml_free(tbl);
        free(conf);
    }
  }
}
