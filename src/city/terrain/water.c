#include "internal.h"

#include <limits.h>

#include "city/maps/tables.h"

// Generation-only ponds, stream routing, water elevations and bank normalization.
// All stages reuse the pipeline's column-major TerrainWork arrays; no live City state is accessed.

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
void ScatterTerrainFreshwater(TerrainWork *work, u32 seed) {
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
void ConnectTerrainWaterfalls(TerrainWork *work) {
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
void AddTerrainStreams(TerrainWork *work, int density, u32 seed) {
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
    int nx = x + kClockwiseNeighborX[near], ny = y + kClockwiseNeighborY[near];
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
    bool own_drop = shape && !(kTerrainRaisedCorners[shape] & (1 << own_corner));
    bool far_drop = other && !(kTerrainRaisedCorners[other] & (1 << far_corner));
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
      if (GeneratedBankWaterLimit(work, next, d, shape > 0 ? kTerrainRaisedCorners[shape] : 15) < water) {
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
              GeneratedBankWaterLimit(work, next, d, shape > 0 ? kTerrainRaisedCorners[shape] : 15));
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
void SetGeneratedWaterElevations(TerrainWork *work) {
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
          int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
      int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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
        int nx = x + kClockwiseNeighborX[d], ny = y + kClockwiseNeighborY[d];
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

