#include "tlog.h"
#include "tinytest.hpp"
#include <string>
#include <cstdio>

spec("TLog C++ Tests") {

  it("should expose log entry metadata to C++ consumers") {
    const tlog_struct_desc *meta = cmeta_log_entry_t_meta();

    check_not_null(meta);
    check(meta->field_count == static_cast<size_t>(7));
    check(meta->fields[0].offset == offsetof(cmeta_log_entry_t, level));
    check(meta->fields[6].offset == offsetof(cmeta_log_entry_t, message));
    check(tlog_struct_find_field(meta, "message_len") == nullptr);
  }

  it("should log const char* with {}") {
    const char* msg = "hello from const char*";
    TLOG_INFOF("const char*: {}", msg);
  }

  it("should log string literal with {}") {
    TLOG_INFOF("literal: {}", "hello literal");
  }

  it("should log int with {}") {
    int x = 42;
    TLOG_INFOF("int: {}", x);
  }

  it("should log double with {}") {
    double d = 3.14159;
    TLOG_INFOF("double: {}", d);
  }

  it("should log size_t with {}") {
    size_t sz = 1024;
    TLOG_INFOF("size_t: {}", sz);
  }

  it("should log bool with {}") {
    TLOG_INFOF("bool true: {}", true);
    TLOG_INFOF("bool false: {}", false);
  }

  it("should log std::string with {}") {
    std::string s = "hello from std::string";
    TLOG_INFOF("std::string: {}", s);
    // Verify it doesn't print a pointer address
    printf("[VERIFY] std::string value: '%s'\n", s.c_str());
  }

  it("should log std::string .c_str() with {}") {
    std::string s = "hello via c_str";
    TLOG_INFOF("c_str: {}", s.c_str());
  }

  it("should log mixed types") {
    std::string name = "Alice";
    int age = 30;
    double score = 95.5;
    TLOG_INFOF("name={} age={} score={}", name, age, score);
    // Also with .c_str() for comparison
    TLOG_INFOF("name={} age={} score={}", name.c_str(), age, score);
  }

  it("should log pointer with {}") {
    int x = 42;
    TLOG_INFOF("pointer: {}", static_cast<void *>(&x));
  }

  it("should log enum with {}") {
    enum Color { RED = 0, GREEN = 1, BLUE = 2 };
    Color c = GREEN;
    TLOG_INFOF("enum: {}", c);
  }
}
