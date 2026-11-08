#ifndef TINYTEST_TRAITS_H
#define TINYTEST_TRAITS_H

#ifdef __cplusplus
#error "TinyTest C11 assertion traits are not available from C++; use tinytest.hpp"
#endif

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "TinyTest traits require C11 or later"
#endif

#include <stdbool.h>
#include "pp.h"

/* Public operation tokens used by the generic assertion layer. */
#define TTEST_EQ 0
#define TTEST_NE 1
#define TTEST_GT 2
#define TTEST_GE 3
#define TTEST_LT 4
#define TTEST_LE 5

/* One canonical builtin map feeds every C11 generic assertion. */
#define TTEST_C11_NUMERIC_ASSOCIATIONS__(signed_handler, unsigned_handler, \
                                         float_handler, double_handler, \
                                         long_double_handler) \
  char:               signed_handler, \
  signed char:        signed_handler, \
  unsigned char:      unsigned_handler, \
  short:              signed_handler, \
  unsigned short:     unsigned_handler, \
  int:                signed_handler, \
  unsigned int:       unsigned_handler, \
  long:               signed_handler, \
  unsigned long:      unsigned_handler, \
  long long:          signed_handler, \
  unsigned long long: unsigned_handler, \
  bool:               signed_handler, \
  float:              float_handler, \
  double:             double_handler, \
  long double:        long_double_handler

#define TTEST_C11_EQUAL_ASSOCIATIONS__(signed_handler, unsigned_handler, \
                                       float_handler, double_handler, \
                                       long_double_handler, string_handler, \
                                       pointer_handler) \
  TTEST_C11_NUMERIC_ASSOCIATIONS__(signed_handler, unsigned_handler, \
                                   float_handler, double_handler, \
                                   long_double_handler), \
  char *:             string_handler, \
  const char *:       string_handler, \
  void *:             pointer_handler, \
  const void *:       pointer_handler

/* Complex values are outside TinyTest's scalar assertion contract. Keep the
 * rejection in the shared selector so builtins and CMeta traits agree. */
#if !defined(__STDC_NO_COMPLEX__) && (!defined(_MSC_VER) || defined(__clang__))
#define TTEST_C11_COMPLEX_ASSOCIATIONS__(handler) \
  , float _Complex:       handler \
  , double _Complex:      handler \
  , long double _Complex: handler
#else
#define TTEST_C11_COMPLEX_ASSOCIATIONS__(handler)
#endif

/* Equality rows are (TOKEN, C_TYPE, COMPARATOR). COMPARATOR borrows two
 * `const C_TYPE *` values. Define the list before including tinytest.h:
 *
 *   #define TTEST_USER_EQUAL_TRAIT_LIST \
 *       , (POINT, Point, point_equal)
 */
#ifndef TTEST_USER_EQUAL_TRAIT_LIST
#define TTEST_USER_EQUAL_TRAIT_LIST
#endif

#define TTEST_EQUAL_TRAIT_TOKEN__(row) TTEST_EQUAL_TRAIT_TOKEN_I__ row
#define TTEST_EQUAL_TRAIT_TOKEN_I__(token, ctype, comparator) token
#define TTEST_EQUAL_TRAIT_CTYPE__(row) TTEST_EQUAL_TRAIT_CTYPE_I__ row
#define TTEST_EQUAL_TRAIT_CTYPE_I__(token, ctype, comparator) ctype
#define TTEST_EQUAL_TRAIT_COMPARATOR__(row) TTEST_EQUAL_TRAIT_COMPARATOR_I__ row
#define TTEST_EQUAL_TRAIT_COMPARATOR_I__(token, ctype, comparator) comparator
#define TTEST_TRAIT_STRINGIFY_I__(value) #value
#define TTEST_TRAIT_STRINGIFY__(value) TTEST_TRAIT_STRINGIFY_I__(value)

static inline const char *ttest_c11_relation_name__(int relation) {
  switch (relation) {
  case TTEST_EQ: return "==";
  case TTEST_NE: return "!=";
  case TTEST_GT: return ">";
  case TTEST_GE: return ">=";
  case TTEST_LT: return "<";
  case TTEST_LE: return "<=";
  default: return "invalid-relation";
  }
}

#define TTEST_C11_COMPARE_BODY__(actual, expected) \
  switch (relation) { \
  case TTEST_EQ: passed = (actual) == (expected); break; \
  case TTEST_NE: passed = (actual) != (expected); break; \
  case TTEST_GT: passed = (actual) > (expected); break; \
  case TTEST_GE: passed = (actual) >= (expected); break; \
  case TTEST_LT: passed = (actual) < (expected); break; \
  case TTEST_LE: passed = (actual) <= (expected); break; \
  default: passed = false; break; \
  }

static inline void ttest_c11_check_signed__(
    int relation, long long actual, long long expected, bool warning,
    const char *file, const char *line) {
  bool passed;
  TTEST_C11_COMPARE_BODY__(actual, expected)
  if (warning) {
    TTEST_WARN_IMPL__(passed, file, line, "expected %s %lld but got %lld",
                     ttest_c11_relation_name__(relation), expected, actual);
  } else {
    TTEST_CHECK_IMPL__(passed, file, line, "expected %s %lld but got %lld",
                      ttest_c11_relation_name__(relation), expected, actual);
  }
}

static inline void ttest_c11_check_unsigned__(
    int relation, unsigned long long actual, unsigned long long expected,
    bool warning, const char *file, const char *line) {
  bool passed;
  TTEST_C11_COMPARE_BODY__(actual, expected)
  if (warning) {
    TTEST_WARN_IMPL__(passed, file, line, "expected %s %llu but got %llu",
                     ttest_c11_relation_name__(relation), expected, actual);
  } else {
    TTEST_CHECK_IMPL__(passed, file, line, "expected %s %llu but got %llu",
                      ttest_c11_relation_name__(relation), expected, actual);
  }
}

#define TTEST_C11_DEFINE_FLOAT_HANDLER__(suffix, type, abs_function, eq_margin) \
  static inline void ttest_c11_check_##suffix##__( \
      int relation, type actual, type expected, bool warning, \
      const char *file, const char *line) { \
    bool passed; \
    if (relation == TTEST_EQ || relation == TTEST_NE) { \
      const bool equal = abs_function(actual - expected) <= (eq_margin); \
      passed = relation == TTEST_EQ ? equal : !equal; \
    } else { \
      TTEST_C11_COMPARE_BODY__(actual, expected) \
    } \
    if (warning) { \
      TTEST_WARN_IMPL__(passed, file, line, "expected %s but values differ", \
                       ttest_c11_relation_name__(relation)); \
    } else { \
      TTEST_CHECK_IMPL__(passed, file, line, "expected %s but values differ", \
                        ttest_c11_relation_name__(relation)); \
    } \
  }

TTEST_C11_DEFINE_FLOAT_HANDLER__(float, float, fabsf,
    8.0f * FLT_EPSILON * (fabsf(actual) > 1.0f ? fabsf(actual) : 1.0f))
TTEST_C11_DEFINE_FLOAT_HANDLER__(double, double, fabs, 1e-9)
TTEST_C11_DEFINE_FLOAT_HANDLER__(long_double, long double, fabsl, 1e-18L)

#undef TTEST_C11_DEFINE_FLOAT_HANDLER__
#undef TTEST_C11_COMPARE_BODY__

static inline void ttest_c11_check_string__(
    int relation, const char *actual, const char *expected, bool warning,
    const char *file, const char *line) {
  const bool passed = relation == TTEST_EQ ? ttest_str_eq__(actual, expected)
                                           : ttest_str_ne__(actual, expected);
  if (warning) {
    TTEST_WARN_IMPL__(passed, file, line, "expected %s \"%s\" but got \"%s\"",
                     ttest_c11_relation_name__(relation),
                     ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
  } else {
    TTEST_CHECK_IMPL__(passed, file, line, "expected %s \"%s\" but got \"%s\"",
                      ttest_c11_relation_name__(relation),
                      ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
  }
}

static inline void ttest_c11_check_pointer__(
    int relation, const void *actual, const void *expected, bool warning,
    const char *file, const char *line) {
  const bool passed = relation == TTEST_EQ ? actual == expected : actual != expected;
  if (warning) {
    TTEST_WARN_IMPL__(passed, file, line, "expected %s %p but got %p",
                     ttest_c11_relation_name__(relation), expected, actual);
  } else {
    TTEST_CHECK_IMPL__(passed, file, line, "expected %s %p but got %p",
                      ttest_c11_relation_name__(relation), expected, actual);
  }
}

static inline void ttest_c11_complex_values_are_unsupported__(void) {}

#define TTEST_C11_DEFINE_RANGE_HANDLER__(suffix, type, format) \
  static inline void ttest_c11_check_range_##suffix##__( \
      type actual, type minimum, type maximum, bool warning, \
      const char *file, const char *line) { \
    const bool passed = actual >= minimum && actual <= maximum; \
    if (warning) { \
      TTEST_WARN_IMPL__(passed, file, line, \
                       "expected " format " in range [" format ", " format "]", \
                       actual, minimum, maximum); \
    } else { \
      TTEST_CHECK_IMPL__(passed, file, line, \
                        "expected " format " in range [" format ", " format "]", \
                        actual, minimum, maximum); \
    } \
  }

TTEST_C11_DEFINE_RANGE_HANDLER__(signed, long long, "%lld")
TTEST_C11_DEFINE_RANGE_HANDLER__(unsigned, unsigned long long, "%llu")
TTEST_C11_DEFINE_RANGE_HANDLER__(float, float, "%f")
TTEST_C11_DEFINE_RANGE_HANDLER__(double, double, "%f")
TTEST_C11_DEFINE_RANGE_HANDLER__(long_double, long double, "%Lf")

#undef TTEST_C11_DEFINE_RANGE_HANDLER__

static inline void ttest_c11_check_within__(
    long double actual, long double expected, long double margin, bool warning,
    const char *file, const char *line) {
  const bool passed = fabsl(actual - expected) <= margin;
  if (warning) {
    TTEST_WARN_IMPL__(passed, file, line,
                     "expected %Lf (+/- %Lf) but got %Lf", expected, margin, actual);
  } else {
    TTEST_CHECK_IMPL__(passed, file, line,
                      "expected %Lf (+/- %Lf) but got %Lf", expected, margin, actual);
  }
}

#define TTEST_C11_CHECK_EQ_SELECT__(actual, ...) \
  _Generic((actual), \
    TTEST_C11_EQUAL_ASSOCIATIONS__(ttest_c11_check_signed__, \
      ttest_c11_check_unsigned__, ttest_c11_check_float__, \
      ttest_c11_check_double__, ttest_c11_check_long_double__, \
      ttest_c11_check_string__, ttest_c11_check_pointer__) \
    TTEST_C11_COMPLEX_ASSOCIATIONS__( \
      ttest_c11_complex_values_are_unsupported__), \
    __VA_ARGS__)

#define TTEST_C11_CHECK_NUMERIC_SELECT__(actual) \
  _Generic((actual), \
    TTEST_C11_NUMERIC_ASSOCIATIONS__(ttest_c11_check_signed__, \
      ttest_c11_check_unsigned__, ttest_c11_check_float__, \
      ttest_c11_check_double__, ttest_c11_check_long_double__) \
    TTEST_C11_COMPLEX_ASSOCIATIONS__( \
      ttest_c11_complex_values_are_unsupported__), \
    default: ttest_c11_check_signed__)

#define TTEST_C11_CHECK_RELATION__(actual, expected, relation, warning) \
  TTEST_C11_CHECK_EQ_SELECT__((actual), default: ttest_c11_check_signed__)( \
      (relation), (actual), (expected), (warning), __FILE__, __STRING__LINE__)

#define TTEST_C11_CHECK_ORDER__(actual, expected, relation, warning) \
  TTEST_C11_CHECK_NUMERIC_SELECT__((actual))( \
      (relation), (actual), (expected), (warning), __FILE__, __STRING__LINE__)

#define TTEST_CHECK_EQUAL_VALUE__(actual, expected) \
  TTEST_C11_CHECK_RELATION__((actual), (expected), TTEST_EQ, false)
#define TTEST_CHECK_EQUAL_VALUE_WARN__(actual, expected) \
  TTEST_C11_CHECK_RELATION__((actual), (expected), TTEST_EQ, true)
#define TTEST_CHECK_NOT_EQUAL_VALUE__(actual, expected) \
  TTEST_C11_CHECK_RELATION__((actual), (expected), TTEST_NE, false)
#define TTEST_CHECK_NOT_EQUAL_VALUE_WARN__(actual, expected) \
  TTEST_C11_CHECK_RELATION__((actual), (expected), TTEST_NE, true)
#define check_equal(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_EQUAL__, \
                         TTEST_CHECK_EQUAL_VALUE__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)
#define check_equal_warn(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_EQUAL_WARN__, \
                         TTEST_CHECK_EQUAL_VALUE_WARN__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)
#define check_not_equal(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_NOT_EQUAL__, \
                         TTEST_CHECK_NOT_EQUAL_VALUE__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)
#define check_not_equal_warn(...) \
  TTEST_EQUAL_OVERLOAD__(__VA_ARGS__, TTEST_CHECK_MEMORY_NOT_EQUAL_WARN__, \
                         TTEST_CHECK_NOT_EQUAL_VALUE_WARN__, TTEST_EQUAL_SENTINEL__)(__VA_ARGS__)
#define check_greater(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_GT, false)
#define check_greater_warn(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_GT, true)
#define check_greater_equal(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_GE, false)
#define check_greater_equal_warn(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_GE, true)
#define check_less(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_LT, false)
#define check_less_warn(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_LT, true)
#define check_less_equal(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_LE, false)
#define check_less_equal_warn(actual, expected) \
  TTEST_C11_CHECK_ORDER__((actual), (expected), TTEST_LE, true)

#define check_within(actual, expected, margin) \
  _Generic((actual), \
    float: ttest_c11_check_within__, \
    double: ttest_c11_check_within__, \
    long double: ttest_c11_check_within__ \
  )((actual), (expected), (margin), false, __FILE__, __STRING__LINE__)
#define check_within_warn(actual, expected, margin) \
  _Generic((actual), \
    float: ttest_c11_check_within__, \
    double: ttest_c11_check_within__, \
    long double: ttest_c11_check_within__ \
  )((actual), (expected), (margin), true, __FILE__, __STRING__LINE__)

#define TTEST_C11_CHECK_RANGE_SELECT__(actual) \
  _Generic((actual), \
    TTEST_C11_NUMERIC_ASSOCIATIONS__(ttest_c11_check_range_signed__, \
      ttest_c11_check_range_unsigned__, ttest_c11_check_range_float__, \
      ttest_c11_check_range_double__, ttest_c11_check_range_long_double__) \
    TTEST_C11_COMPLEX_ASSOCIATIONS__( \
      ttest_c11_complex_values_are_unsupported__), \
    default: ttest_c11_check_range_signed__)
#define check_in_range(actual, minimum, maximum) \
  TTEST_C11_CHECK_RANGE_SELECT__((actual))( \
      (actual), (minimum), (maximum), false, __FILE__, __STRING__LINE__)
#define check_in_range_warn(actual, minimum, maximum) \
  TTEST_C11_CHECK_RANGE_SELECT__((actual))( \
      (actual), (minimum), (maximum), true, __FILE__, __STRING__LINE__)

#define check_between(actual, minimum, maximum) \
  check_in_range((actual), (minimum), (maximum))
#define check_between_warn(actual, minimum, maximum) \
  check_in_range_warn((actual), (minimum), (maximum))

#endif /* TINYTEST_TRAITS_H */
