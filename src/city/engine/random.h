#ifndef SC2K_CITY_ENGINE_RANDOM_H
#define SC2K_CITY_ENGINE_RANDOM_H

#include "city/types.h"

// Seed calls replace only the selected stream and consume no draw. Modulo draws accept a nonnegative range;
// a zero range still advances its stream and returns zero. AdvanceCityMapSeed computes a value without storing it.
u32 Sc2kCrtRand(City *city);

// Always consume one draw, including for an empty range.
static inline u32 Sc2kCrtRandRange(City *city, int n) {
  int draw = Sc2kCrtRand(city);
  return n ? draw % n : 0;
}

void Sc2kSeedCrtRandom(City *city, u32 seed);
void SeedCityMapRandomState(City *city, u32 seed);
void SeedCityEventRandomState(City *city, u32 seed);
int AdvanceCityMapSeed(u32 seed);
int NextCityMapRandomModulo(City *city, int modulus);
int NextEventOrientationRandom(City *city, int modulus);

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
