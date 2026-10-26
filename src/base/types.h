#ifndef SC2K_BASE_TYPES_H
#define SC2K_BASE_TYPES_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Field accessors must inline even when Release favors small code.
#if defined(__clang__) || defined(__GNUC__)
#define SC2K_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SC2K_FORCE_INLINE __forceinline
#else
#define SC2K_FORCE_INLINE inline
#endif

// Keep small hot leaf calls from forcing their callers to spill extra registers.
// Declarations and definitions must use the same calling convention.
#if defined(__clang__) && (defined(__x86_64__) || defined(__aarch64__))
#define SC2K_PRESERVE_MOST __attribute__((preserve_most))
#else
#define SC2K_PRESERVE_MOST
#endif

// Keep shared, repeated helpers out of their callers when duplication grows code.
#if defined(__clang__) || defined(__GNUC__)
#define SC2K_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SC2K_NOINLINE __declspec(noinline)
#else
#define SC2K_NOINLINE
#endif

// Include once so subsequent standard headers do not redefine these names.
#undef bool
#undef false
#undef true

typedef enum bool {
  false = 0,
  true = 1
} bool;
_Static_assert(sizeof(bool) == sizeof(int32_t), "Boolean result ABI width");

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
_Static_assert(sizeof(int) == sizeof(i32), "integer arithmetic uses 32-bit int");

typedef struct PointU16 {
  u16 x, y;
} PointU16;
_Static_assert(sizeof(PointU16) == 4 && offsetof(PointU16, x) == 0 && offsetof(PointU16, y) == 2,
    "unsigned coordinate pair layout");

static inline int IntMin(int a, int b) {
  return a < b ? a : b;
}

static inline int IntMax(int a, int b) {
  return a > b ? a : b;
}

static inline u32 UintMin(u32 a, u32 b) {
  return a < b ? a : b;
}

// Empty ranges resolve to the lower bound, as needed by UI scroll limits.
static inline int IntClamp(int value, int minimum, int maximum) {
  return IntMax(minimum, IntMin(value, maximum));
}

typedef struct { const u8 *data; size_t size; } ByteSpan;

#endif
