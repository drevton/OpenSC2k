#include "city/engine/random.h"

#include "city/state.h"

// Advance the City-owned CRT generator using the MSVC recurrence and return its 15-bit result. This changes only
// random.crt; callers preserve the agreed draw order and must not use it for a read-only query.
u32 Sc2kCrtRand(City *city) {
  return NextCrtRandom(&city->random.crt);
}

// Replace the City's CRT random seed with the supplied deterministic value. This seeds only that generator and does
// not draw a random number.
void Sc2kSeedCrtRandom(City *city, u32 seed) { city->random.crt = seed; }

// Seed the City-owned map generator independently of the event and CRT streams. The supplied seed is stored directly;
// this does not draw or distribute a random value.
void SeedCityMapRandomState(City *city, u32 seed) { city->random.map = seed; }

// Seed the independent event-orientation random generator. Callers must supply the agreed seed; the map and CRT
// generator states are left to their separate seed paths.
void SeedCityEventRandomState(City *city, u32 seed) { city->random.event = seed; }

// Compute the next 16-bit map LFSR value from the supplied seed. Returns the masked recurrence without modifying City;
// callers decide when to commit that next state.
int AdvanceCityMapSeed(u32 seed) {
  int next = (seed & 0xFFFF) << 1;
  return (next ^ ((seed & 0x8000) != 0 ? 0x1BF5 : 0)) & 0xFFFF;
}

// Advance the City event-orientation LCG and return its high 15 bits modulo a nonnegative modulus. Zero still
// consumes a draw and returns zero; only the event stream changes.
int NextEventOrientationRandom(City *city, int modulus) {
  city->random.event = city->random.event * 1103515245 + 12345;
  int random = ((city->random.event >> 16) & 0x7FFF);
  return modulus ? random % modulus : 0;
}

// Advance the map LFSR once and return its 16-bit result modulo a nonnegative modulus. Zero still consumes a draw
// and returns zero; only the map stream changes, so callers must preserve deterministic draw order.
int NextCityMapRandomModulo(City *city, int modulus) {
  int next = AdvanceCityMapSeed(city->random.map);
  city->random.map = next;
  return modulus ? next % modulus : 0;
}
