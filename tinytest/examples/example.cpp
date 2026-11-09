#include "tinytest.hpp"
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>


class Calculator {
public:
  int add(int a, int b) const { return a + b; }
  int subtract(int a, int b) const { return a - b; }
  double divide(double a, double b) const {
    if (!(b > 0.0 || b < 0.0))
      throw std::invalid_argument("division by zero");
    return a / b;
  }
};

class TokenParser {
public:
  std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::string current;
    for (char c : s) {
      if (c == delim) {
        if (!current.empty()) {
          result.push_back(current);
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.empty())
      result.push_back(current);
    return result;
  }
};

struct Point {
  int x, y;
  bool operator==(const Point &o) const { return x == o.x && y == o.y; }
  bool operator!=(const Point &o) const { return !(*this == o); }
  friend std::ostream &operator<<(std::ostream &os, const Point &p) {
    return os << "(" << p.x << "," << p.y << ")";
  }
};

suite("tinytest C++ Example") {

  describe("Typed assertions (C-compatible)") {

    it("should work with C-style checks in C++") {
      int x = 42;
      check_equal(x, 42);
      check_within(3.14, 3.14, 0.001);
      check_equal("hello", "hello");
      check_equal(0xFF, 0xFF);
    }
  }

  describe("std::string assertions") {
    static std::string greeting;

    before_each() { greeting = "hello world"; }

    it("should compare strings") {
      check_equal(greeting, "hello world");
      check_not_equal(greeting, "goodbye");
    }

    it("should check substrings") {
      check_contains(greeting, "world");
      check_starts_with(greeting, "hello");
      check_ends_with(greeting, "world");
    }

    it("should check empty/non-empty") {
      check_not_empty(greeting);

      std::string empty;
      check_empty(empty);
    }

    it("should mix std::string and const char*") {
      const char *raw = "hello world";
      check_equal(greeting, raw);
      check_equal(raw, greeting);
    }
  }

  describe("Container assertions") {

    describe("vector") {
      it("should compare vectors") {
        std::vector<int> actual = {1, 2, 3, 4, 5};
        std::vector<int> expected = {1, 2, 3, 4, 5};
        check_eq_container(actual, expected);
      }

      it("should check size and membership") {
        std::vector<int> v = {10, 20, 30};
        check_size(v, 3);
        check_not_empty(v);
        check_contains(v, 20);
        check_not_contains(v, 99);
      }
    }

    describe("list") {
      it("should compare lists") {
        std::list<std::string> actual = {"a", "b", "c"};
        std::list<std::string> expected = {"a", "b", "c"};
        check_eq_container(actual, expected);
      }
    }

    describe("set") {
      it("should compare sets") {
        std::set<int> actual = {3, 1, 2};
        std::set<int> expected = {1, 2, 3};
        check_eq_container(actual, expected);
      }

      it("should check membership") {
        std::set<std::string> tags = {"fast", "unit", "core"};
        check_size(tags, 3);
        check_contains(tags, "unit");
        check_not_contains(tags, "slow");
      }
    }

    describe("map") {
      it("should compare maps") {
        std::map<std::string, int> actual = {{"a", 1}, {"b", 2}};
        std::map<std::string, int> expected = {{"a", 1}, {"b", 2}};
        check_map_eq(actual, expected);
      }

      it("should check key existence") {
        std::map<std::string, int> config = {{"timeout", 5000}, {"retries", 3}};
        check_map_has_key(config, "timeout");
        check_map_not_has_key(config, "debug");
      }
    }
  }

  describe("Calculator") {
    static Calculator calc;

    it("should add") { check_equal(calc.add(2, 3), 5); }

    it("should subtract") { check_equal(calc.subtract(10, 3), 7); }

    it("should divide") { check_within(calc.divide(10.0, 3.0), 3.333, 0.001); }
  }

  describe("TokenParser") {
    static TokenParser parser;

    it("should split by delimiter") {
      auto tokens = parser.split("one,two,three", ',');
      std::vector<std::string> expected = {"one", "two", "three"};
      check_eq_container(tokens, expected);
    }

    it("should handle single token") {
      auto tokens = parser.split("hello", ',');
      check_size(tokens, 1);
      check_equal(tokens[0], "hello");
    }

    it("should handle empty input") {
      auto tokens = parser.split("", ',');
      check_empty(tokens);
    }
  }

  describe("Smart pointers") {

    it("should work with unique_ptr") {
      auto calc = std::make_unique<Calculator>();
      check_equal(calc->add(5, 7), 12);
    }

    it("should work with shared_ptr") {
      auto calc = std::make_shared<Calculator>();
      check_equal(calc->subtract(10, 4), 6);
    }
  }

  describe("Info and capture in C++") {

    it("should capture std::string context") {
      std::vector<std::string> names = {"alice", "bob", "charlie"};
      for (size_t i = 0; i < names.size(); i++) {
        info("checking name[%zu]=%s", i, names[i].c_str());
        check_not_empty(names[i]);
      }
    }
  }

  given("BDD-style in C++") {
    static std::map<std::string, int> inventory;

    before_each() { inventory = {{"apples", 10}, {"bananas", 5}}; }

    when("adding items") {
      then("should update count") {
        inventory["apples"] += 5;
        check_equal(inventory["apples"], 15);
      }
    }

    when("removing items") {
      then("should decrease count") {
        inventory["bananas"] -= 2;
        check_equal(inventory["bananas"], 3);
      }
    }
  }

  describe("Template assertions") {

    it("should compare ints generically") {
      check_equal(1 + 1, 2);
      check_not_equal(1, 2);
    }

    it("should compare doubles generically") {
      check_equal(3.14, 3.14);
      check_not_equal(3.14, 2.71);
    }

    it("should compare std::string generically") {
      std::string a = "hello";
      std::string b = "hello";
      check_equal(a, b);
      check_not_equal(a, std::string("world"));
    }

    it("should use check_greater and check_less") {
      check_greater(10, 5);
      check_less(3, 7);
      check_greater(std::string("b"), std::string("a"));
      check_less(std::string("a"), std::string("z"));
    }

    it("should work with custom types") {
      Point a{1, 2}, b{1, 2}, c{3, 4};
      check_equal(a, b);
      check_not_equal(a, c);
    }
  }

  describe("Exception testing") {
    static Calculator calc;

    it("should catch division by zero") { check_throws(calc.divide(1.0, 0.0)); }

    it("should catch specific exception type") {
      check_throws_as(calc.divide(1.0, 0.0), std::invalid_argument);
    }

    it("should match exception message") {
      check_throws_with(calc.divide(1.0, 0.0), "division by zero");
    }

    it("should pass when no exception thrown") { check_nothrow(calc.divide(10.0, 2.0)); }

    it("should detect wrong exception type") {
      check_throws_as_warn(calc.divide(1.0, 0.0), std::invalid_argument);
    }

    it("should use check_nothrow_warn for non-fatal") {
      check_nothrow_warn(calc.add(1, 2));
      check_nothrow_warn(calc.subtract(5, 3));
    }

    it("should throw from lambda") {
      auto bad = []() -> int { throw std::runtime_error("boom"); };
      check_throws(bad());
      check_throws_as(bad(), std::runtime_error);
      check_throws_with(bad(), "boom");
    }

    it("should not throw from lambda") {
      auto good = []() -> int { return 42; };
      check_nothrow(good());
      check_equal(good(), 42);
    }
  }
}
