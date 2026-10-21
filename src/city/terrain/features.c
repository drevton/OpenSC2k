#include "internal.h"
#include <limits.h>

// Water features ported from OpenSC2K new_city/features.rs.

enum { kMaximumPaths = 9, kChannelPoints = 33, kMaximumOxbows = 2 };
typedef struct {
  TerrainVector points[kChannelPoints], low, high;
  int count;
} TerrainPath;
typedef struct {
  TerrainPath paths[kMaximumPaths], oxbows[kMaximumOxbows];
  int count, oxbow_count;
} TerrainChannels;
typedef struct {
  int *points;
  int stride;
  int threshold;
} TerrainCoastField;

// Mix an unsigned seed with defined wraparound for reproducible coast variation. Returns a value without advancing any
// City or generation RNG state.
static u32 TerrainCoastHash(u32 value) {
  value ^= value >> 16;
  value *= 0x7FEB352D;
  value ^= value >> 15;
  value *= 0x846CA68B;
  return value ^ (value >> 16);
}

// Advance the supplied coast seed and return a bounded deterministic residue. State must be writable and limit
// positive; no shared simulation generator is touched.
static int TerrainCoastRandom(u32 *state, int limit) {
  *state = TerrainCoastHash(*state + 0x9E3779B9);
  return *state % limit;
}

// Smooth a strided coastline with repeated box filters and clamped endpoints. Points must contain length strided
// values and buffer length temporary values; filtering updates points in place and reuses caller workspace.
static void LowPassTerrainCoastLine(int *points, int length, int stride, int *buffer, int radius, int passes) {
  // Repeated box filters approximate a Gaussian. Clamp the endpoints and keep
  // fractional tile depths until rasterization; integer depths made tiny steps.
  for (int pass = 0; pass < passes; ++pass) {
    for (int i = 0; i < length; ++i) {
      int sum = 0;
      for (int near = i - radius; near <= i + radius; ++near)
        sum += points[IntClamp(near, 0, length - 1) * stride];
      buffer[i] = sum / (radius * 2 + 1);
    }
    for (int i = 0; i < length; ++i) points[i * stride] = buffer[i];
  }
}

// Recompute the low/high fixed-point bounds of the path's active points. Path count must fit its initialized point
// array; only the cached bounds change.
static void TerrainPathBounds(TerrainPath *path) {
  path->low = TerrainV(FIXED_INFINITY, FIXED_INFINITY);
  path->high = TerrainV(-FIXED_INFINITY, -FIXED_INFINITY);
  for (int i = 0; i < path->count; i++) {
    TerrainVector p = path->points[i];
    path->low = TerrainV(FixedMin(path->low.x, p.x), FixedMin(path->low.y, p.y));
    path->high = TerrainV(FixedMax(path->high.x, p.x), FixedMax(path->high.y, p.y));
  }
}

// Append a meandering channel path between two normalized fixed-point endpoints. Caller must reserve path capacity and
// supply a nondegenerate segment; channel storage remains in the generation owner.
static void AddTerrainChannel(
    TerrainChannels *channels, TerrainVector start, TerrainVector end, Fixed bend, Fixed phase) {
  TerrainPath *path = &channels->paths[channels->count++];
  TerrainVector direction = TerrainSub(end, start);
  Fixed length = TerrainLength(direction);
  if (length != 0) direction = TerrainDivideScalar(direction, length);
  TerrainVector normal = {direction.y, -direction.x};
  path->count = kChannelPoints;
  for (int step = 0; step < kChannelPoints; step++) {
    Fixed t = FixedFromInt(step) / 32;
    Fixed meander = FixedMul(FixedSin(FixedMul(t, kTerrainPi)),
        (bend + FixedMul(1704, FixedSin(FixedMul(FixedMul(t, (kTerrainPi * 2)), 98304) + phase))));
    Fixed weight = t;
    TerrainVector point = {
        start.x + FixedMul((end.x - start.x), weight), start.y + FixedMul((end.y - start.y), weight)};
    path->points[step] = TerrainAdd(point, TerrainScale(normal, meander));
  }
}

// Project a fixed-point point onto a segment, clamping to either endpoint. Degenerate segments return their first
// endpoint; no state or randomness is changed.
static TerrainVector ClosestTerrainSegment(TerrainVector point, TerrainVector a, TerrainVector b) {
  TerrainVector offset = TerrainSub(point, a), direction = TerrainSub(b, a);
  Fixed length = TerrainLengthSquared(direction);
  if (length == 0) return a;
  Fixed along = FixedDiv((FixedMul(direction.x, offset.x) + FixedMul(direction.y, offset.y)), length);
  if (along <= 0) return a;
  if (along >= FIXED_ONE) return b;
  return TerrainAdd(a, TerrainScale(direction, along));
}

// Test whether a point lies within the supplied width of any initialized path segment. Width and coordinates use the
// same fixed-point units; the path is borrowed and remains unchanged.
static bool NearTerrainChannel(TerrainVector point, const TerrainPath *path, Fixed width) {
  Fixed px = point.x, py = point.y;
  for (int i = 1; i < path->count; i++) {
    TerrainVector a = path->points[i - 1], b = path->points[i];
    if (px < FixedMin(a.x, b.x) - width || px > FixedMax(a.x, b.x) + width || py < FixedMin(a.y, b.y) - width ||
        py > FixedMax(a.y, b.y) + width)
      continue;
    if (TerrainLengthSquared(TerrainSub(point, ClosestTerrainSegment(point, a, b))) <= FixedMul(width, width))
      return true;
  }
  return false;
}

// Perturb channel paths and record eligible oxbow candidates using generation randomness. Requires initialized bounded
// channel storage; updates paths in place and respects the oxbow-capacity limit.
static void MeanderTerrainChannels(
    TerrainWork *work, TerrainChannels *channels, Fixed width, Fixed angle, Fixed phase) {
  Fixed amplitude = 12452 + FixedFromInt(TerrainGameRandom(work, 60)) / 1000;
  Fixed frequency = FixedMul((kTerrainPi * 2), (95027 + FixedFromInt(TerrainGameRandom(work, 350)) / 1000));
  TerrainVector bends[kMaximumPaths * (kChannelPoints - 2)];
  int count = 0;
  for (int p = 0; p < channels->count; p++) {
    TerrainPath *path = &channels->paths[p];
    for (int i = 0; i < path->count; i++) {
      TerrainVector *point = &path->points[i];
      Fixed fade = TerrainSmoothstep(58982, 39322, FixedAbs(point->y));
      point->x = point->x + FixedMul(FixedMul(fade, amplitude), FixedSin(FixedMul(point->y, frequency) + phase));
    }
    for (int i = 1; i < path->count - 1; i++) {
      TerrainVector point = path->points[i];
      if (FixedAbs(point.x) < 7864 || FixedAbs(point.y) > 20972) continue;
      if (FixedMul((point.x - path->points[i - 1].x), (path->points[i + 1].x - point.x)) < 0) bends[count++] = point;
    }
  }
  if (!count || TerrainGameRandom(work, 100) >= 35) return;
  int first = TerrainGameRandom(work, count), limit = 1 + TerrainGameRandom(work, 2);
  for (int attempt = 0; attempt < count; attempt++) {
    TerrainVector point = bends[(first + attempt) % count];
    Fixed side = FixedFromInt(point.x > 0 ? 1 : point.x < 0 ? -1 : 0);
    TerrainVector center = TerrainAdd(point, TerrainV(FixedMul(side, (FixedMul(width, 98304) + 4588)), 0));
    TerrainPath lake = {.count = 17};
    bool clear = true;
    for (int i = 0; i < 17; i++) {
      Fixed t = (-kTerrainPi) / 2 + (kTerrainPi * i) / 16;
      TerrainVector sample =
          TerrainAdd(center, TerrainV(FixedMul(FixedMul(side, 2949), FixedCos(t)), FixedMul(3604, FixedSin(t))));
      TerrainVector world = TerrainRotate(sample, angle);
      if (FixedAbs(world.x) > 30147 || FixedAbs(world.y) > 30147) clear = false;
      for (int p = 0; p < channels->count; p++)
        if (NearTerrainChannel(sample, &channels->paths[p], FixedMul(width, 98304) + 2621)) clear = false;
      lake.points[i] = sample;
    }
    if (clear) channels->oxbows[channels->oxbow_count++] = lake;
    if (channels->oxbow_count >= limit) break;
  }
}

// Build and smooth seeded depth profiles for enabled map edges. Requires generation scratch large enough for all
// profiles and the reusable queue; these buffers remain reserved until coast carving finishes.
static void BuildTerrainCoastProfiles(TerrainWork *work, u32 seed, Fixed wetness) {
  // Reuse scratch for the edge profiles until water distances need it;
  // four edge profiles fit even in the smallest supported map's buffer.
  int offset = 0;
  bool random_coast = work->features & kRandomCoast;
  for (int side = 0; side < 4; ++side) {
    int length = side & 1 ? work->height : work->width;
    int span = side & 1 ? work->width : work->height;
    if (work->coast_sides & (1 << side)) {
      u32 edge_seed = TerrainCoastHash(seed ^ (side + 1) * 0x9E3779B9);
      if (!random_coast) {
        // Follow independent random targets as the retired coastline generator
        // did. Occasional two-tile steps add sharper crags without long jumps.
        u32 random = edge_seed;
        int depth = 15;
        for (int i = 0; i < length; ++i) {
          int target = TerrainCoastRandom(&random, 31);
          int step = TerrainCoastRandom(&random, 4) == 0 ? 2 : 1;
          depth += IntClamp(target - depth, -step, step);
          work->scratch[offset + i] = depth * 256;
        }
        const int *points = work->scratch + offset;
        for (int i = 0; i < length; ++i) {
          int filtered = (points[IntMax(0, i - 1)] + points[i] * 2 +
              points[IntMin(length - 1, i + 1)]) / 4;
          // Interpolate filter strengths at 32-tile spacing. Q8 smoothstep
          // keeps the calm/craggy transitions gradual without floating point.
          int segment = i >> 5;
          int first = (TerrainCoastHash(edge_seed ^ 0x1B56C4E9 ^ (u32)segment * 0x9E3779B9) >> 24) * 205 / 255;
          int next = (TerrainCoastHash(edge_seed ^ 0x1B56C4E9 ^ (u32)(segment + 1) * 0x9E3779B9) >> 24) * 205 / 255;
          int blend = (i & 31) * 8;
          blend = blend * blend * (768 - 2 * blend) >> 16;
          int strength = (first * (256 - blend) + next * blend + 128) >> 8;
          work->queue[i] = points[i] + ((filtered - points[i]) * strength >> 8);
        }
        // Center Water on the walk's average depth and retain fractional tile
        // centers until rasterization. The ocean is always at least three tiles.
        int water_shift = (work->water * 1536 + 23) / 47 - 768;
        for (int i = 0; i < length; ++i) {
          work->scratch[offset + i] = IntMax(640, work->queue[i] + water_shift - 128);
        }
        offset += length;
        continue;
      }
      Fixed variation = FixedFromInt(((edge_seed >> 16) & 255)) / 255;
      Fixed breadth = FixedFromInt(((edge_seed >> 8) & 255)) / 255;
      Fixed frequency = 42598 + breadth * 6;
      TerrainNoise broad = MakeTerrainNoise(edge_seed, frequency / IntMin(length, 512), 3);
      TerrainNoise medium = MakeTerrainNoise(edge_seed ^ 0xB5297A4D, FIXED_ONE / 64, 2);
      TerrainNoise fine = MakeTerrainNoise(edge_seed ^ 0x68E31DA4, FIXED_ONE / 4, 2);
      Fixed reach = FixedMul(
          TerrainLerp(13107, 22282, wetness) * IntMin(span, 512), (42598 + FixedFromInt((edge_seed >> 24)) / 510));
      int low = INT_MAX, high = INT_MIN;
      for (int i = 0; i < length; ++i) {
        int along = i + (edge_seed & 4095);
        Fixed value = SampleTerrainNoise(&broad, FixedFromInt(along), 0) +
            FixedMul(SampleTerrainNoise(&medium, FixedFromInt(along), 0), FixedMin(18350, FixedDiv(655360, reach)));
        int sample = FixedToInt(value * FIXED_ONE);
        work->scratch[offset + i] = sample;
        low = IntMin(low, sample);
        high = IntMax(high, sample);
      }
      // Keep some dry edge and at least one inlet on every requested side.
      // Increasing Water lowers the cutoff and deepens the same shore, rather
      // than reseeding it. Different sides need not have matching depths.
      Fixed cutoff = TerrainLerp(36700, 18350, wetness) + FixedMul((variation - 32768), 20972);
      for (int i = 0; i < length; ++i) {
        Fixed value = FixedFromInt((work->scratch[offset + i] - low)) / IntMax(1, high - low);
        work->scratch[offset + i] = FixedToInt(FixedDiv(FixedMul((value - cutoff), reach) * 256, (FIXED_ONE - cutoff)));
      }
      LowPassTerrainCoastLine(work->scratch + offset, length, 1, work->queue, 4, 3);
      // Smooth the large inlets, then restore small crags independently.
      for (int i = 0; i < length; ++i) {
        int along = i + (edge_seed & 4095);
        work->scratch[offset + i] +=
            (int)FixedRound(SampleTerrainNoise(&fine, FixedFromInt(along), FixedFromInt(along)) * 384);
      }
    }
    offset += length;
  }
}

// Combine the cached edge profiles into a fixed-point coastal depth at map coordinates. Profiles must already be built
// and x/y in range; this is a read-only sample.
static Fixed TerrainCoastDepthAt(const TerrainWork *work, int x, int y) {
  int offset = 0;
  int positions[] = {x, y, x, y};
  int distances[] = {y, work->width - 1 - x, work->height - 1 - y, x};
  Fixed depth = -FIXED_INFINITY;
  for (int side = 0; side < 4; ++side) {
    int length = side & 1 ? work->height : work->width;
    if (work->coast_sides & (1 << side)) {
      Fixed next = FixedFromInt(work->scratch[offset + positions[side]]) / 256 - FixedFromInt(distances[side]);
      // Round the inner corner where adjacent ordinary shores meet. This
      // bounded smooth maximum leaves the rest of either edge unchanged.
      Fixed blend = work->features & kRandomCoast ? 0 : FixedMax(0, 131072 - FixedAbs(depth - next)) / 2;
      depth = FixedMax(depth, next) + FixedMul(blend, blend) / 2;
    }
    offset += length;
  }
  return depth;
}

// Build the optional seeded relief field and choose a quantile threshold for random coast coverage. Returned field
// borrows generation workspace; its samples cannot outlive reuse of that workspace.
static TerrainCoastField BuildTerrainCoastField(TerrainWork *work, u32 seed) {
  TerrainCoastField field = {0};
  if (!(work->features & kRandomCoast)) return field;
  // One smooth coastal height field creates both headlands and offshore land.
  // Cache at four-tile spacing in existing scratch, following the edge profiles.
  // There is no coordinate warp or separate island-placement/retry pass.
  field.points = work->scratch + 2 * (work->width + work->height);
  field.stride = (work->width >> 2) + 1;
  int rows = (work->height >> 2) + 1;
  TerrainNoise noise = MakeTerrainNoise(seed ^ 0x49534C45, FIXED_ONE / (32 + ((seed >> 16) & 15)), 2);
  for (int y = 0; y < rows; ++y) for (int x = 0; x < field.stride; ++x)
      field.points[y * field.stride + x] =
          FixedToInt(SampleTerrainNoise(&noise, FixedFromInt(x * 4 + 29), FixedFromInt(y * 4 - 43)) * 4096);
  for (int y = 0; y < rows; ++y)
    LowPassTerrainCoastLine(field.points + y * field.stride, field.stride, 1, work->queue, 1, 2);
  for (int x = 0; x < field.stride; ++x)
    LowPassTerrainCoastLine(field.points + x, rows, field.stride, work->queue, 1, 2);

  // Like choosing sea level from a height histogram, choose an offshore
  // threshold from the actual wet area. This avoids seeds with no islands just
  // because all of their noise samples fall below a fixed arbitrary cutoff.
  enum { kCoastScores = 1024 };
  memset(work->queue, 0, kCoastScores * sizeof(*work->queue));
  int candidates = 0;
  for (int y = 1; y < rows - 1; ++y) for (int x = 1; x < field.stride - 1; ++x) {
      if (TerrainCoastDepthAt(work, x * 4, y * 4) < 524288) continue;
      int score = IntClamp(field.points[y * field.stride + x] / 8 + 512, 0, kCoastScores - 1);
      ++work->queue[score];
      ++candidates;
  }
  int goal = candidates * 78 / 100, cutoff = 0, total = 0;
  for (; cutoff < kCoastScores - 1; ++cutoff) {
    total += work->queue[cutoff];
    if (total >= goal) break;
  }
  field.threshold = candidates ? (cutoff - 512) * 8 : 4096;
  return field;
}

// Bilinearly sample the prepared coast field relative to its selected threshold. Field and coordinates must include
// the interpolation neighbors; returns fixed-point relief without changing the field.
static Fixed TerrainCoastReliefAt(const TerrainCoastField *field, int x, int y) {
  const int *points = field->points + (y >> 2) * field->stride + (x >> 2);
  Fixed fx = FixedFromInt((x & 3)) / 4, fy = FixedFromInt((y & 3)) / 4;
  Fixed top = TerrainLerp(FixedFromInt(points[0]), FixedFromInt(points[1]), fx);
  Fixed bottom = TerrainLerp(FixedFromInt(points[field->stride]), FixedFromInt(points[field->stride + 1]), fx);
  return TerrainLerp(top, bottom, fy) - FixedFromInt(field->threshold);
}

// Classify a normalized point against enabled small-map channel paths. Requires initialized channel geometry;
// large-map channels use separate rasterization and this probe returns no water for them.
static int TerrainChannelWet(const TerrainWork *work, TerrainVector point, Fixed rough, Fixed width, Fixed phase,
    const TerrainChannels *channels) {
  u32 f = work->features;
  // Large-map channels are rasterized separately in fixed tile units.
  if (work->cells > kTerrainCells) return 0;
  Fixed px = point.x, py = point.y;
  Fixed local_width = FixedMul(width, (FIXED_ONE + rough / 2 + FixedMul(7864, FixedSin(py * 31 + phase))));
  if (f & kMeander)
    local_width = FixedMul(width, (FIXED_ONE + FixedMul(rough, 13107) + FixedMul(3932, FixedSin(py * 10 + phase))));
  for (int p = 0; p < channels->count; p++) {
    const TerrainPath *path = &channels->paths[p];
    if (px < path->low.x - local_width || px > path->high.x + local_width || py < path->low.y - local_width ||
        py > path->high.y + local_width)
      continue;
    if (NearTerrainChannel(point, path, local_width))
      return kWater | kRiverWater | (work->features & kSalty ? kSaltyChannel : 0);
  }
  return 0;
}

// Rasterize a fixed-point path into map-scale river beds and optional saline channel markers. Requires initialized
// work/path/noise; updates heights and flags only in the path's bounded reach.
static void CarveTerrainChannelPath(
    TerrainWork *work, const TerrainPath *path, Fixed width, Fixed angle, const TerrainNoise *noise, bool salty) {
  Fixed radius = width * kTerrainLast;
  Fixed reach = FixedMul(radius, 114688);
  TerrainVector center = TerrainV(32768 * (work->width - 1), 32768 * (work->height - 1));
  for (int segment = 1; segment < path->count; ++segment) {
    TerrainVector a =
        TerrainAdd(center, TerrainScale(TerrainRotate(path->points[segment - 1], angle), FixedFromInt(kTerrainLast)));
    TerrainVector b =
        TerrainAdd(center, TerrainScale(TerrainRotate(path->points[segment], angle), FixedFromInt(kTerrainLast)));
    int left = IntMax(0, (int)FixedFloor(FixedMin(a.x, b.x) - reach));
    int right = IntMin(work->width - 1, (int)FixedCeil(FixedMax(a.x, b.x) + reach));
    int top = IntMax(0, (int)FixedFloor(FixedMin(a.y, b.y) - reach));
    int bottom = IntMin(work->height - 1, (int)FixedCeil(FixedMax(a.y, b.y) + reach));
    for (int x = left; x <= right; ++x) for (int y = top; y <= bottom; ++y) {
        TerrainVector point = TerrainV(FixedFromInt(x), FixedFromInt(y));
        TerrainVector local = TerrainDetailPoint(work, x, y, angle);
        Fixed rough = SampleTerrainNoise(noise, local.x, local.y);
        Fixed local_radius = FixedMul(radius, (FIXED_ONE + rough / 2 + FixedMul(7864, FixedSin(local.y * 31))));
        if (TerrainLengthSquared(TerrainSub(point, ClosestTerrainSegment(point, a, b))) >
            FixedMul(local_radius, local_radius))
          continue;
        int at = x * work->height + y;
        work->heights[at] = IntMax(work->sea - 2, 0);
        work->flags[at] |= kRiverWater;
        if (salty) work->flags[at] |= kSaltyChannel;
    }
  }
}

// Main systems scale slowly with map size. Each has an irregular course built from connected tile-scale reaches; forks
// and splits occur once per system. Requires prepared channels/noise and generation workspace; rasterizes in tile
// units without changing a live City or allocating a new map plane.
static void CarveLargeTerrainChannels(TerrainWork *work, Fixed width, Fixed angle, const TerrainNoise *noise) {
  if (!work->river) return;
  TerrainVector low = TerrainV(FIXED_INFINITY, FIXED_INFINITY), high = TerrainV(-FIXED_INFINITY, -FIXED_INFINITY);
  for (int corner = 0; corner < 4; ++corner) {
    TerrainVector point = TerrainDetailPoint(work,
        corner & 1 ? work->width - 1 : 0, corner & 2 ? work->height - 1 : 0, angle);
    low = TerrainV(FixedMin(low.x, point.x), FixedMin(low.y, point.y));
    high = TerrainV(FixedMax(high.x, point.x), FixedMax(high.y, point.y));
  }
  int rivers = TerrainMainRiverCount(work);
  Fixed spacing = (high.x - low.x) / rivers;
  for (int river = 0; river < rivers; ++river) {
    Fixed center =
        low.x + FixedMul(spacing, (FixedFromInt(river) + 22938 + FixedFromInt(TerrainGameRandom(work, 301)) / 1000));
    Fixed offset = FixedFromInt(TerrainGameRandom(work, 1000)) / 1000;
    Fixed drift = FixedMin(FixedMul(spacing, 16384), 81920);
    int first = (int)FixedFloor(low.y - offset), last = (int)FixedCeil(high.y - offset);
    int junction_row = first + (last - first) * (30 + TerrainGameRandom(work, 41)) / 100;
    Fixed side = FixedFromInt(work->features & kBranch ? -1 : (TerrainGameRandom(work, 2) ? 1 : -1));
    for (int row = first; row < last; ++row) {
      TerrainChannels channels = {0};
      Fixed phase = FixedFromInt(TerrainGameRandom(work, 6283)) / 1000;
      Fixed from = FixedFromInt(row) + offset, to = from + FIXED_ONE;
      TerrainVector start =
          TerrainV(center + FixedMul(SampleTerrainNoise(noise, center, FixedMul(from, 16384)), 10486) +
                  FixedMul(SampleTerrainNoise(noise, center + 851968, FixedMul(from, 2621)), drift),
              from);
      TerrainVector end = TerrainV(center + FixedMul(SampleTerrainNoise(noise, center, FixedMul(to, 16384)), 10486) +
              FixedMul(SampleTerrainNoise(noise, center + 851968, FixedMul(to, 2621)), drift),
          to);
      Fixed bend = work->features & kMeander ? FixedMul(11796, FixedSin(phase)) : FixedMul(2949, FixedSin(phase));
      if (row == junction_row && (work->features & kRejoin)) {
        AddTerrainChannel(&channels, start, end, -11796, phase);
        AddTerrainChannel(&channels, start, end, 13107, phase + FIXED_ONE);
      } else if (row == junction_row && (work->features & (kBranch | kCrossing))) {
        TerrainVector junction = TerrainV(TerrainLerp(start.x, end.x, 42598), from + 42598);
        TerrainVector tributary = work->features & kBranch
            ? TerrainV(center + FixedMul(FixedMul(side, spacing), 45875), low.y - 32768)
            : TerrainV(side < 0 ? low.x - 32768 : high.x + 32768, from - 32768);
        AddTerrainChannel(&channels, start, junction, bend, phase);
        AddTerrainChannel(&channels, tributary, junction, 5243, phase + 131072);
        AddTerrainChannel(&channels, junction, end, 2294, phase + FIXED_ONE);
      } else {
        AddTerrainChannel(&channels, start, end, bend, phase);
      }
      if (work->features & kDelta) {
        TerrainVector world = TerrainRotate(end, angle);
        int x = (int)FixedRound(world.x * kTerrainLast + 32768 * (work->width - 1));
        int y = (int)FixedRound(world.y * kTerrainLast + 32768 * (work->height - 1));
        if (x >= 0 && y >= 0 && x < work->width && y < work->height &&
            (work->flags[x * work->height + y] & kSaltWater)) {
          for (int branch = -1; branch <= 1; branch += 2)
            AddTerrainChannel(&channels, start, TerrainAdd(end, TerrainV(20972 * branch, 0)), 5243 * branch,
                phase + FixedFromInt(branch));
        }
      }
      for (int path = 0; path < channels.count; ++path)
        CarveTerrainChannelPath(work, &channels.paths[path], width, angle, noise, work->features & kSalty);
      if ((work->features & kMeander) && TerrainGameRandom(work, 8) == 0) {
        TerrainPath oxbow = {.count = 17};
        for (int point = 0; point < oxbow.count; ++point) {
          Fixed t = (-kTerrainPi) / 2 + (kTerrainPi * point) / 16;
          oxbow.points[point] = TerrainV(
              (start.x + end.x) / 2 + 15729 + FixedMul(2949, FixedCos(t)), from + 32768 + FixedMul(3604, FixedSin(t)));
        }
        CarveTerrainChannelPath(work, &oxbow, 786, angle, noise, false);
      }
    }
  }
}

// Carve enabled lake basins using water-distance workspace and generation random state. Requires allocated scratch,
// seen and queue arrays; overwrites those temporary buffers and edits generated heights/flags.
static void CarveTerrainLakes(TerrainWork *work) {
  int count = work->features & kLakes ? 2 : 1;
  TerrainWaterDistances(work);
  for (int at = 0; at < work->cells; ++at) work->seen[at] = IntMin(work->scratch[at], 255);
  for (int origin_x = 0; origin_x < work->width; origin_x += kTerrainEdge)
    for (int origin_y = 0; origin_y < work->height; origin_y += kTerrainEdge) {
      TerrainVector first_center = TerrainV(0, 0);
      Fixed first_radius = 0;
      for (int lake = 0; lake < count; lake++) {
        int px = origin_x + 24 + TerrainGameRandom(work, 80);
        int py = origin_y + 24 + TerrainGameRandom(work, 80);
        TerrainVector preferred = TerrainV(FixedFromInt(px), FixedFromInt(py)), center = TerrainV(0, 0);
        Fixed phase = FixedFromInt(TerrainGameRandom(work, 6283)) / 1000;
        // Find a fixed anchor using the largest footprint, then interpolate area
        // rather than radius. Water changes grow the lake instead of moving it.
        Fixed desired = FixedFromInt(count == 2 ? 30 : 20);
        Fixed clearance = 0, best = -FIXED_INFINITY;
        for (int x = origin_x + 8; x < origin_x + 120; x += 2) {
          for (int y = origin_y + 8; y < origin_y + 120; y += 2) {
            if (work->heights[x * work->height + y] < work->sea) continue;
            int border = IntMin(IntMin(x, y), IntMin(work->width - 1 - x, work->height - 1 - y));
            Fixed space = FixedMin(45875 * work->seen[x * work->height + y], FixedFromInt(border));
            if (lake) {
              Fixed gap = TerrainLength(TerrainSub(TerrainV(FixedFromInt(x), FixedFromInt(y)), first_center)) -
                  FixedMul(first_radius, 78643);
              space = FixedMin(space, FixedMul(gap, 45875));
            }
            Fixed score = FixedMin(space, desired + 262144) * 4 -
                FixedMul(TerrainLength(TerrainSub(TerrainV(FixedFromInt(x), FixedFromInt(y)), preferred)), 1966);
            if (score > best) {
              best = score;
              center = TerrainV(FixedFromInt(x), FixedFromInt(y));
              clearance = space;
            }
          }
        }
        if (clearance < 196608) continue;
        Fixed low = FixedMin(FixedFromInt(count == 2 ? 10 : 13), FixedMul(clearance, 49152));
        Fixed high = FixedMin(desired, FixedMul(clearance, 49152));
        Fixed radius = FixedSqrt(TerrainLerp(FixedMul(low, low), FixedMul(high, high), FixedFromInt(work->water) / 47));
        if (!lake) { first_center = center; first_radius = high; }
        int reach = (int)FixedCeil(FixedMul(radius, 78643));
        int left = IntMax(0, (int)FixedToInt(center.x) - reach),
            right = IntMin(work->width - 1, (int)FixedToInt(center.x) + reach);
        int top = IntMax(0, (int)FixedToInt(center.y) - reach),
            bottom = IntMin(work->height - 1, (int)FixedToInt(center.y) + reach);
        for (int x = left; x <= right; ++x) {
          for (int y = top; y <= bottom; ++y) {
            TerrainVector point = TerrainDivide(TerrainSub(TerrainV(FixedFromInt(x), FixedFromInt(y)), center),
                TerrainV(radius, FixedMul(radius, 55706)));
            Fixed angle = TerrainAngle(point);
            Fixed edge =
                FIXED_ONE + FixedMul(6554, FixedSin(angle * 3 + phase)) + FixedMul(3932, FixedSin(angle * 5 - phase));
            if (TerrainLength(point) < edge) {
              int at = x * work->height + y;
              work->heights[at] = IntMax(work->sea - 2, 0);
              work->flags[at] = 0;
            }
          }
        }
      }
    }
}

// Select deterministic coast/channel geometry, carve enabled water features and apply elevation relief. Requires
// initialized work/settings; generation_seed and local generators determine output without host-time input.
void CarveTerrainFeatures(TerrainWork *work, u32 generation_seed) {
  Fixed wetness = FixedFromInt(work->water) / 47;
  Fixed angle = FixedFromInt(TerrainGameRandom(work, 6283)) / 1000;
  // Effective random edges belong to this generation only. Preserve the
  // player's saved edge checkboxes when choosing a random subset.
  if (work->features & kRandomCoast) {
    int eligible = work->coast_sides ? work->coast_sides : CITY_TERRAIN_COAST_ALL;
    int sides[4], count = 0;
    for (int side = 0; side < 4; ++side)
      if (eligible & (1 << side)) sides[count++] = side;
    u32 random = work->game_state ^ 0x45444745;
    int wanted = 1 + TerrainCoastRandom(&random, count);
    work->coast_sides = 0;
    for (int i = 0; i < wanted; ++i) {
      int selected = i + TerrainCoastRandom(&random, count - i);
      int side = sides[selected];
      sides[selected] = sides[i];
      sides[i] = side;
      work->coast_sides |= 1 << side;
    }
  } else if (!work->coast_sides && work->ocean) {
    work->coast_sides = CITY_TERRAIN_COAST_EAST;
  }
  if (work->coast_sides && work->ocean) {
    int side = TerrainGameRandom(work, 4);
    while (!(work->coast_sides & (1 << side))) side = (side + 1) & 3;
    // Point the main outlet/delta toward one of the requested sea edges.
    angle = side == 0 ? kTerrainPi : side == 1 ? (-kTerrainPi) / 2 : side == 2 ? 0 : kTerrainPi / 2;
  }
  Fixed phase = FixedFromInt(TerrainGameRandom(work, 6283)) / 1000;
  Fixed split = -11796 + FixedFromInt(TerrainGameRandom(work, 280)) / 1000;
  Fixed bend = -7864 + FixedFromInt(TerrainGameRandom(work, 240)) / 1000;
  Fixed width = TerrainLerp(1442, 3408, wetness);
  u32 f = work->features;
  bool delta = f & kDelta;
  TerrainChannels channels = {0};
  TerrainVector junction = TerrainV(bend, split), mouth = TerrainV(0, delta ? 1311 : 58982);
  if (f & kRejoin) {
    TerrainVector start = TerrainV(bend, -19661), end = TerrainV(FixedMul(bend, -32768), delta ? -5243 : 19661);
    AddTerrainChannel(&channels, TerrainV(-bend, -58982), start, 2621, phase);
    AddTerrainChannel(&channels, start, end, -11796, phase);
    AddTerrainChannel(&channels, start, end, 13107, phase + FIXED_ONE);
    AddTerrainChannel(&channels, end, mouth, 2621, phase);
    junction = end;
  } else if (work->river || (f & (kCrossing | kBranch))) {
    AddTerrainChannel(&channels, junction, mouth, 3604, phase);
    AddTerrainChannel(&channels, TerrainV(-bend, -58982), junction, 4915, phase + 85197);
  }
  if (f & kBranch) AddTerrainChannel(&channels, TerrainV(-47186, -52429), junction, -5243, phase + 157286);
  if (f & kCrossing) {
    Fixed side = FixedFromInt(TerrainGameRandom(work, 2) == 0 ? -1 : 1);
    AddTerrainChannel(&channels, TerrainV(FixedMul(side, 58982), split - 7864), junction, 5243, phase + 52429);
  }
  if (delta) {
    for (int branch = 0; branch < 3; branch++) {
      Fixed spread = FixedMul((FixedFromInt(branch) - FIXED_ONE), 27525);
      TerrainVector end = TerrainV(spread + FixedMul(2621, FixedSin(phase + FixedFromInt(branch))), 58982);
      AddTerrainChannel(&channels, mouth, end, FixedMul(spread, 13107), phase + FixedFromInt(branch));
    }
  }
  if ((f & kMeander) && work->cells == kTerrainCells)
    MeanderTerrainChannels(work, &channels, width, angle, phase);
  TerrainNoise noise = MakeTerrainNoise(TerrainGameRandom(work, 2147483647), 458752, 3);
  if (work->ocean)
    BuildTerrainCoastProfiles(work, f & kRandomCoast ? noise.seed : generation_seed, wetness);
  TerrainCoastField coast_field = BuildTerrainCoastField(work, noise.seed);
  for (int p = 0; p < channels.count; p++) TerrainPathBounds(&channels.paths[p]);
  for (int x = 0; x < work->width; x++) {
    for (int y = 0; y < work->height; y++) {
      TerrainVector point = TerrainMapPoint(work, x, y, angle);
      TerrainVector detail = TerrainDetailPoint(work, x, y, angle);
      Fixed rough =
          (SampleTerrainNoise(&noise, detail.x, detail.y) * kTerrainLast) / (IntMax(work->width, work->height) - 1);
      Fixed coast_depth = work->ocean ? TerrainCoastDepthAt(work, x, y) : -FIXED_INFINITY;
      Fixed relief = coast_field.points ? TerrainCoastReliefAt(&coast_field, x, y) : -FIXED_INFINITY;
      // Keep a navigable water channel between offshore peaks and the
      // mainland, and preserve water along the outer edge of the map.
      int border = IntMin(IntMin(x, y), IntMin(work->width - 1 - x, work->height - 1 - y));
      bool offshore_land = coast_depth > 262144 && relief > 0 && border >= 4;
      bool coast = coast_depth > 0 && !offshore_land;
      int wet = coast ? kSaltWater : offshore_land ? 0 : TerrainChannelWet(work, point, rough, width, phase, &channels);
      int at = x * work->height + y;
      if (wet) {
        // A shallow ordinary coastal bed meets raised land without flooding
        // an additional bank outside the generated shoreline.
        int depth = coast && !(work->features & kRandomCoast) ? 1 : 2;
        work->heights[at] = IntMax(work->sea - depth, 0);
        work->flags[at] |= wet & (kSaltWater | kSaltyChannel | kRiverWater);
      } else {
        // Freshwater permits level banks. Equal-height dry tiles remain dry
        // when tiled; the coastal pass handles raised sea shores separately.
        int land = work->sea;
        if (offshore_land) land += 1 + IntMin(3, (int)FixedCeil(relief / 256));
        work->heights[at] = IntMax(work->heights[at], land);
      }
    }
  }
  if (work->cells > kTerrainCells) CarveLargeTerrainChannels(work, width, angle, &noise);
  for (int lake = 0; lake < channels.oxbow_count; lake++) {
    for (int x = 0; x < work->width; x++) {
      for (int y = 0; y < work->height; y++) {
        if (NearTerrainChannel(TerrainMapPoint(work, x, y, angle), &channels.oxbows[lake], 786)) {
          int at = x * work->height + y;
          work->heights[at] = IntMax(work->sea - 2, 0);
          work->flags[at] = 0;
        }
      }
    }
  }
  if (f & (kLake | kLakes)) CarveTerrainLakes(work);
  ApplyTerrainElevation(work, angle, phase, &noise);
}
