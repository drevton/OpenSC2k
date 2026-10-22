#include "drawing/maps/map_animation.h"

#include <stdlib.h>

#include "city/engine/city_types.h"
#include "city/events/events.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include "drawing/maps/map_view.h"
#include "drawing/state.h"
#include "game/errors.h"

// Drawing captures painter-order randomness and effect bounds once, then replays them for clipped repaints.
// Storage grows with visible effects and is reused between frames. Normal frames supply a City paint copy.

void ReleaseCityMapAnimationStorage(Drawing *drawing) {
  free(drawing->animation.records);
  drawing->animation = (DrawingAnimations){0};
  free(drawing->main_animations.bounds);
  drawing->main_animations = (CityMapAnimationFootprints){0};
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    free(drawing->views[index].animations.bounds);
    drawing->views[index].animations = (CityMapAnimationFootprints){0};
  }
}

bool IsCityMapOverlayAnimated(const City *city, int overlay) {
  if (overlay >= CITY_OVERLAY_POLLUTION && overlay < 256) return true;
  if (!IsCityEventOverlay(overlay)) return false;
  for (int slot = CityEventOverlaySlot(overlay); slot >= 0; slot = NextCityMapEventOnTile(city, slot)) {
    int type = city->events.event[slot].type;
    if (type == CITY_MAP_EVENT_EXPLOSION || type == CITY_MAP_EVENT_MONSTER || type == CITY_MAP_EVENT_TORNADO)
      return true;
  }
  return false;
}

static int CompareMapAnimations(const void *left, const void *right) {
  TileIndex a = ((const MapAnimation *)left)->tile, b = ((const MapAnimation *)right)->tile;
  return (a > b) - (a < b);
}

static const MapAnimation *FindMapAnimation(const Drawing *drawing, TileIndex tile) {
  int left = 0, right = drawing->animation.count;
  while (left < right) {
    int middle = left + ((right - left) >> 1);
    const MapAnimation *record = &drawing->animation.records[middle];
    if (record->tile == tile) return record;
    if (record->tile < tile) left = middle + 1;
    else right = middle;
  }
  return NULL;
}

CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile) {
  const MapAnimation *animation = FindMapAnimation(drawing, tile);
  return animation ? animation->bounds : (CityRect){0};
}

// Reuse peak visible-effect storage; its size is independent of the map area.
static bool ReserveMapAnimationStorage(Game *game, void **records, int *capacity, int count, size_t size) {
  if (count <= *capacity) return true;
  int grown = IntMax(count, IntMax(64, *capacity * 2));
  void *storage = realloc(*records, grown * size);
  if (!storage) {
    SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  *records = storage;
  *capacity = grown;
  return true;
}

// A bounds-only traversal records the original painter-order RNG state once.
// Every clipped repaint starts each animated overlay at that recorded state,
// avoiding dependence on how screen damage was split into rectangles.
void BeginCityMapAnimationCapture(Drawing *drawing) {
  drawing->animation.count = 0;
  drawing->animation.mode = ANIMATION_CAPTURE;
}

void EndCityMapAnimationCapture(const City *city, Drawing *drawing) {
  if (drawing->animation.count > 1)
    qsort(drawing->animation.records, drawing->animation.count, sizeof(MapAnimation), CompareMapAnimations);
  drawing->animation.random_after = city->random;
  drawing->animation.mode = ANIMATION_INACTIVE;
}

void ReplayCityMapAnimations(Drawing *drawing) {
  drawing->animation.mode = ANIMATION_REPLAY;
}

void FinishCityMapAnimations(City *city, Drawing *drawing) {
  drawing->animation.mode = ANIMATION_INACTIVE;
  city->random = drawing->animation.random_after;
}

bool BeginCityMapOverlayAnimation(City *city, Drawing *drawing, int x, int y, int overlay) {
  if (drawing->animation.mode == ANIMATION_INACTIVE || !IsCityMapOverlayAnimated(city, overlay)) return false;
  return BeginCityMapSpriteAnimation(city, drawing, MapTileXY(city, x, y));
}

bool BeginCityMapSpriteAnimation(City *city, Drawing *drawing, TileIndex tile) {
  if (drawing->animation.mode == ANIMATION_INACTIVE) return false;
  DrawingAnimations *animations = &drawing->animation;
  if (animations->mode == ANIMATION_CAPTURE) {
    MapAnimation *animation;
    if (animations->count && animations->records[animations->count - 1].tile == tile) {
      animation = &animations->records[animations->count - 1];
      animation->random = city->random;
    } else {
      void *records = animations->records;
      if (!ReserveMapAnimationStorage(city->game, &records, &animations->capacity,
          animations->count + 1, sizeof(MapAnimation))) return false;
      animations->records = records;
      animation = &animations->records[animations->count++];
      *animation = (MapAnimation){.tile = tile, .random = city->random};
    }
    animations->saved_bounds = drawing->sprite_damage_bounds;
    drawing->sprite_damage_bounds = &animation->bounds;
  } else {
    const MapAnimation *animation = FindMapAnimation(drawing, tile);
    if (!animation) return false;
    animations->saved_random = city->random;
    city->random = animation->random;
  }
  return true;
}

void EndCityMapSpriteAnimation(City *city, Drawing *drawing) {
  if (drawing->animation.mode == ANIMATION_CAPTURE)
    drawing->sprite_damage_bounds = drawing->animation.saved_bounds;
  else
    city->random = drawing->animation.saved_random;
}

void PublishCityMapAnimationDamage(City *city, const CityMapViewConfig *view,
    CityMapAnimationFootprints *previous, CityCanvasVisiblePaint mark, void *context) {
  Drawing *drawing = city->game->drawing;
  int dx = drawing->map_state.origin_x - previous->origin_x;
  int dy = drawing->map_state.origin_y - previous->origin_y;
  bool animated = city->settings.speed > CITY_SPEED_PAUSED;
  // Paused effects move with the retained map pixels. Keep their footprints
  // current for resume, but let scrolling repaint only newly exposed strips.
  for (int index = 0; animated && index < previous->count; ++index) {
    CityRect bounds = previous->bounds[index];
    if (previous->zoom == view->zoom) {
      bounds.left += dx; bounds.right += dx;
      bounds.top += dy; bounds.bottom += dy;
    }
    if (bounds.left < bounds.right && bounds.top < bounds.bottom) mark(bounds, context);
  }
  void *bounds_storage = previous->bounds;
  if (!ReserveMapAnimationStorage(city->game, &bounds_storage, &previous->capacity,
      drawing->animation.count, sizeof(CityRect))) return;
  previous->bounds = bounds_storage;
  for (int index = 0; index < drawing->animation.count; ++index) {
    CityRect bounds = drawing->animation.records[index].bounds;
    if (animated && bounds.left < bounds.right && bounds.top < bounds.bottom) mark(bounds, context);
    previous->bounds[index] = bounds;
  }
  previous->count = drawing->animation.count;
  previous->origin_x = drawing->map_state.origin_x;
  previous->origin_y = drawing->map_state.origin_y;
  previous->zoom = view->zoom;
}
