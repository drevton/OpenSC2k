#include "city/terrain/climate.h"

#include "city/maps/tile_storage.h"

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
  int rows = (height >> 6) + 1;
  u32 seed = snow->seed, moisture_seed = seed ^ 0xB5297A4D;
  for (int y = 0; y < rows; ++y) {
    u8 *temperature = snow->points + y * snow->stride;
    u8 *moisture = snow->moisture + y * snow->stride;
    for (int x = 0; x < snow->stride; ++x) {
      temperature[x] = SnowNoiseCorner(seed, x, y);
      moisture[x] = SnowNoiseCorner(moisture_seed, x, y);
    }
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
    [CLIMATE_SIMCITY] = CLIMATE_SIMCITY,
    [CLIMATE_TEMPERATE] = CLIMATE_TEMPERATE,
    [CLIMATE_ARCTIC] = CLIMATE_ARCTIC,
    [CLIMATE_SNOW_25] = CLIMATE_ARCTIC,
    [CLIMATE_SNOW_50] = CLIMATE_ARCTIC,
    [CLIMATE_SNOW_75] = CLIMATE_ARCTIC,
    [CLIMATE_SNOW_100] = CLIMATE_ARCTIC,
    [CLIMATE_TROPICAL] = CLIMATE_TROPICAL,
    [CLIMATE_RAINFOREST] = CLIMATE_RAINFOREST,
    [CLIMATE_DESERT_50] = CLIMATE_DESERT_50,
    [CLIMATE_DESERT_100] = CLIMATE_DESERT_100,
    [CLIMATE_TEMPERATE_ARCTIC] = CLIMATE_TEMPERATE_ARCTIC,
    [CLIMATE_TEMPERATE_TROPICAL] = CLIMATE_TEMPERATE_TROPICAL,
    [CLIMATE_ARCTIC_TROPICAL] = CLIMATE_ARCTIC_TROPICAL,
    [CLIMATE_SNOW_TEMPERATE_ARCTIC_50] = CLIMATE_TEMPERATE_ARCTIC,
    [CLIMATE_SNOW_ARCTIC_TROPICAL_50] = CLIMATE_ARCTIC_TROPICAL,
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

