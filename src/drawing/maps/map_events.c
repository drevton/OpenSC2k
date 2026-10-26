#include "drawing/maps/map_events.h"

#include <stdlib.h>
#include <string.h>

#include "assets/graphics.h"
#include "city/engine/city_types.h"
#include "city/engine/random.h"
#include "city/events/events.h"
#include "city/maps/terrain.h"
#include "city/maps/tile_storage.h"
#include "city/state.h"
#include "drawing/maps/map_depth.h"
#include "drawing/maps/map_sprites.h"
#include "drawing/maps/map_vehicles.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"

// Event artwork selection belongs to Drawing. Values are indexed by saved event kinds, not tile kinds.
static const u16 kCityMapEventBaseSprite[CITY_MAP_EVENT_KIND_COUNT] = {
  [CITY_MAP_EVENT_UNUSED] = 0,
  [CITY_MAP_EVENT_AIRPLANE] = 359,
  [CITY_MAP_EVENT_HELICOPTER] = 364,
  [CITY_MAP_EVENT_SHIP] = 369,
  [CITY_MAP_EVENT_BULLDOZER] = 390,
  [CITY_MAP_EVENT_MONSTER] = 490,
  [CITY_MAP_EVENT_EXPLOSION] = 387,
  [CITY_MAP_EVENT_POLICE] = 382,
  [CITY_MAP_EVENT_FIRE] = 383,
  [CITY_MAP_EVENT_SAILBOAT] = 380,
  [CITY_MAP_EVENT_TRAIN_ENGINE] = 374,
  [CITY_MAP_EVENT_TRAIN_CAR] = 374,
  [CITY_MAP_EVENT_SUBWAY_ENGINE] = 374,
  [CITY_MAP_EVENT_SUBWAY_CAR] = 374,
  [CITY_MAP_EVENT_MILITARY] = 384,
  [CITY_MAP_EVENT_TORNADO] = 497,
  [CITY_MAP_EVENT_MAXIS_MAN] = 495,
};

static const u8 kCityMapEventMinZoom[CITY_MAP_EVENT_KIND_COUNT] = {
  [CITY_MAP_EVENT_HELICOPTER] = CITY_MAP_ZOOM_LARGE,
  [CITY_MAP_EVENT_EXPLOSION] = CITY_MAP_ZOOM_MEDIUM,
  [CITY_MAP_EVENT_SAILBOAT] = CITY_MAP_ZOOM_LARGE,
  [CITY_MAP_EVENT_TRAIN_ENGINE] = CITY_MAP_ZOOM_LARGE,
  [CITY_MAP_EVENT_TRAIN_CAR] = CITY_MAP_ZOOM_LARGE,
  [CITY_MAP_EVENT_SUBWAY_ENGINE] = CITY_MAP_ZOOM_LARGE,
  [CITY_MAP_EVENT_SUBWAY_CAR] = CITY_CANVAS_MAP_ZOOM_MAGNIFIED,
  [CITY_MAP_EVENT_MAXIS_MAN] = CITY_MAP_ZOOM_LARGE,
};

static const u8 kCityMapEventHeadingSprite[CITY_HEADING_COUNT] = {
  1, 2, 3, 4, 3, 2, 1, 0
};

static const u8 kCityMapEventHeadingFlip[CITY_HEADING_COUNT] = {
  0, 0, 0, 0, 1, 1, 1, 0
};

static const u8 kCityMapEventCardinalSprite[CITY_LINE_DIRECTION_COUNT] = {
  0, 1, 1, 0
};

static const u8 kCityMapEventCardinalFlip[CITY_LINE_DIRECTION_COUNT] = {
  0, 0, 1, 1
};

static const u8 kCityMapEventXDivisor[CITY_CANVAS_MAP_ZOOM_COUNT] = {
  4, 2, 1, 1
};

static const u8 kCityMapEventYDivisor[CITY_CANVAS_MAP_ZOOM_COUNT] = {
  8, 4, 2, 2
};

static bool PrepareEventFootprints(MapSpriteFootprints *sprites, int capacity) {
  if (sprites->capacity >= capacity) return true;
  // Bounds and canonical records share one reusable allocation per map window.
  CityRect *bounds = calloc(capacity, sizeof(CityRect) + sizeof(CityMapEvent));
  if (!bounds) return false;
  free(sprites->event_bounds);
  sprites->event_bounds = bounds;
  sprites->events = (CityMapEvent *)(bounds + capacity);
  sprites->capacity = capacity;
  sprites->initialized = false;
  return true;
}

bool PrepareDrawingEventStorage(Drawing *drawing, int capacity) {
  if (drawing->vehicles.capacity < capacity) {
    VehicleMotion *slots = calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    free(drawing->vehicles.slots);
    drawing->vehicles.slots = slots;
    drawing->vehicles.capacity = capacity;
    drawing->vehicles.initialized = false;
  }
  if (drawing->tile_traversal.capacity < capacity) {
    TileIndex *cells = realloc(drawing->tile_traversal.event_cells, capacity * sizeof(*cells));
    if (!cells) return false;
    drawing->tile_traversal.event_cells = cells;
    drawing->tile_traversal.capacity = capacity;
  }
  if (!PrepareEventFootprints(&drawing->main_sprites, capacity)) return false;
  for (int i = 0; i < CITY_EXTRA_MAP_VIEW_COUNT; ++i)
    if (drawing->views[i].window_id && !PrepareEventFootprints(&drawing->views[i].sprites, capacity)) return false;
  return true;
}

void ReleaseDrawingEventStorage(Drawing *drawing) {
  free(drawing->vehicles.slots);
  drawing->vehicles = (DrawingVehicles){0};
  free(drawing->tile_traversal.event_cells);
  drawing->tile_traversal.event_cells = NULL;
  drawing->tile_traversal.capacity = 0;
  free(drawing->main_sprites.event_bounds);
  drawing->main_sprites = (MapSpriteFootprints){0};
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    free(drawing->views[index].sprites.event_bounds);
    drawing->views[index].sprites = (MapSpriteFootprints){0};
  }
}

void PublishCityMapEventDamage(const City *city, MapSpriteFootprints *sprites,
    CityCanvasVisiblePaint mark, void *context) {
  Drawing *drawing = city->drawing;
  if (!PrepareDrawingEventStorage(drawing, city->events.capacity)) return;
  int phase = city->event_runtime.animation_phase;
  int dx = drawing->map_state.origin_x - sprites->origin_x;
  int dy = drawing->map_state.origin_y - sprites->origin_y;
  for (int slot = CITY_EVENT_SLOT_FIRST; slot < city->events.capacity; ++slot) {
    CityMapEvent canonical = city->events.event[slot];
    canonical.hash_next = 0;
    canonical.coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
    CityMapEvent *event = &canonical;
    CityRect bounds = {0};
    bool random = event->type == CITY_MAP_EVENT_EXPLOSION || event->type == CITY_MAP_EVENT_MONSTER ||
        event->type == CITY_MAP_EVENT_TORNADO;
    bool rotor_changed = sprites->helicopter_frame != drawing->vehicles.helicopter_frame &&
        ((event->type == CITY_MAP_EVENT_HELICOPTER && event->state != CITY_HELICOPTER_ON_GROUND) ||
            event->type == CITY_MAP_EVENT_MAXIS_MAN);
    if (!random && event->type != CITY_MAP_EVENT_UNUSED && IsMapTileValid(city, event->tile))
      bounds = CityMapEventSpriteBounds(city, event->tile, drawing->default_map_view->zoom, slot);
    if (!sprites->initialized || sprites->animation_phase != phase || rotor_changed ||
        memcmp(event, &sprites->events[slot], sizeof(*event)) ||
        memcmp(&bounds, &sprites->event_bounds[slot], sizeof(bounds))) {
      CityRect previous = sprites->event_bounds[slot];
      if (previous.left < previous.right && previous.top < previous.bottom &&
          sprites->zoom == drawing->default_map_view->zoom) {
        previous.left += dx; previous.right += dx;
        previous.top += dy; previous.bottom += dy;
      }
      mark(previous, context);
      mark(bounds, context);
    }
    sprites->event_bounds[slot] = bounds;
    sprites->events[slot] = *event;
  }
  sprites->animation_phase = phase;
  sprites->helicopter_frame = drawing->vehicles.helicopter_frame;
  sprites->origin_x = drawing->map_state.origin_x;
  sprites->origin_y = drawing->map_state.origin_y;
  sprites->zoom = drawing->default_map_view->zoom;
  sprites->initialized = true;
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
void DrawCityMapAnimatedOverlay(const City *city, TileIndex tile, int zoom, int overlay) {
  Drawing *drawing = city->drawing;
  CityPoint camera = CityMapViewTilePoint(city, tile);
  const MapSpriteMetrics *catalog = drawing->map_state.sprite_metrics;
  int elevation_step = CityMapHeightStep(zoom);
  int half_width = 4 << zoom;
  int row_step = 2 << zoom;
  if (HasTileWater(&city->tile[tile]) &&
      overlay != CITY_OVERLAY_POLLUTION && overlay != CITY_OVERLAY_FLOOD) return;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  int sprite_id;
  switch (overlay) {
    case CITY_OVERLAY_POLLUTION: sprite_id = (CITY_MAP_SPRITES_PER_ZOOM * zoom + CITY_SPRITE_CATALOG_POLLUTION); break;
    case CITY_OVERLAY_FLOOD: sprite_id = (CITY_MAP_SPRITES_PER_ZOOM * zoom + CITY_SPRITE_CATALOG_FLOOD); break;
    case CITY_OVERLAY_RIOTS_SOUTH_EAST:
    case CITY_OVERLAY_RIOTS_NORTH_WEST:
      sprite_id = CITY_MAP_SPRITES_PER_ZOOM * zoom + CITY_SPRITE_CATALOG_RIOTS_FIRST +
          (NextCrtRandom(&drawing->animation.random.crt) & 1);
      break;
    case CITY_OVERLAY_FIRE:
      sprite_id = CITY_MAP_SPRITES_PER_ZOOM * zoom + CITY_SPRITE_CATALOG_FIRE_FIRST +
          (NextCrtRandom(&drawing->animation.random.crt) & 3);
      break;
    default: return;
  }
  const MapSpriteMetrics *sprite = &catalog[sprite_id];
  int sprite_y = drawing->map_state.origin_y + row_step * (camera.y + camera.x) -
      elevation_step * elevation - sprite->baseline;
  int sprite_x = drawing->map_state.origin_x + half_width * (camera.y - camera.x + 1) - (sprite->width >> 1);
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, (NextCrtRandom(&drawing->animation.random.crt) & 1));
}

// Unanimated map-event sprite for event types 7, 8, and 14.
static void DrawCityMapEventStaticSprite(const City *city, TileIndex tile, int zoom, int slot) {
  Drawing *drawing = city->drawing;
  CityPoint camera = CityMapViewTilePoint(city, tile);
  const MapSpriteMetrics *catalog = drawing->map_state.sprite_metrics;
  static const i16 kZoomSpriteBase[CITY_CANVAS_MAP_ZOOM_COUNT] = {
    [CITY_CANVAS_MAP_ZOOM_SMALL] = CITY_SPRITE_CATALOG_SMALL_BASE,
    [CITY_CANVAS_MAP_ZOOM_MEDIUM] = CITY_SPRITE_CATALOG_MEDIUM_BASE,
    [CITY_CANVAS_MAP_ZOOM_LARGE] = CITY_SPRITE_CATALOG_LARGE_BASE,
    [CITY_CANVAS_MAP_ZOOM_MAGNIFIED] = CITY_SPRITE_CATALOG_LARGE_BASE,
  };
  const CityMapEvent *event = &city->events.event[slot];
  int sprite_id = kZoomSpriteBase[zoom] + kCityMapEventBaseSprite[event->type];
  const MapSpriteMetrics *sprite = &catalog[sprite_id];
  int zoom_scale = 1 << zoom;
  int sprite_x = drawing->map_state.origin_x + (4 * (camera.y - camera.x) + 4) * zoom_scale - (sprite->width >> 1);
  int sprite_y = drawing->map_state.origin_y + ((2 * (camera.y + camera.x)) << zoom) -
      (CityMapHeightStep(zoom) * ReadCityLandHeight(&city->tile[tile])) - sprite->baseline;
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, 0);
}

// Randomized sprite for map-event type 15.
static void DrawCityMapEventRandomSprite(const City *city, TileIndex tile, int zoom) {
  Drawing *drawing = city->drawing;
  CityPoint camera = CityMapViewTilePoint(city, tile);
  const MapSpriteMetrics *catalog = drawing->map_state.sprite_metrics;
  if (drawing->map_state.events_suppressed != 0)
    return;
  int sprite_id = CITY_MAP_SPRITES_PER_ZOOM * zoom + kCityMapEventBaseSprite[CITY_MAP_EVENT_TORNADO] +
      NextCrtRandom(&drawing->animation.random.crt) % 3;
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  const MapSpriteMetrics *sprite = &catalog[sprite_id];
  int sprite_y = drawing->map_state.origin_y + (2 << zoom) * (camera.y + camera.x) -
      sprite->baseline - (CityMapHeightStep(zoom)) * elevation;
  int sprite_x = drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) - sprite->width;
  DrawCityMapSprite(drawing, sprite_id, sprite_x, sprite_y, (NextCrtRandom(&drawing->animation.random.crt) & 1));
}

// Draw the authored type-5 crawler as one registered sprite.
static void DrawCityMapEventStructure(const City *city, TileIndex tile, int zoom, int slot) {
  Drawing *drawing = city->drawing;
  if (drawing->map_state.events_suppressed) return;
  const CityMapEvent *event = &city->events.event[slot];
  CityPoint camera = CityMapViewTilePoint(city, tile);
  int direction = (event->direction + 6 * CityMapCameraRotation(drawing)) & 7;
  int pose = (direction >> 1) & 1;
  if (event->monster.lower_pose & 128) pose = NextCrtRandom(&drawing->animation.random.crt) & 1;
  int sprite_id = CITY_MAP_SPRITES_PER_ZOOM * zoom + CITY_SPRITE_CATALOG_CRAWLER_FIRST + pose;
  const MapSpriteMetrics *sprite = &drawing->map_state.sprite_metrics[sprite_id];
  int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
  int screen_x = drawing->map_state.origin_x + (4 << zoom) * (camera.y - camera.x + 1) - sprite->width / 2;
  int screen_y = drawing->map_state.origin_y + (2 << zoom) * (camera.x + camera.y) - sprite->baseline -
      CityMapHeightStep(zoom) * elevation - event->height * (2 << zoom);
  DrawCityMapSprite(drawing, sprite_id, screen_x, screen_y, direction >= 4);
}

// Dispatch the event slot stored in overlay IDs 201 through 240.
void DrawCityMapEventOverlay(const City *city, TileIndex tile, int zoom, int slot) {
  Drawing *drawing = city->drawing;
  CityPoint camera = CityMapViewTilePoint(city, tile);
  const MapSpriteMetrics *catalog = drawing->map_state.sprite_metrics;
  const CityMapEvent *event = &city->events.event[slot];
  if (!drawing->map_state.vehicles_enabled && IsCityMapVehicle(event->type)) return;
  if (kCityMapEventMinZoom[event->type] > zoom)
    return;
  if (event->tile != tile && event->type != CITY_MAP_EVENT_TRAIN_ENGINE &&
      event->type != CITY_MAP_EVENT_TRAIN_CAR) {
    return;
  }
  int direction = (event->direction + 6 * CityMapCameraRotation(city->drawing)) & 7;
  CityPoint target = ReadCityMapEventTarget(city, event);
  target = CityMapCameraVector(CityMapCameraRotation(city->drawing), target.x, target.y);
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
        sprite_base += kCityMapEventHeadingSprite[direction];
        if (event->type == CITY_MAP_EVENT_HELICOPTER) {
          int frame = event->state == CITY_HELICOPTER_ON_GROUND ? 0 : drawing->vehicles.helicopter_frame;
          sprite_base = CITY_SPRITE_CATALOG_HELICOPTER_ANIMATION_FIRST +
              kCityMapEventHeadingSprite[direction] * CITY_HELICOPTER_ROTOR_FRAMES + frame;
        }
        flip = kCityMapEventHeadingFlip[direction];
      } else if (event->type == CITY_MAP_EVENT_EXPLOSION) {
        // Explosion direction is an animation stage, independent of camera rotation.
        sprite_base += event->direction;
        drawing->animation.random.map = AdvanceCityMapSeed(drawing->animation.random.map);
        flip = drawing->animation.random.map & 1;
      } else if (event->type == CITY_MAP_EVENT_SAILBOAT && event->state != CITY_SAILBOAT_SAILING) {
        sprite_base = 379;
        flip = city->event_runtime.animation_phase & 1;
      } else {
        // Sailboats and the default artwork use four cardinal views, not eight-way headings.
        int facing = (event->direction + 3 * CityMapCameraRotation(drawing)) & 3;
        sprite_base += kCityMapEventCardinalSprite[facing];
        flip = kCityMapEventCardinalFlip[facing];
      }
      int sprite_id = CITY_MAP_SPRITES_PER_ZOOM * zoom + sprite_base;
      const MapSpriteMetrics *sprite = &catalog[sprite_id];
      int elevation = ReadCitySurfaceHeight(&city->tile[tile]);
      int sprite_y = drawing->map_state.origin_y + (2 << zoom) * (camera.y + camera.x) +
          (target.x + target.y) / (CITY_EVENT_OFFSET_SCALE * kCityMapEventYDivisor[zoom]) - sprite->baseline -
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
      DrawCityMapEventStaticSprite(city, tile, zoom, slot);
      return;
    case CITY_MAP_EVENT_TRAIN_ENGINE:
    case CITY_MAP_EVENT_TRAIN_CAR:
      DrawCityMapTrain(city, tile, zoom, slot);
      return;
    case CITY_MAP_EVENT_MONSTER:
      DrawCityMapEventStructure(city, tile, zoom, slot);
      return;
    case CITY_MAP_EVENT_SUBWAY_ENGINE:
    case CITY_MAP_EVENT_SUBWAY_CAR:
      return;
    case CITY_MAP_EVENT_TORNADO:
      DrawCityMapEventRandomSprite(city, tile, zoom);
      return;
    default:
      SetGameError(city->game, -2);
  }
}

// Read the aircraft's sprite bounds without drawing, changing the current camera or touching animation RNG.
CityRect CityAirplaneBounds(const City *city, int slot, CityMapCamera camera) {
  const Drawing *drawing = city->drawing;
  const CityMapEvent *event = &city->events.event[slot];
  if (camera.zoom < kCityMapEventMinZoom[CITY_MAP_EVENT_AIRPLANE]) return (CityRect){0};
  CityPoint position = ReadCityMapEventPosition(city, event);
  CityPoint point = CityMapCameraPoint(city, camera.rotation, position.x, position.y);
  CityPoint offset = ReadCityMapEventTarget(city, event);
  offset = CityMapCameraVector(camera.rotation, offset.x, offset.y);
  int direction = (event->direction + 6 * camera.rotation) & CITY_HEADING_MASK;
  int id = CITY_SPRITE_CATALOG_SMALL_BASE + CITY_MAP_SPRITES_PER_ZOOM * camera.zoom +
      kCityMapEventBaseSprite[CITY_MAP_EVENT_AIRPLANE] + kCityMapEventHeadingSprite[direction];
  const MapSpriteMetrics *sprite = &drawing->map_state.sprite_metrics[id];
  int x = camera.origin_x + (4 << camera.zoom) * (point.y - point.x + 1) - sprite->width / 2 +
      (offset.y - offset.x) / (CITY_EVENT_OFFSET_SCALE * kCityMapEventXDivisor[camera.zoom]);
  int y = camera.origin_y + (2 << camera.zoom) * (point.x + point.y) - sprite->baseline +
      (offset.x + offset.y) / (CITY_EVENT_OFFSET_SCALE * kCityMapEventYDivisor[camera.zoom]) -
      CityMapHeightStep(camera.zoom) * ReadCitySurfaceHeight(&city->tile[event->tile]) -
      event->height * (2 << camera.zoom);
  if (drawing->vehicles.enabled && slot < drawing->vehicles.capacity) {
    VehiclePosition3D interpolation = drawing->vehicles.slots[slot].offset;
    CityPoint delta = CityMapCameraVector(camera.rotation, interpolation.x, interpolation.y);
    int divisor = 4096 >> (camera.zoom + 2);
    x += (delta.y - delta.x) / divisor;
    y += ((delta.x + delta.y) / 2 - interpolation.z) / divisor;
  }
  return (CityRect){x, y, x + sprite->width, y + sprite->baseline};
}

// Collect the current deterministic sprite footprint through the same prepared drawing path.
CityRect CityMapEventSpriteBounds(const City *city, TileIndex tile, int zoom, int slot) {
  Drawing *drawing = city->drawing;
  CityRect bounds = {0};
  CityRect *previous = drawing->sprite_damage_bounds;
  drawing->sprite_damage_bounds = &bounds;
  DrawCityMapEventOverlay(city, tile, zoom, slot);
  drawing->sprite_damage_bounds = previous;
  return bounds;
}
