#ifndef SC2K_FIXED_H
#define SC2K_FIXED_H

#include "base/types.h"

// Signed Q16 fixed-point arithmetic.
typedef i64 Fixed;
enum { FIXED_ONE = 65536, FIXED_HALF = 32768, FIXED_PI = 205887, FIXED_TAU = 411775 };
#define FIXED_INFINITY ((Fixed)1 << 44)

static inline Fixed FixedFromInt(i64 value) { return value * FIXED_ONE; }
static inline i64 FixedToInt(Fixed value) { return value / FIXED_ONE; }
static inline i64 FixedRound(Fixed value) { return (value + (value < 0 ? -FIXED_HALF : FIXED_HALF)) / FIXED_ONE; }
static inline i64 FixedFloor(Fixed value) { return value >> 16; }
static inline i64 FixedCeil(Fixed value) { return -FixedFloor(-value); }
static inline Fixed FixedMul(Fixed a, Fixed b) { return a * b / FIXED_ONE; }
static inline Fixed FixedDiv(Fixed a, Fixed b) { return b ? a * FIXED_ONE / b : 0; }
static inline Fixed FixedAbs(Fixed value) { return value < 0 ? -value : value; }
static inline Fixed FixedMin(Fixed a, Fixed b) { return a < b ? a : b; }
static inline Fixed FixedMax(Fixed a, Fixed b) { return a > b ? a : b; }

Fixed FixedSqrt(Fixed value);
Fixed FixedSin(Fixed radians);
static inline Fixed FixedCos(Fixed radians) { return FixedSin(radians + FIXED_PI / 2); }
Fixed FixedAtan2(Fixed y, Fixed x);
Fixed FixedExp(Fixed value);

#endif
