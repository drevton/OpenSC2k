#ifndef SC2K_CITY_ENGINE_RANDOM_H
#define SC2K_CITY_ENGINE_RANDOM_H

#include "city/types.h"

// Shared MSVC recurrence; inlining the route's many draws preserves every
// state update while avoiding a call and City pointer reload per search step.
static inline u32 NextCrtRandom(u32 *state) {
  *state = *state * 214013 + 2531011;
  return (*state >> 16) & 0x7FFF;
}

// Independent xorshift stream for native ground and tree evolution; callers must preserve draw order.
static inline u32 NextLandscapeRandom(u32 *state) {
  u32 value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

#endif
