#include "city/terrain/terrain.h"
#include <limits.h>

#include "city/maps/tables.h"
#include "internal.h"

// Native city terrain: continuous relief, city-scale waterways and cached climate.

enum { kMaximumConstructionHeight = CITY_ALTITUDE_LAND_MASK - kGeneratedSeaLevel + kConstructionLandOffset };

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
          int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
      // Use the shared clockwise offsets, then transpose the corner bits
      // into native city axes below.
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
      int nx = x + kClockwiseNeighborX[direction], ny = y + kClockwiseNeighborY[direction];
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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

// Generate an owned terrain map from validated settings and deterministic seeds. Output must be a valid map owner;
// false reports admission/allocation failure, and successful storage is released through ReleaseCityTerrainMap.
bool GenerateTerrainMap(const CityTerrainSettings *settings, CityTerrainMap *map) {
  if (!map || !AreCityTerrainSettingsValid(settings)) return false;
  int width = settings->width ? settings->width : CITY_TERRAIN_DEFAULT_SIZE;
  int height = settings->height ? settings->height : CITY_TERRAIN_DEFAULT_SIZE;
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
  if (!ResetCityTerrainMap(map, width, height)) { free(work.storage); return false; }
  map->water_level = kGeneratedSeaLevel;
  map->ocean = work.ocean || salty_river;
  map->river = work.river;
  map->process_state = work.process_state;
  map->game_state = work.game_state;
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
    }
  FinishCityTerrainMap(map, settings);
  free(work.storage);
  return true;
}
