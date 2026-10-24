#include "city/events/events.h"
#include "drawing/maps/map_vehicles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "assets/graphics.h"
#include "city/engine/clock.h"
#include "city/engine/city_types.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "drawing/maps/map_depth.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/map_sprites.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "rail-paths.inc"

// Vehicle interpolation is Drawing presentation state. City supplies canonical event positions and routes;
// the unpaused host clock samples their motion without advancing City events or consuming simulation RNG.

// Keep allocated interpolation storage reusable, but discard every visual vehicle.
void ClearCityVehicleVisualization(Drawing *drawing) {
  drawing->road_traffic = (DrawingRoadTraffic){0};
  DrawingVehicles *vehicles = &drawing->vehicles;
  if (vehicles->slots) memset(vehicles->slots, 0, vehicles->capacity * sizeof(*vehicles->slots));
  *vehicles = (DrawingVehicles){.slots = vehicles->slots, .capacity = vehicles->capacity, .enabled = vehicles->enabled};
}

static VehiclePosition3D TrainTilePosition(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  const TileInfo *tp = &city->tile[tile];
  int height;
  if (IsTileKindRailBridge(tp->kind)) {
    int axis = ReadCityBridgeAxis(tp);
    int first = CityBridgeTransportEdgeHeight(city, tile, axis ? CITY_LINE_NORTH : CITY_LINE_WEST);
    int last = CityBridgeTransportEdgeHeight(city, tile, axis ? CITY_LINE_SOUTH : CITY_LINE_EAST);
    height = (first + last) * 128;
  } else {
    CityMapSurface surface = ResolveCitySurfaceLineGround(city, tile);
    static const u8 kRaisedCorners[] = {0, 2, 2, 2, 2, 3, 3, 3, 3, 1, 1, 1, 1, 4};
    height = (ReadCityLandHeight(tp) + surface.raised) * 256 + kRaisedCorners[surface.shape] * 64;
  }
  return (VehiclePosition3D){4096 * point.x, 4096 * point.y, CityMapHeightStep(CITY_MAP_ZOOM_LARGE) * height};
}

// Interpolation is world-space presentation state shared by every camera.
static VehiclePosition3D VehiclePosition(const City *city, const CityMapEvent *event) {
  if (IsCityMapEventTrain(event->type)) return TrainTilePosition(city, event->tile);
  CityPoint position = ReadCityMapEventPosition(city, event);
  TileIndex tile = event->tile;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  int x = 4096 * position.x, y = 4096 * position.y;
  int z = CityMapHeightStep(CITY_MAP_ZOOM_LARGE) * elevation;
  CityPoint offset = ReadCityMapEventTarget(city, event);
  x += CITY_EVENT_OFFSET_SCALE * offset.x;
  y += CITY_EVENT_OFFSET_SCALE * offset.y;
  z += 8 * event->age;
  return (VehiclePosition3D){x, y, z * 256};
}

static u8 TrainPathForEdges(const City *city, TileIndex tile, int entry, int exit) {
  CitySurfaceLinePath path = ResolveCitySurfaceLinePath(city, tile, entry, exit, true);
  int descriptor = kRailPathKinds[path.kind];
  return (descriptor & 31) | (((path.rotation + (descriptor >> 5)) & 3) << 5);
}

// Cache the artwork-derived rail centreline after a City tile move. Positions stay within that tile; adjacent
// recipes meet at the same boundary point and tangent, including diagonal end pieces.
static u8 PrepareTrainPath(const City *city, const CityMapEvent *event, const VehicleMotion *motion) {
  CityPoint point = MapTilePt(city, event->tile);
  CityPoint next = point;
  if ((event->coordinate_flags & CITY_EVENT_TARGET_TILE) && IsMapTileValid(city, event->train.next_tile))
    next = MapTilePt(city, event->train.next_tile);
  int dx = next.x - point.x, dy = next.y - point.y;
  int exit = dx < 0 ? CITY_LINE_WEST : dx > 0 ? CITY_LINE_EAST : dy > 0 ? CITY_LINE_SOUTH :
      dy < 0 ? CITY_LINE_NORTH : event->direction & 3;
  dx = motion->from.x - motion->target.x;
  dy = motion->from.y - motion->target.y;
  // A fresh drawing history can still infer its entry edge from the following linked coach.
  int trailing = event->next_slot;
  if (!dx && !dy && trailing >= CITY_EVENT_SLOT_FIRST && trailing < city->events.capacity) {
    const CityMapEvent *coach = &city->events.event[trailing];
    if ((coach->type == CITY_MAP_EVENT_TRAIN_CAR || coach->type == CITY_MAP_EVENT_SUBWAY_CAR) &&
        IsMapTileValid(city, coach->tile)) {
      CityPoint previous = MapTilePt(city, coach->tile);
      dx = previous.x - point.x;
      dy = previous.y - point.y;
    }
  }
  int entry = dx < 0 ? CITY_LINE_WEST : dx > 0 ? CITY_LINE_EAST : dy > 0 ? CITY_LINE_SOUTH :
      dy < 0 ? CITY_LINE_NORTH : (exit + 2) & 3;
  return TrainPathForEdges(city, event->tile, entry, exit);
}

typedef struct {
  VehiclePosition3D position;
  int heading;
} TrainPathSample;

// Position and tangent heading describe the same point on the selected cached path.
static TrainPathSample SampleTrainPath(const City *city, const CityMapEvent *event,
    const VehicleMotion *motion, double fraction, bool previous) {
  VehiclePosition3D target = previous ? motion->from : motion->target;
  int path = previous ? motion->train_previous_path : motion->train_path;
  int progress = IntClamp((int)(fraction * 2048), 0, 2047);
  const u32 *points = kRailPathPoints[(path & 31) - 1];
  int step = progress >> 6, blend = progress & 63;
  u32 a = points[step], b = points[step + 1];
  int x = ((int)(a & 511) * (64 - blend) + (int)(b & 511) * blend + 2) / 4;
  int y = ((int)(a >> 9 & 511) * (64 - blend) + (int)(b >> 9 & 511) * blend + 2) / 4;
  int yaw = a >> 18, turn = (i8)((b >> 18) - yaw);
  yaw += (turn * blend + 32) >> 6;
  int rotation = path >> 5;
  for (int quarter = 0; quarter < rotation; ++quarter) {
    int old_x = x;
    x = y;
    y = 4096 - old_x;
  }
  int heading = (5 - ((yaw + 16) >> 5) + 2 * rotation) & 7;
  VehiclePosition3D next = motion->target;
  if (!previous && (event->coordinate_flags & CITY_EVENT_TARGET_TILE) && IsMapTileValid(city, event->train.next_tile))
    next = TrainTilePosition(city, event->train.next_tile);
  double before = (1 - fraction) * (1 - fraction) / 2, after = fraction * fraction / 2;
  return (TrainPathSample){.position = {target.x + x - 2048, target.y + y - 2048,
      (int)(motion->from.z * before + target.z * (1 - before - after) + next.z * after)}, .heading = heading};
}

static double VehicleMotionFraction(const VehicleMotion *motion, double now) {
  double fraction = (now - motion->started) * 1000 / CITY_CADENCE_INTERVAL_MS;
  return fmax(0, fmin(1, fraction));
}

static int TrainHeading(CityPoint direction) {
  // OpenGFX2 headings start at screen north and advance clockwise.
  return (int)lround(5 - atan2(direction.y, direction.x) * 4 / 3.141592653589793) & 7;
}

// Waiting engines must not coast towards occupied track while City has stopped their movement. Apply the same
// phase to followers so compression cannot push a coach into its stopped leader.
static double TrainMotionFraction(const City *city, const CityMapEvent *engine,
    const VehicleMotion *motion, double now) {
  // A reset or reversal has no incoming motion segment. Keep both the engine and its coaches at one shared phase.
  if (motion->from.x == motion->target.x && motion->from.y == motion->target.y) return .5;
  double fraction = VehicleMotionFraction(motion, now);
  if (engine->train.next_tile != engine->tile && IsMapTileValid(city, engine->train.next_tile)) {
    int overlay = ReadOverlay(city, engine->train.next_tile);
    if (IsCityEventOrMarkerOverlay(overlay)) {
      int first = engine->next_slot;
      int last = first >= CITY_EVENT_SLOT_FIRST && first < city->events.capacity ?
          city->events.event[first].next_slot : -1;
      // City permits entering the rear car's tile because that car leaves during the same move.
      if (last < CITY_EVENT_SLOT_FIRST || last >= city->events.capacity || overlay != CityEventOverlay(last))
        fraction = fmin(fraction, .5);
    }
  }
  return fraction;
}

// City exchanges the engine and rear record on reversal; copied follower targets catch up on the next tile move.
// Their current tiles still describe the reversed chain. Use those edges until the copied targets catch up.
static bool PrepareReversedTrainPaths(const City *city, DrawingVehicles *vehicles, const int slots[3]) {
  for (int car = 1; car < 3; ++car) {
    const VehiclePosition3D *a = &vehicles->slots[slots[car - 1]].target;
    const VehiclePosition3D *b = &vehicles->slots[slots[car]].target;
    if (abs(a->x - b->x) + abs(a->y - b->y) != 4096) return false;
  }
  VehicleMotion *engine = &vehicles->slots[slots[0]];
  for (int car = 0; car < 3; ++car) {
    VehicleMotion *motion = &vehicles->slots[slots[car]];
    const CityMapEvent *event = &city->events.event[slots[car]];
    int direction = event->direction & 3;
    VehiclePosition3D ahead = car ? vehicles->slots[slots[car - 1]].target :
        (VehiclePosition3D){motion->target.x + 4096 * kLineXStep[direction],
            motion->target.y + 4096 * kLineYStep[direction], motion->target.z};
    if (!car && (event->coordinate_flags & CITY_EVENT_TARGET_TILE) &&
        IsMapTileValid(city, event->train.next_tile) && event->train.next_tile != event->tile) {
      CityPoint next = MapTilePt(city, event->train.next_tile);
      ahead.x = next.x * 4096;
      ahead.y = next.y * 4096;
    }
    int dx = ahead.x - motion->target.x, dy = ahead.y - motion->target.y;
    int exit = dx < 0 ? CITY_LINE_WEST : dx > 0 ? CITY_LINE_EAST : dy > 0 ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
    int entry = (exit + 2) & 3;
    if (car < 2) {
      const VehiclePosition3D *behind = &vehicles->slots[slots[car + 1]].target;
      dx = behind->x - motion->target.x;
      dy = behind->y - motion->target.y;
      entry = dx < 0 ? CITY_LINE_WEST : dx > 0 ? CITY_LINE_EAST : dy > 0 ? CITY_LINE_SOUTH : CITY_LINE_NORTH;
    }
    motion->train_path = TrainPathForEdges(city, event->tile, entry, exit);
    // The first reversal has no incoming history. Later tile moves retain the
    // corrected path behind the rear coach while copied City targets catch up.
    if (!engine->train_reversed) motion->train_previous_path = 0;
  }
  engine->train_reversed = 1;
  return true;
}

static void SampleTrainMotion(const City *city, const CityMapEvent *event, const VehicleMotion *source,
    double fraction, bool previous, VehicleMotion *destination) {
  // Finish sampling before updating destination: source and destination may be the same history record.
  TrainPathSample sample = SampleTrainPath(city, event, source, fraction, previous);
  destination->offset = (VehiclePosition3D){sample.position.x - destination->target.x,
      sample.position.y - destination->target.y, sample.position.z - destination->target.z};
  destination->train_heading = sample.heading;
}

// Overlay groups retain their existing animation/RNG scope. Moving vehicles
// contribute their interpolated position; several effects on one tile stay together.
i32 CityMapOverlayDepth(const City *city, TileIndex tile) {
  const Drawing *drawing = city->drawing;
  CityPoint anchor = CityMapViewTilePoint(city, tile);
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  i32 depth = CityMapObjectDepth(anchor.x * 4096, anchor.y * 4096, elevation * 4096) + 2;
  if (!IsCityEventOverlay(ReadOverlay(city, tile))) return depth;
  for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0; slot = NextCityMapEventOnTile(city, slot)) {
    const CityMapEvent *event = &city->events.event[slot];
    if (!IsCityMapVehicle(event->type)) continue;
    VehiclePosition3D position = VehiclePosition(city, event);
    if (drawing->vehicles.enabled && slot < drawing->vehicles.capacity) {
      VehiclePosition3D offset = drawing->vehicles.slots[slot].offset;
      position.x += offset.x; position.y += offset.y; position.z += offset.z;
    }
    CityPoint point = ReadCityMapEventPosition(city, event);
    CityPoint camera = CityMapCameraPoint(city, CityMapCameraRotation(drawing), point.x, point.y);
    CityPoint offset = CityMapCameraVector(CityMapCameraRotation(drawing),
        position.x - point.x * 4096, position.y - point.y * 4096);
    int height = position.z * (16 / CityMapHeightStep(CITY_MAP_ZOOM_LARGE));
    i32 candidate = CityMapObjectDepth(camera.x * 4096 + offset.x, camera.y * 4096 + offset.y, height) + 2;
    depth = IntMax(depth, candidate);
  }
  return depth;
}

bool UpdateCityVehicleAnimation(const City *city, double now) {
  DrawingVehicles *vehicles = &city->drawing->vehicles;
  if (!city->drawing->map_state.vehicles_enabled ||
      !vehicles->enabled || !PrepareCityEventDrawingStorage(city)) return false;
  bool reset = !vehicles->initialized || vehicles->rotation != city->geography.rotation || now < vehicles->time;
  bool paused = city->settings.speed <= CITY_SPEED_PAUSED;
  bool changed = false;
  int rotor = paused ? vehicles->helicopter_frame :
      (u64)(now * CITY_HELICOPTER_ROTOR_FPS) % CITY_HELICOPTER_ROTOR_FRAMES;
  bool rotor_changed = rotor != vehicles->helicopter_frame;
  vehicles->helicopter_frame = rotor;
  vehicles->animating = false;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    const CityMapEvent *event = &city->events.event[slot];
    VehicleMotion *motion = &vehicles->slots[slot];
    VehiclePosition3D before = motion->offset;
    int previous_heading = motion->train_heading;
    bool spinning = !paused && IsMapTileValid(city, event->tile) &&
        ((event->type == CITY_MAP_EVENT_HELICOPTER && event->next_slot != CITY_HELICOPTER_ON_GROUND) ||
            event->type == CITY_MAP_EVENT_MAXIS_MAN);
    vehicles->animating |= spinning;
    changed |= spinning && rotor_changed;
    if (!IsCityMapVehicle(event->type) || !IsMapTileValid(city, event->tile)) {
      *motion = (VehicleMotion){0};
    } else {
      VehiclePosition3D target = VehiclePosition(city, event);
      bool train = IsCityMapEventTrain(event->type);
      bool moved = target.x != motion->target.x || target.y != motion->target.y;
      // A train history describes one cardinal tile step. Reversal exchanges the engine and rear-car
      // positions; skipped updates can also jump farther. Neither jump supplies a path to interpolate.
      bool train_jump = train && abs(target.x - motion->target.x) + abs(target.y - motion->target.y) > 4096;
      if (reset || train_jump || (motion->type != event->type && !(train && IsCityMapEventTrain(motion->type))) ||
          abs(target.x - motion->target.x) > 96 * 256 ||
          abs(target.y - motion->target.y) > 96 * 256 || abs(target.z - motion->target.z) > 96 * 256) {
        *motion = (VehicleMotion){.from = target, .target = target, .started = now, .type = event->type};
      } else {
        if (paused) motion->started += now - vehicles->time;
        double fraction = VehicleMotionFraction(motion, now);
        if (target.x != motion->target.x || target.y != motion->target.y || target.z != motion->target.z) {
          // Continue from the last displayed position if a delayed update or
          // input arrives while the previous segment is still in flight.
          if (train && moved) motion->train_previous_path = motion->train_path;
          motion->from = train ? motion->target : (VehiclePosition3D){
              motion->target.x + (int)((motion->from.x - motion->target.x) * (1 - fraction)),
              motion->target.y + (int)((motion->from.y - motion->target.y) * (1 - fraction)),
              motion->target.z + (int)((motion->from.z - motion->target.z) * (1 - fraction))};
          motion->target = target;
          motion->started = now;
          fraction = 0;
        }
        if (!train) motion->offset = (VehiclePosition3D){(int)((motion->from.x - target.x) * (1 - fraction)),
            (int)((motion->from.y - target.y) * (1 - fraction)),
            (int)((motion->from.z - target.z) * (1 - fraction))};
        motion->type = event->type;
      }
      if (train && (reset || moved || !motion->train_path))
        motion->train_path = PrepareTrainPath(city, event, motion);
      if (train && (event->type == CITY_MAP_EVENT_TRAIN_ENGINE || event->type == CITY_MAP_EVENT_SUBWAY_ENGINE))
        SampleTrainMotion(city, event, motion, TrainMotionFraction(city, event, motion, now), false, motion);
    }
    vehicles->animating |= !paused && VehicleMotionFraction(motion, now) < 1 &&
        (motion->offset.x != 0 || motion->offset.y != 0 || motion->offset.z != 0);
    changed |= before.x != motion->offset.x || before.y != motion->offset.y || before.z != motion->offset.z;
    changed |= previous_heading != motion->train_heading;
  }
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    const CityMapEvent *engine = &city->events.event[slot];
    if (engine->type != CITY_MAP_EVENT_TRAIN_ENGINE && engine->type != CITY_MAP_EVENT_SUBWAY_ENGINE) continue;
    int slots[3] = {slot, engine->next_slot, 0};
    if (slots[1] < CITY_EVENT_SLOT_FIRST || slots[1] >= city->events.capacity || slots[1] == slot) continue;
    const CityMapEvent *first = &city->events.event[slots[1]];
    if (first->type != CITY_MAP_EVENT_TRAIN_CAR && first->type != CITY_MAP_EVENT_SUBWAY_CAR) continue;
    slots[2] = first->next_slot;
    if (slots[2] < CITY_EVENT_SLOT_FIRST || slots[2] >= city->events.capacity ||
        slots[2] == slot || slots[2] == slots[1]) continue;
    const CityMapEvent *last = &city->events.event[slots[2]];
    if (last->type != CITY_MAP_EVENT_TRAIN_CAR && last->type != CITY_MAP_EVENT_SUBWAY_CAR) continue;
    if (!vehicles->slots[slot].train_path || !vehicles->slots[slots[1]].train_path ||
        !vehicles->slots[slots[2]].train_path) continue;
    // Space coaches by track distance, not by a fraction of each tile: diagonal tiles are shorter than straights.
    // The rear record retains its preceding path in existing padding, covering all three coaches on short bends.
    const int spacing = 180; // Q8 tile distance between vehicle centres.
    double phase = TrainMotionFraction(city, engine, &vehicles->slots[slot], now);
    bool continuous = first->train.next_tile == engine->tile && last->train.next_tile == first->tile;
    bool reversed = !continuous && PrepareReversedTrainPaths(city, vehicles, slots);
    if (continuous) vehicles->slots[slot].train_reversed = 0;
    // Reversal preparation can change the engine's curve too; use that same curve for the whole formation.
    if (reversed) {
      VehicleMotion *head = &vehicles->slots[slot];
      VehiclePosition3D before = head->offset;
      int previous_heading = head->train_heading;
      SampleTrainMotion(city, engine, head, phase, false, head);
      changed |= before.x != head->offset.x || before.y != head->offset.y || before.z != head->offset.z;
      changed |= previous_heading != head->train_heading;
    }
    for (int car = 1; car < 3; ++car) {
      int source = 0;
      int path = vehicles->slots[slot].train_path;
      double distance = phase * kRailPathLengths[(path & 31) - 1] - car * spacing;
      while (distance < 0 && source < 3) {
        if (source == 2 && !vehicles->slots[slots[2]].train_previous_path) break;
        ++source;
        path = source == 3 ? vehicles->slots[slots[2]].train_previous_path :
            vehicles->slots[slots[source]].train_path;
        distance += kRailPathLengths[(path & 31) - 1];
      }
      int source_slot = slots[IntMin(source, 2)];
      double fraction = fmax(0, fmin(1, distance / kRailPathLengths[(path & 31) - 1]));
      VehicleMotion *motion = &vehicles->slots[slots[car]];
      if (!IsCityMapEventTrain(motion->type) || !IsCityMapEventTrain(vehicles->slots[source_slot].type)) continue;
      VehiclePosition3D before = motion->offset;
      int previous_heading = motion->train_heading;
      if (continuous || reversed) {
        SampleTrainMotion(city, &city->events.event[source_slot], &vehicles->slots[source_slot],
            fraction, source == 3, motion);
      } else {
        // Missing or non-adjacent history cannot define a continuous formation.
        const CityMapEvent *event = &city->events.event[slots[car]];
        int direction = event->direction & 3;
        motion->from = motion->target;
        motion->offset = (VehiclePosition3D){0};
        motion->train_heading = TrainHeading(
            (CityPoint){kLineXStep[direction], kLineYStep[direction]});
      }
      changed |= before.x != motion->offset.x || before.y != motion->offset.y || before.z != motion->offset.z;
      changed |= previous_heading != motion->train_heading;
    }
  }
  vehicles->time = now;
  vehicles->rotation = city->geography.rotation;
  vehicles->initialized = true;
  return changed;
}

// Train artwork uses registered wheel centers, independently of rail tile sprites.
void DrawCityMapTrain(City *city, TileIndex tile, int zoom, int slot) {
  Drawing *drawing = city->drawing;
  if (!drawing->map_state.tile_details_enabled ||
      (!drawing->sprite_damage_bounds && drawing->map_depth.phase == MAP_DEPTH_GROUND)) return;
  const CityMapEvent *event = &city->events.event[slot];
  // New trains initially share one tile. A coach appears after its route separates from that spawn position.
  if (event->type == CITY_MAP_EVENT_TRAIN_CAR && event->train.next_tile == event->tile) return;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  CityPoint camera = CityMapViewTilePoint(city, tile);
  CityPoint next = ReadCityMapEventTarget(city, event);
  CityPoint direction = {next.x - x, next.y - y};
  if (!direction.x && !direction.y)
    direction = (CityPoint){kLineXStep[event->direction & 3], kLineYStep[event->direction & 3]};
  int heading = TrainHeading(direction);
  VehiclePosition3D position = TrainTilePosition(city, tile);
  if (drawing->vehicles.enabled && slot < drawing->vehicles.capacity) {
    const VehicleMotion *motion = &drawing->vehicles.slots[slot];
    if (IsCityMapEventTrain(motion->type)) {
      heading = motion->train_heading;
      position.x += motion->offset.x; position.y += motion->offset.y; position.z += motion->offset.z;
    }
  }
  heading = (heading + 6 * CityMapViewRotation(drawing->default_map_view)) & 7;
  static const u16 kSprites[2][8] = {
    {CITY_SPRITE_CATALOG_TRAIN_ENGINE_FIRST, CITY_SPRITE_CATALOG_TRAIN_ENGINE_FIRST + 1,
      CITY_SPRITE_CATALOG_TRAIN_ENGINE_FIRST + 2, CITY_SPRITE_CATALOG_TRAIN_ENGINE_FIRST + 3,
      CITY_SPRITE_CATALOG_TRAIN_ENGINE_FIRST + 4, CITY_SPRITE_CATALOG_TRAIN_ENGINE_EXTRA_FIRST,
      CITY_SPRITE_CATALOG_TRAIN_ENGINE_EXTRA_FIRST + 1, CITY_SPRITE_CATALOG_TRAIN_ENGINE_EXTRA_FIRST + 2},
    {CITY_SPRITE_CATALOG_TRAIN_COACH_FIRST, CITY_SPRITE_CATALOG_TRAIN_COACH_FIRST + 1,
      CITY_SPRITE_CATALOG_TRAIN_COACH_FIRST + 2, CITY_SPRITE_CATALOG_TRAIN_COACH_FIRST + 3,
      CITY_SPRITE_CATALOG_TRAIN_COACH_FIRST + 4, CITY_SPRITE_CATALOG_TRAIN_COACH_EXTRA_FIRST,
      CITY_SPRITE_CATALOG_TRAIN_COACH_EXTRA_FIRST + 1, CITY_SPRITE_CATALOG_TRAIN_COACH_EXTRA_FIRST + 2}
  };
  int sprite_id = CITY_SPRITE_CATALOG_LARGE_BASE + kSprites[event->type == CITY_MAP_EVENT_TRAIN_CAR][heading];
  const MapSpriteMetrics *sprite = &drawing->map_state.sprite_metrics[sprite_id];
  CityPoint offset = CityMapCameraVector(CityMapViewRotation(drawing->default_map_view),
      position.x - x * 4096, position.y - y * 4096);
  int divisor = zoom == CITY_MAP_ZOOM_SMALL ? 4 : zoom == CITY_MAP_ZOOM_MEDIUM ? 2 : 1;
  i64 screen_x = (i64)(drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) -
      sprite->width / 2) * 256 + (offset.y - offset.x) / divisor;
  i64 baseline = (i64)(drawing->map_state.origin_y + (2 << zoom) * (camera.x + camera.y - 1)) * 256 +
      ((offset.x + offset.y) / 2 - position.z) / divisor;
  DrawCityMapSpriteAtBaselineQ8(drawing, sprite_id, screen_x, baseline, false);
}
