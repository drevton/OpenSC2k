#ifndef SC2K_BASE_INTEGER_H
#define SC2K_BASE_INTEGER_H

#include "base/types.h"

// Truncate a * b / divisor towards zero without overflowing the product.
// The final quotient must fit in i64; a zero divisor returns zero.
static inline i64 MulDiv(i64 a, i64 b, i64 divisor) {
  if (!divisor || !a || !b) return 0;
  bool negative = (a < 0) ^ (b < 0) ^ (divisor < 0);
  // Keep each signed input's magnitude unsigned so INT64_MIN remains representable.
  u64 x = a < 0 ? 0 - (u64)a : (u64)a;
  u64 y = b < 0 ? 0 - (u64)b : (u64)b;
  u64 denominator = divisor < 0 ? 0 - (u64)divisor : (u64)divisor;
  u64 result;
  if (x <= UINT64_MAX / y) {
    result = x * y / denominator;
  } else {
    result = x / denominator * y;
    u64 remainder = x % denominator, carry = 0, fraction = 0;
    for (int bit = 63 - __builtin_clzll(y); bit >= 0; --bit) {
      fraction *= 2;
      if (carry >= denominator - carry) {
        carry -= denominator - carry;
        ++fraction;
      } else carry *= 2;
      if (y >> bit & 1) {
        if (carry >= denominator - remainder) {
          carry -= denominator - remainder;
          ++fraction;
        } else carry += remainder;
      }
    }
    result += fraction;
  }
  return negative ? (i64)(0 - result) : (i64)result;
}

// Add with saturation at the signed 64-bit limits, without overflowing the intermediate sum.
static inline i64 AddInt64Clamped(i64 a, i64 b) {
  if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
  if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
  return a + b;
}

// A zero divisor returns zero.
static inline u32 UintDiv(u32 numerator, u32 denominator) {
  return denominator ? numerator / denominator : 0;
}

#endif
