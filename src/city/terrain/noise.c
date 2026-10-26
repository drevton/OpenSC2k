#include "internal.h"

// FastNoiseLite OpenSimplex2S, ported from OpenSC2K. See LICENSE.FastNoiseLite.
// Repeat the same gradient cycle to keep the original hash-index lookup.
#define TERRAIN_GRADIENT_CYCLE                                                                                         \
  8554, 64975, 25080, 60547, 39896, 51993, 51993, 39896, 60547, 25080, 64975, 8554, 64975, -8554, 60547, -25080,       \
      51993, -39896, 39896, -51993, 25080, -60547, 8554, -64975, -8554, -64975, -25080, -60547, -39896, -51993,        \
      -51993, -39896, -60547, -25080, -64975, -8554, -64975, 8554, -60547, 25080, -51993, 39896, -39896, 51993,        \
      -25080, 60547, -8554, 64975

static const i32 kGradients2D[256] = {
    TERRAIN_GRADIENT_CYCLE,
    TERRAIN_GRADIENT_CYCLE,
    TERRAIN_GRADIENT_CYCLE,
    TERRAIN_GRADIENT_CYCLE,
    TERRAIN_GRADIENT_CYCLE,
    25080,
    60547,
    60547,
    25080,
    60547,
    -25080,
    25080,
    -60547,
    -25080,
    -60547,
    -60547,
    -25080,
    -60547,
    25080,
    -25080,
    60547,
};
#undef TERRAIN_GRADIENT_CYCLE

// Build immutable fixed-point fractal-noise parameters and normalization for a seed. Frequency and octave count must
// be supported generation values; construction allocates nothing and does not advance a mutable RNG.
TerrainNoise MakeTerrainNoise(u32 seed, Fixed frequency, int octaves) {
  Fixed amplitude = 32768, fractal = FIXED_ONE;
  for (int i = 1; i < octaves; i++) {
    fractal += amplitude;
    amplitude = amplitude / 2;
  }
  return (TerrainNoise){seed, frequency, FixedDiv(FIXED_ONE, fractal), octaves};
}

// Hash a noise lattice corner and compute its fixed-point gradient contribution. Coordinates use the simplex hash
// convention; arithmetic retains the intended signed shift and changes no owner state.
static Fixed TerrainGradient(u32 seed, u32 x, u32 y, Fixed xd, Fixed yd) {
  u32 hash = (seed ^ x ^ y) * 0x27d4eb2d;
  // The source uses an arithmetic signed shift.
  hash ^= (u32)((i32)hash >> 15);
  int index = hash & 254;
  return FixedMul(xd, kGradients2D[index]) + FixedMul(yd, kGradients2D[index | 1]);
}

// Add one positive-radius simplex corner contribution to a caller-owned fixed-point sum. Value must be writable and
// coordinates use the same noise space; corners outside the kernel contribute nothing.
static void AddTerrainCorner(Fixed *value, u32 seed, u32 px, u32 py, Fixed x, Fixed y) {
  Fixed a = (131072 / 3) - FixedMul(x, x) - FixedMul(y, y);
  if (a > 0) *value += FixedMul(FixedMul((FixedMul(a, a)), (FixedMul(a, a))), TerrainGradient(seed, px, py, x, y));
}

// Sample one fixed-point OpenSimplex2S octave at the supplied seeded coordinates. Inputs must fit supported generation
// arithmetic; the result is deterministic with no mutable random state.
static Fixed TerrainOpenSimplex(u32 seed, Fixed x, Fixed y) {
  const u32 prime_x = 501125321, prime_y = 1136930381;
  const Fixed g2 = (196608 - 113512) / 6;
  int floor_x = FixedFloor(x);
  int floor_y = FixedFloor(y);
  Fixed xi = x - FixedFromInt(floor_x), yi = y - FixedFromInt(floor_y);
  u32 i = (u32)floor_x * prime_x, j = (u32)floor_y * prime_y;
  u32 i1 = i + prime_x, j1 = j + prime_y;
  Fixed t = FixedMul((xi + yi), g2), x0 = xi - t, y0 = yi - t;
  Fixed a0 = (131072 / 3) - FixedMul(x0, x0) - FixedMul(y0, y0);
  Fixed value = FixedMul(FixedMul((FixedMul(a0, a0)), (FixedMul(a0, a0))), TerrainGradient(seed, i, j, x0, y0));
  Fixed a1 = FixedMul((FixedMul((FIXED_ONE - g2 * 2) * 2, (FixedDiv(FIXED_ONE, g2) - 131072))), t) +
      ((FixedMul((FIXED_ONE - g2 * 2) * -2, (FIXED_ONE - g2 * 2))) + a0);
  Fixed x1 = x0 - (FIXED_ONE - g2 * 2), y1 = y0 - (FIXED_ONE - g2 * 2);
  value += FixedMul(FixedMul((FixedMul(a1, a1)), (FixedMul(a1, a1))), TerrainGradient(seed, i1, j1, x1, y1));
  Fixed xmyi = xi - yi;
  if (t > g2) {
    if (xi + xmyi > FIXED_ONE)
      AddTerrainCorner(&value, seed, i + prime_x * 2, j1, x0 + (g2 * 3 - 131072), y0 + (g2 * 3 - FIXED_ONE));
    else
      AddTerrainCorner(&value, seed, i, j1, x0 + g2, y0 + (g2 - FIXED_ONE));
    if (yi - xmyi > FIXED_ONE)
      AddTerrainCorner(&value, seed, i1, j + prime_y * 2, x0 + (g2 * 3 - FIXED_ONE), y0 + (g2 * 3 - 131072));
    else
      AddTerrainCorner(&value, seed, i1, j, x0 + (g2 - FIXED_ONE), y0 + g2);
  } else {
    if (xi + xmyi < 0)
      AddTerrainCorner(&value, seed, i - prime_x, j, x0 + (FIXED_ONE - g2), y0 - g2);
    else
      AddTerrainCorner(&value, seed, i1, j, x0 + (g2 - FIXED_ONE), y0 + g2);
    if (yi < xmyi)
      AddTerrainCorner(&value, seed, i, j - prime_y, x0 - g2, y0 - (g2 - FIXED_ONE));
    else
      AddTerrainCorner(&value, seed, i, j1, x0 + g2, y0 + (g2 - FIXED_ONE));
  }
  return FixedMul(value, 1195505);
}

// Sum the configured fixed-point octaves with their normalized amplitudes. Noise must come from valid generation
// parameters; the read-only sample does not allocate or advance any generator.
Fixed SampleTerrainNoise(const TerrainNoise *noise, Fixed x, Fixed y) {
  x = FixedMul(x, noise->frequency);
  y = FixedMul(y, noise->frequency);
  const Fixed f2 = (113512 - FIXED_ONE) / 2;
  Fixed t = FixedMul((x + y), f2);
  x += t;
  y += t;
  u32 seed = noise->seed;
  Fixed sum = 0, amplitude = noise->bounding;
  for (int i = 0; i < noise->octaves; i++) {
    sum += FixedMul(TerrainOpenSimplex(seed++, x, y), amplitude);
    x = x * 2;
    y = y * 2;
    amplitude = amplitude / 2;
  }
  return sum;
}
