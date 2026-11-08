#include <tinytest.h>
#include "toonc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ANSI color codes for better output readability */
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_BOLD "\033[1m"

/* ============================================================================
 * Test Framework Macros (Wrappers for TinyTest)
 * ========================================================================== */

#define ASSERT(cond) check(cond)
#define ASSERT_MSG(cond, ...) check(cond, __VA_ARGS__)
#define ASSERT_NOT_NULL(ptr) check_not_null(ptr)
#define ASSERT_NULL(ptr) check_null(ptr)
#define ASSERT_EQ(a, b) check_equal(a, b)
#define ASSERT_STR_EQ(a, b) check_equal(a, b)
#define ASSERT_FLOAT_EQ(a, b, epsilon) check_within(a, b, epsilon)
#define ASSERT_TYPE(obj, type_check) check(type_check(obj), "Type check failed")

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * Print a visual separator
 */
static void print_separator(void) {
  printf("-----------------------------------------------------------\n");
}

/**
 * Print object tree for debugging (with color)
 */
static void debug_print_object(toonObject *obj, int depth) {
  if (!obj)
    return;

  for (int i = 0; i < depth; i++)
    printf("  ");

  if (obj->key)
    printf(COLOR_YELLOW "%s: " COLOR_RESET, obj->key);

  switch (obj->kvtype) {
  case KV_STRING:
    printf(COLOR_GREEN "\"%s\"" COLOR_RESET " (string)\n", obj->str.ptr);
    break;
  case KV_INT:
    printf(COLOR_MAGENTA "%d" COLOR_RESET " (int)\n", obj->i);
    break;
  case KV_DOUBLE:
    printf(COLOR_MAGENTA "%f" COLOR_RESET " (double)\n", obj->d);
    break;
  case KV_BOOL:
    printf(COLOR_CYAN "%s" COLOR_RESET " (bool)\n", obj->boolean ? "true" : "false");
    break;
  case KV_NULL:
    printf(COLOR_RED "null" COLOR_RESET "\n");
    break;
  case KV_LIST:
    printf("[...] (array, len=%zu)\n", obj->array.len);
    break;
  case KV_OBJ:
    printf("{ ... } (object)\n");
    if (obj->child)
      debug_print_object(obj->child, depth + 1);
    break;
  }

  if (obj->next)
    debug_print_object(obj->next, depth);
}

spec("toonc") {
    it("should parse basic primitives and simple values") {
        const char *toon = "name: John Doe\n"
                           "age: 30\n"
                           "height: 1.75\n"
                           "active: true\n"
                           "inactive: false\n"
                           "nickname: \"Johnny\"\n"
                           "middle_name: null\n"
                           "empty_string: \"\"\n"
                           "negative_int: -42\n"
                           "negative_float: -3.14\n"
                           "scientific: 1.5e10\n"
                           "scientific_neg: -2.5e-3\n";

        toonObject *root = TOONc_parseString(toon);
        ASSERT_NOT_NULL(root);

        /* Test string value (unquoted) */
        toonObject *name = TOONc_get(root, "name");
        ASSERT_NOT_NULL(name);
        ASSERT_TYPE(name, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(name), "John Doe");

        /* Test integer value */
        toonObject *age = TOONc_get(root, "age");
        ASSERT_NOT_NULL(age);
        ASSERT_TYPE(age, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(age), 30);

        /* Test double value */
        toonObject *height = TOONc_get(root, "height");
        ASSERT_NOT_NULL(height);
        ASSERT_TYPE(height, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(height), 1.75, 0.0001);

        /* Test boolean true */
        toonObject *active = TOONc_get(root, "active");
        ASSERT_NOT_NULL(active);
        ASSERT_TYPE(active, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(active), 1);

        /* Test boolean false */
        toonObject *inactive = TOONc_get(root, "inactive");
        ASSERT_NOT_NULL(inactive);
        ASSERT_TYPE(inactive, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(inactive), 0);

        /* Test quoted string */
        toonObject *nickname = TOONc_get(root, "nickname");
        ASSERT_NOT_NULL(nickname);
        ASSERT_TYPE(nickname, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(nickname), "Johnny");

        /* Test null value */
        toonObject *middle_name = TOONc_get(root, "middle_name");
        ASSERT_NOT_NULL(middle_name);
        ASSERT_TYPE(middle_name, TOON_IS_NULL);

        /* Test empty string */
        toonObject *empty = TOONc_get(root, "empty_string");
        ASSERT_NOT_NULL(empty);
        ASSERT_TYPE(empty, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(empty), "");

        /* Test negative numbers */
        toonObject *neg_int = TOONc_get(root, "negative_int");
        ASSERT_NOT_NULL(neg_int);
        ASSERT_TYPE(neg_int, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(neg_int), -42);

        toonObject *neg_float = TOONc_get(root, "negative_float");
        ASSERT_NOT_NULL(neg_float);
        ASSERT_TYPE(neg_float, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(neg_float), -3.14, 0.0001);

        /* Test scientific notation */
        toonObject *scientific = TOONc_get(root, "scientific");
        ASSERT_NOT_NULL(scientific);
        ASSERT_TYPE(scientific, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(scientific), 1.5e10, 1e6);

        TOONc_free(root);
    }

    it("should parse nested objects with indentation") {
        const char *toon = "user:\n"
                           "  name: Alice\n"
                           "  age: 25\n"
                           "  address:\n"
                           "    street: 123 Main St\n"
                           "    city: Springfield\n"
                           "    coordinates:\n"
                           "      lat: 42.1234\n"
                           "      lon: -71.5678\n"
                           "  preferences:\n"
                           "    theme: dark\n"
                           "    notifications: true\n";

        toonObject *root = TOONc_parseString(toon);
        ASSERT_NOT_NULL(root);

        /* Verify structure with debug print */
        printf("  Parsed structure:\n");
        debug_print_object(root->child, 1);

        /* Test top-level object */
        toonObject *user = TOONc_get(root, "user");
        ASSERT_NOT_NULL(user);
        ASSERT_TYPE(user, TOON_IS_OBJ);

        /* Test direct children of user */
        toonObject *name = TOONc_get(root, "user.name");
        ASSERT_MSG(name != NULL, "user.name should not be NULL");
        ASSERT_TYPE(name, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(name), "Alice");

        toonObject *age = TOONc_get(root, "user.age");
        ASSERT_NOT_NULL(age);
        ASSERT_TYPE(age, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(age), 25);

        /* Test nested address object */
        toonObject *address = TOONc_get(root, "user.address");
        ASSERT_NOT_NULL(address);
        ASSERT_TYPE(address, TOON_IS_OBJ);

        toonObject *street = TOONc_get(root, "user.address.street");
        ASSERT_NOT_NULL(street);
        ASSERT_TYPE(street, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(street), "123 Main St");

        toonObject *city = TOONc_get(root, "user.address.city");
        ASSERT_NOT_NULL(city);
        ASSERT_TYPE(city, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(city), "Springfield");

        /* Test deeply nested coordinates */
        toonObject *lat = TOONc_get(root, "user.address.coordinates.lat");
        ASSERT_NOT_NULL(lat);
        ASSERT_TYPE(lat, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(lat), 42.1234, 0.0001);

        toonObject *lon = TOONc_get(root, "user.address.coordinates.lon");
        ASSERT_NOT_NULL(lon);
        ASSERT_TYPE(lon, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(lon), -71.5678, 0.0001);

        /* Test sibling object (preferences) */
        toonObject *preferences = TOONc_get(root, "user.preferences");
        ASSERT_NOT_NULL(preferences);
        ASSERT_TYPE(preferences, TOON_IS_OBJ);

        toonObject *theme = TOONc_get(root, "user.preferences.theme");
        ASSERT_NOT_NULL(theme);
        ASSERT_TYPE(theme, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(theme), "dark");

        toonObject *notifications = TOONc_get(root, "user.preferences.notifications");
        ASSERT_NOT_NULL(notifications);
        ASSERT_TYPE(notifications, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(notifications), 1);

        TOONc_free(root);
    }

    it("should parse simple arrays") {
        const char *toon = "numbers[5]: 1,2,3,4,5\n"
                           "names[3]: alice,bob,charlie\n"
                           "mixed[4]: 42,\"hello\",true,null\n"
                           "empty[0]:\n"
                           "floats[3]: 1.1,2.2,3.3\n"
                           "single[1]: only_one\n";

        toonObject *root = TOONc_parseString(toon);
        ASSERT_NOT_NULL(root);

        /* Test integer array */
        toonObject *numbers = TOONc_get(root, "numbers");
        ASSERT_NOT_NULL(numbers);
        ASSERT_TYPE(numbers, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(numbers), 5);

        for (int i = 0; i < 5; i++) {
            toonObject *item = TOONc_getArrayItem(numbers, i);
            ASSERT_NOT_NULL(item);
            ASSERT_TYPE(item, TOON_IS_INT);
            ASSERT_EQ(TOON_GET_INT(item), i + 1);
        }

        /* Test string array */
        toonObject *names = TOONc_get(root, "names");
        ASSERT_NOT_NULL(names);
        ASSERT_TYPE(names, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(names), 3);

        const char *expected_names[] = {"alice", "bob", "charlie"};
        for (int i = 0; i < 3; i++) {
            toonObject *item = TOONc_getArrayItem(names, i);
            ASSERT_NOT_NULL(item);
            ASSERT_TYPE(item, TOON_IS_STRING);
            ASSERT_STR_EQ(TOON_GET_STRING(item), expected_names[i]);
        }

        /* Test mixed type array */
        toonObject *mixed = TOONc_get(root, "mixed");
        ASSERT_NOT_NULL(mixed);
        ASSERT_TYPE(mixed, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(mixed), 4);

        ASSERT_TYPE(TOONc_getArrayItem(mixed, 0), TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(TOONc_getArrayItem(mixed, 0)), 42);

        ASSERT_TYPE(TOONc_getArrayItem(mixed, 1), TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(TOONc_getArrayItem(mixed, 1)), "hello");

        ASSERT_TYPE(TOONc_getArrayItem(mixed, 2), TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(TOONc_getArrayItem(mixed, 2)), 1);

        ASSERT_TYPE(TOONc_getArrayItem(mixed, 3), TOON_IS_NULL);

        /* Test empty array */
        toonObject *empty = TOONc_get(root, "empty");
        ASSERT_NOT_NULL(empty);
        ASSERT_TYPE(empty, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(empty), 0);

        /* Test float array */
        toonObject *floats = TOONc_get(root, "floats");
        ASSERT_NOT_NULL(floats);
        ASSERT_TYPE(floats, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(floats), 3);

        double expected_floats[] = {1.1, 2.2, 3.3};
        for (int i = 0; i < 3; i++) {
            toonObject *item = TOONc_getArrayItem(floats, i);
            ASSERT_NOT_NULL(item);
            ASSERT_TYPE(item, TOON_IS_DOUBLE);
            ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(item), expected_floats[i], 0.0001);
        }

        TOONc_free(root);
    }

    it("should parse tabular data (CSV-style)") {
        const char *toon = "users[3]{id,name,email,active}:\n"
                           "  1,Alice,alice@example.com,true\n"
                           "  2,Bob,bob@example.com,false\n"
                           "  3,Charlie,charlie@example.com,true\n"
                           "\n"
                           "products[2]{id,name,price,category}:\n"
                           "  101,Laptop,999.99,Electronics\n"
                           "  102,Coffee Mug,15.50,Home\n"
                           "\n"
                           "empty_table[0]{col1,col2}:\n";

        toonObject *root = TOONc_parseString(toon);
        ASSERT_NOT_NULL(root);

        /* Test users table */
        toonObject *users = TOONc_get(root, "users");
        ASSERT_NOT_NULL(users);
        ASSERT_TYPE(users, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(users), 3);

        /* Verify first user row */
        toonObject *user1 = TOONc_getArrayItem(users, 0);
        ASSERT_NOT_NULL(user1);
        ASSERT_TYPE(user1, TOON_IS_OBJ);

        toonObject *u1_id = TOONc_get(user1, "id");
        ASSERT_NOT_NULL(u1_id);
        ASSERT_TYPE(u1_id, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(u1_id), 1);

        toonObject *u1_name = TOONc_get(user1, "name");
        ASSERT_NOT_NULL(u1_name);
        ASSERT_TYPE(u1_name, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(u1_name), "Alice");

        toonObject *u1_email = TOONc_get(user1, "email");
        ASSERT_NOT_NULL(u1_email);
        ASSERT_TYPE(u1_email, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(u1_email), "alice@example.com");

        toonObject *u1_active = TOONc_get(user1, "active");
        ASSERT_NOT_NULL(u1_active);
        ASSERT_TYPE(u1_active, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(u1_active), 1);

        /* Verify second user (different bool value) */
        toonObject *user2 = TOONc_getArrayItem(users, 1);
        ASSERT_NOT_NULL(user2);

        toonObject *u2_active = TOONc_get(user2, "active");
        ASSERT_NOT_NULL(u2_active);
        ASSERT_TYPE(u2_active, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(u2_active), 0);

        /* Test products table */
        toonObject *products = TOONc_get(root, "products");
        ASSERT_NOT_NULL(products);
        ASSERT_TYPE(products, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(products), 2);

        toonObject *product2 = TOONc_getArrayItem(products, 1);
        ASSERT_NOT_NULL(product2);

        toonObject *p2_price = TOONc_get(product2, "price");
        ASSERT_NOT_NULL(p2_price);
        ASSERT_TYPE(p2_price, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(p2_price), 15.50, 0.0001);

        /* Test empty table */
        toonObject *empty_table = TOONc_get(root, "empty_table");
        ASSERT_NOT_NULL(empty_table);
        ASSERT_TYPE(empty_table, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(empty_table), 0);

        TOONc_free(root);
    }

    it("should handle comments and whitespace") {
        const char *toon = "# This is a header comment\n"
                           "  # Indented comment\n"
                           "\n"
                           "key1: value1\n"
                           "# key2: should_be_ignored\n"
                           "\n"
                           "# Multiple comments\n"
                           "# Between values\n"
                           "key2: value2\n"
                           "\n"
                           "  # Comment before nested block\n"
                           "parent:\n"
                           "  # Comment inside nested block\n"
                           "  child: value\n"
                           "  # Trailing comment\n"
                           "\n"
                           "# Final comment\n";

        toonObject *root = TOONc_parseString(toon);
        ASSERT_NOT_NULL(root);

        /* Verify comments are ignored */
        toonObject *key1 = TOONc_get(root, "key1");
        ASSERT_NOT_NULL(key1);
        ASSERT_TYPE(key1, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(key1), "value1");

        toonObject *key2 = TOONc_get(root, "key2");
        ASSERT_NOT_NULL(key2);
        ASSERT_TYPE(key2, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(key2), "value2");

        /* Verify nested value works after comments */
        toonObject *child = TOONc_get(root, "parent.child");
        ASSERT_NOT_NULL(child);
        ASSERT_TYPE(child, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(child), "value");

        /* Verify that commented key was not parsed */
        toonObject *should_be_ignored = TOONc_get(root, "should_be_ignored");
        ASSERT_NULL(should_be_ignored);

        TOONc_free(root);
    }

    it("should handle edge cases and error conditions") {
        /* Subtest: Empty input */
        toonObject *root1 = TOONc_parseString("");
        ASSERT_NOT_NULL(root1);
        ASSERT_NULL(root1->child);
        TOONc_free(root1);

        /* Subtest: Whitespace only */
        toonObject *root2 = TOONc_parseString("   \n  \t  \n");
        ASSERT_NOT_NULL(root2);
        ASSERT_NULL(root2->child);
        TOONc_free(root2);

        /* Subtest: Comments only */
        toonObject *root3 = TOONc_parseString("# comment1\n# comment2\n");
        ASSERT_NOT_NULL(root3);
        ASSERT_NULL(root3->child);
        TOONc_free(root3);

        /* Subtest: Malformed keys */
        const char *malformed = "valid: ok\n"
                                "no_colon\n"
                                "another: valid\n";

        toonObject *root4 = TOONc_parseString(malformed);
        ASSERT_NOT_NULL(root4);

        toonObject *valid = TOONc_get(root4, "valid");
        ASSERT_NOT_NULL(valid);
        ASSERT_STR_EQ(TOON_GET_STRING(valid), "ok");

        toonObject *another = TOONc_get(root4, "another");
        ASSERT_NOT_NULL(another);
        ASSERT_STR_EQ(TOON_GET_STRING(another), "valid");

        /* Verify malformed line was skipped */
        toonObject *no_colon = TOONc_get(root4, "no_colon");
        ASSERT_NULL(no_colon);

        TOONc_free(root4);

        /* Subtest: Array bounds checking */
        const char *array_test = "numbers[2]: 1,2\n";
        toonObject *root5 = TOONc_parseString(array_test);
        ASSERT_NOT_NULL(root5);

        toonObject *numbers = TOONc_get(root5, "numbers");
        ASSERT_NOT_NULL(numbers);
        ASSERT_TYPE(numbers, TOON_IS_LIST);

        /* Valid indices */
        ASSERT_NOT_NULL(TOONc_getArrayItem(numbers, 0));
        ASSERT_NOT_NULL(TOONc_getArrayItem(numbers, 1));

        /* Out of bounds (should return NULL) */
        ASSERT_NULL(TOONc_getArrayItem(numbers, 2));

        TOONc_free(root5);

        /* Subtest: Whitespace in keys */
        const char *whitespace_keys = "  key_with_leading_space: value1\n"
                                      "key_with_trailing_space  : value2\n"
                                      "  key_with_both  : value3\n";

        toonObject *root6 = TOONc_parseString(whitespace_keys);
        ASSERT_NOT_NULL(root6);

        /* Keys should be trimmed */
        ASSERT_NOT_NULL(TOONc_get(root6, "key_with_leading_space"));
        ASSERT_NOT_NULL(TOONc_get(root6, "key_with_trailing_space"));
        ASSERT_NOT_NULL(TOONc_get(root6, "key_with_both"));

        TOONc_free(root6);
    }

    it("should support programmatic object creation") {
        /* Create root object */
        toonObject *root = TOONc_newObject(KV_OBJ);
        ASSERT_NOT_NULL(root);

        /* Create various typed properties */
        toonObject *name = TOONc_newStringObj("Test User", 9);
        name->key = strdup("name");

        toonObject *age = TOONc_newIntObj(42);
        age->key = strdup("age");

        toonObject *score = TOONc_newDoubleObj(95.5);
        score->key = strdup("score");

        toonObject *active = TOONc_newBoolObj(1);
        active->key = strdup("active");

        toonObject *null_val = TOONc_newNullObj();
        null_val->key = strdup("null_field");

        /* Create array */
        toonObject *tags = TOONc_newListObj();
        tags->key = strdup("tags");
        TOONc_listPush(tags, TOONc_newStringObj("admin", 5));
        TOONc_listPush(tags, TOONc_newStringObj("user", 4));
        TOONc_listPush(tags, TOONc_newStringObj("tester", 6));

        /* Link properties as siblings */
        root->child = name;
        name->next = age;
        age->next = score;
        score->next = active;
        active->next = null_val;
        null_val->next = tags;

        /* Verify all properties */
        ASSERT_TYPE(name, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(name), "Test User");

        ASSERT_TYPE(age, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(age), 42);

        ASSERT_TYPE(score, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(score), 95.5, 0.0001);

        ASSERT_TYPE(active, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(active), 1);

        ASSERT_TYPE(null_val, TOON_IS_NULL);

        ASSERT_TYPE(tags, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(tags), 3);

        TOONc_free(root);
    }

    it("should convert JSON into TOON using the shared JSON parser") {
        const char *json = "{"
                           "\"name\":\"Alice\\nBob\","
                           "\"unicode\":\"\\u0041\\u0042\","
                           "\"age\":42,"
                           "\"score\":95.5,"
                           "\"active\":true,"
                           "\"none\":null,"
                           "\"tags\":[\"admin\",2,false],"
                           "\"profile\":{\"city\":\"Paris\"}"
                           "}";

        toonObject *root = TOONc_fromJSONString(json, strlen(json));
        ASSERT_NOT_NULL(root);
        ASSERT_TYPE(root, TOON_IS_OBJ);

        toonObject *name = TOONc_get(root, "name");
        ASSERT_NOT_NULL(name);
        ASSERT_TYPE(name, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(name), "Alice\nBob");

        toonObject *unicode = TOONc_get(root, "unicode");
        ASSERT_NOT_NULL(unicode);
        ASSERT_TYPE(unicode, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(unicode), "AB");

        toonObject *age = TOONc_get(root, "age");
        ASSERT_NOT_NULL(age);
        ASSERT_TYPE(age, TOON_IS_INT);
        ASSERT_EQ(TOON_GET_INT(age), 42);

        toonObject *score = TOONc_get(root, "score");
        ASSERT_NOT_NULL(score);
        ASSERT_TYPE(score, TOON_IS_DOUBLE);
        ASSERT_FLOAT_EQ(TOON_GET_DOUBLE(score), 95.5, 0.0001);

        toonObject *active = TOONc_get(root, "active");
        ASSERT_NOT_NULL(active);
        ASSERT_TYPE(active, TOON_IS_BOOL);
        ASSERT_EQ(TOON_GET_BOOL(active), 1);

        toonObject *none = TOONc_get(root, "none");
        ASSERT_NOT_NULL(none);
        ASSERT_TYPE(none, TOON_IS_NULL);

        toonObject *tags = TOONc_get(root, "tags");
        ASSERT_NOT_NULL(tags);
        ASSERT_TYPE(tags, TOON_IS_LIST);
        ASSERT_EQ(TOONc_getArrayLength(tags), 3);
        ASSERT_STR_EQ(TOON_GET_STRING(TOONc_getArrayItem(tags, 0)), "admin");
        ASSERT_EQ(TOON_GET_INT(TOONc_getArrayItem(tags, 1)), 2);
        ASSERT_EQ(TOON_GET_BOOL(TOONc_getArrayItem(tags, 2)), 0);

        toonObject *city = TOONc_get(root, "profile.city");
        ASSERT_NOT_NULL(city);
        ASSERT_TYPE(city, TOON_IS_STRING);
        ASSERT_STR_EQ(TOON_GET_STRING(city), "Paris");

        TOONc_free(root);
    }

    it("should reject malformed JSON during TOON conversion") {
        const char *missing_colon = "{\"key\" 1}";
        toonObject *root = TOONc_fromJSONString(missing_colon, strlen(missing_colon));
        ASSERT_NULL(root);
    }

    it("should preserve a long plain string while skipping comments") {
        enum { VALUE_BYTES = 4096 };
        const char prefix[] = "  # ignored comment\nvalue: \"";
        const char suffix[] = "\"\n";
        char *toon = (char *)malloc(sizeof(prefix) + VALUE_BYTES + sizeof(suffix));
        size_t offset = 0;

        ASSERT_NOT_NULL(toon);
        memcpy(toon + offset, prefix, sizeof(prefix) - 1);
        offset += sizeof(prefix) - 1;
        memset(toon + offset, 'x', VALUE_BYTES);
        offset += VALUE_BYTES;
        memcpy(toon + offset, suffix, sizeof(suffix));

        toonObject *root = TOONc_parseStringLen(toon, offset + sizeof(suffix) - 1);
        ASSERT_NOT_NULL(root);
        toonObject *value = TOONc_get(root, "value");
        ASSERT_NOT_NULL(value);
        ASSERT_TYPE(value, TOON_IS_STRING);
        check_equal(value->str.len, VALUE_BYTES);
        check_equal(value->str.ptr, toon + sizeof(prefix) - 1, VALUE_BYTES);

        TOONc_free(root);
        free(toon);
    }
}
