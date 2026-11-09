#ifndef TINYTEST_DETAIL_PP_H
#define TINYTEST_DETAIL_PP_H

/* Strict-C11 preprocessor kernel used by TinyTest's assertion layer. */
#define TTEST_PP_CAT_I__(left, right) left##right
#define TTEST_PP_CAT__(left, right) TTEST_PP_CAT_I__(left, right)
#define TTEST_PP_INDEXED_NAME_I__(prefix, index) prefix##index##__
#define TTEST_PP_INDEXED_NAME__(prefix, index) \
  TTEST_PP_INDEXED_NAME_I__(prefix, index)

#define TTEST_PP_NARG_I__(_1, _2, _3, _4, _5, _6, _7, _8, \
                          _9, _10, _11, _12, _13, _14, _15, _16, \
                          count, ...) count
#define TTEST_PP_NARG__(...) \
  TTEST_PP_NARG_I__(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, \
                    8, 7, 6, 5, 4, 3, 2, 1, 0)

/* mapper(index, context) */
#define TTEST_PP_REPEAT_0__(mapper, context)
#define TTEST_PP_REPEAT_1__(mapper, context) \
  mapper(0, context)
#define TTEST_PP_REPEAT_2__(mapper, context) \
  TTEST_PP_REPEAT_1__(mapper, context) mapper(1, context)
#define TTEST_PP_REPEAT_3__(mapper, context) \
  TTEST_PP_REPEAT_2__(mapper, context) mapper(2, context)
#define TTEST_PP_REPEAT_4__(mapper, context) \
  TTEST_PP_REPEAT_3__(mapper, context) mapper(3, context)
#define TTEST_PP_REPEAT_5__(mapper, context) \
  TTEST_PP_REPEAT_4__(mapper, context) mapper(4, context)
#define TTEST_PP_REPEAT_6__(mapper, context) \
  TTEST_PP_REPEAT_5__(mapper, context) mapper(5, context)
#define TTEST_PP_REPEAT_7__(mapper, context) \
  TTEST_PP_REPEAT_6__(mapper, context) mapper(6, context)
#define TTEST_PP_REPEAT_8__(mapper, context) \
  TTEST_PP_REPEAT_7__(mapper, context) mapper(7, context)
#define TTEST_PP_REPEAT_9__(mapper, context) \
  TTEST_PP_REPEAT_8__(mapper, context) mapper(8, context)
#define TTEST_PP_REPEAT_10__(mapper, context) \
  TTEST_PP_REPEAT_9__(mapper, context) mapper(9, context)
#define TTEST_PP_REPEAT_11__(mapper, context) \
  TTEST_PP_REPEAT_10__(mapper, context) mapper(10, context)
#define TTEST_PP_REPEAT_12__(mapper, context) \
  TTEST_PP_REPEAT_11__(mapper, context) mapper(11, context)
#define TTEST_PP_REPEAT_13__(mapper, context) \
  TTEST_PP_REPEAT_12__(mapper, context) mapper(12, context)
#define TTEST_PP_REPEAT_14__(mapper, context) \
  TTEST_PP_REPEAT_13__(mapper, context) mapper(13, context)
#define TTEST_PP_REPEAT_15__(mapper, context) \
  TTEST_PP_REPEAT_14__(mapper, context) mapper(14, context)
#define TTEST_PP_REPEAT_16__(mapper, context) \
  TTEST_PP_REPEAT_15__(mapper, context) mapper(15, context)
#define TTEST_PP_REPEAT__(count, mapper, context) \
  TTEST_PP_INDEXED_NAME__(TTEST_PP_REPEAT_, count)(mapper, context)

/* Select a zero-based item from a parenthesized argument list. */
#define TTEST_PP_ARG_AT_0__(_0, ...) _0
#define TTEST_PP_ARG_AT_1__(_0, _1, ...) _1
#define TTEST_PP_ARG_AT_2__(_0, _1, _2, ...) _2
#define TTEST_PP_ARG_AT_3__(_0, _1, _2, _3, ...) _3
#define TTEST_PP_ARG_AT_4__(_0, _1, _2, _3, _4, ...) _4
#define TTEST_PP_ARG_AT_5__(_0, _1, _2, _3, _4, _5, ...) _5
#define TTEST_PP_ARG_AT_6__(_0, _1, _2, _3, _4, _5, _6, ...) _6
#define TTEST_PP_ARG_AT_7__(_0, _1, _2, _3, _4, _5, _6, _7, ...) _7
#define TTEST_PP_ARG_AT_8__(_0, _1, _2, _3, _4, _5, _6, _7, _8, ...) _8
#define TTEST_PP_ARG_AT_9__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, ...) _9
#define TTEST_PP_ARG_AT_10__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, ...) _10
#define TTEST_PP_ARG_AT_11__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, ...) _11
#define TTEST_PP_ARG_AT_12__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, ...) _12
#define TTEST_PP_ARG_AT_13__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, ...) _13
#define TTEST_PP_ARG_AT_14__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, ...) _14
#define TTEST_PP_ARG_AT_15__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15
#define TTEST_PP_ARG_AT__(index, arguments) \
  TTEST_PP_INDEXED_NAME__(TTEST_PP_ARG_AT_, index) arguments

#define TTEST_PP_COMMA_IF_0__
#define TTEST_PP_COMMA_IF_1__ ,
#define TTEST_PP_COMMA_IF_2__ ,
#define TTEST_PP_COMMA_IF_3__ ,
#define TTEST_PP_COMMA_IF_4__ ,
#define TTEST_PP_COMMA_IF_5__ ,
#define TTEST_PP_COMMA_IF_6__ ,
#define TTEST_PP_COMMA_IF_7__ ,
#define TTEST_PP_COMMA_IF_8__ ,
#define TTEST_PP_COMMA_IF_9__ ,
#define TTEST_PP_COMMA_IF_10__ ,
#define TTEST_PP_COMMA_IF_11__ ,
#define TTEST_PP_COMMA_IF_12__ ,
#define TTEST_PP_COMMA_IF_13__ ,
#define TTEST_PP_COMMA_IF_14__ ,
#define TTEST_PP_COMMA_IF_15__ ,
#define TTEST_PP_COMMA_IF__(index) \
  TTEST_PP_INDEXED_NAME__(TTEST_PP_COMMA_IF_, index)

/* mapper(row, context) */
#define TTEST_PP_FE_1__(mapper, context, row) mapper(row, context)
#define TTEST_PP_FE_2__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_1__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_3__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_2__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_4__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_3__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_5__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_4__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_6__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_5__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_7__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_6__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_8__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_7__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_9__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_8__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_10__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_9__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_11__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_10__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_12__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_11__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_13__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_12__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_14__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_13__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_15__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_14__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FE_16__(mapper, context, row, ...) \
  mapper(row, context) TTEST_PP_FE_15__(mapper, context, __VA_ARGS__)
#define TTEST_PP_FOR_EACH__(mapper, context, ...) \
  TTEST_PP_INDEXED_NAME__(TTEST_PP_FE_, TTEST_PP_NARG__(__VA_ARGS__))( \
      mapper, context, __VA_ARGS__)

#endif /* TINYTEST_DETAIL_PP_H */
