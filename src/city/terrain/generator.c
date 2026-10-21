#include "city/maps/tile_storage.h"
#include <limits.h>

#include "city/maps/map_types.h"
#include "city/maps/tables.h"
#include "internal.h"

// Native city terrain: continuous relief, city-scale waterways and cached climate.

enum {
  kGeneratedSeaLevel = 4,
  kConstructionLandOffset = 3,
  kMaximumConstructionHeight = CITY_ALTITUDE_LAND_MASK - kGeneratedSeaLevel + kConstructionLandOffset,
  // Generation-only flag for water placed on existing ground after carving.
  // Broad river/lake beds retain their depth; streams and scattered ponds do not.
  kGroundLevelWater = 8,
  kInclinedWater = 16 // Water follows the bed, unlike a submerged bank slope.
};

// Allocate and bind the generator's single temporary backing block for all work planes. Requires supported dimensions
// and an empty work owner; false leaves allocation unavailable and successful storage must later be freed.
static bool AllocateTerrainWork(TerrainWork *work, int width, int height) {
  size_t cells = (size_t)width * height;
  u8 *storage = calloc(cells, 19);
  if (!storage) return false;
  work->storage = storage;
  work->width = width; work->height = height; work->cells = cells;
  work->height_shift = __builtin_ctz(height);
  work->heights = (int *)storage;
  work->scratch = (int *)(storage + 4 * cells);
  work->queue = (int *)(storage + 8 * cells);
  work->altitude = (u16 *)(storage + 12 * cells);
  work->seen = storage + 14 * cells;
  work->water_marks = storage + 15 * cells;
  work->flags = storage + 16 * cells;
  work->terrain = storage + 17 * cells;
  work->buildings = storage + 18 * cells;
  return true;
}

// Release generated map storage and reset the output object. NULL is accepted; callers must not retain tile, climate
// or other views into the released backing.
void ReleaseCityTerrainMap(CityTerrainMap *map) {
  if (!map) return;
  free(map->storage);
  *map = (CityTerrainMap){0};
}

// Build continuous multi-scale initial terrain relief using the selected smoothness. Requires allocated generation
// arrays and seeded process state; writes heights using the generator's column-major indexing.
static void SeedTerrainHeights(TerrainWork *work, int smoothness) {
  // Continuous multi-scale relief, with detail amplitudes selected by
  // smoothness. Cache the field every four tiles, retaining fractional heights
  // until final interpolation instead of the old integer midpoint subdivision.
  static const int kAmplitudes[CITY_TERRAIN_SMOOTHNESS_COUNT][5] = {
    {1024, 256, 64, 8, 0}, {1024, 512, 192, 48, 8},
    {1024, 704, 384, 192, 48}, {1024, 896, 640, 384, 128}
  };
  static const int kLowRelief[] = {0, 1, 2, 3};
  TerrainNoise bands[5];
  for (int band = 0; band < 5; ++band)
    bands[band] = MakeTerrainNoise(work->game_state ^ (u32)(band + 1) * 0x9E3779B9, FIXED_ONE / (256 >> band), 1);
  int stride = (work->height >> 2) + 1, columns = (work->width >> 2) + 1;
  int minimum = INT_MAX, maximum = INT_MIN;
  for (int x = 0; x < columns; ++x) for (int y = 0; y < stride; ++y) {
      Fixed value = 0;
      for (int band = 0; band < 5; ++band)
        value += SampleTerrainNoise(&bands[band], FixedFromInt(x * 4 + 19), FixedFromInt(y * 4 - 31)) *
            kAmplitudes[smoothness][band];
      int sample = (int)FixedRound(value * 256);
      work->scratch[x * stride + y] = sample;
      minimum = IntMin(minimum, sample);
      maximum = IntMax(maximum, sample);
  }
  int relief = IntMin(kMaximumConstructionHeight - work->sea,
      (work->hills * 27 + 23) / 47 + kLowRelief[smoothness]);
  for (int x = 0; x < work->width; ++x) for (int y = 0; y < work->height; ++y) {
    const int *points = work->scratch + (x >> 2) * stride + (y >> 2);
    Fixed fx = FixedFromInt((x & 3)) / 4, fy = FixedFromInt((y & 3)) / 4;
    Fixed left = TerrainLerp(FixedFromInt(points[0]), FixedFromInt(points[1]), fy);
    Fixed right = TerrainLerp(FixedFromInt(points[stride]), FixedFromInt(points[stride + 1]), fy);
    Fixed normalized = (TerrainLerp(left, right, fx) - FixedFromInt(minimum)) / IntMax(maximum - minimum, 1);
    // Squaring leaves extensive low plains for city construction.
    work->heights[x * work->height + y] = work->sea + (int)FixedRound(FixedMul(normalized, normalized) * relief);
  }
}

// Map a normalized height through a selected regional elevation curve. Curve must index the supported immutable curve
// table; fixed-point interpolation is deterministic and changes no state.
static Fixed TerrainRegionalHeight(int curve, Fixed height) {
  // TGP HeightMapCurves: low plains, foothills, hills, and high plateaus.
  // Coordinates are fractions of this seed's height range.
  static const Fixed kCurves[4][5][2] = {{{0, 0}, {52429, 8520}, {FIXED_ONE, 26214}},
      {{0, 0}, {34734, 8520}, {52429, 17695}, {FIXED_ONE, 39322}},
      {{0, 0}, {34734, 17695}, {52429, 37356}, {FIXED_ONE, 52429}},
      {{0, 0}, {26214, 19661}, {45875, 52429}, {60293, 64881}, {FIXED_ONE, 64881}}};
  static const int kCounts[] = {3, 4, 4, 5};
  for (int i = 1; i < kCounts[curve]; ++i) {
    const Fixed *a = kCurves[curve][i - 1], *b = kCurves[curve][i];
    if (height <= b[0]) return TerrainLerp(a[1], b[1], FixedDiv((height - a[0]), (b[0] - a[0])));
  }
  return kCurves[curve][kCounts[curve] - 1][1];
}

// Blend regional height curves and seeded detail into the current relief. Requires initialized heights and a supported
// variety option; NONE leaves the relief unchanged.
static void DistributeTerrainVariety(TerrainWork *work, int variety, u32 seed) {
  if (variety == CITY_TERRAIN_VARIETY_NONE) return;
  // More variety means smaller regions, as in TGP. A tiny generation-only
  // grid in the existing queue blends the curves at fixed tile spacing.
  // No saved layer or render-time work is needed.
  int regions = 1 << variety;
  int stride = ((work->width - 1) * regions + kTerrainLast - 1) / kTerrainLast + 1;
  int rows = ((work->height - 1) * regions + kTerrainLast - 1) / kTerrainLast + 1;
  u8 *curves = (u8 *)work->queue;
  u32 state = seed ^ 0x56415259;
  for (int i = 0; i < stride * rows; ++i) {
    state = state * 1664525 + 1013904223;
    curves[i] = state >> 30;
  }
  int minimum = work->sea, maximum = minimum;
  for (int at = 0; at < work->cells; ++at) {
    maximum = IntMax(maximum, work->heights[at]);
  }
  // Keep a little regional relief at minimum Height, even on flat lowlands.
  // Fade this out before ordinary hills take over.
  Fixed lowland_relief = (131072 * (12 - IntMin(work->hills, 12))) / 12;
  if (minimum == maximum && lowland_relief == 0) return;
  for (int x = 0; x < work->width; ++x) {
    Fixed gx = (FixedFromInt(x) * regions) / kTerrainLast;
    int ix = IntMin((int)FixedToInt(gx), stride - 2);
    Fixed wx = TerrainSmoothstep(0, FIXED_ONE, gx - FixedFromInt(ix));
    for (int y = 0; y < work->height; ++y) {
      Fixed gy = (FixedFromInt(y) * regions) / kTerrainLast;
      int iy = IntMin((int)FixedToInt(gy), rows - 2), at = x * work->height + y;
      if (work->heights[at] < minimum) continue;
      // Keep flat banks beside carved water; inland lowlands may rise gently.
      if (work->heights[at] == minimum &&
          ((x && work->heights[at - work->height] < minimum) ||
           (x < work->width - 1 && work->heights[at + work->height] < minimum) ||
           (y && work->heights[at - 1] < minimum) ||
           (y < work->height - 1 && work->heights[at + 1] < minimum))) continue;
      Fixed wy = TerrainSmoothstep(0, FIXED_ONE, gy - FixedFromInt(iy));
      Fixed h = FixedFromInt((work->heights[at] - minimum)) / IntMax(maximum - minimum, 1);
      Fixed low = TerrainLerp(
          TerrainRegionalHeight(curves[ix * rows + iy], h), TerrainRegionalHeight(curves[(ix + 1) * rows + iy], h), wx);
      Fixed high = TerrainLerp(TerrainRegionalHeight(curves[ix * rows + iy + 1], h),
          TerrainRegionalHeight(curves[(ix + 1) * rows + iy + 1], h), wx);
      Fixed regional_low =
          TerrainLerp(FixedFromInt(curves[ix * rows + iy]), FixedFromInt(curves[(ix + 1) * rows + iy]), wx);
      Fixed regional_high =
          TerrainLerp(FixedFromInt(curves[ix * rows + iy + 1]), FixedFromInt(curves[(ix + 1) * rows + iy + 1]), wx);
      Fixed relief = FixedMul(lowland_relief, TerrainLerp(regional_low, regional_high, wy)) / 3;
      work->heights[at] = minimum + (int)FixedRound(TerrainLerp(low, high, wy) * (maximum - minimum) + relief);
    }
  }
}

// Carve the classic broad river course while leaving connected buildable regions. Requires initialized work and
// generation RNG; updates heights and water flags without touching a live City.
static void CarveClassicRiver(TerrainWork *work) {
  // A few regional waterways leave large connected building areas. Width
  // stays in tiles; the original 128-square channel keeps its visit order.
  int width = 5 + work->water * 20 / 47, fraction = work->water * 20 % 47;
  int rivers = TerrainMainRiverCount(work);
  for (int river = 0; river < rivers; ++river) {
    int left_edge = work->width * river / rivers, right_edge = work->width * (river + 1) / rivers;
    int center = (left_edge + right_edge) / 2;
    if (work->cells > kTerrainCells)
      center = left_edge + (right_edge - left_edge) * (35 + TerrainGameRandom(work, 31)) / 100;
    int bend = TerrainGameRandom(work, 3) - 1;
    for (int y = work->height - 1; y >= 0; --y) {
      int extent = width + ((y * 17 % 47) < fraction);
      int left = extent / 2, right = extent - left - 1;
      for (int x = center - left; x <= center + right; ++x) {
        int at = x * work->height + y;
        work->heights[at] = work->sea - 3;
        work->flags[at] |= kRiverWater;
        if (work->features & kSalty) work->flags[at] |= kSaltyChannel;
      }
      work->heights[(center - left - 1) * work->height + y] = work->sea - 2;
      work->heights[(center + right + 1) * work->height + y] = work->sea - 2;
      if (TerrainGameRandom(work, 8) == 0) bend = TerrainGameRandom(work, 3) - 1;
      center = IntClamp(center + bend + TerrainGameRandom(work, 3) - 1, left_edge + 14, right_edge - 15);
    }
  }
}

// Choose coherent wet regions by height/noise quantiles instead of flooding an entire equal-height plain. Requires
// initialized relief; updates sea level, heights and flags using reusable scratch.
static void CarveNaturalTerrainWater(TerrainWork *work) {
  // Quantiles avoid flooding an entire height-four plain in one slider step.
  // Coherent noise separates equal-height basins without speckled water. This
  // uses only the existing generation scratch buffers at the actual map size.
  if (!work->water) { work->sea = 0; return; }
  enum { kWaterHeightDetail = 512 };
  memset(work->queue, 0, kTerrainCells * sizeof(*work->queue));
  TerrainNoise noise = MakeTerrainNoise(work->game_state ^ 0x57415452, 393216, 3);
  for (int x = 0; x < work->width; ++x) for (int y = 0; y < work->height; ++y) {
    int at = x * work->height + y;
    int detail = IntClamp(
        (int)FixedToInt(((SampleTerrainNoise(&noise, FixedFromInt(x) / 127, FixedFromInt(y) / 127) + FIXED_ONE) * 256)),
        0, 511);
    int score = IntClamp(work->heights[at], 0, 31) * kWaterHeightDetail + detail;
    work->scratch[at] = score;
    ++work->queue[score];
  }
  // Natural layouts range from dry land to about forty percent water.
  int goal = work->cells * work->water * 2 / (47 * 5), cutoff = 0, total = 0;
  while (cutoff < kTerrainCells - 1 && total + work->queue[cutoff] < goal)
    total += work->queue[cutoff++];
  int remaining = goal - total;
  work->sea = IntClamp(cutoff / kWaterHeightDetail, 1, 30);
  for (int at = 0; at < work->cells; ++at) {
    bool wet = work->scratch[at] < cutoff || (work->scratch[at] == cutoff && remaining-- > 0);
    work->heights[at] = wet ? IntMin(work->heights[at], work->sea - 2) :
        IntMax(work->heights[at], work->sea);
  }
}

// Limit height differences by the eight-neighbor distance envelope in both scan directions. Heights must hold the
// complete work-sized array and may be a scratch copy; work geometry is read-only.
static void GradeTerrainHeights(const TerrainWork *work, int *heights) {
  // Minimum envelope of initial height plus eight-neighbor distance. This
  // also enforces cardinal grading; no separate four-neighbor pass is needed.
  for (int pass = 0; pass < 2; pass++) {
    int direction = pass ? -1 : 1;
    for (int x = pass ? work->width - 1 : 0; x >= 0 && x < work->width; x += direction) {
      for (int y = pass ? work->height - 1 : 0; y >= 0 && y < work->height; y += direction) {
        int at = x * work->height + y, height = heights[at], nx = x - direction, ny = y - direction;
        if (nx >= 0 && nx < work->width) {
          int near = nx * work->height + y;
          height = IntMin(height, heights[near] + 1);
          if (y > 0) height = IntMin(height, heights[near - 1] + 1);
          if (y < work->height - 1) height = IntMin(height, heights[near + 1] + 1);
        }
        if (ny >= 0 && ny < work->height) height = IntMin(height, heights[at - direction] + 1);
        heights[at] = height;
      }
    }
  }
}

// Repair unsupported generated corner combinations through a bounded work queue. Requires initialized terrain heights
// and workspace; preserve_water controls whether admitted water cells can be changed.
static void FillUnsupportedTerrainSlopes(TerrainWork *work, bool preserve_water) {
  // Scan the initial tiles directly, then revisit changed neighbors through
  // the bounded FIFO. Future initial tiles already have a scheduled visit.
  int head = 0, tail = 0, count = 0, initial = 0;
  memset(work->seen, 0, work->cells);
  while (initial < work->cells || count) {
    int at;
    if (initial < work->cells) at = initial++;
    else {
      at = work->queue[head];
      head = (head + 1) % work->cells;
      --count;
      work->seen[at] = false;
    }
    if (preserve_water && (work->flags[at] & kWater)) continue;
    int x = at >> work->height_shift, y = at & (work->height - 1);
    int height = work->heights[at], maximum = height, mask = 0;
    if (x > 0 && y > 0 && x + 1 < work->width && y + 1 < work->height) {
      const int *h = work->heights + at;
      int stride = work->height;
      int north = h[-1], north_east = h[stride - 1], east = h[stride], south_east = h[stride + 1];
      int south = h[1], south_west = h[1 - stride], west = h[-stride], north_west = h[-stride - 1];
      maximum = IntMax(height, IntMax(
          IntMax(IntMax(north, north_east), IntMax(east, south_east)),
          IntMax(IntMax(south, south_west), IntMax(west, north_west))));
      mask = (north > height ? kNeighborMasks[0] : 0) | (north_east > height ? kNeighborMasks[1] : 0) |
          (east > height ? kNeighborMasks[2] : 0) | (south_east > height ? kNeighborMasks[3] : 0) |
          (south > height ? kNeighborMasks[4] : 0) | (south_west > height ? kNeighborMasks[5] : 0) |
          (west > height ? kNeighborMasks[6] : 0) | (north_west > height ? kNeighborMasks[7] : 0);
    } else {
      for (int d = 0; d < 8; d++) {
        int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
        if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
        int near = work->heights[nx * work->height + ny];
        maximum = IntMax(maximum, near);
        if (near > height) mask |= kNeighborMasks[d];
      }
    }
    int target = IntMax(height, maximum - 1);
    if (mask == 5 || mask == 10 || mask == 15) target = IntMax(target, height + 1);
    if (target == height) continue;
    work->heights[at] = target;
    for (int d = 0; d < 8; d++) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if (next < initial && !work->seen[next] && (!preserve_water || !(work->flags[next] & kWater))) {
        work->seen[next] = true;
        work->queue[tail] = next;
        tail = (tail + 1) % work->cells;
        count++;
      }
    }
  }
}

// Extend carved ocean salinity across its graded coastline without flooding connected freshwater channels. Requires
// completed initial carving and grading; updates generation flags in place.
static void PrepareTerrainCoasts(TerrainWork *work) {
  // Extend the carved ocean footprint across its graded bank in tile units.
  // A water flood fill would swallow connected freshwater channels. Ordinary
  // coasts have a one-level bed and need no extra flooded bank.
  int passes = work->features & kRandomCoast ? 4 : 0;
  for (int pass = 0; pass < passes; ++pass) {
    memcpy(work->seen, work->flags, work->cells);
    for (int x = 0; x < work->width; ++x) {
      for (int y = 0; y < work->height; ++y) {
        int at = x * work->height + y;
        if (!(work->seen[at] & kSaltWater)) continue;
        for (int d = 0; d < 8; ++d) {
          int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
          if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height)
            work->flags[nx * work->height + ny] |= kSaltWater;
        }
      }
    }
  }
}

// Encode graded heights into generated altitude, slope and water/bank records. Requires complete height and flag
// planes; output remains generation-format data until the native import boundary.
static void RetileGeneratedTerrain(TerrainWork *work) {
  for (int at = 0; at < work->cells; at++) work->altitude[at] = work->heights[at] & 31;
  for (int at = 0; at < work->cells; at++) {
    int x = at >> work->height_shift, y = at & (work->height - 1), land = work->altitude[at] & 31, mask = 0;
    for (int d = 0; d < 8; d++) {
      // Use the generator's constant offsets, then transpose its corner bits
      // into native city axes below.
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height && (work->altitude[nx * work->height + ny] & 31) > land)
        mask |= kNeighborMasks[d];
    }
    mask = (mask & 5) | ((mask & 2) << 2) | ((mask & 8) >> 2);
    int shape = kTerrainShapeByHigherNeighbors[mask];
    bool basin = shape == CITY_TERRAIN_RAISE_LEVEL;
    if (basin) {
      land = IntMin(land + 1, 31);
      work->altitude[at] = (work->altitude[at] & ~31) | land;
      shape = kFlat;
    }
    bool raised = (work->flags[at] & kSaltWater) != 0;
    if (land > work->sea || (land == work->sea && !raised)) {
      work->flags[at] &= ~(kWater | kSaltWater);
      work->terrain[at] = shape;
    } else {
      work->flags[at] |= kWater;
      work->altitude[at] = (work->altitude[at] & ~0x3e0) | (work->sea << 5);
      if (raised)
        work->terrain[at] = land == work->sea && shape ? kShore + shape : kDeepWater;
      else
        work->terrain[at] = basin ? kDeepWater : shape + (work->sea - land == 1 ? kShore : kDeepWater);
    }
  }
}

// Grading can widen a carved channel beyond its marked center. Mark its connected inland water using the existing
// queue, stopping at ocean tiles; isolated lakes retain open-water behavior. No RNG or new storage is needed.
static void MarkGeneratedRiverChannels(TerrainWork *work) {
  int count = 0;
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kSaltWater)) != kWater) {
      work->flags[at] &= ~kRiverWater;
      continue;
    }
    if (work->flags[at] & kRiverWater) work->queue[count++] = at;
  }
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    for (int direction = 0; direction < 8; ++direction) {
      int nx = x + kNeighborX[direction], ny = y + kNeighborY[direction];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->flags[next] & (kWater | kSaltWater | kRiverWater)) != kWater) continue;
      work->flags[next] |= kRiverWater;
      work->queue[count++] = next;
    }
  }
}

// Propagate marked river salinity after freshwater geometry is tiled. Reuses the queue and updates salt flags without
// expanding coast geometry or consuming additional map storage.
static void FinishTerrainSaltyRiver(TerrainWork *work) {
  // Resolve salinity only after the freshwater footprint has been tiled.
  // Feeding these seeds to PrepareTerrainCoasts would expand the channel and
  // turn equal-height dry banks into water. The marker never leaves generation.
  int count = 0;
  for (int at = 0; at < work->cells; ++at) {
    if (!(work->flags[at] & kSaltyChannel)) continue;
    work->flags[at] &= ~kSaltyChannel;
    if (!(work->flags[at] & kWater) || (work->flags[at] & kSaltWater)) continue;
    work->flags[at] |= kSaltWater;
    work->queue[count++] = at;
  }
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    for (int d = 0; d < 8; d += 2) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->flags[next] & (kWater | kSaltWater)) != kWater) continue;
      work->flags[next] |= kSaltWater;
      work->queue[count++] = next;
    }
  }
}

// Populate generated forest sites from coherent scores and the requested tree density. Requires finalized land/water
// geometry and initialized generation RNG; queue/scratch are reused and disabled trees leave the map unchanged.
static void GrowTerrainTrees(TerrainWork *work) {
  if (!work->trees) return;
  enum { kForestScores = 1024 };
  memset(work->queue, 0, kForestScores * sizeof(*work->queue));
  TerrainNoise noise = MakeTerrainNoise(work->process_state ^ 0x464F5253, FIXED_ONE / 32, 3);
  int candidates = 0;
  for (int x = 0; x < work->width; ++x) for (int y = 0; y < work->height; ++y) {
    int at = x * work->height + y;
    work->scratch[at] = -1;
    if (work->flags[at] & kWater) continue;
    int score = IntClamp(
        (int)FixedToInt(((SampleTerrainNoise(&noise, FixedFromInt(x), FixedFromInt(y)) + FIXED_ONE) * 512)), 0,
        kForestScores - 1);
    work->scratch[at] = score;
    ++work->queue[score];
    ++candidates;
  }
  // Coverage rises with Trees; coherent scores make forest stands. Species and
  // tree age are assigned by native climate decoration after generation.
  int goal = (i64)candidates * work->trees * 4 / (47 * 5), cutoff = kForestScores - 1, total = 0;
  while (cutoff > 0 && total + work->queue[cutoff] < goal) total += work->queue[cutoff--];
  int remaining = goal - total;
  for (int at = 0; at < work->cells; ++at) {
    int score = work->scratch[at];
    if (score < cutoff || (score == cutoff && remaining-- <= 0)) continue;
    work->buildings[at] = kTreeFirst + TerrainProcessRandom(work) % (kTreeLast - kTreeFirst + 1);
  }
}

// Decode the packed water-surface level of a valid generation-array index. Requires initialized altitude bytes; this
// reads generator encoding rather than native City geometry.
static int ReadGeneratedWaterHeight(const TerrainWork *work, int at) {
  return (work->altitude[at] & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
}

// Add confined ground-level water to a valid generation cell, preserving existing water outlets and protected coast
// and waterfall categories. Pools and streams must not flood their surrounding terrace. Updates terrain, altitude,
// flags and building state without allocating; carved lakes and oceans keep their existing open-water behavior.
static void MakeTerrainWater(TerrainWork *work, int at) {
  int current = work->terrain[at];
  if ((work->flags[at] & kWater) || current == kForbiddenCoast || current == kWaterfall) return;
  bool slope = current > kFlat && current < kDeepWater;
  work->terrain[at] = slope ? kWaterfall : kSurfaceWater;
  work->buildings[at] = 0;
  int water = (work->altitude[at] & CITY_ALTITUDE_LAND_MASK) + slope;
  work->altitude[at] = (work->altitude[at] & ~CITY_ALTITUDE_WATER_MASK) | (water << CITY_ALTITUDE_WATER_SHIFT);
  work->flags[at] |= kWater | kGroundLevelWater | kRiverWater;
}

// Select coherent flat freshwater pools using a seed and the water-density target. Requires tiled generation geometry;
// uses existing score workspace and leaves water disabled when its target is zero.
static void ScatterTerrainFreshwater(TerrainWork *work, u32 seed) {
  if (!work->water) return;
  enum { kPondScores = 1024 };
  memset(work->queue, 0, kPondScores * sizeof(*work->queue));
  TerrainNoise noise = MakeTerrainNoise(seed ^ 0x504F4E44, FIXED_ONE / 24, 3);
  int candidates = 0;
  for (int x = 0; x < work->width; ++x) for (int y = 0; y < work->height; ++y) {
    int at = x * work->height + y;
    work->scratch[at] = -1;
    if ((work->flags[at] & kWater) || work->terrain[at] != kFlat) continue;
    // Leave a dry tile between ponds and existing water, especially the sea.
    bool near_water = false;
    for (int d = 0; d < 8; ++d) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height &&
          (work->flags[nx * work->height + ny] & kWater)) near_water = true;
    }
    if (near_water) continue;
    int score = IntClamp(
        (int)FixedToInt(((SampleTerrainNoise(&noise, FixedFromInt(x), FixedFromInt(y)) + FIXED_ONE) * 512)), 0,
        kPondScores - 1);
    work->scratch[at] = score;
    ++work->queue[score];
    ++candidates;
  }
  // Grow coherent patches through a quantile of suitable flat land. Water
  // covers zero to twenty percent of that land independently of river width
  // and stream count. Larger maps gain more ponds of the same tile scale.
  int goal = candidates * work->water / (47 * 5), cutoff = 0, total = 0;
  while (cutoff < kPondScores - 1 && total + work->queue[cutoff] < goal)
    total += work->queue[cutoff++];
  int remaining = goal - total;
  for (int at = 0; at < work->cells; ++at) {
    int score = work->scratch[at];
    if (score < 0 || score > cutoff || (score == cutoff && remaining-- <= 0)) continue;
    // Local flat surface elevations survive the final bed/shore grading.
    MakeTerrainWater(work, at);
  }
}

// OpenTTD's FlowRiver/YapfBuildRiver approach, adapted to our explicit slope
// shapes and scratch storage. See LICENSE.OpenTTD. No terrain writes occur
// while searching. Heights are already encoded in altitude and can temporarily
// hold heap positions; SetGeneratedWaterElevations rebuilds them afterwards.
typedef struct {
  TerrainWork *work;
  int target, count;
  int left, top, right, bottom;
} TerrainRiverSearch;

enum {
  kRiverParentMask = 3, kRiverClosed = 4, kRiverVisited = 8,
  kRiverOutgoingMask = 15, kRiverDrainageShift = 4,
  kRiverWaypointSpacing = 32, kRiverMaximumLength = 16384,
  kRiverDestinationCount = kRiverMaximumLength / kRiverWaypointSpacing + 1
};
static const int kStreamDx[4] = {-1, 0, 1, 0}, kStreamDy[4] = {0, 1, 0, -1};
static const u8 kStreamDownhillSlopes[] = {4, 1, 2, 3};
static const i8 kStreamDownhillDirections[] = {-1, 1, 2, 3, 0};
static const u8 kRiverRaisedCorners[] = {0, 9, 3, 6, 12};

// Return the adjacent column-major cell in the selected cardinal direction within the search rectangle. Index and
// direction must be valid; returns -1 when the neighbor leaves the admitted search bounds.
static int TerrainStreamNeighbor(const TerrainRiverSearch *search, int at, int direction) {
  const TerrainWork *work = search->work;
  int x = (at >> work->height_shift) + kStreamDx[direction], y = (at & (work->height - 1)) + kStreamDy[direction];
  if (x < search->left || y < search->top || x > search->right || y > search->bottom) return -1;
  return x * work->height + y;
}

// Decode the generator cell's supported flat or planar river slope. Index must be valid; returns -1 for geometry that
// cannot participate in the downhill stream search.
static int TerrainStreamSlope(const TerrainWork *work, int at) {
  if (!(work->flags[at] & kWater)) return work->terrain[at] <= CITY_TERRAIN_SHAPE_4 ? work->terrain[at] : -1;
  if (work->terrain[at] != kWaterfall) return kFlat;
  for (int d = 0; d < 4; d++)
    if ((work->water_marks[at] & kRiverOutgoingMask) == (1 << d)) return kStreamDownhillSlopes[d];
  return -1;
}

// Read a stream cell's upper water or land surface level for the supplied decoded slope. Index and slope must describe
// the same cell; does not modify river geometry.
static int TerrainStreamUpperHeight(const TerrainWork *work, int at, int slope) {
  if (work->flags[at] & kWater)
    return ReadGeneratedWaterHeight(work, at);
  return (work->altitude[at] & CITY_ALTITUDE_LAND_MASK) + (slope != kFlat);
}

// Test whether adjacent generation cells meet at a compatible downhill water edge. Endpoints and direction must
// describe that adjacency; unsupported slopes return false without editing either cell.
static bool TerrainRiverFlowsDown(const TerrainWork *work, int begin, int end, int direction) {
  int from = TerrainStreamSlope(work, begin), to = TerrainStreamSlope(work, end);
  if (from < 0 || to < 0) return false;
  if ((from && kStreamDownhillDirections[from] != direction) ||
      (to && kStreamDownhillDirections[to] != direction)) return false;
  // Match the outgoing low edge to the incoming high edge. Turns are flat;
  // consecutive inclines descend straight by exactly one level each.
  int exit = TerrainStreamUpperHeight(work, begin, from) - (from != kFlat);
  return exit == TerrainStreamUpperHeight(work, end, to);
}

// Flat pond placement excludes slopes, so adjacent terrace pools can leave a dry spillway between them. Join an
// existing straight one-level descent; the ordinary water normalization then handles it like a routed stream. Requires
// tiled dry slopes and adjacent water pools; updates admitted spillway geometry and directional river markers once
// before normalization.
static void ConnectTerrainWaterfalls(TerrainWork *work) {
  for (int at = 0; at < work->cells; ++at) {
    int slope = work->terrain[at];
    if ((work->flags[at] & kWater) || slope < CITY_TERRAIN_SHAPE_1 || slope > CITY_TERRAIN_SHAPE_4) continue;
    int x = at >> work->height_shift, y = at & (work->height - 1);
    // Both lateral neighbors and both water endpoints must be inside the map.
    if (!x || !y || x + 1 == work->width || y + 1 == work->height) continue;
    int direction = kStreamDownhillDirections[slope];
    int stride = kStreamDx[direction] * work->height + kStreamDy[direction];
    int upper = at - stride, lower = at + stride;
    if ((work->flags[upper] & (kWater | kSaltWater)) != kWater || !(work->flags[lower] & kWater)) continue;
    int bank_stride = kStreamDx[direction ^ 1] * work->height + kStreamDy[direction ^ 1];
    int height = TerrainStreamUpperHeight(work, at, slope);
    bool aligned = true;
    for (int side = -1; side <= 1; side += 2) {
      int neighbor = at + side * bank_stride;
      if (!(work->flags[neighbor] & kWater)) continue;
      // Matching parallel waterfalls share both side-edge corner heights.
      // Flat water and differently oriented drops cannot cover this edge.
      if ((work->flags[neighbor] & kSaltWater) || TerrainStreamSlope(work, neighbor) != slope ||
          TerrainStreamUpperHeight(work, neighbor, slope) != height) {
        aligned = false;
        break;
      }
    }
    if (!aligned) continue;
    if (!TerrainRiverFlowsDown(work, upper, at, direction) ||
        !TerrainRiverFlowsDown(work, at, lower, direction)) continue;
    MakeTerrainWater(work, at);
    work->water_marks[upper] |= 1 << direction;
    work->water_marks[at] |= 1 << direction;
  }
}

// Clear visited state inside the admitted river search rectangle and reset its heap count. Bounds must fit allocated
// work arrays; prior search state in that rectangle is deliberately discarded.
static void BeginTerrainRiverSearch(TerrainRiverSearch *search) {
  search->count = 0;
  for (int x = search->left; x <= search->right; ++x)
    memset(search->work->seen + x * search->work->height + search->top, 0, search->bottom - search->top + 1);
}

// Compute the bounded search budget from intended route length and available map cells. Length must be nonnegative;
// the result is capped by the work map and allocates no search storage.
static int TerrainRiverSearchLimit(const TerrainWork *work, int length) {
  return IntMin(work->cells, IntMax(4096, length * 64));
}

// Reverse downhill reachability from existing water. Each dry tile enters the queue at most once, recording a
// direction toward an earlier queue node. The high nibble is temporary; no extra map plane or saved data is needed.
// Requires tiled water and empty scratch markers; each cell enters the existing queue once and cached drainage
// directions are generation-only.
static void PrepareTerrainRiverOutlets(TerrainWork *work) {
  int count = 0;
  for (int at = 0; at < work->cells; ++at)
    if (work->flags[at] & kWater) work->queue[count++] = at;
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    int first = (x * 13 + y * 7) & 3;
    for (int i = 0; i < 4; ++i) {
      int d = (first + i) & 3, nx = x + kStreamDx[d], ny = y + kStreamDy[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->flags[next] & kWater) || (work->water_marks[next] >> kRiverDrainageShift) ||
          !TerrainRiverFlowsDown(work, next, at, d ^ 2)) continue;
      work->water_marks[next] = ((d ^ 2) + 1) << kRiverDrainageShift;
      work->queue[count++] = next;
    }
  }
}

// Reject isolated flat endpoints. Follow the cached downhill directions to a real water body, rather than truncating
// the entire river by the Water slider. Begin must be a valid dry generation index; returns a reached water index or
// -1 when the cached route is unavailable/too short.
static int FindTerrainRiverDestination(const TerrainWork *work, int begin, int minimum) {
  int at = begin, length = 0;
  while (!(work->flags[at] & kWater)) {
    int direction = (work->water_marks[at] >> kRiverDrainageShift) - 1;
    if (direction < 0 || length == kRiverMaximumLength) return -1;
    at += kStreamDx[direction] * work->height + kStreamDy[direction];
    ++length;
  }
  return length >= minimum ? at : -1;
}

// Combine accumulated path cost with Manhattan distance to the search target. Cell and target must be valid generation
// indices; this is a read-only A* priority estimate.
static int TerrainRiverEstimate(const TerrainRiverSearch *search, int at) {
  const TerrainWork *work = search->work;
  return work->scratch[at] + abs((at >> work->height_shift) - (search->target >> work->height_shift)) +
      abs((at & (work->height - 1)) - (search->target & (work->height - 1)));
}

// Order two open river-search cells by estimated cost, breaking ties by cell index. Both cells must have initialized
// costs; deterministic tie-breaking avoids dependence on heap insertion history.
static bool TerrainRiverHeapBefore(const TerrainRiverSearch *search, int a, int b) {
  int first = TerrainRiverEstimate(search, a), second = TerrainRiverEstimate(search, b);
  return first < second || (first == second && a < b);
}

// Bubble an inserted or decreased-key river node toward the heap root. Position must be within the open heap; queue
// entries and each cell's reverse heap position are updated together.
static void RaiseTerrainRiverHeapNode(TerrainRiverSearch *search, int at, int position) {
  TerrainWork *work = search->work;
  while (position) {
    int parent = (position - 1) / 2, other = work->queue[parent];
    if (!TerrainRiverHeapBefore(search, at, other)) break;
    work->queue[position] = other;
    work->heights[other] = position;
    position = parent;
  }
  work->queue[position] = at;
  work->heights[at] = position;
}

// Remove the best open river node, restore heap order and mark it closed. Requires a nonempty initialized search heap;
// returns its generation index and retains reverse positions for remaining nodes.
static int PopTerrainRiverHeap(TerrainRiverSearch *search) {
  TerrainWork *work = search->work;
  int result = work->queue[0], last = work->queue[--search->count], position = 0;
  while (position * 2 + 1 < search->count) {
    int child = position * 2 + 1;
    if (child + 1 < search->count && TerrainRiverHeapBefore(search, work->queue[child + 1], work->queue[child]))
      child++;
    int other = work->queue[child];
    if (!TerrainRiverHeapBefore(search, other, last)) break;
    work->queue[position] = other;
    work->heights[other] = position;
    position = child;
  }
  if (search->count) { work->queue[position] = last; work->heights[last] = position; }
  work->seen[result] |= kRiverClosed;
  return result;
}

// Random positive edge costs and a Manhattan estimate match the river YAPF search. One heap entry per open tile, with
// in-place decrease-key. The seen byte packs visited/closed flags and a two-bit predecessor direction. Search
// rectangle and endpoints must be valid; false leaves search work changed but does not claim a completed route, while
// success commits its reaches.
static bool BuildTerrainRiverRoute(TerrainRiverSearch *search, int begin, int end, int length) {
  TerrainWork *work = search->work;
  BeginTerrainRiverSearch(search);
  search->target = end;
  work->seen[begin] = kRiverVisited;
  work->scratch[begin] = 0;
  RaiseTerrainRiverHeapNode(search, begin, search->count++);
  int visited = 1, limit = IntMin(work->cells, TerrainRiverSearchLimit(work, length) * 4);
  bool found = false;
  while (search->count) {
    int at = PopTerrainRiverHeap(search);
    if (at == end) { found = true; break; }
    for (int d = 0; d < 4; d++) {
      int next = TerrainStreamNeighbor(search, at, d);
      if (next < 0 || !TerrainRiverFlowsDown(work, at, next, d)) continue;
      bool seen = work->seen[next] & kRiverVisited;
      if (seen && (work->seen[next] & kRiverClosed)) continue;
      int cost = work->scratch[at] + 1 + TerrainProcessRandom(work) % 4;
      if (seen && cost >= work->scratch[next]) continue;
      if (!seen && visited == limit) continue;
      int position = seen ? work->heights[next] : search->count++;
      if (!seen) visited++;
      work->scratch[next] = cost;
      work->seen[next] = kRiverVisited | (d ^ 2);
      RaiseTerrainRiverHeapNode(search, next, position);
    }
  }
  if (!found) return false;
  // Build from the outlet back to the source, retaining each downhill edge
  // for the final water-surface consistency pass.
  for (int at = end;;) {
    MakeTerrainWater(work, at);
    if (at == begin) break;
    int direction = work->seen[at] & kRiverParentMask;
    int parent = TerrainStreamNeighbor(search, at, direction);
    work->water_marks[parent] |= 1 << (direction ^ 2);
    at = parent;
  }
  return true;
}

// The precomputed outlet route is also a bounded fallback when a randomized local search exhausts its budget. Validate
// before committing, then build upstream so every newly placed segment already connects to water. Endpoints must
// follow the prepared outlet directions; validates the bounded fallback first, then commits from its connected
// downstream end.
static bool BuildTerrainDrainageRoute(TerrainWork *work, int begin, int end) {
  int route[kRiverWaypointSpacing + 1] = {begin}, count = 1;
  while (route[count - 1] != end && !(work->flags[route[count - 1]] & kWater)) {
    int at = route[count - 1], direction = (work->water_marks[at] >> kRiverDrainageShift) - 1;
    if (direction < 0 || count == kRiverWaypointSpacing + 1) return false;
    int next = at + kStreamDx[direction] * work->height + kStreamDy[direction];
    if (!TerrainRiverFlowsDown(work, at, next, direction)) return false;
    route[count++] = next;
  }
  for (int i = count - 1; i >= 0; --i) {
    int at = route[i];
    if (i + 1 < count) {
      int direction = (work->water_marks[at] >> kRiverDrainageShift) - 1;
      work->water_marks[at] |= 1 << direction;
    }
    MakeTerrainWater(work, at);
  }
  return true;
}

// Attempt a routed stream from an admitted flat dry spring toward reachable existing water. Search and generation work
// must be initialized; failure may retain earlier committed reaches and deterministic search draws.
static bool MakeTerrainStream(TerrainRiverSearch *search, int x, int y, int minimum) {
  TerrainWork *work = search->work;
  int spring = x * work->height + y;
  // Flat, dry springs match OpenTTD; existing terrain supplies planar drops.
  if ((work->flags[spring] & kWater) || work->terrain[spring] != kFlat) return false;
  int outlet = FindTerrainRiverDestination(work, spring, minimum);
  if (outlet < 0) return false;
  int destinations[kRiverDestinationCount] = {spring}, count = 1, length = 0, at = spring;
  while (at != outlet) {
    int direction = (work->water_marks[at] >> kRiverDrainageShift) - 1;
    at += kStreamDx[direction] * work->height + kStreamDy[direction];
    ++length;
    if (length % kRiverWaypointSpacing == 0 || at == outlet) destinations[count++] = at;
  }
  for (int i = count - 1; i > 0; --i) {
    int begin = destinations[i - 1], end = destinations[i];
    int bx = begin >> work->height_shift, by = begin & (work->height - 1);
    int ex = end >> work->height_shift, ey = end & (work->height - 1);
    // Keep A* and visited-mark clearing local on 4096-square maps.
    search->left = IntMax(0, IntMin(bx, ex) - 16);
    search->right = IntMin(work->width - 1, IntMax(bx, ex) + 16);
    search->top = IntMax(0, IntMin(by, ey) - 16);
    search->bottom = IntMin(work->height - 1, IntMax(by, ey) + 16);
    if (!BuildTerrainRiverRoute(search, begin, end, kRiverWaypointSpacing) &&
        !BuildTerrainDrainageRoute(work, begin, end)) return false;
  }
  return true;
}

// Add density-scaled freshwater streams after coast and lake carving. Uses existing work arrays and an isolated seeded
// process state, restoring the caller's process RNG state afterward.
static void AddTerrainStreams(TerrainWork *work, int density, u32 seed) {
  // Coast orientation and feature carving must not disable the natural
  // freshwater streams/pools. Add them after all coast and lake shaping.
  // OpenTTD's 8/16/32 slots per 256-square map, distributed over local regions
  // so huge maps gain tributaries throughout their area. Each slot can retry a
  // failed source; the amount controls slots, not individual random attempts.
  static const u8 kStreamSlots[] = {0, 2, 4, 8};
  int per_region = kStreamSlots[density];
  int streams = per_region * work->cells / kTerrainCells;
  // water_marks is still zero from allocation: stream routing is its first writer.
  if (!streams) return;
  PrepareTerrainRiverOutlets(work);
  TerrainRiverSearch river_search = {.work = work};
  u32 process_state = work->process_state;
  for (int stream = 0; stream < streams; stream++) {
    int region = stream / per_region;
    int origin_x = region % (work->width / kTerrainEdge) * kTerrainEdge;
    int origin_y = region / (work->width / kTerrainEdge) * kTerrainEdge;
    for (int attempt = 0; attempt < 32; ++attempt) {
      // Each source has its own seed; failed routing and tree growth do not
      // move the other slots. Bound retries to keep huge-map work predictable.
      work->process_state = seed ^ 0x5354524D ^ (u32)stream * 0x9E3779B9 ^ (u32)attempt * 0x85EBCA6B;
      int x = origin_x + TerrainProcessRandom(work) % kTerrainEdge;
      int y = origin_y + TerrainProcessRandom(work) % kTerrainEdge;
      int minimum = stream % 10 == 0 ? 20 : 5;
      if (MakeTerrainStream(&river_search, x, y, minimum)) break;
    }
  }
  work->process_state = process_state;
}

// Lower connected river mouths to the adjoining ocean level where admitted. Requires tiled water geometry; reuses the
// queue and updates altitude/terrain consistently across the connected mouth.
static void LevelGeneratedRiverMouths(TerrainWork *work) {
  int count = 0;
  for (int at = 0; at < work->cells; ++at)
    if ((work->flags[at] & (kWater | kSaltWater)) == (kWater | kSaltWater)) work->queue[count++] = at;
  // Connected lowland water meets the sea on one plane. Keep salinity
  // independent: these tiles still use the upstream freshwater artwork.
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    for (int d = 0; d < 8; d += 2) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      int water = ReadGeneratedWaterHeight(work, next);
      if ((work->flags[next] & (kWater | kSaltWater)) != kWater || water != 1) continue;
      // Zero marks visited water; each tile enters the queue only once.
      work->altitude[next] = 0;
      work->heights[next] = 0;
      work->terrain[next] = kSurfaceWater;
      work->queue[count++] = next;
    }
  }
}

// Surface heights are upper levels. Only one cardinal outlet, exactly one level lower, can define one of OpenTTD's
// four planar river slopes. Requires a valid generated water cell; returns a supported planar slope or -1 when no
// compatible single downhill outlet exists.
static int GeneratedRiverSlope(const TerrainWork *work, int at) {
  if (work->flags[at] & kSaltWater) return CITY_TERRAIN_FLAT;
  int x = at >> work->height_shift, y = at & (work->height - 1);
  int water = ReadGeneratedWaterHeight(work, at);
  int shape = CITY_TERRAIN_FLAT;
  for (int d = 0; d < 4; ++d) {
    int nx = x + kStreamDx[d], ny = y + kStreamDy[d];
    if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
    int next = nx * work->height + ny;
    if (!(work->flags[next] & kWater)) continue;
    int lower = ReadGeneratedWaterHeight(work, next);
    if (lower >= water) continue;
    if (shape || lower != water - 1) return -1;
    shape = kStreamDownhillSlopes[d];
  }
  return shape;
}

// Compute the water level allowed by a bank's neighboring corner geometry. Bank index and direction must be valid;
// raised is the candidate water-corner mask and the probe makes no changes.
static int GeneratedBankWaterLimit(const TerrainWork *work, int bank, int direction, int raised) {
  int x = bank >> work->height_shift, y = bank & (work->height - 1);
  int ground = work->heights[bank], corners = 0;
  for (int near = 0; near < 8; ++near) {
    int nx = x + kNeighborX[near], ny = y + kNeighborY[near];
    if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height &&
        work->heights[nx * work->height + ny] > ground) corners |= kNeighborMasks[near];
  }
  corners = (corners & 5) | ((corners & 2) << 2) | ((corners & 8) >> 2);
  int limit = 32;
  for (int corner = 0; corner < 2; ++corner) {
    int own = (direction + corner) & 3, far = (direction + 3 - corner) & 3;
    limit = IntMin(limit, ground + ((corners >> far) & 1) + !(raised & (1 << own)));
  }
  return limit;
}

enum { kGeneratedQueued = 1, kGeneratedSlopeValid = 2, kGeneratedSlopeShift = 2 };
// Return the candidate river slope, computing and caching it in seen when absent. Requires a valid generation cell and
// initialized cache flags; edits to adjacent geometry must invalidate this cache.
static int CachedGeneratedRiverSlope(TerrainWork *work, int at) {
  int bits = work->seen[at];
  if (!(bits & kGeneratedSlopeValid)) {
    bits = (bits & kGeneratedQueued) | kGeneratedSlopeValid |
        ((GeneratedRiverSlope(work, at) + 1) << kGeneratedSlopeShift);
    work->seen[at] = bits;
  }
  return (bits >> kGeneratedSlopeShift) - 1;
}
// Clear cached slope results for a changed generation cell and its cardinal neighbors. Index must be valid; preserves
// queue-membership flags so invalidation cannot duplicate queued work.
static void InvalidateGeneratedRiverSlopes(TerrainWork *work, int at) {
  work->seen[at] &= kGeneratedQueued;
  int x = at >> work->height_shift, y = at & (work->height - 1);
  for (int d = 0; d < 4; ++d) {
    int nx = x + kStreamDx[d], ny = y + kStreamDy[d];
    if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height)
      work->seen[nx * work->height + ny] &= kGeneratedQueued;
  }
}

// Calculate the maximum water level compatible with the neighboring edge's corner heights. Direction and shapes must
// be supported decoded values; returns an integer limit without touching work arrays.
static int GeneratedWaterEdgeLimit(int direction, int shape, int other, int adjacent) {
  int limit = 32;
  for (int corner = 0; corner < 2; ++corner) {
    int own_corner = (direction + corner) & 3, far_corner = (direction + 3 - corner) & 3;
    bool own_drop = shape && !(kRiverRaisedCorners[shape] & (1 << own_corner));
    bool far_drop = other && !(kRiverRaisedCorners[other] & (1 << far_corner));
    limit = IntMin(limit, adjacent - far_drop + own_drop);
  }
  return limit;
}

// Resolve freshwater surface slopes with a bounded queue and cached local geometry. Requires tiled water and allocated
// workspace; updates water heights and selects flat-water versus waterfall terrain in place.
static void NormalizeGeneratedRiverSlopes(TerrainWork *work) {
  int head = 0, tail = 0, count = 0;
  memset(work->seen, 0, work->cells);
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kSaltWater)) != kWater) continue;
    work->queue[tail] = at;
    tail = (tail + 1) % work->cells;
    ++count;
    work->seen[at] |= kGeneratedQueued;
  }
  // A tile ID alone cannot make a bend or junction into an incline. Compare
  // both corners of every shared water edge; terrace incompatible reaches
  // downward until their surfaces meet. Each edit strictly lowers a 5-bit
  // water height, and the circular queue holds at most one entry per tile.
  while (count) {
    int at = work->queue[head], x = at >> work->height_shift, y = at & (work->height - 1);
    head = (head + 1) % work->cells;
    --count;
    work->seen[at] &= ~kGeneratedQueued;
    int water = ReadGeneratedWaterHeight(work, at);
    int shape = CachedGeneratedRiverSlope(work, at), target = water;
    for (int d = 0; d < 4; ++d) {
      int nx = x + kStreamDx[d], ny = y + kStreamDy[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if (!(work->flags[next] & kWater)) continue;
      int adjacent = ReadGeneratedWaterHeight(work, next);
      if (shape < 0) {
        target = IntMin(target, adjacent);
        continue;
      }
      int other = CachedGeneratedRiverSlope(work, next);
      if (other < 0) continue; // Its queued correction will revisit this edge.
      // Preserve the carved route's downhill direction when later grading
      // lowers an upstream reach. Never introduce an uphill step along it.
      if (work->water_marks[next] & (1 << ((d + 2) & 3)))
        target = IntMin(target, adjacent - (other != CITY_TERRAIN_FLAT));
      target = IntMin(target, GeneratedWaterEdgeLimit(d, shape, other, adjacent));
    }
    if (target == water) continue;
    work->altitude[at] = (work->altitude[at] & ~CITY_ALTITUDE_WATER_MASK) |
        (target << CITY_ALTITUDE_WATER_SHIFT);
    InvalidateGeneratedRiverSlopes(work, at);
    // A height edit changes its neighbors' candidate slopes too. Revisit
    // their other edges, up to two cardinal steps from the changed tile.
    for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy) {
      if (abs(dx) + abs(dy) > 2) continue;
      int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->seen[next] & kGeneratedQueued) || (work->flags[next] & (kWater | kSaltWater)) != kWater) continue;
      work->queue[tail] = next;
      tail = (tail + 1) % work->cells;
      ++count;
      work->seen[next] |= kGeneratedQueued;
    }
  }
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kSaltWater)) != kWater) continue;
    int water = ReadGeneratedWaterHeight(work, at);
    int shape = CachedGeneratedRiverSlope(work, at);
    int land = water - (shape != CITY_TERRAIN_FLAT);
    if (!shape && !(work->flags[at] & kGroundLevelWater)) land = IntMin(work->heights[at], water);
    work->heights[at] = land;
    work->altitude[at] = land | (water << CITY_ALTITUDE_WATER_SHIFT);
    work->terrain[at] = shape ? kWaterfall : kSurfaceWater;
  }
}

// Propagate freshwater containment limits through the generated water network. Requires normalized geometry and
// reusable seen/queue storage; edits water surfaces without allocating a second map.
static void ContainGeneratedFreshwater(TerrainWork *work) {
  int head = 0, tail = 0, count = 0;
  memset(work->seen, 0, work->cells);
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kSaltWater)) != kWater) continue;
    int x = at >> work->height_shift, y = at & (work->height - 1);
    int water = ReadGeneratedWaterHeight(work, at);
    bool exposed = false;
    int shape = -2;
    for (int d = 0; d < 4; ++d) {
      int nx = x + kStreamDx[d], ny = y + kStreamDy[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->flags[next] & kWater) || work->heights[next] >= water) continue;
      if (shape == -2) shape = CachedGeneratedRiverSlope(work, at);
      if (GeneratedBankWaterLimit(work, next, d, shape > 0 ? kRiverRaisedCorners[shape] : 15) < water) {
        exposed = true;
        break;
      }
    }
    if (!exposed) continue;

    work->queue[tail] = at;
    tail = (tail + 1) % work->cells;
    ++count;
    work->seen[at] |= kGeneratedQueued;
  }
  // Lower exposed water to the bank's spill edge and grade affected ground
  // with it. Unaffected water and dry land do not enter the work queue.
  while (count) {
    int at = work->queue[head], x = at >> work->height_shift, y = at & (work->height - 1);
    head = (head + 1) % work->cells;
    --count;
    work->seen[at] &= ~kGeneratedQueued;
    bool edited = false;
    if ((work->flags[at] & (kWater | kSaltWater)) == kWater) {
      int water = ReadGeneratedWaterHeight(work, at);
      int shape = CachedGeneratedRiverSlope(work, at), target = water;
      for (int d = 0; d < 4; ++d) {
        int nx = x + kStreamDx[d], ny = y + kStreamDy[d];
        if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
        int next = nx * work->height + ny;
        if (!(work->flags[next] & kWater)) {
          if (work->heights[next] >= water) continue;
          target = IntMin(target,
              GeneratedBankWaterLimit(work, next, d, shape > 0 ? kRiverRaisedCorners[shape] : 15));
          continue;
        }
        int adjacent = ReadGeneratedWaterHeight(work, next);
        if (shape < 0) {
          target = IntMin(target, adjacent);
          continue;
        }
        int other = CachedGeneratedRiverSlope(work, next);
        if (other < 0) continue;
        target = IntMin(target, GeneratedWaterEdgeLimit(d, shape, other, adjacent));
      }
      target = IntMax(0, target);
      if (target != water) {
        work->altitude[at] = (work->altitude[at] & ~CITY_ALTITUDE_WATER_MASK) |
            (target << CITY_ALTITUDE_WATER_SHIFT);
        InvalidateGeneratedRiverSlopes(work, at);
        edited = true;
      }
      int slope = CachedGeneratedRiverSlope(work, at);
      int land = work->heights[at];
      if (slope > 0) land = IntMax(0, target - 1);
      else if (work->flags[at] & kGroundLevelWater) land = target;
      else if (target != water) land = IntMin(land, IntMax(0, target - 1));
      if (land != work->heights[at]) {
        work->heights[at] = land;
        edited = true;
      }
      int terrain = slope ? kWaterfall : kSurfaceWater;
      if (terrain != work->terrain[at]) edited = true;
      work->terrain[at] = terrain;
      work->altitude[at] = land | (target << CITY_ALTITUDE_WATER_SHIFT);

    } else if (!(work->flags[at] & kWater)) {
      int land = work->heights[at];
      for (int d = 0; d < 8; ++d) {
        int nx = x + kTerrainNeighborXOffsets[d], ny = y + kTerrainNeighborYOffsets[d];
        if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
        int next = nx * work->height + ny, limit = work->heights[next] + 1;
        if ((work->flags[next] & (kWater | kSaltWater)) == kWater) {
          int water = ReadGeneratedWaterHeight(work, next);
          limit = water - (CachedGeneratedRiverSlope(work, next) > 0);
        }
        land = IntMin(land, limit);
      }
      land = IntMax(0, land);
      if (land != work->heights[at]) {
        work->heights[at] = land;
        work->altitude[at] = land;
        edited = true;
      }
    }
    if (!edited) continue;
    // Water slopes and bank corners depend on nearby water. Dry-bank
    // grading needs only immediate neighbors. Hold at most one entry per tile.
    for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy) {
      int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      bool fresh = (work->flags[next] & (kWater | kSaltWater)) == kWater;
      if ((work->seen[next] & kGeneratedQueued) || (!fresh && (abs(dx) > 1 || abs(dy) > 1))) continue;
      work->queue[tail] = next;
      tail = (tail + 1) % work->cells;
      ++count;
      work->seen[next] |= kGeneratedQueued;
    }
  }
}

// Apply shallow bounded bank cuts beside freshwater without raising ground or cutting through mountains. Requires
// initialized final water markers; edits only eligible nearby dry heights and altitude bytes.
static void LowerGeneratedFreshwaterBanks(TerrainWork *work) {
  enum { kBankReach = 3, kUnvisitedBank = 255 };
  // Occasional shallow cuts beside carved waterways, never an uplift or a
  // trench through flat land. Ground-level streams/ponds do not seed cuts.
  // Reuse routing marks after river normalization.
  memset(work->water_marks, kUnvisitedBank, work->cells);
  int count = 0;
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kSaltWater | kGroundLevelWater)) != kWater ||
        work->terrain[at] == kWaterfall) continue;
    work->water_marks[at] = 0;
    work->scratch[at] = ReadGeneratedWaterHeight(work, at);
    work->queue[count++] = at;
  }
  TerrainNoise noise = MakeTerrainNoise(work->game_state ^ 0x42414E4B, FIXED_ONE / 48, 2);
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    int distance = work->water_marks[at] + 1;
    if (distance > kBankReach) continue;
    for (int d = 0; d < 8; ++d) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if ((work->flags[next] & kWater) || work->water_marks[next] != kUnvisitedBank) continue;
      work->water_marks[next] = distance;
      work->scratch[next] = work->scratch[at];
      work->queue[count++] = next;
      if (distance == 1 || work->heights[next] <= IntMax(1, work->scratch[next]) ||
          SampleTerrainNoise(&noise, FixedFromInt(nx), FixedFromInt(ny)) < 9830)
        continue;
      --work->heights[next];
      work->altitude[next] = work->heights[next];
    }
  }
}

// Grade dry banks against final river surface constraints using a distance envelope. Requires final water elevations
// and scratch storage; updates land heights/altitudes while preserving the river surface.
static void GradeGeneratedRiverBanks(TerrainWork *work) {
  // Distance envelope from the final river surfaces. Banks next to level
  // water start at its height; banks beside an incline start at its low end.
  // Two raster passes grade the surrounding dry land without new storage.
  for (int at = 0; at < work->cells; ++at) {
    int water = ReadGeneratedWaterHeight(work, at);
    work->scratch[at] = (work->flags[at] & (kWater | kSaltWater)) == kWater ?
        water - (work->terrain[at] == kWaterfall) : work->heights[at] + 1;
  }
  GradeTerrainHeights(work, work->scratch);
  for (int at = 0; at < work->cells; ++at) {
    if (work->flags[at] & kWater) continue;
    int land = IntMax(0, work->scratch[at] - 1);
    work->heights[at] = land;
    work->altitude[at] = land;
  }
}

// Normalize water levels and grade land/water together in sea-relative coordinates. Requires completed carving and
// initialized work arrays; queue/scratch are reused before final export encoding.
static void SetGeneratedWaterElevations(TerrainWork *work) {
  // Grade in sea-relative coordinates, then translate land and water together
  // on export. Every cell enters this existing scratch queue at most once.
  int count = 0;
  for (int at = 0; at < work->cells; ++at) {
    bool sea = (work->flags[at] & (kWater | kSaltWater)) == (kWater | kSaltWater);
    work->scratch[at] = sea ? 0 : 32;
    if (sea) work->queue[count++] = at;
  }
  for (int cursor = 0; cursor < count; ++cursor) {
    int at = work->queue[cursor], x = at >> work->height_shift, y = at & (work->height - 1);
    int distance = work->scratch[at] + 1;
    if (distance > 31) continue;
    for (int d = 0; d < 8; ++d) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny;
      if (work->scratch[next] != 32) continue;
      work->scratch[next] = distance;
      work->queue[count++] = next;
    }
  }
  for (int at = 0; at < work->cells; ++at) {
    int land = IntMax(0, (work->altitude[at] & CITY_ALTITUDE_LAND_MASK) - kConstructionLandOffset);
    int water = ReadGeneratedWaterHeight(work, at);
    water = IntMax(0, water - kConstructionLandOffset);
    if (work->flags[at] & kSaltWater) {
      land = water = 0;
    } else if (work->flags[at] & kWater) {
      water = IntMax(1, IntMin(water, work->scratch[at]));
      if (work->flags[at] & kGroundLevelWater) land = water - (work->terrain[at] == kWaterfall);
      else if (work->terrain[at] != kWaterfall) land = IntMin(land, water - 1);
      else land = IntMin(land, water);
    } else {
      land = IntMax(1, IntMin(land, IntMax(1, work->scratch[at] - 1)));
      water = 0;
    }
    work->heights[at] = land;
    work->altitude[at] = land | (water << CITY_ALTITUDE_WATER_SHIFT);
  }
  LevelGeneratedRiverMouths(work);
  NormalizeGeneratedRiverSlopes(work);
  LowerGeneratedFreshwaterBanks(work);
  GradeGeneratedRiverBanks(work);
  // Bank grading can expose a lake's side below its water level. Lower the
  // water to its spill edge and regrade the bed/banks together, rather than
  // leaving a retaining wall to support the water in the surface renderer.
  ContainGeneratedFreshwater(work);
  // Carved level water retains a submerged bed even where its surface meets
  // the sea. Heights may reach -1 here; packed elevations stay unchanged until
  // export rebases them. Streams/ponds placed on ground keep their local plane.
  // Keep sea shores raised, except beside sea-level freshwater.
  for (int at = 0; at < work->cells; ++at) {
    if ((work->flags[at] & (kWater | kGroundLevelWater)) == kWater && work->terrain[at] != kWaterfall) {
      int bed = ReadGeneratedWaterHeight(work, at) - 1;
      if (work->flags[at] & kSaltWater) {
        // Mixed sea shores rise above the surface; retain their bank base.
        // Interior saltwater channels and ocean tiles still have real depth.
        int x = at >> work->height_shift, y = at & (work->height - 1);
        for (int d = 0; d < 8; ++d) {
          int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
          if (nx >= 0 && ny >= 0 && nx < work->width && ny < work->height &&
              !(work->flags[nx * work->height + ny] & kWater)) {
            ++bed;
            break;
          }
        }
      }
      work->heights[at] = IntMin(work->heights[at], bed);
    }
    if ((work->flags[at] & kWater) || work->heights[at]) continue;
    int x = at >> work->height_shift, y = at & (work->height - 1);
    bool sea_shore = false, river_bank = false;
    for (int d = 0; d < 8; ++d) {
      int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
      if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
      int next = nx * work->height + ny, kind = work->flags[next] & (kWater | kSaltWater);
      sea_shore |= kind == (kWater | kSaltWater);
      river_bank |= kind == kWater && !(work->altitude[next] & CITY_ALTITUDE_WATER_MASK);
    }
    if (sea_shore && !river_bank) work->heights[at] = work->altitude[at] = 1;
  }
  // Bank grading can introduce dry-land saddles after the first slope pass.
  // Resolve those without raising completed canal beds or river inclines.
  // Water geometry was already normalized against its surface and neighbors.
  FillUnsupportedTerrainSlopes(work, true);
  // Carved beds follow their banks; ground-level water stays flat unless it
  // has a normalized downhill incline. Keep water elevations unchanged until
  // export so neighboring slopes can be decoded directly in this pass.
  for (int at = 0; at < work->cells; ++at) {
    int shape = CITY_TERRAIN_FLAT;
    if (work->terrain[at] == kWaterfall) {
      work->flags[at] |= kInclinedWater;
      shape = GeneratedRiverSlope(work, at);
      if (shape < CITY_TERRAIN_SHAPE_1 || shape > CITY_TERRAIN_SHAPE_4) shape = CITY_TERRAIN_SHAPE_13;
    }
    if (!shape && !(work->flags[at] & kGroundLevelWater)) {
      int x = at >> work->height_shift, y = at & (work->height - 1), higher = 0;
      for (int d = 0; d < 8; ++d) {
        int nx = x + kNeighborX[d], ny = y + kNeighborY[d];
        if (nx < 0 || ny < 0 || nx >= work->width || ny >= work->height) continue;
        if (work->heights[nx * work->height + ny] > work->heights[at]) higher |= kNeighborMasks[d];
      }
      higher = (higher & 5) | ((higher & 2) << 2) | ((higher & 8) >> 2);
      shape = kTerrainShapeByHigherNeighbors[higher];
      if (shape == CITY_TERRAIN_RAISE_LEVEL) shape = CITY_TERRAIN_SHAPE_13;
    }
    work->terrain[at] = shape;
    work->flags[at] &= ~kGroundLevelWater;
  }
}

// Setup constraints adapted from OpenSC2K's new_city_terrain_dialog.gd.
// See LICENSE.OpenSC2K for its MIT notice.
static const u32 kRiverFeatures = kDelta | kMeander | kCrossing | kBranch | kRejoin | kValley | kSalty;
static const u32 kOceanFeatures = kDelta | kCliffs | kRandomCoast;
static const u32 kExclusiveTerrainFeatures[] = {
  kPlateau | kRidge | kRolling | kBasin,
  kValley | kCanyon | kBasin,
  kLake | kLakes
};

// Compute feature choices excluded by the current terrain feature mask. Input must use supported feature bits; this
// query does not alter settings or generate terrain.
u32 DisabledCityTerrainFeatures(u32 features) {
  u32 disabled = features & kCanyon ? kRiver | kRiverFeatures : 0;
  for (int i = 0; i < sizeof(kExclusiveTerrainFeatures) / sizeof(kExclusiveTerrainFeatures[0]); ++i) {
    u32 group = kExclusiveTerrainFeatures[i];
    if (features & group) disabled |= group & ~features;
  }
  return disabled;
}

// Toggle an admitted terrain feature while enforcing its dependency and exclusion groups. Feature must be a supported
// choice bit; disabled choices are ignored and settings remain caller-owned.
void ToggleCityTerrainFeature(CityTerrainSettings *settings, u32 feature) {
  u32 features = settings->features;
  if (DisabledCityTerrainFeatures(features) & feature) return;
  if (features & feature) {
    features &= ~feature;
    if (feature == kOcean) features &= ~kOceanFeatures;
    if (feature == kRiver) features &= ~kRiverFeatures;
  } else {
    for (int i = 0; i < sizeof(kExclusiveTerrainFeatures) / sizeof(kExclusiveTerrainFeatures[0]); ++i) {
      u32 group = kExclusiveTerrainFeatures[i];
      if (feature & group) features &= ~group;
    }
    if (feature & kCanyon) features &= ~(kRiver | kRiverFeatures);
    if (feature & (kRiver | kRiverFeatures)) features &= ~kCanyon;
    if (feature & kRiverFeatures) features |= kRiver;
    if (feature & kOceanFeatures) features |= kOcean;
    features |= feature;
  }
  settings->features = features;
}

enum { kClimateHeightCount = CITY_ALTITUDE_LAND_MASK + 1 };

// Derive the non-desert ground climate near one valid generated-map tile. Requires initialized climate noise and
// desert threshold; reads bounded neighbors and does not update the map.
static int TerrainGreenClimate(const CityTerrainMap *map, TileIndex tile) {
  if (map->climate != CITY_TERRAIN_MIXED) return CLIMATE_TROPICAL;
  int x = tile % map->width, y = tile / map->width;
  int green = TerrainGrassPaletteAt(&map->snow, (CityPoint){x, y});
  if (!map->desert_limit) return green;
  // Desert coverage controls the dry regions, rather than being capped by
  // the temperature seed. Use the desert preset's tropical grass around those
  // regions, keeping the sandy border snow-free even in a cold region.
  // Two tiles beyond raw dry candidates plus their existing one-tile green rim
  // leave at least three grass tiles before any snow-capable Arctic blend.
  // Raw candidates include every desert tile without nested neighbor scans.
  enum { kDesertGrassMargin = 2 };
  for (int dy = -kDesertGrassMargin; dy <= kDesertGrassMargin; ++dy) {
    for (int dx = -kDesertGrassMargin; dx <= kDesertGrassMargin; ++dx) {
      int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
      if (!IsTileKindExposedWater(map->buildings[ny * map->width + nx]) &&
          TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true) < map->desert_limit)
        return CLIMATE_TROPICAL;
    }
  }
  return green;
}

// Like OpenTTD's CalculateCoverageLine, choose the nearest land quantile. Snow uses height relative to the smooth
// regional snow-line offset. Coverage must be zero through 100 and climate/height arrays initialized; returns a
// threshold without changing the generated map.
static int CalculateTerrainSnowLine(const CityTerrainMap *map, int coverage) {
  if (!coverage) return kClimateHeightCount;
  if (coverage == 100) return 0;
  int heights[kClimateHeightCount] = {0};
  CitySnowClimate offset = map->snow;
  offset.line = 0;
  for (int y = 0; y < map->height; ++y) for (int x = 0; x < map->width; ++x) {
    TileIndex tile = y * map->width + x;
    if (IsTileKindExposedWater(map->buildings[tile])) continue;
    // Both Arctic blends originate on the Arctic side. Avoid constructing
    // their neighbor-dependent appearance just to collect eligible heights.
    if (map->climate == CITY_TERRAIN_MIXED &&
        TerrainGreenClimate(map, tile) != CLIMATE_ARCTIC) continue;
    int height = (int)(map->altitude[tile] & CITY_ALTITUDE_LAND_MASK) -
        TerrainSnowLineAt(&offset, (CityPoint){x, y});
    ++heights[IntClamp(height, 0, kClimateHeightCount - 1)];
  }
  int goal = (map->cell_count - map->water_tiles) * coverage / 100, current = 0;
  int best_line = kClimateHeightCount, best_score = goal;
  for (int height = kClimateHeightCount - 1; height >= 0; --height) {
    current += heights[height];
    int score = goal - current;
    if (abs(score) < abs(best_score)) {
      best_score = score;
      best_line = height;
    }
  }
  return best_line;
}

// Desert uses the regional moisture field, never elevation. Include the green neighbor margin in the quantile so
// coverage remains close to the chosen target. Coverage must be zero through 100 and moisture data initialized;
// returns an integer threshold without advancing RNG.
static int CalculateTerrainDesertLimit(const CityTerrainMap *map, int coverage) {
  if (!coverage) return 0;
  if (coverage == 100) return 256;
  int moisture[256] = {0};
  for (int y = 0; y < map->height; ++y) for (int x = 0; x < map->width; ++x) {
    TileIndex tile = y * map->width + x;
    if (IsTileKindExposedWater(map->buildings[tile])) continue;
    int value = TerrainClimateValue(&map->snow, (CityPoint){x, y}, true);
    bool excluded = false;
    for (int direction = 0; direction < 8; ++direction) {
      int nx = x + kNeighborX[direction], ny = y + kNeighborY[direction];
      if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
      excluded |= IsTileKindExposedWater(map->buildings[ny * map->width + nx]);
      value = IntMax(value, TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true));
    }
    if (!excluded) ++moisture[value];
  }
  int goal = (map->cell_count - map->water_tiles) * coverage / 100, current = 0;
  int best_limit = 0, best_score = goal;
  for (int value = 0; value < 256; ++value) {
    current += moisture[value];
    int score = goal - current;
    if (abs(score) < abs(best_score)) {
      best_score = score;
      best_limit = value + 1;
    }
  }
  return best_limit;
}

// A one-tile green margin separates desert from water and moist regions. Tile must index the supplied generated map;
// reads the bounded moisture/water neighborhood without modifying climate or ground.
static bool TerrainTileIsDesert(const CityTerrainMap *map, TileIndex tile) {
  if (IsTileKindExposedWater(map->buildings[tile]) ||
      TerrainClimateValue(&map->snow, (CityPoint){tile % map->width, tile / map->width}, true) >=
          map->desert_limit) return false;
  int x = tile % map->width, y = tile / map->width;
  for (int direction = 0; direction < 8; ++direction) {
    int nx = x + kNeighborX[direction], ny = y + kNeighborY[direction];
    if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (IsTileKindExposedWater(map->buildings[neighbor]) ||
        TerrainClimateValue(&map->snow, (CityPoint){nx, ny}, true) >= map->desert_limit) return false;
  }
  return true;
}

// Choose a mixed grass-climate appearance where neighboring climate regions meet. Tile must be valid and green an
// admitted base climate; this does not generate new noise or change climate storage.
static int TerrainGrassTransition(const CityTerrainMap *map, TileIndex tile, int green) {
  int x = tile % map->width, y = tile / map->width;
  bool temperate = false, tropical = false;
  for (int i = 0; i < 8; i += 2) {
    int nx = x + kNeighborX[i], ny = y + kNeighborY[i];
    if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (IsTileKindExposedWater(map->buildings[neighbor]) || TerrainTileIsDesert(map, neighbor)) continue;
    int adjacent = TerrainGreenClimate(map, neighbor);
    temperate |= adjacent == CLIMATE_TEMPERATE;
    tropical |= adjacent == CLIMATE_TROPICAL;
  }
  // Only one side contributes the half blend, making a one-tile border.
  if (green == CLIMATE_ARCTIC) {
    if (temperate) return CLIMATE_TEMPERATE_ARCTIC;
    if (tropical) return CLIMATE_ARCTIC_TROPICAL;
  }
  if (green == CLIMATE_TROPICAL && temperate) return CLIMATE_TEMPERATE_TROPICAL;
  return green;
}

// Generate permanent ground independently of elevation and snow. Tile must index the supplied generated map; returns
// permanent climate independently of transient snow appearance.
static int TerrainTileGroundClimate(const CityTerrainMap *map, TileIndex tile) {
  if (map->climate == CITY_TERRAIN_SIMCITY) return CLIMATE_SIMCITY;
  if (map->climate == CITY_TERRAIN_NORMAL) return CLIMATE_TEMPERATE;
  if (map->climate == CITY_TERRAIN_SNOWY) return CLIMATE_ARCTIC;
  CityPoint position = {tile % map->width, tile / map->width};
  int green = TerrainGreenClimate(map, tile);
  if (map->climate == CITY_TERRAIN_DESERT &&
      TerrainClimateValue(&map->snow, position, true) >= 128)
    green = CLIMATE_RAINFOREST;
  if (!TerrainTileIsDesert(map, tile))
    return map->climate == CITY_TERRAIN_MIXED ? TerrainGrassTransition(map, tile, green) : green;
  // OpenTTD 15.3 NeighbourIsNormal / TileLoopClearDesert: half desert only
  // touches a non-desert cardinal neighbor; the interior uses full desert.
  // A narrow desert strip can therefore be entirely half-covered.
  int tx = tile % map->width, ty = tile / map->width;
  for (int i = 0; i < 8; i += 2) {
    int nx = tx + kNeighborX[i], ny = ty + kNeighborY[i];
    if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
    TileIndex neighbor = ny * map->width + nx;
    if (!TerrainTileIsDesert(map, neighbor)) return CLIMATE_DESERT_50;
  }
  return CLIMATE_DESERT_100;
}

// Preview and City generation share the same cached appearance encoding. Tile must index the supplied generated map;
// returns its cached ground/snow appearance without allocating or modifying generation state.
int CityTerrainTileClimate(const CityTerrainMap *map, TileIndex tile) {
  int ground = TerrainTileGroundClimate(map, tile);
  if (!TerrainClimateSupportsSnow(ground) || map->snow.line == CITY_SNOW_LINE_DISABLED) return ground;
  CityPoint position = {tile % map->width, tile / map->width};
  int snow = (int)(map->altitude[tile] & CITY_ALTITUDE_LAND_MASK) - TerrainSnowLineAt(&map->snow, position);
  return TerrainSnowClimate(ground, snow);
}

// Validate feature bits, slider ranges, climate values and supported map dimensions. NULL and unsupported settings
// return false; no work buffers or random state are created.
bool AreCityTerrainSettingsValid(const CityTerrainSettings *settings) {
  if (!settings || (settings->features >> kTerrainFeatureCount) ||
      settings->hills < 0 || settings->hills > 47 || settings->water < 0 || settings->water > 47 ||
      settings->trees < 0 || settings->trees > 47 || settings->climate >= CITY_TERRAIN_CLIMATE_COUNT ||
      settings->smoothness >= CITY_TERRAIN_SMOOTHNESS_COUNT || settings->variety >= CITY_TERRAIN_VARIETY_COUNT ||
      settings->small_rivers >= CITY_TERRAIN_SMALL_RIVERS_COUNT ||
      settings->snow_coverage > 100 || settings->desert_coverage > 100 ||
      (settings->coast_sides & ~CITY_TERRAIN_COAST_ALL)) return false;
  int width = settings->width ? settings->width : kTerrainEdge;
  int height = settings->height ? settings->height : kTerrainEdge;
  return IsSupportedMapAxis(width) && IsSupportedMapAxis(height);
}

// Reuse the existing terrain/preview owner, with the same five map planes used by generation.
bool PrepareCityTerrainMap(CityTerrainMap *map, int width, int height) {
  if (!IsSupportedMapAxis(width) || !IsSupportedMapAxis(height)) return false;
  int cells = width * height;
  size_t size = 5 * cells + TerrainSnowNoiseSize(width, height);
  void *storage = map->storage;
  if (!storage || map->width != width || map->height != height) {
    storage = calloc(1, size);
    if (!storage) return false;
    ReleaseCityTerrainMap(map);
  } else memset(storage, 0, size);
  *map = (CityTerrainMap){.storage = storage, .width = width, .height = height, .cell_count = cells,
      .altitude = storage, .terrain = (u8 *)storage + 2 * cells,
      .buildings = (u8 *)storage + 3 * cells, .flags = (u8 *)storage + 4 * cells};
  return true;
}

// Finish imported or generated terrain using the selected climate, without generating new land or trees.
void FinishCityTerrainMap(CityTerrainMap *map, const CityTerrainSettings *settings) {
  map->climate = settings->climate;
  map->water_tiles = map->tree_tiles = 0;
  map->minimum_height = 31; map->maximum_height = 0;
  for (int tile = 0; tile < map->cell_count; ++tile) {
    int height = map->altitude[tile] & CITY_ALTITUDE_LAND_MASK;
    map->water_tiles += IsTileKindExposedWater(map->buildings[tile]);
    map->tree_tiles += map->buildings[tile] >= kTreeFirst && map->buildings[tile] <= kTreeLast;
    map->minimum_height = IntMin(map->minimum_height, height);
    map->maximum_height = IntMax(map->maximum_height, height);
  }
  map->snow = (CitySnowClimate){.seed = settings->seed ^ 0x6C8E9CF5, .line = CITY_SNOW_LINE_DISABLED,
      .points = (u8 *)map->storage + 5 * map->cell_count};
  InitializeTerrainSnowNoise(&map->snow, map->width, map->height);
  map->desert_limit = 0;
  if (settings->climate == CITY_TERRAIN_DESERT || settings->climate == CITY_TERRAIN_MIXED)
    map->desert_limit = CalculateTerrainDesertLimit(map, settings->desert_coverage);
  if (settings->climate == CITY_TERRAIN_SNOWY || settings->climate == CITY_TERRAIN_MIXED) {
    map->snow.variation = settings->snow_coverage && settings->snow_coverage < 100 ? CITY_SNOW_LINE_VARIATION : 0;
    map->snow.line = CalculateTerrainSnowLine(map, settings->snow_coverage);
  }
}

// Generate an owned terrain map from validated settings and deterministic seeds. Output must be a valid map owner;
// false reports admission/allocation failure, and successful storage is released through ReleaseCityTerrainMap.
bool GenerateTerrainMap(const CityTerrainSettings *settings, CityTerrainMap *map) {
  if (!map || !AreCityTerrainSettingsValid(settings)) return false;
  int width = settings->width ? settings->width : kTerrainEdge;
  int height = settings->height ? settings->height : kTerrainEdge;
  TerrainWork work = {0};
  if (!AllocateTerrainWork(&work, width, height)) return false;
  work.process_state = work.game_state = settings->seed;
  work.features = settings->features;
  work.coast_sides = settings->coast_sides;
  work.smoothness = settings->smoothness;
  if (work.features & kCanyon)
    work.features &= ~(kMeander | kDelta | kCrossing | kBranch | kRejoin | kValley | kSalty);
  u32 f = work.features;
  work.ocean = f & (kOcean | kDelta | kCliffs | kRandomCoast);
  work.river = !(f & kCanyon) &&
      (f & (kRiver | kValley | kDelta | kMeander | kCrossing | kBranch | kRejoin));
  bool salty_river = work.river && (f & kSalty);
  bool extended_features = (f & ~(kOcean | kRiver | kSalty)) || ((f & kOcean) && (f & kRiver)) ||
      (work.coast_sides && work.ocean) || (work.cells > kTerrainCells && work.ocean);
  work.hills = settings->hills;
  work.water = settings->water;
  work.trees = settings->trees;
  work.sea = kGeneratedSeaLevel;
  SeedTerrainHeights(&work, settings->smoothness);
  if (!extended_features && work.river) CarveClassicRiver(&work);
  if (extended_features || work.ocean) CarveTerrainFeatures(&work, settings->seed);
  DistributeTerrainVariety(&work, settings->variety, settings->seed);
  bool natural_water = !work.ocean && !work.river && !(f & (kLake | kLakes));
  if (natural_water) CarveNaturalTerrainWater(&work);
  // Reserve the export offset before grading and temporary five-bit encoding;
  // finished hills must fit without clipping or wrapping on translation.
  for (int at = 0; at < work.cells; ++at)
    work.heights[at] = IntClamp(work.heights[at], 0, kMaximumConstructionHeight);
  PrepareTerrainCoasts(&work);
  GradeTerrainHeights(&work, work.heights);
  FillUnsupportedTerrainSlopes(&work, false);
  RetileGeneratedTerrain(&work);
  MarkGeneratedRiverChannels(&work);
  if (salty_river) FinishTerrainSaltyRiver(&work);
  GrowTerrainTrees(&work);
  if (!natural_water) ScatterTerrainFreshwater(&work, settings->seed);
  AddTerrainStreams(&work, settings->small_rivers, settings->seed);
  ConnectTerrainWaterfalls(&work);
  SetGeneratedWaterElevations(&work);
  if (!PrepareCityTerrainMap(map, width, height)) { free(work.storage); return false; }
  map->minimum_height = 31;
  map->water_level = kGeneratedSeaLevel;
  map->ocean = work.ocean || salty_river;
  map->river = work.river;
  map->process_state = work.process_state;
  map->game_state = work.game_state;
  map->climate = settings->climate;
  // Working data is x-major and sea-relative. Rebase both elevations while
  // copying to native y-major order; sloped freshwater stores its bed's base.
  for (int bx = 0; bx < width; bx += 32) for (int by = 0; by < height; by += 32)
    for (int x = bx; x < bx + 32; x++) for (int y = by; y < by + 32; y++) {
      int source = x * height + y, destination = y * width + x;
      int flags = work.flags[source], shape = work.terrain[source];
      int land = work.heights[source] + kGeneratedSeaLevel;
      int water = flags & kWater ? ReadGeneratedWaterHeight(&work, source) + kGeneratedSeaLevel : 0;
      if (flags & kInclinedWater) water = land;
      map->altitude[destination] = land | (water << CITY_ALTITUDE_WATER_SHIFT);
      int kind = work.buildings[source];
      if (flags & kWater) {
        // Saltwater channels use coastal banks; their generation-only river
        // identity must not flatten the shore slopes.
        bool river = !(flags & kSaltWater) && (flags & (kRiverWater | kInclinedWater));
        kind = river ? TK_RIVER : land == water && shape ? TK_WATER_BANK : TK_WATER;
      }
      if (kind == TK_RIVER && !(flags & kInclinedWater) && land == water) shape = CITY_TERRAIN_FLAT;
      map->terrain[destination] = shape;
      map->buildings[destination] = kind;
      map->flags[destination] = flags & ~(kRiverWater | kInclinedWater | kWater);
      map->water_tiles += (flags & kWater) != 0;
      map->tree_tiles += work.buildings[source] >= kTreeFirst && work.buildings[source] <= kTreeLast;
      map->minimum_height = IntMin(map->minimum_height, land);
      map->maximum_height = IntMax(map->maximum_height, land);
    }
  FinishCityTerrainMap(map, settings);
  free(work.storage);
  return true;
}
