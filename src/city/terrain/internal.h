#ifndef SC2K_TERRAIN_INTERNAL_H
#define SC2K_TERRAIN_INTERNAL_H

#include "base/fixed.h"
#include <stdlib.h>
#include <string.h>

#include "terrain.h"

// OpenSC2K terrain port; see LICENSE.OpenSC2K and README.md.
// The original map size is the reference spacing for generator feature density.
enum { kTerrainEdge = 128, kTerrainLast = kTerrainEdge - 1,
    kTerrainCells = kTerrainEdge * kTerrainEdge };

enum {
  kSaltWater = 1, kSaltyChannel = 2, kWater = 4, kTreeFirst = 6, kTreeLast = 12,
  kRiverWater = 32, // Generation-only channel identity, independent of salinity and slope.
  kFlat = 0, kDeepWater = 0x10, kShore = 0x20, kForbiddenCoast = 0x2e,
  kSurfaceWater = 0x30, kWaterfall = 0x3e
};
typedef struct {
  Fixed x, y;
} TerrainVector;
typedef struct {
  u32 seed;
  Fixed frequency, bounding;
  int octaves;
} TerrainNoise;
typedef struct {
  void *storage;
  int width, height, cells;
  int *heights, *scratch, *queue;
  // During river routing: heights = heap positions, scratch = distance/cost,
  // queue = BFS FIFO/A* heap, seen = visited/closed/predecessor bits.
  // During water normalization, seen packs queue membership and cached slope;
  // water-level edits invalidate only that tile and its cardinal neighbors.
  // Altitude retains land heights until heights is rebuilt after routing.
  u8 *seen, *water_marks, *flags;
  // water_marks starts zero; routing stores a temporary drainage direction
  // in its high nibble and outgoing stream-edge bits in the low.
  // After river normalization it is reused for freshwater-bank distances.
  u16 *altitude;
  u8 *terrain, *buildings;
  u32 process_state, game_state, features;
  int sea, hills, water, trees;
  u8 coast_sides, smoothness;
  bool ocean, river;
  u8 height_shift; // Supported power-of-two axes; uses existing tail padding.
} TerrainWork;
static const int kNeighborX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int kNeighborY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static const int kNeighborMasks[8] = {3, 2, 6, 4, 12, 8, 9, 1};
static const Fixed kTerrainPi = 205887;
static inline int TerrainMainRiverCount(const TerrainWork *work) {
  int count = 1;
  for (int span = IntMin(work->width, work->height); span > 256 && count < 4; span >>= 1) ++count;
  return count;
}
static inline Fixed TerrainLerp(Fixed a, Fixed b, Fixed t) { return a + FixedMul((b - a), t); }
static inline Fixed TerrainSmoothstep(Fixed a, Fixed b, Fixed value) {
  if (a == b || FixedAbs(a - b) < FixedMax(FixedMul(1, FixedAbs(a)), 1))
    return FixedFromInt(a <= b ? (value <= a ? 0 : 1) : (value <= b ? 1 : 0));
  Fixed t = FixedMax(0, FixedMin(FIXED_ONE, FixedDiv((value - a), (b - a))));
  return FixedMul(FixedMul(t, t), (196608 - t * 2));
}
static inline TerrainVector TerrainV(Fixed x, Fixed y) { return (TerrainVector){x, y}; }
static inline TerrainVector TerrainAdd(TerrainVector a, TerrainVector b) {
  return (TerrainVector){a.x + b.x, a.y + b.y};
}
static inline TerrainVector TerrainSub(TerrainVector a, TerrainVector b) {
  return (TerrainVector){a.x - b.x, a.y - b.y};
}
static inline TerrainVector TerrainDivide(TerrainVector a, TerrainVector b) {
  return (TerrainVector){FixedDiv(a.x, b.x), FixedDiv(a.y, b.y)};
}
static inline TerrainVector TerrainScale(TerrainVector a, Fixed scalar) {
  Fixed s = scalar;
  return (TerrainVector){FixedMul(a.x, s), FixedMul(a.y, s)};
}
static inline TerrainVector TerrainDivideScalar(TerrainVector a, Fixed scalar) {
  Fixed s = scalar;
  return (TerrainVector){FixedDiv(a.x, s), FixedDiv(a.y, s)};
}
static inline Fixed TerrainLengthSquared(TerrainVector a) { return FixedMul(a.x, a.x) + FixedMul(a.y, a.y); }
static inline Fixed TerrainLength(TerrainVector a) { return FixedSqrt(TerrainLengthSquared(a)); }
static inline Fixed TerrainAngle(TerrainVector a) { return FixedAtan2(a.y, a.x); }
static inline TerrainVector TerrainRotate(TerrainVector a, Fixed angle) {
  Fixed v = angle, sine = FixedSin(v), cosine = FixedCos(v);
  return (TerrainVector){FixedMul(a.x, cosine) - FixedMul(a.y, sine), FixedMul(a.x, sine) + FixedMul(a.y, cosine)};
}
static inline TerrainVector TerrainMapPoint(const TerrainWork *work, int x, int y, Fixed angle) {
  return TerrainRotate(
      TerrainV(FixedFromInt(x) / (work->width - 1) - 32768, FixedFromInt(y) / (work->height - 1) - 32768), -angle);
}
static inline TerrainVector TerrainDetailPoint(const TerrainWork *work, int x, int y, Fixed angle) {
  return TerrainRotate(TerrainV((FixedFromInt(x) - 32768 * (work->width - 1)) / kTerrainLast,
                           (FixedFromInt(y) - 32768 * (work->height - 1)) / kTerrainLast),
      -angle);
}
static inline int TerrainProcessRandom(TerrainWork *work) {
  work->process_state = work->process_state * 214013 + 2531011;
  return (work->process_state >> 16) & 0x7fff;
}
static inline int TerrainGameRandom(TerrainWork *work, int divisor) {
  work->game_state = work->game_state * 1103515245 + 12345;
  return ((work->game_state >> 16) & 0x7fff) % divisor;
}
TerrainNoise MakeTerrainNoise(u32 seed, Fixed frequency, int octaves);
Fixed SampleTerrainNoise(const TerrainNoise *noise, Fixed x, Fixed y);
void CarveTerrainFeatures(TerrainWork *work, u32 generation_seed);
void TerrainWaterDistances(TerrainWork *work);
void ApplyTerrainElevation(TerrainWork *work, Fixed angle, Fixed phase, const TerrainNoise *noise);
// Generation samples the prepared temperature/moisture lattice without changing it.
int TerrainClimateValue(const CitySnowClimate *snow, CityPoint position, bool moisture);
int TerrainGrassPaletteAt(const CitySnowClimate *snow, CityPoint position);
#endif
