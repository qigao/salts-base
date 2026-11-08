#include "tinytest.h"
#include <math.h>
#include <stdlib.h>

/* 使用 suite/spec 註冊測試組 */
suite("Math Benchmarks") {
  /* 使用 describe 來分組 */
  describe("Basic Arithmetic vs Libc Math") {

    /* 使用 bench(...) 標記這是一個性能測試節點 */
    bench("Square Root Calculation") {
      /* 每个计时样本执行一次运算。 */
      benchmark_batch("x * x (Multiplication)", 5000000) {
        volatile double x = 123.456;
        volatile double y = x * x;
        (void)y;
      }

      benchmark_batch("pow(x, 2.0)", 5000000) {
        volatile double x = 123.456;
        volatile double y = pow(x, 2.0);
        (void)y;
      }
    }

    bench("Trigonometry") {
      benchmark_batch("sin(x)", 5000000) {
        volatile double x = 1.0;
        volatile double y = sin(x);
        (void)y;
      }

      benchmark_batch("cos(x)", 5000000) {
        volatile double x = 1.0;
        volatile double y = cos(x);
        (void)y;
      }
    }
  }

  describe("Memory Operations") {

    bench("Allocation") {
      benchmark_batch("malloc + free (128 bytes)", 1000000) {
        void *ptr = malloc(128);
        if (ptr) {
          free(ptr);
        }
      }

      benchmark_batch("calloc + free (128 bytes)", 1000000) {
        void *ptr = calloc(1, 128);
        if (ptr) {
          free(ptr);
        }
      }

    }
  }
}
