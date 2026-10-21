#include "city/maps/tile_storage.h"
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

// Hash a seed and lattice coordinate into one climate-noise byte. Uses defined unsigned arithmetic and does not
// consume any simulation or generation random stream.
static int SnowNoiseCorner(u32 seed, int x, int y) {
  u32 hash = seed ^ (u32)x * 0x9E3779B9 ^ (u32)y * 0x85EBCA6B;
  hash ^= hash >> 16;
  hash *= 0x7FEB352D;
  hash ^= hash >> 15;
  return hash & 255;
}

// Return the bytes needed for temperature and moisture lattices at the given map dimensions. Dimensions must be
// supported; the caller provides backing storage before InitializeTerrainSnowNoise.
int TerrainSnowNoiseSize(int width, int height) {
  return 2 * ((width >> 6) + 1) * ((height >> 6) + 1);
}

// Fill temperature/moisture lattices from the climate seed and bind their stride. Snow points must hold
// TerrainSnowNoiseSize bytes; no allocation occurs and the two fields use distinct deterministic seeds.
void InitializeTerrainSnowNoise(CitySnowClimate *snow, int width, int height) {
  snow->stride = (width >> 6) + 1;
  snow->moisture = snow->points + (TerrainSnowNoiseSize(width, height) >> 1);
  for (int y = 0; y <= height >> 6; ++y)
    for (int x = 0; x < snow->stride; ++x) {
      snow->points[y * snow->stride + x] = SnowNoiseCorner(snow->seed, x, y);
      snow->moisture[y * snow->stride + x] = SnowNoiseCorner(snow->seed ^ 0xB5297A4D, x, y);
    }
}

// Cubically interpolate temperature or moisture from the prepared 64-tile lattice. Position and interpolation
// neighbors must fit the initialized map; returns a deterministic integer value without RNG or allocation.
int TerrainClimateValue(const CitySnowClimate *snow, CityPoint position, bool moisture) {
  // One noise lattice point per 64 tiles. Cubic interpolation gives broad,
  // smooth changes without per-tile randomness, allocations or mutable RNG.
  int x = position.x >> 6, y = position.y >> 6;
  int fx = (position.x & 63) << 2, fy = (position.y & 63) << 2;
  fx = fx * fx * (768 - 2 * fx) >> 16;
  fy = fy * fy * (768 - 2 * fy) >> 16;
  const u8 *point = (moisture ? snow->moisture : snow->points) + y * snow->stride + x;
  int a = point[0], b = point[1];
  int c = point[snow->stride], d = point[snow->stride + 1];
  int top = a + ((b - a) * fx >> 8), bottom = c + ((d - c) * fx >> 8);
  return top + ((bottom - top) * fy >> 8);
}

// Sample the regional snow-line variation, or return the disabled/constant level directly. Snow data must be
// initialized for the position; no permanent climate metadata is modified.
int TerrainSnowLineAt(const CitySnowClimate *snow, CityPoint position) {
  if (snow->line == CITY_SNOW_LINE_DISABLED || !snow->variation) return snow->line;
  int value = TerrainClimateValue(snow, position, false) - 128;
  return snow->line + ((value * snow->variation + 64) >> 7);
}

// Map the sampled temperature to arctic, temperate or tropical base grass. Requires an initialized climate lattice and
// in-range position; this is a read-only appearance selection.
int TerrainGrassPaletteAt(const CitySnowClimate *snow, CityPoint position) {
  int temperature = TerrainClimateValue(snow, position, false);
  if (temperature < 96) return CLIMATE_ARCTIC;
  return temperature >= 160 ? CLIMATE_TROPICAL : CLIMATE_TEMPERATE;
}

// Strip snow appearance to the persistent underlying climate category. Climate must index the supported climate table;
// result is independent of map height and random state.
int TerrainBaseClimate(int climate) {
  static const u8 kBaseClimate[CLIMATE_COUNT] = {
    CLIMATE_SIMCITY, CLIMATE_TEMPERATE, CLIMATE_ARCTIC,
    CLIMATE_ARCTIC, CLIMATE_ARCTIC, CLIMATE_ARCTIC, CLIMATE_ARCTIC,
    CLIMATE_TROPICAL, CLIMATE_RAINFOREST, CLIMATE_DESERT_50, CLIMATE_DESERT_100,
    CLIMATE_TEMPERATE_ARCTIC, CLIMATE_TEMPERATE_TROPICAL, CLIMATE_ARCTIC_TROPICAL,
    CLIMATE_TEMPERATE_ARCTIC, CLIMATE_ARCTIC_TROPICAL
  };
  return kBaseClimate[climate];
}

// Test whether a base climate permits snow appearance. The supplied value is a climate category; the predicate does
// not infer permanent ground from elevation.
bool TerrainClimateSupportsSnow(int climate) {
  return climate == CLIMATE_ARCTIC || climate == CLIMATE_TEMPERATE_ARCTIC ||
      climate == CLIMATE_ARCTIC_TROPICAL;
}

// Select snow coverage from a base grass climate and height above the local snow line. Negative heights preserve
// grass; unsupported grass categories remain unchanged and no map storage is written.
int TerrainSnowClimate(int grass, int height_above_line) {
  if (height_above_line < 0) return grass;
  if (grass == CLIMATE_ARCTIC) return CLIMATE_SNOW_25 + IntMin(height_above_line, 3);
  if (grass == CLIMATE_TEMPERATE_ARCTIC) return CLIMATE_SNOW_TEMPERATE_ARCTIC_50;
  if (grass == CLIMATE_ARCTIC_TROPICAL) return CLIMATE_SNOW_ARCTIC_TROPICAL_50;
  return grass;
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
