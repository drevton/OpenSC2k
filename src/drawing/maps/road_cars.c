#include "city/maps/layers.h"
#include <limits.h>
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/bridges.h"
#include "city/engine/city_types.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "city/presentation.h"
#include "city/engine/simulation.h"
#include "city/infrastructure/infrastructure.h"
#include "ui/state.h"
#include "ui/frontend.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/tables.h"

#include "road-car-paths.inc"

// Decorative cars share the map vehicle renderer and host animation clock.
// Their bounded pool and random stream belong to Drawing, never saved City events.
static const i8 kRoadCarX[4] = {-1, 0, 1, 0};
static const i8 kRoadCarY[4] = {0, 1, 0, -1};

static u32 RoadTrafficRandom(DrawingRoadTraffic *traffic) {
  traffic->random = traffic->random * 1664525 + 1013904223;
  return traffic->random ^ (traffic->random >> 16);
}

static int RoadCarConnections(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return 0;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (IsTileKindBridge(kind)) {
    int mask = 0;
    for (int side = 0; side < 4; ++side)
      if (CityBridgeTransportKind(tp, side) == TK_ROAD) mask |= 1 << side;
    return mask;
  }
  if (kind != TK_ROAD &&
      !(IsTileKindGroundCrossing(kind) && IsTileKindRoadRoute(kind)) &&
      kind != TK_HIGHWAY_ROAD)
    return 0;
  if (HasTileWater(tp)) return 0;
  int connections = CityRoadConnectionMask(city, tile);
  if (IsTileKindGroundCrossing(kind))
    connections &= ReadCityCrossingConnections(tp);
  else if (kind == TK_HIGHWAY_ROAD)
    connections &= ReadCityHighwayAxis(tp) ? CONN_EW : CONN_NS;
  return connections;
}

static TileIndex RoadCarNeighbor(const City *city, TileIndex tile, int side) {
  return GetRelativeTileOrInvalid(city, tile, kRoadCarX[side], kRoadCarY[side]);
}

static int RoadCarExits(const City *city, TileIndex tile) {
  int mask = RoadCarConnections(city, tile), exits = 0;
  for (int side = 0; side < 4; ++side) {
    if (!(mask & (1 << side))) continue;
    TileIndex next = RoadCarNeighbor(city, tile, side);
    if (!(RoadCarConnections(city, next) & (1 << ((side + 2) & 3)))) continue;
    if ((IsTileKindBridge(city->tile[tile].kind) || IsTileKindBridge(city->tile[next].kind)) &&
        CityBridgeTransportEdgeHeight(city, tile, side) != CityBridgeTransportEdgeHeight(city, next, (side + 2) & 3))
      continue;
    exits |= 1 << side;
  }
  return exits;
}

static int RoadCarRouteExits(const City *city, TileIndex tile, int entry) {
  if (tile == INVALID_TILE) return 0;
  int exits = RoadCarExits(city, tile);
  // Deck and underpass are independent routes even when they share a map tile.
  if (IsTileKindBridge(city->tile[tile].kind)) exits &= entry & 1 ? CONN_NS : CONN_EW;
  return exits;
}

static int ChooseRoadCarExit(DrawingRoadTraffic *traffic, int exits, int entry) {
  int reverse = exits & (1 << entry);
  exits &= ~(1 << entry);
  int straight = (entry + 2) & 3;
  if ((exits & (1 << straight)) && (RoadTrafficRandom(traffic) & 3)) return straight;
  int count = __builtin_popcount((unsigned)exits);
  if (!count) return reverse ? entry : -1;
  int choice = RoadTrafficRandom(traffic) % count;
  for (int side = 0; side < 4; ++side)
    if ((exits & (1 << side)) && !choice--) return side;
  return -1;
}

static void RoadCarVisibleFragment(CityRect rect, void *context) {
  if (rect.right > rect.left && rect.bottom > rect.top) *(bool *)context = true;
}

static int RoadCarHeight(const RoadCar *car);

static bool RoadCarInView(const City *city, TileIndex tile, int height, CityMapCamera view, int view_index) {
  if (view.zoom < CITY_MAP_ZOOM_LARGE || view.width <= 0 || view.height <= 0) return false;
  const Drawing *drawing = city->game->drawing;
  u32 window_id = view_index ? drawing->views[view_index - 1].window_id : 0;
  if (view_index) {
    const Window *window = FindCityCanvasWindow(&city->game->ui->desktop, window_id);
    if (!window || !window->visible || window->shaded) return false;
  }
  CityPoint p = CityMapCameraPoint(city, view.rotation, MapTileX(city, tile), MapTileY(city, tile));
  int scale = 1 << view.zoom, margin = 64 * scale;
  int x = view.origin_x + (p.y - p.x + 1) * 4 * scale;
  int y = view.origin_y + (p.x + p.y) * 2 * scale -
      height * CityMapHeightStep(view.zoom) / 256;
  if (x < -margin || y < -margin || x >= view.width + margin || y >= view.height + margin) return false;
  int magnification = view_index                        ? 1 << IntMax(0, drawing->views[view_index - 1].zoom - 2)
      : drawing->default_map_view->magnified_large_zoom ? 2
                                                        : 1;
  CityRect vicinity = {(x - margin) * magnification, (y - margin) * magnification, (x + margin + 1) * magnification,
      (y + margin + 1) * magnification};
  bool visible = false;
  NativeMapCacheVisitView(
      &city->game->ui->desktop, window_id, vicinity, magnification, RoadCarVisibleFragment, &visible);
  return visible;
}

static bool RoadCarWatched(const City *city, const RoadCar *car) {
  const Drawing *drawing = city->game->drawing;
  int height = RoadCarHeight(car);
  for (int i = 0; i <= CITY_EXTRA_MAP_VIEW_COUNT; ++i) {
    if (i && !drawing->views[i - 1].window_id) continue;
    if (RoadCarInView(city, car->tile, height, drawing->map_snapshot.regions[i], i)) return true;
  }
  return false;
}

static int RoadCarBucket(TileIndex tile) { return (tile ^ (tile >> 10)) & (CITY_ROAD_CAR_BUCKETS - 1); }

static TileIndex SampleRoadCarTile(City *city, CityMapCamera view) {
  DrawingRoadTraffic *traffic = &city->game->drawing->road_traffic;
  int scale = 1 << view.zoom, margin = 64 * scale;
  int px = (int)(RoadTrafficRandom(traffic) % (view.width + margin * 2)) - margin - view.origin_x;
  int py = (int)(RoadTrafficRandom(traffic) % (view.height + margin * 2)) - margin - view.origin_y;
  CityPoint size = CityMapCameraSize(city, view.rotation);
  int elevation = 0;
  TileIndex tile = INVALID_TILE;
  // Invert a sampled screen position, following local ground height rather
  // than sampling the large conservative rectangle used for terrain culling.
  for (int pass = 0; pass < 5; ++pass) {
    int sum = (py + elevation * CityMapHeightStep(view.zoom)) * 2;
    int x = IntClamp((sum - px) >> (view.zoom + 3), 0, size.x - 1);
    int y = IntClamp((sum + px) >> (view.zoom + 3), 0, size.y - 1);
    tile = CityMapCameraTile(city, view.rotation, x, y);
    int next = ReadCitySurfaceHeight(&city->tile[tile]);
    if (next == elevation) break;
    elevation = next;
  }
  return tile;
}

static void InsertRoadCarBucket(DrawingRoadTraffic *traffic, RoadCar *car) {
  int bucket = RoadCarBucket(car->tile);
  car->next = traffic->buckets[bucket];
  traffic->buckets[bucket] = car - traffic->cars + 1;
}

static void RemoveRoadCarBucket(DrawingRoadTraffic *traffic, RoadCar *car, TileIndex tile) {
  int bucket = RoadCarBucket(tile), link = traffic->buckets[bucket], previous = 0;
  int slot = car - traffic->cars + 1;
  while (link && link != slot) {
    previous = link;
    link = traffic->cars[link - 1].next;
  }
  if (link) {
    if (previous)
      traffic->cars[previous - 1].next = car->next;
    else
      traffic->buckets[bucket] = car->next;
  }
  car->next = 0;
}

enum {
  ROAD_CAR_PROGRESS_ONE = 2048,
  ROAD_CAR_PROGRESS_MAX = ROAD_CAR_PROGRESS_ONE - 1,
  ROAD_CAR_AGE_SCALE = 256,
  ROAD_CAR_AGE_MAX = 32767,
  ROAD_CAR_LANE_CURVE = 0,
  ROAD_CAR_LANE_DIAGONAL = 2,
  ROAD_CAR_LANE_TRANSITION = 8,
  ROAD_CAR_LANE_APPROACH = 12,
  ROAD_CAR_LANE_SHARED = 16,
  ROAD_CAR_LANE_TURNAROUND = 32,
  ROAD_CAR_LANE_STRAIGHT = 33
};

typedef struct {
  int x, y; // Q12 tile coordinates, including the exact boundary endpoints.
  int length;
  u8 pose;
} RoadCarPath;

static RoadCarPath RotateRoadCarPath(RoadCarPath path, int rotation) {
  switch (rotation & 3) {
  case 1:
    path = (RoadCarPath){path.y, 4096 - path.x, path.length, path.pose};
    break;
  case 2:
    path = (RoadCarPath){4096 - path.x, 4096 - path.y, path.length, path.pose};
    break;
  case 3:
    path = (RoadCarPath){4096 - path.y, path.x, path.length, path.pose};
    break;
  }
  path.pose -= rotation * 64;
  return path;
}

static int RoadCarEntry(const RoadCar *car) {
  return ((kRoadCarPathPorts[car->path - 1] & 3) + car->rotation) & 3;
}

static int RoadCarExit(const RoadCar *car) {
  return ((kRoadCarPathPorts[car->path - 1] >> 2) + car->rotation) & 3;
}

static RoadCarPath SampleRoadCarPath(const RoadCar *car) {
  const u32 *points = kRoadCarPathPoints[car->path - 1];
  int step = car->progress >> 6, fraction = car->progress & 63;
  u32 a = points[step], b = points[step + 1];
  // Keep Q12 positions through projection. Rounding and clamping to the cached
  // Q8 tile coordinates makes diagonal lanes shift sideways at tile boundaries.
  int x = ((int)(a & 511) * (64 - fraction) + (int)(b & 511) * fraction + 2) / 4;
  int y = ((int)(a >> 9 & 511) * (64 - fraction) + (int)(b >> 9 & 511) * fraction + 2) / 4;
  int pose = a >> 18, turn = (i8)((b >> 18) - pose);
  pose += (turn * fraction + 32) >> 6;
  return RotateRoadCarPath((RoadCarPath){x, y, kRoadCarPathLengths[car->path - 1], pose}, car->rotation);
}

static void PrepareRoadCarPath(const City *city, RoadCar *car, int entry, int exit) {
  TileIndex tile = car->tile;
  // Contextual artwork is resolved once on entry, not while advancing or painting.
  int sprite = ReadTile(city, tile) == TK_ROAD
      ? CitySurfaceLineSprite(
            city, tile, ResolveCitySurfaceLineGround(city, tile).layout, CITY_MAP_ZOOM_LARGE, 0)
      : 0;
  int first =
      MAP_LANDSCAPE_SPRITE_FIRST + CITY_MAP_ZOOM_LARGE * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  int diagonal = sprite - first - CITY_LANDSCAPE_DIAGONAL_ROAD_FIRST;
  int transition = sprite - first - CITY_LANDSCAPE_DIAGONAL_ROAD_TRANSITION_FIRST;
  int shared = sprite - first - CITY_LANDSCAPE_DIAGONAL_ROAD_SHARED_FIRST;
  int kind, rotation;
  if (entry == exit) {
    kind = ROAD_CAR_LANE_TURNAROUND;
    rotation = exit;
  } else if (diagonal >= 0 && diagonal < 12) {
    rotation = diagonal / 3;
    int mask = diagonal % 3;
    bool reverse = ((entry - rotation) & 3) != CITY_LINE_WEST;
    kind = ROAD_CAR_LANE_DIAGONAL + mask * 2 + reverse;
  } else if (transition >= 0 && transition < 16) {
    rotation = transition / 4;
    int mask = transition / 2 & 1;
    bool approach = transition & 1;
    bool reverse = ((entry - rotation) & 3) != (approach && !mask ? CITY_LINE_NORTH : CITY_LINE_WEST);
    kind = (approach ? ROAD_CAR_LANE_APPROACH : ROAD_CAR_LANE_TRANSITION) + mask * 2 + reverse;
  } else if (shared >= 0 && shared < 8) {
    rotation = 0;
    bool reverse = entry != (shared / 4 ? CITY_LINE_NORTH : CITY_LINE_WEST);
    kind = ROAD_CAR_LANE_SHARED + shared * 2 + reverse;
  } else if (exit == ((entry + 2) & 3)) {
    kind = ROAD_CAR_LANE_STRAIGHT;
    rotation = (exit - CITY_LINE_EAST) & 3;
  } else {
    rotation = exit == ((entry + 1) & 3) ? entry : exit;
    kind = ROAD_CAR_LANE_CURVE + (entry != rotation);
  }
  int descriptor = kRoadCarPathKinds[kind];
  car->path = descriptor & 31;
  car->rotation = (rotation + (descriptor >> 5)) & 3;
  car->straight = kind == ROAD_CAR_LANE_STRAIGHT;
}

static CityPoint RoadCarDirection(u16 heading) {
  int step = (heading >> 8) & 63, fraction = heading & 255;
  int x = (kRoadCarDirectionQuarter[64 - step] * (256 - fraction) +
      kRoadCarDirectionQuarter[63 - step] * fraction + 128) >> 8;
  int y = (kRoadCarDirectionQuarter[step] * (256 - fraction) +
      kRoadCarDirectionQuarter[step + 1] * fraction + 128) >> 8;
  switch (heading >> 14) {
  case 1:
    return (CityPoint){-y, x};
  case 2:
    return (CityPoint){-x, -y};
  case 3:
    return (CityPoint){y, -x};
  default:
    return (CityPoint){x, y};
  }
}

static bool RoadCarFootprintsOverlap(const RoadCar *a, const RoadCar *b, int x, int y) {
  CityPoint axis = RoadCarDirection(a->heading), other = RoadCarDirection(b->heading);
  int along = abs(axis.x * other.x + axis.y * other.y) >> 8;
  int across = abs(axis.x * other.y - axis.y * other.x) >> 8;
  // Q8 half-extents include the bumper gap. All products fit signed 32 bits.
  const int half_length = 74, half_width = 24;
  int length = half_length * (256 + along) + half_width * across;
  int width = half_width * (256 + along) + half_length * across;
  return abs(x * axis.x + y * axis.y) < length && abs(x * axis.y - y * axis.x) < width &&
      abs(x * other.x + y * other.y) < length && abs(x * other.y - y * other.x) < width;
}

static CityPoint RoadCarBodyPoint(const RoadCar *car) {
  RoadCarPath path = SampleRoadCarPath(car);
  CityPoint direction = RoadCarDirection(car->heading);
  int offset = kRoadCarFrontAxles[car->model];
  return (CityPoint){(path.x - (direction.x * offset >> 8)) >> 4,
      (path.y - (direction.y * offset >> 8)) >> 4};
}

static int RoadCarSpriteView(const RoadCar *car, int rotation) {
  u16 heading = car->heading + rotation * 16384;
  CityPoint direction = RoadCarDirection(heading);
  int nearest = kRoadCarPoseViews[(u8)((heading + 128) >> 8)], best = nearest, distance = INT_MAX;
  // Pick the neighboring raster whose projected axle positions are closest.
  // Full heading precision avoids delaying a switch by a whole byte of yaw.
  for (int step = -1; step <= 1; ++step) {
    int view = (nearest + step + CITY_ROAD_VEHICLE_VIEWS) % CITY_ROAD_VEHICLE_VIEWS;
    CityPoint artwork = RoadCarDirection(kRoadCarViewHeadings[view]);
    int x = artwork.x - direction.x, y = artwork.y - direction.y;
    int error = 4 * (y - x) * (y - x) + (x + y) * (x + y);
    if (error < distance) { best = view; distance = error; }
  }
  return best;
}

static void UpdateRoadCarSteering(RoadCar *car) {
  int turn = (i16)(SampleRoadCarPath(car).pose * 256 - car->heading);
  // Engage at 17.5 degrees and release at 12.5. Quantized lane tangents near
  // the threshold must not flicker between straight and turned tyres.
  if (turn < -3186) car->steering = 1;
  else if (turn > 3186) car->steering = 2;
  else if (abs(turn) < 2276) car->steering = 0;
}

static int RoadCarSprite(const RoadCar *car, int view) {
  return (car->model * CITY_ROAD_VEHICLE_STEERING + car->steering) * CITY_ROAD_VEHICLE_VIEWS +
      view;
}

static CityPoint RoadCarDrawingPoint(const RoadCar *car, int rotation, int view) {
  RoadCarPath path = SampleRoadCarPath(car);
  CityPoint direction = RoadCarDirection(car->heading);
  int front = kRoadCarFrontAxles[car->model], rear = kRoadCarRearAxles[car->model];
  int wheelbase = front + rear;
  CityPoint midpoint = {path.x - 2048 - (direction.x * wheelbase >> 9),
      path.y - 2048 - (direction.y * wheelbase >> 9)};
  CityPoint artwork = RoadCarDirection(kRoadCarViewHeadings[view] - rotation * 16384);
  // Balance the small raster-heading error between both axles. Neither end
  // takes the complete heading step when the selected image changes.
  return (CityPoint){midpoint.x + (artwork.x * (rear - front) >> 9),
      midpoint.y + (artwork.y * (rear - front) >> 9)};
}

static void PrepareRoadCarSurface(const City *city, RoadCar *car, int entry) {
  TileIndex tile = car->tile;
  const TileInfo *tp = &city->tile[tile];
  if (IsTileKindBridge(tp->kind)) {
    bool axis = ReadCityBridgeAxis(tp);
    if ((entry & 1) != axis) {
      car->base_height = ReadCitySurfaceHeight(tp);
      car->surface = 0;
    } else {
      int a = CityBridgeTransportEdgeHeight(city, tile, axis ? CITY_LINE_NORTH : CITY_LINE_WEST);
      int b = CityBridgeTransportEdgeHeight(city, tile, axis ? CITY_LINE_SOUTH : CITY_LINE_EAST);
      car->base_height = IntMin(a, b);
      // A native bridge head joins its bank to a deck exactly one level higher.
      car->surface = a == b ? 0 : a < b ? (axis ? 6 : 12) : (axis ? 9 : 3);
    }
    return;
  }
  CityMapSurface surface = ResolveCitySurfaceLineGround(city, tile);
  static const u8 kCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15};
  car->base_height = ReadCityLandHeight(tp) + surface.raised;
  car->surface = kCorners[surface.shape];
}

static int RoadCarHeightAt(const RoadCar *car, int u, int v) {
  int mask = car->surface;
  int north = (mask & 1 ? 256 - u : 0) + (mask & 8 ? u : 0);
  int south = (mask & 2 ? 256 - u : 0) + (mask & 4 ? u : 0);
  return car->base_height * 256 + (north * (256 - v) + south * v + 128) / 256;
}

static int RoadCarHeight(const RoadCar *car) {
  RoadCarPath path = SampleRoadCarPath(car);
  return RoadCarHeightAt(car, IntClamp((path.x + 8) >> 4, 0, 255), IntClamp((path.y + 8) >> 4, 0, 255));
}

static int RoadCarHeadingChange(u16 heading, int dx, int dy, int wheelbase) {
  CityPoint direction = RoadCarDirection(heading);
  // 10430 is 65536 / (2*pi). Front-axle displacement is Q12, the direction
  // is Q8. This keeps the rear axle's velocity along the body, without slip.
  return (direction.x * dy - direction.y * dx) * 10430 / (wheelbase * 256);
}

static void FollowRoadCarPath(RoadCar *car, int progress) {
  progress = IntClamp(progress, 0, ROAD_CAR_PROGRESS_MAX);
  int wheelbase = kRoadCarFrontAxles[car->model] + kRoadCarRearAxles[car->model];
  RoadCarPath before = SampleRoadCarPath(car);
  while (car->progress < progress) {
    car->progress = IntMin(progress, car->progress + 16);
    RoadCarPath after = SampleRoadCarPath(car);
    int dx = after.x - before.x, dy = after.y - before.y;
    int half_turn = RoadCarHeadingChange(car->heading, dx, dy, wheelbase) / 2;
    car->heading += RoadCarHeadingChange(car->heading + half_turn, dx, dy, wheelbase);
    before = after;
  }
  UpdateRoadCarSteering(car);
}

static bool RoadCarStraightRoute(const City *city, TileIndex tile, int entry);

static void PrepareRoadCar(const City *city, RoadCar *car, int entry, int exit) {
  PrepareRoadCarPath(city, car, entry, exit);
  PrepareRoadCarSurface(city, car, entry);
  car->speed = 83 - (ReadTrafficDensity(city, car->tile) * 33 + 128) / 256;
  TileIndex next = RoadCarNeighbor(city, car->tile, exit);
  car->junction_ahead = __builtin_popcount((unsigned)RoadCarRouteExits(city, next, (exit + 2) & 3)) > 2;
  car->ahead = car->behind = 0;
  if (car->straight) {
    for (int side = 0; side < 2; ++side) {
      TileIndex tile = car->tile;
      int count = 0;
      while (count < 3) {
        tile = RoadCarNeighbor(city, tile, side ? entry : exit);
        if (!RoadCarStraightRoute(city, tile, entry)) break;
        ++count;
      }
      if (side) car->behind = count;
      else car->ahead = count;
    }
  }
  UpdateRoadCarSteering(car);
}

// Road edits refresh the entry cache, including while paused. Ordinary car
// movement never resolves its current tile's connections or surface again.
void RefreshCityRoadTrafficPoses(City *city) {
  Drawing *drawing = city->game->drawing;
  DrawingRoadTraffic *traffic = &drawing->road_traffic;
  for (int slot = 0; slot < CITY_ROAD_CAR_COUNT; ++slot) {
    RoadCar *car = &traffic->cars[slot];
    if (!car->path) continue;
    RoadCar previous = *car;
    int entry = RoadCarEntry(car), exit = RoadCarExit(car);
    if (!(RoadCarRouteExits(city, car->tile, entry) & (1 << exit))) {
      car->path = 0;
      RemoveRoadCarBucket(traffic, car, car->tile);
    } else {
      PrepareRoadCar(city, car, entry, exit);
      car->reserved = false;
    }
    CityPoint before = RoadCarDrawingPoint(&previous, 0, RoadCarSpriteView(&previous, 0));
    CityPoint after = car->path ? RoadCarDrawingPoint(car, 0, RoadCarSpriteView(car, 0)) : before;
    if (drawing->tile_damage_enabled && (!car->path || before.x != after.x || before.y != after.y ||
        car->heading != previous.heading || car->steering != previous.steering ||
        RoadCarHeight(car) != RoadCarHeight(&previous)))
      PublishCityRoadCarDamage(city, &previous, car);
  }
}

static bool RoadCarBlocked(
    const City *city, const DrawingRoadTraffic *traffic, const RoadCar *car, const RoadCar *candidate) {
  int height = RoadCarHeight(candidate);
  CityPoint point = RoadCarBodyPoint(candidate);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      TileIndex tile = GetRelativeTileOrInvalid(city, candidate->tile, dx, dy);
      if (tile == INVALID_TILE) continue;
      for (int link = traffic->buckets[RoadCarBucket(tile)]; link; link = traffic->cars[link - 1].next) {
        const RoadCar *other = &traffic->cars[link - 1];
        if (other == car || !other->path || other->tile != tile) continue;
        if (abs(height - RoadCarHeight(other)) >= 230) continue;
        CityPoint other_point = RoadCarBodyPoint(other);
        int x = dx * 256 + other_point.x - point.x;
        int y = dy * 256 + other_point.y - point.y;
        if (RoadCarFootprintsOverlap(candidate, other, x, y)) return true;
      }
    }
  }
  return false;
}

enum { ROAD_CAR_DENSITY_SCALE = 256 };
static const int kRoadCarDensityGrace = 3 * ROAD_CAR_AGE_SCALE;
static const int kRoadCarFadeAge = 154; // Rounded up from 0.6 seconds.
static const int kRoadCarStopLine = 1331;
static const int kRoadCarSpawnLookAhead = 307;

static int RoadCarFadeAge(const DrawingRoadTraffic *traffic, const RoadCar *car) {
  return (traffic->age_clock - car->age) & ROAD_CAR_AGE_MAX;
}

// Match the seeding rate: density/256 cars per incoming lane. A bounded
// neighbourhood smooths individual-tile rounding without mixing bridge levels.
// Fading cars already count as departing, so an entire queue cannot be retired
// for the same excess. The candidate is absent from the buckets during admission.
static int RoadCarPopulationExcess(const City *city, const DrawingRoadTraffic *traffic, const RoadCar *car) {
  int height = RoadCarHeight(car);
  int population = 0, expected = 0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      TileIndex tile = GetRelativeTileOrInvalid(city, car->tile, dx, dy);
      if (tile == INVALID_TILE) continue;
      int connections = RoadCarConnections(city, tile);
      if (!connections) continue;
      int lanes = 0;
      bool bridge = IsTileKindBridge(city->tile[tile].kind);
      int ground_height = bridge ? 0 : ReadCitySurfaceHeight(&city->tile[tile]) * 256;
      for (int side = 0; side < 4; ++side) {
        if (!(connections & (1 << side))) continue;
        int lane_height = ground_height;
        if (bridge) {
          RoadCar surface = {.tile = tile};
          PrepareRoadCarSurface(city, &surface, side);
          lane_height = RoadCarHeightAt(&surface, 128, 128);
        }
        if (abs(height - lane_height) < 230) ++lanes;
      }
      expected += ReadTrafficDensity(city, tile) * lanes;
      for (int link = traffic->buckets[RoadCarBucket(tile)]; link; link = traffic->cars[link - 1].next) {
        const RoadCar *other = &traffic->cars[link - 1];
        if (!other->path || other->tile != tile || other->fading) continue;
        if (abs(height - RoadCarHeight(other)) < 230) ++population;
      }
    }
  return population - (expected + ROAD_CAR_DENSITY_SCALE - 1) / ROAD_CAR_DENSITY_SCALE;
}

// An approaching car reserves the whole junction. The exit is chosen only
// when it can enter; the current path already identifies both its own ports.
static bool RoadCarJunctionBusy(
    const City *city, const DrawingRoadTraffic *traffic, TileIndex junction, const RoadCar *car) {
  if (junction == INVALID_TILE) return false;
  int height = car ? RoadCarHeight(car) : 0;
  for (int side = -1; side < 4; ++side) {
    TileIndex tile = side < 0 ? junction : RoadCarNeighbor(city, junction, side);
    if (tile == INVALID_TILE) continue;
    for (int link = traffic->buckets[RoadCarBucket(tile)]; link; link = traffic->cars[link - 1].next) {
      const RoadCar *other = &traffic->cars[link - 1];
      if (other == car || !other->path || other->tile != tile) continue;
      if (car && abs(height - RoadCarHeight(other)) >= 230) continue;
      if (side < 0 || (other->reserved && RoadCarNeighbor(city, tile, RoadCarExit(other)) == junction))
        return true;
    }
  }
  return false;
}

static int RoadCarContinuation(int exits, int entry) {
  exits &= ~(1 << entry);
  return exits ? __builtin_ctz((unsigned)exits) : entry;
}

static bool RoadCarJunctionClear(
    const City *city, const DrawingRoadTraffic *traffic, const RoadCar *car, const RoadCar *prepared) {
  TileIndex junction = prepared->tile;
  if (RoadCarJunctionBusy(city, traffic, junction, car)) return false;
  RoadCar candidate = *prepared;
  for (int sample = 0; sample <= 4; ++sample) {
    FollowRoadCarPath(&candidate, 512 * sample);
    if (RoadCarBlocked(city, traffic, car, &candidate)) return false;
  }
  int exit = RoadCarExit(prepared), incoming = (exit + 2) & 3;
  TileIndex landing = RoadCarNeighbor(city, junction, exit);
  int exits = RoadCarRouteExits(city, landing, incoming);
  if (!exits || (__builtin_popcount((unsigned)exits) > 2 && RoadCarJunctionBusy(city, traffic, landing, car)))
    return false;
  candidate.tile = landing;
  candidate.progress = 0;
  PrepareRoadCar(city, &candidate, incoming, RoadCarContinuation(exits, incoming));
  for (int sample = 0; sample <= 3; ++sample) {
    FollowRoadCarPath(&candidate, 451 * sample);
    if (RoadCarBlocked(city, traffic, car, &candidate)) return false;
  }
  return true;
}

static bool RoadCarStraightRoute(const City *city, TileIndex tile, int entry) {
  if (tile == INVALID_TILE) return false;
  int axis = entry & 1 ? CONN_NS : CONN_EW;
  if (RoadCarRouteExits(city, tile, entry) != axis) return false;
  if (ReadTile(city, tile) != TK_ROAD) return true;
  int artwork = ResolveCitySurfaceLineGround(city, tile).layout;
  // A straight connection tile can contain half of a prepared diagonal bend.
  return CitySurfaceLineSprite(city, tile, artwork, CITY_MAP_ZOOM_LARGE, 0) < MAP_LANDSCAPE_SPRITE_FIRST;
}

static int RoadCarLeaderDistance(const City *city, const RoadCar *car, TileIndex tile) {
  const DrawingRoadTraffic *traffic = &city->game->drawing->road_traffic;
  CityPoint start = MapTilePt(city, car->tile);
  int entry = RoadCarEntry(car), exit = RoadCarExit(car), distance = 5 * 256;
  int height = RoadCarHeight(car);
  for (int link = traffic->buckets[RoadCarBucket(tile)]; link; link = traffic->cars[link - 1].next) {
    const RoadCar *other = &traffic->cars[link - 1];
    if (other == car || !other->path || other->tile != tile || RoadCarEntry(other) != entry ||
        abs(height - RoadCarHeight(other)) >= 230) continue;
    CityPoint route = MapTilePt(city, other->tile);
    int offset = (route.x - start.x) * kRoadCarX[exit] + (route.y - start.y) * kRoadCarY[exit];
    int gap = offset * 256 + ((int)other->progress - (int)car->progress) / 8;
    if (gap >= 0) distance = IntMin(distance, gap - 154);
  }
  return distance;
}

static int RoadCarSpeed(const City *city, const RoadCar *car) {
  int speed = car->speed;
  if (car->model != CITY_ROAD_VEHICLE_SPORTS_COUPE || !car->straight) return speed;
  int behind = car->progress / 8 + car->behind * 256;
  int ahead = 256 - car->progress / 8 + car->ahead * 256;
  int leader = RoadCarLeaderDistance(city, car, car->tile);
  TileIndex tile = car->tile;
  int exit = RoadCarExit(car);
  for (int distance = 0; distance < car->ahead; ++distance) {
    tile = RoadCarNeighbor(city, tile, exit);
    leader = IntMin(leader, RoadCarLeaderDistance(city, car, tile));
  }
  int run = IntClamp(IntMin(behind, IntMin(ahead - 192, leader)) * 2 / 5, 0, 256);
  int age = IntClamp(((int)car->age - 128) * 2 / 3, 0, 256);
  int ease = (run * run * (768 - run * 2) + 32768) >> 16;
  int boost = ((205 * ease >> 8) * age) >> 8;
  return (speed * (256 + boost) + 128) >> 8;
}

static void AdvanceRoadCar(City *city, DrawingRoadTraffic *traffic, RoadCar *car, u32 step) {
  int progress = car->progress, length = kRoadCarPathLengths[car->path - 1];
  // Q8 time times Q7 speed, scaled to the eleven-bit path cursor.
  u32 distance = step * RoadCarSpeed(city, car) * 16;
  int phase = progress + distance / length;
  if (phase > kRoadCarStopLine && IsCityDrawbridgeOpen(city, RoadCarNeighbor(city, car->tile, RoadCarExit(car))))
    phase = IntMax(progress, kRoadCarStopLine);
  if (car->junction_ahead && phase > kRoadCarStopLine && !car->reserved) {
    TileIndex junction = RoadCarNeighbor(city, car->tile, RoadCarExit(car));
    car->reserved = !RoadCarJunctionBusy(city, traffic, junction, car);
    if (!car->reserved) phase = IntMax(progress, kRoadCarStopLine);
  }
  RoadCar candidate = *car;
  if (phase >= ROAD_CAR_PROGRESS_ONE) {
    int out = RoadCarExit(car), entry = (out + 2) & 3;
    TileIndex next = RoadCarNeighbor(city, car->tile, out);
    int exits = RoadCarRouteExits(city, next, entry);
    int exit = ChooseRoadCarExit(traffic, exits, entry);
    if (exit < 0) {
      car->path = 0;
      return;
    }
    FollowRoadCarPath(&candidate, ROAD_CAR_PROGRESS_MAX);
    candidate.tile = next;
    candidate.reserved = false;
    candidate.progress = 0;
    PrepareRoadCar(city, &candidate, entry, exit);
    if (car->junction_ahead && !RoadCarJunctionClear(city, traffic, car, &candidate)) {
      bool clear = false;
      for (int side = 0; side < 4; ++side) {
        if (side == entry || side == exit || !(exits & (1 << side))) continue;
        PrepareRoadCar(city, &candidate, entry, side);
        if (RoadCarJunctionClear(city, traffic, car, &candidate)) {
          clear = true;
          break;
        }
      }
      if (!clear) return;
    }
    int next_length = kRoadCarPathLengths[candidate.path - 1];
    FollowRoadCarPath(&candidate, (distance - (ROAD_CAR_PROGRESS_ONE - progress) * length) / next_length);
  } else {
    FollowRoadCarPath(&candidate, phase);
  }
  if (!RoadCarBlocked(city, traffic, car, &candidate)) *car = candidate;
}

static bool SpawnRoadCar(City *city, TileIndex tile, int entry, int phase) {
  if (IsCityDrawbridgeOpen(city, tile)) return false;
  Drawing *drawing = city->game->drawing;
  DrawingRoadTraffic *traffic = &drawing->road_traffic;
  int exits = RoadCarRouteExits(city, tile, entry);
  // Never materialize in a junction or in the exit space reserved by an approaching car.
  if (__builtin_popcount((unsigned)exits) != 2 || !(exits & (1 << entry))) return false;
  TileIndex behind = RoadCarNeighbor(city, tile, entry);
  if (__builtin_popcount((unsigned)RoadCarRouteExits(city, behind, entry)) > 2 &&
      RoadCarJunctionBusy(city, traffic, behind, NULL)) return false;
  int exit = RoadCarContinuation(exits, entry);
  TileIndex next = RoadCarNeighbor(city, tile, exit);
  if (__builtin_popcount((unsigned)RoadCarRouteExits(city, next, (exit + 2) & 3)) > 2)
    phase = IntMin(phase, kRoadCarStopLine);
  RoadCar candidate = {.tile = tile};
  candidate.model = (RoadTrafficRandom(traffic) >> 16) % CITY_ROAD_VEHICLE_TYPES;
  candidate.palette = (RoadTrafficRandom(traffic) >> 16) % CITY_ROAD_VEHICLE_PALETTES;
  PrepareRoadCar(city, &candidate, entry, exit);
  candidate.heading = SampleRoadCarPath(&candidate).pose * 256;
  FollowRoadCarPath(&candidate, phase);
  if (RoadCarBlocked(city, traffic, NULL, &candidate)) return false;
  RoadCar space = candidate;
  // The footprint already includes a bumper gap. Add a little forward space
  // for admission without requiring another whole car length of empty road.
  FollowRoadCarPath(&space, phase + kRoadCarSpawnLookAhead);
  if (RoadCarBlocked(city, traffic, NULL, &space)) return false;
  if (RoadCarPopulationExcess(city, traffic, &candidate) >= 0) return false;
  for (int i = 0; i < CITY_ROAD_CAR_COUNT; ++i) {
    RoadCar *car = &traffic->cars[i];
    if (car->path) continue;
    *car = candidate;
    InsertRoadCarBucket(traffic, car);
    if (drawing->tile_damage_enabled) PublishCityRoadCarDamage(city, NULL, car);
    return true;
  }
  return false;
}

static CityMapCamera RoadCarCurrentCamera(const Drawing *drawing, int index) {
  if (index) {
    const CityExtraMapView *view = &drawing->views[index - 1];
    return (CityMapCamera){view->origin_x, view->origin_y, IntMin(view->zoom, CITY_MAP_ZOOM_LARGE), view->width,
        view->height, view->rotation};
  }
  int magnification = drawing->default_map_view->magnified_large_zoom ? 2 : 1;
  return (CityMapCamera){drawing->map_state.origin_x, drawing->map_state.origin_y, drawing->default_map_view->zoom,
      drawing->map_width / magnification, drawing->map_height / magnification,
      CityMapViewRotation(drawing->default_map_view)};
}

static bool RoadCarWatchedCurrent(const City *city, const RoadCar *car) {
  const Drawing *drawing = city->game->drawing;
  for (int i = 0; i <= CITY_EXTRA_MAP_VIEW_COUNT; ++i) {
    if (i && !drawing->views[i - 1].window_id) continue;
    if (RoadCarInView(city, car->tile, RoadCarHeight(car), RoadCarCurrentCamera(drawing, i), i)) return true;
  }
  return false;
}

static bool RoadCarVisibleCurrent(const City *city, const RoadCar *car) {
  const Drawing *drawing = city->game->drawing;
  int height = RoadCarHeight(car);
  for (int i = 0; i <= CITY_EXTRA_MAP_VIEW_COUNT; ++i) {
    if (i && !drawing->views[i - 1].window_id) continue;
    CityMapCamera view = RoadCarCurrentCamera(drawing, i);
    if (!RoadCarInView(city, car->tile, height, view, i)) continue;
    CityPoint tile = CityMapCameraPoint(city, view.rotation, MapTileX(city, car->tile), MapTileY(city, car->tile));
    CityPoint body = RoadCarDrawingPoint(car, view.rotation, RoadCarSpriteView(car, view.rotation));
    CityPoint point = CityMapCameraVector(view.rotation, body.x, body.y);
    int scale = 1 << view.zoom;
    int x = view.origin_x + (tile.y - tile.x + 1) * 4 * scale + (point.y - point.x) * scale / 1024;
    int base_y = view.origin_y + (tile.x + tile.y - 1) * 2 * scale + (point.x + point.y) * scale / 2048;
    int y = base_y - height * CityMapHeightStep(view.zoom) / 256;
    int margin = 8 * scale;
    if (x >= -margin && y >= -margin && x < view.width + margin && y < view.height + margin) return true;
  }
  return false;
}

static void SeedRoadCarRect(City *city, CityMapCamera view, int index, CityRect rect) {
  if (rect.left >= rect.right || rect.top >= rect.bottom) return;
  DrawingRoadTraffic *traffic = &city->game->drawing->road_traffic;
  CityRect bounds = VisibleCityMapTileBounds(city, view.origin_x, view.origin_y, view.zoom, view.rotation, rect);
  int scale = 1 << view.zoom;
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      int density = ReadTrafficDensity(city, tile);
      if (!density || !RoadCarConnections(city, tile)) continue;
      CityPoint point = CityMapCameraPoint(city, view.rotation, x, y);
      int px = view.origin_x + (point.y - point.x + 1) * 4 * scale;
      int surface_height = ReadCitySurfaceHeight(&city->tile[tile]);
      int py = view.origin_y + (point.x + point.y) * 2 * scale - surface_height * CityMapHeightStep(view.zoom);
      if (px < rect.left || px >= rect.right || py < rect.top || py >= rect.bottom ||
          !RoadCarInView(city, tile, surface_height * 256, view, index))
        continue;
      int exits = RoadCarExits(city, tile);
      for (int entry = 0; entry < 4; ++entry) {
        if (!(exits & (1 << entry)) || (int)(RoadTrafficRandom(traffic) % ROAD_CAR_DENSITY_SCALE) >= density) continue;
        SpawnRoadCar(city, tile, entry, RoadTrafficRandom(traffic) & ROAD_CAR_PROGRESS_MAX);
      }
    }
}

// Camera history already belongs to the map snapshot. Seed only the newly
// exposed strips and the viewing margin; no visited-map mask or camera copy is needed.
void PopulateCityRoadTrafficView(City *city, int index, CityMapCamera previous, CityMapCamera view) {
  Drawing *drawing = city->game->drawing;
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || !drawing->map_state.tile_details_enabled ||
      !drawing->map_state.vehicles_enabled || drawing->map_state.events_suppressed ||
      (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE) ||
      view.zoom < CITY_MAP_ZOOM_LARGE || view.width <= 0 || view.height <= 0 ||
      memcmp(&previous, &view, sizeof(view)) == 0)
    return;
  DrawingRoadTraffic *traffic = &drawing->road_traffic;
  if (traffic->map != city->tile) *traffic = (DrawingRoadTraffic){.map = city->tile, .random = 0x63A9D51B};
  for (int i = 0; i < CITY_ROAD_CAR_COUNT; ++i) {
    RoadCar *car = &traffic->cars[i];
    if (!car->path || RoadCarWatchedCurrent(city, car)) continue;
    RoadCar previous = *car;
    car->path = 0;
    RemoveRoadCarBucket(traffic, car, car->tile);
    if (drawing->tile_damage_enabled) PublishCityRoadCarDamage(city, &previous, NULL);
  }
  int margin = 64 << view.zoom;
  CityRect area = {-margin, -margin, view.width + margin, view.height + margin};
  if (previous.zoom != view.zoom || previous.rotation != view.rotation || previous.width <= 0 || previous.height <= 0) {
    SeedRoadCarRect(city, view, index, area);
    return;
  }
  int dx = view.origin_x - previous.origin_x, dy = view.origin_y - previous.origin_y;
  CityRect overlap = {IntMax(area.left, dx - margin), IntMax(area.top, dy - margin),
      IntMin(area.right, dx + previous.width + margin), IntMin(area.bottom, dy + previous.height + margin)};
  if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) {
    SeedRoadCarRect(city, view, index, area);
    return;
  }
  SeedRoadCarRect(city, view, index, (CityRect){area.left, area.top, area.right, overlap.top});
  SeedRoadCarRect(city, view, index, (CityRect){area.left, overlap.bottom, area.right, area.bottom});
  SeedRoadCarRect(city, view, index, (CityRect){area.left, overlap.top, overlap.left, overlap.bottom});
  SeedRoadCarRect(city, view, index, (CityRect){overlap.right, overlap.top, area.right, overlap.bottom});
}

bool UpdateCityRoadTraffic(City *city, u32 now) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->map_state.vehicles_enabled) return false;
  DrawingRoadTraffic *traffic = &drawing->road_traffic;
  if (traffic->map != city->tile || (i32)(now - traffic->time) < 0 || !drawing->map_snapshot.valid) {
    *traffic = (DrawingRoadTraffic){.map = city->tile, .time = now, .next_spawn = now, .random = 0x63A9D51B};
  }
  u32 elapsed = now - traffic->time;
  bool check_population = (now >> 8) != (traffic->time >> 8);
  traffic->time = now;
  traffic->enabled = (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) != 0;
  if (!traffic->enabled) return false;
  if (drawing->map_state.events_suppressed || !drawing->map_state.tile_details_enabled ||
      (drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE))
    return false;
  if (!elapsed || city->settings.speed <= CITY_SPEED_PAUSED || !drawing->map_snapshot.valid) return false;
  u32 step = elapsed < 26 ? elapsed : 26;
  // Slot indices spread lifetime writes over sixteen updates. Each phase retains
  // elapsed presentation time, so variable frame times do not change lifetimes.
  traffic->age_clock += step;
  int age_phase = traffic->age_phase;
  int age_delta = (u16)(traffic->age_clock - traffic->age_updated[age_phase]);
  traffic->age_updated[age_phase] = traffic->age_clock;
  traffic->age_phase = (age_phase + 1) & (CITY_ROAD_CAR_AGE_PHASES - 1);
  for (int i = age_phase; i < CITY_ROAD_CAR_COUNT; i += CITY_ROAD_CAR_AGE_PHASES) {
    RoadCar *car = &traffic->cars[i];
    if (car->path && !car->fading) car->age = IntMin(ROAD_CAR_AGE_MAX, car->age + age_delta);
  }
  bool changed = false;
  for (int i = 0; i < CITY_ROAD_CAR_COUNT; ++i) {
    RoadCar *car = &traffic->cars[i];
    if (!car->path) continue;
    RoadCar previous = *car;
    TileIndex before = car->tile;
    u16 previous_progress = car->progress;
    bool fading = car->fading;
    if (!RoadCarWatched(city, car) ||
        (!fading && car->age > 120 * ROAD_CAR_AGE_SCALE && !RoadCarVisibleCurrent(city, car))) {
      car->path = 0;
    } else if (fading) {
      if (RoadCarFadeAge(traffic, car) >= kRoadCarFadeAge) car->path = 0;
    } else {
      AdvanceRoadCar(city, traffic, car, step);
    }
    if (!car->path || car->tile != before) {
      RemoveRoadCarBucket(traffic, car, before);
      if (car->path) InsertRoadCarBucket(traffic, car);
    }
    // Count the updated front-axle tile, including creeping queues.
    if (car->path && !fading && car->age >= kRoadCarDensityGrace && check_population &&
        RoadCarPopulationExcess(city, traffic, car) > 0) {
      car->fading = true;
      car->age = traffic->age_clock & ROAD_CAR_AGE_MAX;
    }
    // A car can extend beyond its front-axle tile. Restore its full previous
    // footprint as well as the new one, including when fading in place.
    if (!car->path || car->tile != before || car->progress != previous_progress || fading || car->fading) {
      if (drawing->tile_damage_enabled) PublishCityRoadCarDamage(city, &previous, car);
      changed = true;
    }
  }
  if ((i32)(now - traffic->next_spawn) >= 0) {
    traffic->next_spawn = now + 10;
    int available = 0;
    for (int slot = 0; slot < CITY_ROAD_CAR_COUNT; ++slot)
      available += !traffic->cars[slot].path;
    for (int attempt = 0; attempt < 48 && available; ++attempt) {
      int view_index = RoadTrafficRandom(traffic) % (CITY_EXTRA_MAP_VIEW_COUNT + 1);
      if (view_index && !drawing->views[view_index - 1].window_id) view_index = 0;
      CityMapCamera view = drawing->map_snapshot.regions[view_index];
      if (view.zoom < CITY_MAP_ZOOM_LARGE || view.width <= 0 || view.height <= 0) continue;
      TileIndex tile = SampleRoadCarTile(city, view);
      int surface_height = ReadCitySurfaceHeight(&city->tile[tile]);
      if (!RoadCarInView(city, tile, surface_height * 256, view, view_index)) continue;
      int density = ReadTrafficDensity(city, tile);
      if (!density || (int)(RoadTrafficRandom(traffic) % ROAD_CAR_DENSITY_SCALE) >= density) continue;
      // The initial visible population is seeded when a camera changes.
      // Ongoing arrivals come from its margin rather than popping into view.
      CityPoint point = CityMapCameraPoint(city, view.rotation, MapTileX(city, tile), MapTileY(city, tile));
      int scale = 1 << view.zoom;
      int px = view.origin_x + (point.y - point.x + 1) * 4 * scale;
      int py = view.origin_y + (point.x + point.y) * 2 * scale - surface_height * CityMapHeightStep(view.zoom);
      if (px >= 0 && py >= 0 && px < view.width && py < view.height) continue;
      if (SpawnRoadCar(city, tile, RoadTrafficRandom(traffic) & 3, 0)) {
        --available;
        changed = true;
      }
    }
  }
  return changed;
}

static CityPoint RoadCarSpriteOffset(CityPoint point, int height, int zoom) {
  int scale = 1 << zoom;
  return (CityPoint){
      (point.y - point.x) * scale / 4 + (1024 - CITY_ROAD_VEHICLE_GROUND_X * 16) * scale,
      (point.x + point.y) * scale / 8 - height * CityMapHeightStep(zoom)};
}

CityRect CityRoadCarPixelBounds(const City *city, const RoadCar *car, CityMapCamera view, int pixel_scale) {
  if (!car || !car->path || !IsMapTileValid(city, car->tile) || view.zoom < CITY_MAP_ZOOM_LARGE)
    return (CityRect){0};
  const MapSpriteCache *cache = &city->game->drawing->sprite_cache;
  int slot = FindMapSpriteScale(cache, pixel_scale * 100);
  if (slot < 0) return (CityRect){0};
  int heading = RoadCarSpriteView(car, view.rotation);
  int id = MAP_LANDSCAPE_SPRITE_FIRST + view.zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
      CITY_LANDSCAPE_ROAD_VEHICLE_FIRST - CITY_LANDSCAPE_GROUND_FIRST + RoadCarSprite(car, heading);
  const RunSprite *sprite = cache->sprites[slot][id];
  if (!sprite || !sprite->stream_size) return (CityRect){0};
  CityPoint tile = CityMapCameraPoint(city, view.rotation, MapTileX(city, car->tile), MapTileY(city, car->tile));
  CityPoint body = RoadCarDrawingPoint(car, view.rotation, heading);
  CityPoint point = CityMapCameraVector(view.rotation, body.x, body.y);
  CityPoint offset = RoadCarSpriteOffset(point, RoadCarHeight(car), view.zoom);
  int scale = 1 << view.zoom;
  i64 x_q8 = (i64)(view.origin_x + (tile.y - tile.x) * 4 * scale) * 256 + offset.x;
  i64 y_q8 = (i64)(view.origin_y + (tile.x + tile.y - 1) * 2 * scale) * 256 + offset.y;
  // Match DrawCachedMapSprite's fractional positioning and prepared anchors.
  int x = x_q8 * pixel_scale / 256;
  int y = y_q8 * pixel_scale / 256 - cache->baselines[slot][id];
  int offset_x = cache->offsets_x[slot][id];
  if (offset_x) x += sprite->mirrored ? (8 << view.zoom) * pixel_scale - sprite->width - offset_x : offset_x;
  return (CityRect){x, y, x + sprite->width, y + sprite->height};
}

void DrawCityRoadCar(City *city, int slot, int zoom) {
  const RoadCar *car = &city->game->drawing->road_traffic.cars[slot];
  // A car already on the leaf when a ship approaches evacuates through its cached path without painting on air.
  if (IsCityDrawbridgeOpen(city, car->tile)) return;
  Drawing *drawing = city->game->drawing;
  TileIndex tile = car->tile;
  CityPoint position = CityMapViewTilePoint(city, tile);
  int scale = 1 << zoom;
  int screen_x = drawing->map_state.origin_x + (position.y - position.x) * 4 * scale;
  int ground_y = drawing->map_state.origin_y + (position.x + position.y) * 2 * scale;
  int height = RoadCarHeight(car);
  int rotation = CityMapCameraRotation(drawing);
  int view = RoadCarSpriteView(car, rotation);
  int sprite = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM + CITY_LANDSCAPE_ROAD_VEHICLE_FIRST -
      CITY_LANDSCAPE_GROUND_FIRST + RoadCarSprite(car, view);
  // The Blender origin is the wheel footprint centre, registered at (32, 48)
  // in every 64px canvas. No silhouette-dependent vertical correction is needed.
  CityPoint body = RoadCarDrawingPoint(car, rotation, view);
  CityPoint point = CityMapCameraVector(rotation, body.x, body.y);
  CityPoint saved = drawing->sprite_offset;
  CityPoint offset = RoadCarSpriteOffset(point, height, zoom);
  drawing->sprite_offset.x += offset.x;
  drawing->sprite_offset.y += offset.y;
  if (drawing->map_depth.preparing) {
    int depth = CityMapObjectDepth(position.x * 4096 + point.x, position.y * 4096 + point.y, height * 16) + 1;
    const TileInfo *tp = &city->tile[tile];
    if (IsTileKindBridge(tp->kind) && !IsTileKindHighwayBridge(tp->kind)) {
      if ((RoadCarEntry(car) & 1) != ReadCityBridgeAxis(tp))
        depth = IntMin(depth, CityMapBridgeArtworkDepth(city, tile, false) - 1024);
      else if (IsCityBridgeHead(tp))
        depth = IntMin(depth, CityMapBridgeArtworkDepth(city, tile, true) - 1024);
    }
    drawing->map_depth.depth = depth;
  }
  int alpha = car->fading ?
      IntClamp(255 - RoadCarFadeAge(&drawing->road_traffic, car) * 255 / kRoadCarFadeAge, 0, 255) : 255;
  u32 flags = SPRITE_OPAQUE_PALETTE | (u32)(255 - alpha) << SPRITE_TRANSPARENCY_SHIFT;
  DrawCityMapPaletteSpriteAtBaseline(drawing, sprite, screen_x, ground_y - 2 * scale,
      flags, MAP_PALETTE_CAR_FIRST + car->palette);
  drawing->sprite_offset = saved;
}

void CollectCityRoadTraffic(City *city, int zoom) {
  Drawing *drawing = city->game->drawing;
  if (zoom < CITY_MAP_ZOOM_LARGE || !drawing->map_state.tile_details_enabled || !drawing->map_state.vehicles_enabled ||
      drawing->map_state.events_suppressed || !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)))
    return;
  drawing->map_depth.phase = MAP_DEPTH_OBJECTS;
  for (int i = 0; i < CITY_ROAD_CAR_COUNT; ++i)
    if (drawing->road_traffic.cars[i].path) DrawCityRoadCar(city, i, zoom);
}
