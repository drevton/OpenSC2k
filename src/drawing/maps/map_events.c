#include "city/maps/layers.h"
#include "drawing/maps/map_effects.h"
#include <assert.h>
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/surface_lines.h"
#include "assets/graphics.h"
#include "city/engine/city_types.h"
#include "city/events/events.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "city/engine/simulation.h"
#include <math.h>
#include "game/errors.h"
#include "city/infrastructure/infrastructure.h"
#include "ui/state.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/tables.h"

int UseCityMapSpriteEffect(Drawing *drawing, CityMapSpriteEffect effect) {
  if (effect == CITY_MAP_EFFECT_WIND) {
    drawing->wind_power_visible = true;
    return drawing->wind_power_frame;
  }
  assert(effect == CITY_MAP_EFFECT_POWER_SMOKE);
  drawing->power_smoke_visible = true;
  return drawing->power_smoke_frame;
}

// Drawing subsystem: map events.

void StartCityScreenShake(Drawing *drawing) {
  drawing->screen_shake_active = true;
  drawing->screen_shake_started = -1;
}

bool UpdateCityScreenShake(Drawing *drawing, double now) {
  if (!drawing->screen_shake_active) return false;
  if (drawing->screen_shake_started < 0 || now < drawing->screen_shake_started)
    drawing->screen_shake_started = now;
  double elapsed = now - drawing->screen_shake_started;
  int frame = elapsed >= .12 ? 24 : elapsed / .005;
  drawing->screen_shake_active = frame < 24;
  int offset = drawing->screen_shake_active && !(frame & 1) ?
      4 << drawing->default_map_view->zoom : 0;
  if (offset == drawing->screen_shake_offset) return false;
  drawing->screen_shake_offset = offset;
  MarkScreenDirty(&drawing->screen_damage, (CityRect){0, 0, drawing->framebuffer.width, drawing->framebuffer.height});
  return true;
}

bool UpdateCityPowerAnimation(const City *city, double now) {
  Drawing *drawing = city->game->drawing;
  int speed = city->settings.speed;
  double elapsed = now - drawing->power_smoke_time;
  drawing->power_smoke_time = now;
  if (drawing->power_smoke_speed != speed) {
    drawing->power_smoke_speed = speed;
    return false;
  }
  if (speed <= CITY_SPEED_PAUSED || elapsed <= 0) return false;
  double rate = CITY_POWER_SMOKE_FPS;
  if (speed == CITY_SPEED_SLOW) rate *= .5;
  else if (speed == CITY_SPEED_FAST) rate *= 2;
  else if (speed == CITY_SPEED_UNLIMITED) rate *= 4;
  drawing->power_smoke_phase += elapsed * rate;
  int frames = drawing->power_smoke_phase;
  drawing->power_smoke_phase -= frames;
  drawing->power_smoke_frame = (drawing->power_smoke_frame + frames) % CITY_POWER_SMOKE_FRAMES;
  int rotor = (drawing->power_smoke_frame * 4 + (int)(drawing->power_smoke_phase * 4)) % CITY_WIND_FRAMES;
  bool changed = (frames && drawing->power_smoke_visible) ||
      (rotor != drawing->wind_power_frame && drawing->wind_power_visible);
  drawing->wind_power_frame = rotor;
  return changed;
}

void ShowCityDemolitionEffect(City *city, TileIndex tile, bool large) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->demolition_enabled || drawing->demolition_suppressed || !IsMapTileValid(city, tile)) return;
  int slot = 0, unused = -1;
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    if (effect->active && effect->tile == tile) return;
    if (!effect->active && unused < 0) unused = i;
    if (effect->started < drawing->demolition_effects[slot].started) slot = i;
  }
  if (unused >= 0) slot = unused;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  // Presentation randomness never advances City's deterministic streams.
  drawing->demolition_random = drawing->demolition_random * 214013 + 2531011;
  drawing->demolition_effects[slot] = (CityDemolitionEffect){.tile = tile, .started = -1,
      .elevation = elevation, .flipped = drawing->demolition_random >> 16 & 1, .large = large, .active = true};
  drawing->demolition_active = true;
}

// This presentation clock is real time, including when City simulation is paused.
bool UpdateCityDemolitionEffects(Drawing *drawing, double now) {
  bool dirty = false;
  drawing->demolition_active = false;
  for (int i = 0; i < CITY_DEMOLITION_EFFECT_COUNT; ++i) {
    CityDemolitionEffect *effect = &drawing->demolition_effects[i];
    if (!effect->active) continue;
    if (effect->started < 0 || now < effect->started) { effect->started = now; dirty = true; }
    int count = effect->large ? CITY_DEMOLITION_LARGE_FRAMES : CITY_DEMOLITION_SMALL_FRAMES;
    double elapsed = now - effect->started;
    int frame = elapsed >= count * 0.12 ? count : (int)(elapsed / 0.12);
    dirty |= frame != effect->frame;
    effect->frame = frame;
    effect->active = frame < count;
    drawing->demolition_active |= effect->active;
  }
  return dirty;
}

// Keep allocated interpolation storage reusable, but discard every visual vehicle.
void ClearCityVehicleVisualization(Drawing *drawing) {
  drawing->road_traffic = (DrawingRoadTraffic){0};
  DrawingVehicles *vehicles = &drawing->vehicles;
  if (vehicles->slots) memset(vehicles->slots, 0, vehicles->capacity * sizeof(*vehicles->slots));
  *vehicles = (DrawingVehicles){.slots = vehicles->slots, .capacity = vehicles->capacity, .enabled = vehicles->enabled};
}

static bool IsMovingVehicle(int type) {
  return type == CITY_MAP_EVENT_AIRPLANE || type == CITY_MAP_EVENT_HELICOPTER ||
      type == CITY_MAP_EVENT_SHIP || type == CITY_MAP_EVENT_BULLDOZER || type == CITY_MAP_EVENT_SAILBOAT ||
      IsCityMapEventTrain(type);
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

// Each car follows the same rounded path one City update behind its leader.
// The tangent chooses artwork, so entering a bend does not rotate a car while
// it is still travelling along the preceding straight section.
static VehiclePosition3D TrainPathPosition(const City *city, const CityMapEvent *event,
    const VehicleMotion *motion, double fraction, CityPoint *direction) {
  VehiclePosition3D next = motion->target;
  if ((event->coordinate_flags & CITY_EVENT_TARGET_TILE) && IsMapTileValid(city, event->train.next_tile))
    next = TrainTilePosition(city, event->train.next_tile);
  double before = (1 - fraction) * (1 - fraction) / 2;
  double after = fraction * fraction / 2;
  double center = 1 - before - after;
  if (direction) *direction = (CityPoint){
      (int)((motion->target.x - motion->from.x) * (1 - fraction) + (next.x - motion->target.x) * fraction),
      (int)((motion->target.y - motion->from.y) * (1 - fraction) + (next.y - motion->target.y) * fraction)};
  return (VehiclePosition3D){
      (int)(motion->from.x * before + motion->target.x * center + next.x * after),
      (int)(motion->from.y * before + motion->target.y * center + next.y * after),
      (int)(motion->from.z * before + motion->target.z * center + next.z * after)};
}

static double VehicleMotionFraction(const VehicleMotion *motion, double now) {
  double fraction = (now - motion->started) * 1000 / CITY_CADENCE_INTERVAL_MS;
  return fmax(0, fmin(1, fraction));
}

static int TrainHeading(CityPoint direction) {
  // OpenGFX2 headings start at screen north and advance clockwise.
  return (int)lround(5 - atan2(direction.y, direction.x) * 4 / 3.141592653589793) & 7;
}

static void SampleTrainMotion(const City *city, const CityMapEvent *event, const VehicleMotion *source,
    double fraction, VehicleMotion *destination) {
  CityPoint direction;
  VehiclePosition3D position = TrainPathPosition(city, event, source, fraction, &direction);
  if (!direction.x && !direction.y)
    direction = (CityPoint){CityTrainUpdateXStep(event->direction & 3), CityTrainUpdateYStep(event->direction & 3)};
  destination->offset = (VehiclePosition3D){position.x - destination->target.x,
      position.y - destination->target.y, position.z - destination->target.z};
  destination->train_heading = TrainHeading(direction);
}

// Overlay groups retain their existing animation/RNG scope. Moving vehicles
// contribute their interpolated position; several effects on one tile stay together.
i32 CityMapOverlayDepth(const City *city, TileIndex tile) {
  const Drawing *drawing = city->game->drawing;
  CityPoint anchor = CityMapViewTilePoint(city, tile);
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  i32 depth = CityMapObjectDepth(anchor.x * 4096, anchor.y * 4096, elevation * 4096) + 2;
  if (!IsCityEventOverlay(ReadOverlay(city, tile))) return depth;
  for (int slot = FirstCityMapEventOnTile(city, tile); slot >= 0; slot = NextCityMapEventOnTile(city, slot)) {
    const CityMapEvent *event = &city->events.event[slot];
    if (!IsMovingVehicle(event->type)) continue;
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
  DrawingVehicles *vehicles = &city->game->drawing->vehicles;
  if (!city->game->drawing->map_state.vehicles_enabled ||
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
    if (!IsMovingVehicle(event->type) || !IsMapTileValid(city, event->tile)) {
      *motion = (VehicleMotion){0};
    } else {
      VehiclePosition3D target = VehiclePosition(city, event);
      bool train = IsCityMapEventTrain(event->type);
      if (reset || (motion->type != event->type && !(train && IsCityMapEventTrain(motion->type))) ||
          abs(target.x - motion->target.x) > 96 * 256 ||
          abs(target.y - motion->target.y) > 96 * 256 || abs(target.z - motion->target.z) > 96 * 256) {
        *motion = (VehicleMotion){.from = target, .target = target, .started = now, .type = event->type};
      } else {
        if (paused) motion->started += now - vehicles->time;
        double fraction = VehicleMotionFraction(motion, now);
        if (target.x != motion->target.x || target.y != motion->target.y || target.z != motion->target.z) {
          // Continue from the last displayed position if a delayed update or
          // input arrives while the previous segment is still in flight.
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
      if (train && (event->type == CITY_MAP_EVENT_TRAIN_ENGINE || event->type == CITY_MAP_EVENT_SUBWAY_ENGINE))
        SampleTrainMotion(city, event, motion, VehicleMotionFraction(motion, now), motion);
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
    // City spaces its records a whole tile apart. Sample the retained paths
    // closer together to keep the imported half-tile vehicles coupled.
    double phase = VehicleMotionFraction(&vehicles->slots[slot], now);
    for (int car = 1; car < 3; ++car) {
      int source = 0;
      double fraction = phase - car * .65;
      while (fraction < 0) { ++source; fraction += 1; }
      VehicleMotion *motion = &vehicles->slots[slots[car]];
      if (!IsCityMapEventTrain(motion->type) || !IsCityMapEventTrain(vehicles->slots[slots[source]].type)) continue;
      VehiclePosition3D before = motion->offset;
      int previous_heading = motion->train_heading;
      SampleTrainMotion(city, &city->events.event[slots[source]], &vehicles->slots[slots[source]], fraction, motion);
      changed |= before.x != motion->offset.x || before.y != motion->offset.y || before.z != motion->offset.z;
      changed |= previous_heading != motion->train_heading;
    }
  }
  vehicles->time = now;
  vehicles->rotation = city->geography.rotation;
  vehicles->initialized = true;
  return changed;
}

static void DrawVehicleSprite(Drawing *drawing, int sprite, int x, int y, int flip, int slot, int zoom) {
  if (!drawing->sprite_damage_bounds && drawing->map_depth.phase == MAP_DEPTH_GROUND) return;
  CityPoint saved = drawing->sprite_offset;
  if (drawing->vehicles.enabled) {
    const VehiclePosition3D *offset = &drawing->vehicles.slots[slot].offset;
    CityPoint point = CityMapCameraVector(CityMapViewRotation(drawing->default_map_view), offset->x, offset->y);
    int divisor = zoom == CITY_MAP_ZOOM_SMALL ? 4 : zoom == CITY_MAP_ZOOM_MEDIUM ? 2 : 1;
    drawing->sprite_offset = (CityPoint){(point.y - point.x) / divisor,
        ((point.x + point.y) / 2 - offset->z) / divisor};
  }
  DrawCityMapSprite(drawing, sprite, x, y, flip);
  drawing->sprite_offset = saved;
}

// Animated map overlays 251 through 255.
void DrawCityMapAnimatedOverlay(City *city, int zoom, int x, int y, int overlay) {
  Drawing *drawing = city->game->drawing;
  TileIndex cell = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, cell);
  CityMapSprite *catalog = CityDrawingSpriteCatalog(drawing);
  int elevation_step = CityMapHeightStep(zoom);
  int half_width = 4 << zoom;
  int row_step = 2 << zoom;
  if (IsMapTileValid(city, cell) && HasTileWater(&city->tile[cell]) &&
      overlay != CITY_OVERLAY_POLLUTION && overlay != CITY_OVERLAY_FLOOD) return;
  int elevation = ReadCitySurfaceHeight(&city->tile[cell]);
  int sprite_id;
  switch (overlay) {
    case CITY_OVERLAY_POLLUTION: sprite_id = (500 * zoom + CITY_SPRITE_CATALOG_POLLUTION); break;
    case CITY_OVERLAY_FLOOD: sprite_id = (500 * zoom + CITY_SPRITE_CATALOG_FLOOD); break;
    case CITY_OVERLAY_RIOTS_SOUTH_EAST:
    case CITY_OVERLAY_RIOTS_NORTH_WEST:
      sprite_id = (500 * zoom + CITY_SPRITE_CATALOG_RIOTS_FIRST + (Sc2kCrtRand(city) & 1));
      break;
    case CITY_OVERLAY_FIRE:
      sprite_id = (500 * zoom + CITY_SPRITE_CATALOG_FIRE_FIRST + (Sc2kCrtRand(city) & 3));
      break;
    default: return;
  }
  CityMapSprite *sprite = &catalog[sprite_id];
  int sprite_y = drawing->map_state.origin_y + row_step * (camera.y + camera.x) -
      elevation_step * elevation - sprite->height;
  int sprite_x = drawing->map_state.origin_x + half_width * (camera.y - camera.x + 1) - (sprite->width >> 1);
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, (Sc2kCrtRand(city) & 1));
}

// Unanimated map-event sprite for event types 7, 8, and 14.
void DrawCityMapEventStaticSprite(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  TileIndex cell = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, cell);
  CityMapSprite *catalog = CityDrawingSpriteCatalog(drawing);
  static const i16 kZoomSpriteBase[CITY_CANVAS_MAP_ZOOM_COUNT] = {
    [CITY_CANVAS_MAP_ZOOM_SMALL] = CITY_SPRITE_CATALOG_SMALL_BASE,
    [CITY_CANVAS_MAP_ZOOM_MEDIUM] = CITY_SPRITE_CATALOG_MEDIUM_BASE,
    [CITY_CANVAS_MAP_ZOOM_LARGE] = CITY_SPRITE_CATALOG_LARGE_BASE,
    [CITY_CANVAS_MAP_ZOOM_MAGNIFIED] = CITY_SPRITE_CATALOG_LARGE_BASE,
  };
  CityMapEvent *event = &city->events.event[slot];
  int sprite_id = kZoomSpriteBase[zoom] + kCityMapEventBaseSprite[event->type];
  CityMapSprite *sprite = &catalog[sprite_id];
  int zoom_scale = 1 << zoom;
  int sprite_x = drawing->map_state.origin_x + (4 * (camera.y - camera.x) + 4) * zoom_scale - (sprite->width >> 1);
  int sprite_y = drawing->map_state.origin_y + ((2 * (camera.y + camera.x)) << zoom) -
      (CityMapHeightStep(zoom) * ReadCityLandHeight(&city->tile[cell])) - sprite->height;
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, 0);
}

// Train artwork uses registered wheel centers, independently of rail tile sprites.
void DrawCityMapEventTileDetail(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->map_state.tile_details_enabled ||
      (!drawing->sprite_damage_bounds && drawing->map_depth.phase == MAP_DEPTH_GROUND)) return;
  const CityMapEvent *event = &city->events.event[slot];
  TileIndex tile = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, tile);
  CityPoint next = ReadCityMapEventTarget(city, event);
  CityPoint direction = {next.x - x, next.y - y};
  if (!direction.x && !direction.y)
    direction = (CityPoint){CityTrainUpdateXStep(event->direction & 3), CityTrainUpdateYStep(event->direction & 3)};
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
  const CityMapSprite *sprite = &CityDrawingSpriteCatalog(drawing)[sprite_id];
  CityPoint offset = CityMapCameraVector(CityMapViewRotation(drawing->default_map_view),
      position.x - x * 4096, position.y - y * 4096);
  int divisor = zoom == CITY_MAP_ZOOM_SMALL ? 4 : zoom == CITY_MAP_ZOOM_MEDIUM ? 2 : 1;
  i64 screen_x = (i64)(drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) -
      sprite->width / 2) * 256 + (offset.y - offset.x) / divisor;
  i64 baseline = (i64)(drawing->map_state.origin_y + (2 << zoom) * (camera.x + camera.y - 1)) * 256 +
      ((offset.x + offset.y) / 2 - position.z) / divisor;
  DrawCityMapSpriteAtBaselineQ8(drawing, sprite_id, screen_x, baseline, false);
}

// Randomized sprite for map-event type 15.
void DrawCityMapEventRandomSprite(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  TileIndex cell = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, cell);
  CityMapSprite *catalog = CityDrawingSpriteCatalog(drawing);
  if (drawing->map_state.events_suppressed != 0)
    return;
  int sprite_id = 500 * zoom + kCityMapEventBaseSprite[CITY_MAP_EVENT_TORNADO] + Sc2kCrtRandRange(city, 3);
  int elevation = ReadCitySurfaceHeight(&city->tile[cell]);
  CityMapSprite *sprite = &catalog[sprite_id];
  int sprite_y = drawing->map_state.origin_y + (2 << zoom) * (camera.y + camera.x) -
      sprite->height - (CityMapHeightStep(zoom)) * elevation;
  int sprite_x = drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) - sprite->width;
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, (Sc2kCrtRand(city) & 1));
}

// Scale and draw one piece of event type 5.
void DrawCityMapEventStructureSprite(City *city, int zoom, int sprite_id, int x, int y, int flipped) {
  Drawing *drawing = city->game->drawing;
  int divisor = zoom == CITY_MAP_ZOOM_SMALL ? 4 : zoom == CITY_MAP_ZOOM_MEDIUM ? 2 : 1;
  int screen_x = drawing->map_state.origin_x + x / divisor;
  int screen_y = drawing->map_state.origin_y + y / divisor;
  sprite_id = sprite_id + 500 * zoom;
  DrawCityMapSprite(drawing, sprite_id, screen_x, screen_y, flipped);
}

// Draw the authored type-5 crawler as one registered sprite.
void DrawCityMapEventStructure(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  if (drawing->map_state.events_suppressed) return;
  CityMapEvent *event = &city->events.event[slot];
  TileIndex tile = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, tile);
  int direction = (event->direction + 6 * CityMapCameraRotation(drawing)) & 7;
  int pose = (direction >> 1) & 1;
  if (event->monster.lower_pose & 128) pose = Sc2kCrtRand(city) & 1;
  int sprite_id = 500 * zoom + CITY_SPRITE_CATALOG_CRAWLER_FIRST + pose;
  const CityMapSprite *sprite = &CityDrawingSpriteCatalog(drawing)[sprite_id];
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  int screen_x = drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) - sprite->width / 2;
  int screen_y = drawing->map_state.origin_y + (2 << zoom) * (camera.x + camera.y) - sprite->height -
      CityMapHeightStep(zoom) * elevation - event->height * (2 << zoom);
  DrawCityMapSprite(drawing, sprite_id, screen_x, screen_y, direction >= 4);
}

// Dispatch the event slot stored in overlay IDs 201 through 240.
void DrawCityMapEventOverlay(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  TileIndex cell = MapTileXY(city, x, y);
  CityPoint camera = CityMapViewTilePoint(city, cell);
  CityMapSprite *catalog = CityDrawingSpriteCatalog(drawing);
  CityMapEvent *event = &city->events.event[slot];
  if (!drawing->map_state.vehicles_enabled && IsMovingVehicle(event->type)) return;
  CityPoint position = ReadCityMapEventPosition(city, event);
  if (kCityMapEventMinZoom[event->type] > zoom)
    return;
  if ((position.y != y || position.x != x) && event->type != CITY_MAP_EVENT_TRAIN_ENGINE &&
      event->type != CITY_MAP_EVENT_TRAIN_CAR) {
    return;
  }
  int direction = (event->direction + 6 * CityMapCameraRotation(city->game->drawing)) & 7;
  CityPoint target = ReadCityMapEventTarget(city, event);
  target = CityMapCameraVector(CityMapCameraRotation(city->game->drawing), target.x, target.y);
  switch (event->type) {
    case CITY_MAP_EVENT_UNUSED:
    case CITY_MAP_EVENT_AIRPLANE:
    case CITY_MAP_EVENT_HELICOPTER:
    case CITY_MAP_EVENT_SHIP:
    case CITY_MAP_EVENT_BULLDOZER:
    case CITY_MAP_EVENT_EXPLOSION:
    case CITY_MAP_EVENT_SAILBOAT:
    case CITY_MAP_EVENT_MAXIS_MAN: {
      if (event->type != CITY_MAP_EVENT_MAXIS_MAN && event->type != CITY_MAP_EVENT_EXPLOSION &&
          drawing->map_state.events_suppressed != 0)
        return;
      int sprite_base = kCityMapEventBaseSprite[event->type];
      int flip;
      if (event->type == CITY_MAP_EVENT_MAXIS_MAN) {
        sprite_base = CITY_SPRITE_CATALOG_RESCUE_ANIMATION_FIRST + drawing->vehicles.helicopter_frame;
        flip = direction > 3;
      } else if (event->type >= CITY_MAP_EVENT_AIRPLANE && event->type <= CITY_MAP_EVENT_SHIP) {
        sprite_base += kCityMapEventDirectionSprite[direction];
        if (event->type == CITY_MAP_EVENT_HELICOPTER) {
          int frame = event->next_slot == CITY_HELICOPTER_ON_GROUND ? 0 : drawing->vehicles.helicopter_frame;
          sprite_base = CITY_SPRITE_CATALOG_HELICOPTER_ANIMATION_FIRST +
              kCityMapEventDirectionSprite[direction] * CITY_HELICOPTER_ROTOR_FRAMES + frame;
        }
        flip = kCityMapEventDirectionFlip[direction];
      } else if (event->type == CITY_MAP_EVENT_EXPLOSION) {
        sprite_base += direction;
        flip = NextCityMapRandomModulo(city, 2);
      } else if (event->type == CITY_MAP_EVENT_SAILBOAT && event->next_slot != 0) {
        sprite_base = 379;
        flip = city->event_runtime.animation_phase & 1;
      } else {
        sprite_base += kCityMapEventDefaultSprite[direction];
        flip = kCityMapEventDefaultFlip[direction];
      }
      int sprite_id = 500 * zoom + sprite_base;
      CityMapSprite *sprite = &catalog[sprite_id];
      int elevation = ReadCitySurfaceHeight(&city->tile[cell]);
      int sprite_y = drawing->map_state.origin_y + (2 << zoom) * (camera.y + camera.x) +
          (target.x + target.y) / (CITY_EVENT_OFFSET_SCALE * kCityMapEventYDivisor[zoom]) - sprite->height -
          ((CityMapHeightStep(zoom)) * elevation + event->height * (2 << zoom));
      int sprite_x = drawing->map_state.origin_x +
          (target.y - target.x) / (CITY_EVENT_OFFSET_SCALE * kCityMapEventXDivisor[zoom]) +
          (4 << zoom) * (camera.y - camera.x + 1) - (sprite->width >> 1);
      DrawVehicleSprite(drawing, sprite_id, sprite_x, sprite_y, flip, slot, zoom);
      return;
    }
    case CITY_MAP_EVENT_POLICE:
    case CITY_MAP_EVENT_FIRE:
    case CITY_MAP_EVENT_MILITARY:
      DrawCityMapEventStaticSprite(city, zoom, x, y, slot);
      return;
    case CITY_MAP_EVENT_TRAIN_ENGINE:
    case CITY_MAP_EVENT_TRAIN_CAR:
      DrawCityMapEventTileDetail(city, zoom, x, y, slot);
      return;
    case CITY_MAP_EVENT_MONSTER:
      DrawCityMapEventStructure(city, zoom, x, y, slot);
      return;
    case CITY_MAP_EVENT_SUBWAY_ENGINE:
    case CITY_MAP_EVENT_SUBWAY_CAR:
      return;
    case CITY_MAP_EVENT_TORNADO:
      DrawCityMapEventRandomSprite(city, zoom, x, y, slot);
      return;
    default:
      SetGameError(city->game, -2);
  }
}

// Native damage query for deterministic event sprites; original event boundaries stay intact.
CityRect Sc2kCityMapEventBounds(City *city, int zoom, int x, int y, int slot) {
  Drawing *drawing = city->game->drawing;
  CityRect bounds = {0};
  CityRect *previous = drawing->sprite_damage_bounds;
  drawing->sprite_damage_bounds = &bounds;
  DrawCityMapEventOverlay(city, zoom, x, y, slot);
  drawing->sprite_damage_bounds = previous;
  return bounds;
}
