#ifndef SALTS_CORE_FMT_PP_H
#define SALTS_CORE_FMT_PP_H
/* ISO C11 bounded formatter argument-list combinators: original 1..16 ABI. */
#define FMT_PP_JOIN_(a,b) a##b
#define FMT_PP_JOIN(a,b) FMT_PP_JOIN_(a,b)
#define FMT_PP_NARG_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define FMT_PP_NARG(...) FMT_PP_NARG_IMPL(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define FMT_PP_FOR_EACH_1(M,C,a1) M(a1,C)
#define FMT_PP_FOR_EACH_2(M,C,a1,a2) M(a1,C) M(a2,C)
#define FMT_PP_FOR_EACH_3(M,C,a1,a2,a3) M(a1,C) M(a2,C) M(a3,C)
#define FMT_PP_FOR_EACH_4(M,C,a1,a2,a3,a4) M(a1,C) M(a2,C) M(a3,C) M(a4,C)
#define FMT_PP_FOR_EACH_5(M,C,a1,a2,a3,a4,a5) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C)
#define FMT_PP_FOR_EACH_6(M,C,a1,a2,a3,a4,a5,a6) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C)
#define FMT_PP_FOR_EACH_7(M,C,a1,a2,a3,a4,a5,a6,a7) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C)
#define FMT_PP_FOR_EACH_8(M,C,a1,a2,a3,a4,a5,a6,a7,a8) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C)
#define FMT_PP_FOR_EACH_9(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C)
#define FMT_PP_FOR_EACH_10(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C)
#define FMT_PP_FOR_EACH_11(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C)
#define FMT_PP_FOR_EACH_12(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C) M(a12,C)
#define FMT_PP_FOR_EACH_13(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C) M(a12,C) M(a13,C)
#define FMT_PP_FOR_EACH_14(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C) M(a12,C) M(a13,C) M(a14,C)
#define FMT_PP_FOR_EACH_15(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C) M(a12,C) M(a13,C) M(a14,C) M(a15,C)
#define FMT_PP_FOR_EACH_16(M,C,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16) M(a1,C) M(a2,C) M(a3,C) M(a4,C) M(a5,C) M(a6,C) M(a7,C) M(a8,C) M(a9,C) M(a10,C) M(a11,C) M(a12,C) M(a13,C) M(a14,C) M(a15,C) M(a16,C)
#define FMT_PP_FOR_EACH(M,C,...) FMT_PP_JOIN(FMT_PP_FOR_EACH_,FMT_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)
#endif
