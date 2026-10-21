#include "drawing/maps/map_effects.h"
#include "city/maps/buildings.h"
#include "assets/graphics.h"
#include "city/engine/city_types.h"
#include "city/events/events.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include <stdlib.h>
#include <string.h>

#include "city/state.h"
#include "city/growth/growth.h"
#include "city/maps/layers.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"

// Map Damage

static int LayerScale(CityMapChangeLayer layer) {
  if (layer >= CITY_MAP_CHANGE_DENSITY)
    return 4;
  return layer >= CITY_MAP_CHANGE_POLLUTION ? 2 : 1;
}

static int CellValue(City *city, int x, int y, CityMapChangeLayer layer) {
  TileIndex tile = MapTileXY(city, x, y);
  switch (layer) {
  case CITY_MAP_CHANGE_TILE:
    return city->tile != NULL ? ReadTile(city, tile) : 0;
  case CITY_MAP_CHANGE_FLAGS:
    return city->tile != NULL ? city->tile[tile].flags & ~TF_VISITED : 0;
  case CITY_MAP_CHANGE_ZONE:
    return city->tile != NULL ? city->tile[tile].zone : 0;
  case CITY_MAP_CHANGE_TERRAIN:
    return city->tile != NULL ? city->tile[tile].terrain : 0;
  case CITY_MAP_CHANGE_UNDERGROUND:
    return city->tile != NULL ? ReadUnderground(city, tile) : 0;
  case CITY_MAP_CHANGE_OVERLAY:
    return city->tile != NULL ? city->tile[tile].overlay : 0;
  case CITY_MAP_CHANGE_ALTITUDE:
    return city->tile != NULL ? ReadAltitudeWord(city, tile) : 0;
  case CITY_MAP_CHANGE_POLLUTION:
    return city->half != NULL ? ReadPollution(city, tile) : 0;
  case CITY_MAP_CHANGE_TRAFFIC_DENSITY:
    return city->half != NULL ? ReadTrafficDensity(city, tile) : 0;
  case CITY_MAP_CHANGE_CRIME:
    return city->half != NULL ? ReadCrime(city, tile) : 0;
  case CITY_MAP_CHANGE_LAND_VALUE:
    return city->half != NULL ? ReadLandValue(city, tile) : 0;
  case CITY_MAP_CHANGE_DENSITY:
    return city->quarter != NULL ? ReadDensity(city, tile) : 0;
  case CITY_MAP_CHANGE_POLICE_COVERAGE:
    return city->quarter != NULL ? ReadPoliceCoverage(city, tile) : 0;
  case CITY_MAP_CHANGE_FIRE_COVERAGE:
    return city->quarter != NULL ? ReadFireCoverage(city, tile) : 0;
  case CITY_MAP_CHANGE_DENSITY_HISTORY:
    return city->quarter != NULL ? ReadDensityHistory(city, tile) : 0;
  default:
    return 0;
  }
}

static u8 *PreviousByteValues(const CityMapDrawingSnapshot *snapshot, CityMapChangeLayer layer) {
  const CityMapDrawingValues *values = &snapshot->values;
  switch (layer) {
  case CITY_MAP_CHANGE_TILE:
    return values->tiles;
  case CITY_MAP_CHANGE_FLAGS:
    return values->flags;
  case CITY_MAP_CHANGE_ZONE:
    return values->zones;
  case CITY_MAP_CHANGE_TERRAIN:
    return values->terrain;
  case CITY_MAP_CHANGE_UNDERGROUND:
    return values->underground;
  case CITY_MAP_CHANGE_OVERLAY:
    return values->overlays;
  case CITY_MAP_CHANGE_POLLUTION:
    return values->pollution;
  case CITY_MAP_CHANGE_TRAFFIC_DENSITY:
    return values->traffic_density;
  case CITY_MAP_CHANGE_CRIME:
    return values->crime;
  case CITY_MAP_CHANGE_LAND_VALUE:
    return values->land_value;
  case CITY_MAP_CHANGE_DENSITY:
    return values->density;
  case CITY_MAP_CHANGE_POLICE_COVERAGE:
    return values->police_coverage;
  case CITY_MAP_CHANGE_FIRE_COVERAGE:
    return values->fire_coverage;
  case CITY_MAP_CHANGE_DENSITY_HISTORY:
    return values->density_history;
  default:
    __builtin_unreachable(); // Altitude uses word storage below.
  }
}

// One conservative height per kind covers every footprint tile and camera rotation.
static int DrawingTileHeight(const City *city, TileIndex tile) {
  const CityResourceState *assets = city->game->resources;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) return 0;
  if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || IsTileKindPlainTransport(kind)) return 4;
  return assets->kind_sprite_heights[kind];
}

bool PrepareCityMapDrawingSnapshot(City *city, CityMapDrawingSnapshot *snapshot) {
  if (snapshot->width == city->map_width && snapshot->height == city->map_height && snapshot->storage)
    return true;
  size_t count = city->map_cell_count;
  if (snapshot->cell_count != city->map_cell_count || !snapshot->storage) {
    void *storage = calloc(count * 9 + count + (count >> 2), 1);
    if (!storage) return false;
    free(snapshot->storage);
    snapshot->storage = storage;
  }
  CityMapDrawingValues *values = &snapshot->values;
  values->altitude = snapshot->storage;
  u8 *next = (u8 *)snapshot->storage + 2 * count;
  u8 **layers[] = {&values->tiles, &values->flags, &values->zones, &values->terrain,
      &values->underground, &values->overlays, &values->pollution, &values->traffic_density, &values->crime,
      &values->land_value, &values->density, &values->police_coverage, &values->fire_coverage, &values->density_history};
  for (int layer = 0; layer < 14; ++layer) {
    *layers[layer] = next;
    next += layer < 6 ? count : layer < 10 ? count >> 2 : count >> 4;
  }
  values->heights = next;
  snapshot->width = city->map_width;
  snapshot->height = city->map_height;
  snapshot->cell_count = city->map_cell_count;
  snapshot->valid = false;
  return true;
}

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

bool PrepareCityEventDrawingStorage(const City *city) {
  Drawing *drawing = city->game->drawing;
  int capacity = city->events.capacity;
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

bool PrepareCityMapDrawingStorage(City *city) {
  Drawing *drawing = city->game->drawing;
  bool resized = drawing->map_snapshot.width != city->map_width ||
      drawing->map_snapshot.height != city->map_height;
  if (!PrepareCityEventDrawingStorage(city) ||
      !PrepareCityMapDrawingSnapshot(city, &drawing->map_snapshot)) return false;
  if (resized) {
    drawing->main_cache.valid = false;
    drawing->animation.count = 0;
    drawing->main_animations.count = 0;
    for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
      drawing->views[index].cache.valid = false;
      drawing->views[index].animations.count = 0;
    }
  }
  return true;
}

void ReleaseCityMapDrawingStorage(Drawing *drawing) {
  free(drawing->vehicles.slots);
  drawing->vehicles = (DrawingVehicles){0};
  free(drawing->tile_traversal.event_cells);
  drawing->tile_traversal.event_cells = NULL;
  drawing->tile_traversal.capacity = 0;
  free(drawing->main_sprites.event_bounds);
  drawing->main_sprites = (MapSpriteFootprints){0};
  free(drawing->map_snapshot.storage);
  drawing->map_snapshot = (CityMapDrawingSnapshot){0};
  free(drawing->animation.records);
  drawing->animation = (DrawingAnimations){0};
  free(drawing->main_animations.bounds);
  drawing->main_animations = (CityMapAnimationFootprints){0};
  for (int index = 0; index < CITY_EXTRA_MAP_VIEW_COUNT; ++index) {
    free(drawing->views[index].sprites.event_bounds);
    drawing->views[index].sprites = (MapSpriteFootprints){0};
    free(drawing->views[index].animations.bounds);
    drawing->views[index].animations = (CityMapAnimationFootprints){0};
  }
}

static int PreviousCellValue(const CityMapDrawingSnapshot *snapshot, int x, int y, CityMapChangeLayer layer) {
  int scale = LayerScale(layer);
  int index = (y / scale) * (snapshot->width / scale) + x / scale;
  if (layer == CITY_MAP_CHANGE_ALTITUDE) return snapshot->values.altitude[index];
  int value = PreviousByteValues(snapshot, layer)[index];
  return layer == CITY_MAP_CHANGE_FLAGS ? value & ~TF_VISITED : value;
}

static int ChangedMapLayers(City *city, const CityMapDrawingSnapshot *snapshot, TileIndex tile, int layers) {
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  for (int layer = 0; layer < CITY_MAP_CHANGE_COUNT; ++layer)
    if ((layers & (1 << layer)) && PreviousCellValue(snapshot, x, y, layer) == CellValue(city, x, y, layer))
      layers &= ~(1 << layer);
  return layers;
}

void FilterDrawingMapChanges(City *city, SimulationMapChanges *changes, CityMapDrawingSnapshot *snapshot) {
  if (!snapshot->valid || snapshot->city != city) changes->full = true;
}

static void StoreSnapshotCell(City *city, CityMapDrawingSnapshot *snapshot, int x, int y, int layer) {
  int scale = LayerScale(layer), stride = city->map_width / scale;
  int index = (y / scale) * stride + x / scale;
  int value = CellValue(city, x, y, layer);
  if (layer == CITY_MAP_CHANGE_ALTITUDE) snapshot->values.altitude[index] = value;
  else PreviousByteValues(snapshot, layer)[index] = value;
}

static void StoreDrawingTileHeights(City *city, CityMapDrawingSnapshot *snapshot, CityRect area) {
  for (int y = area.top; y < area.bottom; ++y)
    for (int x = area.left; x < area.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      snapshot->values.heights[tile] = DrawingTileHeight(city, tile);
    }
}

// Newly exposed cells capture live values once. Stable viewports advance only
// notified cells/layers, so an idle frame never scans the visible map again.
void UpdateDrawingMapSnapshot(City *city, CityMapDrawingSnapshot *snapshot,
    int viewport, CityMapCamera region) {
  if (!snapshot->valid || snapshot->city != city)
    memset(snapshot->regions, 0, sizeof(snapshot->regions));
  bool moved = memcmp(&snapshot->regions[viewport], &region, sizeof(region)) != 0;
  const SimulationMapChanges *changes = &city->map_changes;
  int layers = moved || changes->full || changes->tiles_changed ?
      (1 << CITY_MAP_CHANGE_COUNT) - 1 : changes->invalidated_layers;
  if (layers) {
    CityRect area = VisibleCityMapTileBounds(city, region.origin_x, region.origin_y, region.zoom, region.rotation,
        (CityRect){0, 0, region.width, region.height});
    for (int layer = 0; layer < CITY_MAP_CHANGE_COUNT; ++layer) {
      if (!(layers & (1 << layer))) continue;
      int scale = LayerScale(layer);
      for (int y = area.top / scale; y < (area.bottom + scale - 1) / scale; ++y)
        for (int x = area.left / scale; x < (area.right + scale - 1) / scale; ++x)
          StoreSnapshotCell(city, snapshot, x * scale, y * scale, layer);
    }
    int height_layers = (1 << CITY_MAP_CHANGE_TILE) | (1 << CITY_MAP_CHANGE_ZONE) |
        (1 << CITY_MAP_CHANGE_TERRAIN) | (1 << CITY_MAP_CHANGE_FLAGS) | (1 << CITY_MAP_CHANGE_ALTITUDE);
    if (layers & height_layers) {
      CityMapCamera previous = snapshot->regions[viewport];
      bool changed = changes->full || changes->tiles_changed || (changes->invalidated_layers & height_layers);
      if (moved && !changed && previous.width > 0 && previous.height > 0) {
        CityRect retained = VisibleCityMapTileBounds(city, previous.origin_x, previous.origin_y,
            previous.zoom, previous.rotation, (CityRect){0, 0, previous.width, previous.height});
        retained = IntersectCityRects(area, retained);
        if (retained.left < retained.right && retained.top < retained.bottom) {
          StoreDrawingTileHeights(city, snapshot, (CityRect){area.left, area.top, area.right, retained.top});
          StoreDrawingTileHeights(city, snapshot, (CityRect){area.left, retained.bottom, area.right, area.bottom});
          StoreDrawingTileHeights(city, snapshot, (CityRect){area.left, retained.top, retained.left, retained.bottom});
          StoreDrawingTileHeights(city, snapshot,
              (CityRect){retained.right, retained.top, area.right, retained.bottom});
        } else StoreDrawingTileHeights(city, snapshot, area);
      } else StoreDrawingTileHeights(city, snapshot, area);
    }
  }
  snapshot->regions[viewport] = region;
  snapshot->city = city;
  snapshot->valid = true;
}

static int MaximumElevation(int altitude, bool building_stage) {
  int land = altitude & CITY_ALTITUDE_LAND_MASK;
  int water = building_stage ? land : (altitude >> CITY_ALTITUDE_WATER_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  int tunnel = (altitude >> CITY_ALTITUDE_TUNNEL_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  int height = IntMax(land, water);
  return IntMax(height, tunnel);
}

static int PreviousSurfaceElevation(const CityMapDrawingSnapshot *snapshot, TileIndex tile) {
  TileInfo record = {.kind = snapshot->values.tiles[tile], .flags = snapshot->values.flags[tile]};
  int altitude = snapshot->values.altitude[tile];
  int land = altitude & CITY_ALTITUDE_LAND_MASK;
  int water = CityTileCanStoreBuildingStage(&record) ? land :
      (altitude >> CITY_ALTITUDE_WATER_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  return IntMax(land, water);
}

typedef struct {
  const CityMapDrawingSnapshot *snapshot;
  int origin_x, origin_y, zoom, width, height, rotation;
  bool data_overlay;
  CityCanvasVisiblePaint mark;
  void *context;
} MapDamageProjection;

static bool ProjectTileHeightBounds(const City *city, const MapDamageProjection *projection,
    TileIndex tile, int screen_x, int screen_y, CityRect *bounds) {
  const CityMapDrawingSnapshot *snapshot = projection->snapshot;
  if (!snapshot || !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || projection->data_overlay) return false;
  const TileInfo *record = &city->tile[tile];
  int kind = record->kind, previous = snapshot->values.tiles[tile];
  // These animated and suspended objects have independent extents outside a
  // tile's vertical column. Retain their existing conservative damage bounds.
  if (CityBuildingSmokeIndex(kind) >= 0 || CityBuildingSmokeIndex(previous) >= 0 ||
      kind == TK_WIND_POWER || previous == TK_WIND_POWER ||
      (record->flags & TF_POWER_SERVICE) == TF_POWERABLE ||
      (snapshot->values.flags[tile] & TF_POWER_SERVICE) == TF_POWERABLE ||
      IsCityEventOverlay(record->overlay) || IsCityEventOverlay(snapshot->values.overlays[tile]) ||
      (record->overlay & CITY_TILE_EFFECT_MASK) >= CITY_TILE_POLLUTION_EFFECT ||
      (snapshot->values.overlays[tile] & CITY_TILE_EFFECT_MASK) >= CITY_TILE_POLLUTION_EFFECT)
    return false;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (geometry.surface_shape != CITY_TERRAIN_FLAT || record->terrain != snapshot->values.terrain[tile]) return false;
  int height = DrawingTileHeight(city, tile);
  int old_height = snapshot->values.heights[tile];
  if (!height || !old_height) return false;
  int elevation = geometry.surface_height, old_elevation = PreviousSurfaceElevation(snapshot, tile);
  int lowest = IntMin(elevation, old_elevation);
  // Flat ground can still expose a vertical face next to lower terrain.
  static const i8 kX[] = {-1, 0, 1, 0}, kY[] = {0, 1, 0, -1};
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kX[direction], kY[direction]);
    if (neighbor == INVALID_TILE) continue;
    lowest = IntMin(lowest, ReadCitySurfaceHeight(&city->tile[neighbor]));
    lowest = IntMin(lowest, PreviousSurfaceElevation(snapshot, neighbor));
  }
  int scale = 1 << projection->zoom;
  int top = IntMax(height + elevation * CITY_MAP_HEIGHT_UNIT, old_height + old_elevation * CITY_MAP_HEIGHT_UNIT);
  *bounds = (CityRect){screen_x, screen_y - top * scale,
      screen_x + 8 * scale, screen_y - lowest * CITY_MAP_HEIGHT_UNIT * scale + 1};
  return true;
}

static void ProjectChangedMapCell(City *city, const MapDamageProjection *projection,
    TileIndex cell, int layers, bool forced) {
  const CityMapDrawingSnapshot *snapshot = projection->snapshot;
  int scale = 1 << projection->zoom;
  int origin_x = projection->origin_x, origin_y = projection->origin_y;
  int zoom = projection->zoom, width = projection->width, height = projection->height;
  bool data_overlay = projection->data_overlay;
  CityCanvasVisiblePaint mark = projection->mark;
  void *context = projection->context;
  int index = cell, x = MapTileX(city, cell), y = MapTileY(city, cell);
  int tile = CityMapTileSpriteIndex(city, cell);
  int old_tile = snapshot ? kCityMapKindSprites[snapshot->values.tiles[index]] : tile;
  // Moving cars publish their own damage; density alone affects only data maps.
  if (!data_overlay && !forced && layers == (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY)) return;
  int altitude = MaximumElevation(ReadAltitudeWord(city, cell), CityTileCanStoreBuildingStage(&city->tile[cell]));
  bool old_stage = false;
  if (snapshot) {
    TileInfo previous = {.kind = snapshot->values.tiles[index], .flags = snapshot->values.flags[index]};
    old_stage = CityTileCanStoreBuildingStage(&previous);
  }
  int old_altitude = snapshot ? MaximumElevation(snapshot->values.altitude[index], old_stage) : CITY_ALTITUDE_LAND_MASK;
  altitude = IntMax(altitude, old_altitude);
  CityPoint point = CityMapCameraPoint(city, projection->rotation, x, y);
  int screen_x = origin_x + (point.y - point.x) * 4 * scale;
  int screen_y = origin_y + (point.x + point.y) * 2 * scale;
  CityRect rect;
  if (!ProjectTileHeightBounds(city, projection, cell, screen_x, screen_y, &rect)) {
    CityPoint current_extent = CityBuildingSpriteExtent(city,
        CityMapCameraSprite(city->game->resources, tile, projection->rotation), zoom);
    CityPoint old_extent = CityBuildingSpriteExtent(city,
        CityMapCameraSprite(city->game->resources, old_tile, projection->rotation), zoom);
    int sprite_width = IntMax(IntMax(current_extent.x, old_extent.x), 8 * scale);
    // Include slope faces, suspended networks and ground edges.
    int sprite_height = IntMax(IntMax(current_extent.y, old_extent.y), 32 * scale);
    if ((tile >= CITY_MAP_TILE_SPRITE_SUSPENSION_BRIDGE_FIRST && tile <= CITY_MAP_TILE_SPRITE_RAIL_BRIDGE_LAST) ||
        (old_tile >= CITY_MAP_TILE_SPRITE_SUSPENSION_BRIDGE_FIRST && old_tile <= CITY_MAP_TILE_SPRITE_RAIL_BRIDGE_LAST))
      sprite_height = IntMax(sprite_height, 80 * scale);
    rect = (CityRect){screen_x - 20 * scale,
        screen_y - altitude * CITY_MAP_HEIGHT_UNIT * scale - sprite_height - 20 * scale,
        screen_x + sprite_width + 20 * scale, screen_y + (sprite_width >> 2) + 20 * scale};
  }
  int sign = ReadCitySignOverlay(city, cell);
  if (city->game->drawing->map_state.label_display && sign) {
    // Sign edits invalidate before and after changing text/location, preserving both extents.
    const char *text = (const char *)ReadCityOverlayName(city, sign)->character;
    CityRect bounds = CitySignMapBounds(city, zoom, screen_x,
        screen_y - altitude * CITY_MAP_HEIGHT_UNIT * scale, text);
    rect = UnionCityRects(rect, bounds);
  }
  rect = IntersectCityRects(rect, (CityRect){0, 0, width, height});
  if (rect.left < rect.right && rect.top < rect.bottom) mark(rect, context);
}

void ProjectCityMapTileDamage(City *city, TileIndex tile, CityMapCamera region,
    CityCanvasVisiblePaint mark, void *context) {
  Drawing *drawing = city->game->drawing;
  const CityMapDrawingSnapshot *snapshot = &drawing->map_snapshot;
  if (!snapshot->valid || snapshot->city != city || snapshot->width != city->map_width ||
      snapshot->height != city->map_height) snapshot = NULL;
  bool data = drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE;
  MapDamageProjection projection = {snapshot, region.origin_x, region.origin_y, region.zoom,
      region.width, region.height, region.rotation, data, mark, context};
  if (!data) {
    ProjectChangedMapCell(city, &projection, tile, (1 << CITY_MAP_CHANGE_COUNT) - 1, true);
    // Cable endpoints depend only on this tile and their immediate neighbor's
    // height/support. Repaint those spans when either endpoint changes.
    {
      static const i8 kX[] = {-1, 0, 1, 0}, kY[] = {0, 1, 0, -1};
      for (int direction = 0; direction < 4; ++direction) {
        TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kX[direction], kY[direction]);
        if (neighbor == INVALID_TILE) continue;
        if (IsTileKindPowerLine(ReadTile(city, neighbor)))
          ProjectChangedMapCell(city, &projection, neighbor, (1 << CITY_MAP_CHANGE_COUNT) - 1, true);
      }
    }
    return;
  }
  // Coarse data layers share a 4-by-4 cell. Their entire visible footprint changes.
  int x = MapTileX(city, tile) & ~3, y = MapTileY(city, tile) & ~3;
  for (int dy = 0; dy < 4; ++dy)
    for (int dx = 0; dx < 4; ++dx)
      ProjectChangedMapCell(city, &projection, MapTileXY(city, x + dx, y + dy),
          (1 << CITY_MAP_CHANGE_COUNT) - 1, true);
}

void ProjectSimulationMapDamage(City *city, const SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot,
    CityMapCamera region, bool data_overlay, CityCanvasVisiblePaint mark, void *context) {
  int origin_x = region.origin_x, origin_y = region.origin_y, zoom = region.zoom;
  int width = region.width, height = region.height;
  if (changes->full) {
    mark((CityRect){0, 0, width, height}, context);
    return;
  }
  int visible_layers = data_overlay ? (1 << CITY_MAP_CHANGE_COUNT) - 1 :
      ((1 << CITY_MAP_CHANGE_POLLUTION) - 1) | (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY);
  MapDamageProjection projection = {snapshot, origin_x, origin_y, zoom, width, height, region.rotation,
      data_overlay, mark, context};
  int layers = changes->invalidated_layers;
  // Without immediate tile damage, deferred publishers discover tile edits from
  // observed layer differences. Native viewports already received rectangles.
  if (changes->tiles_changed && !city->game->drawing->tile_damage_enabled)
    layers |= (1 << CITY_MAP_CHANGE_COUNT) - 1;
  layers &= visible_layers;
  if (!layers) return;
  CityRect area = VisibleCityMapTileBounds(city, origin_x, origin_y, zoom, region.rotation,
      (CityRect){0, 0, width, height});
  for (int y = area.top; y < area.bottom; ++y)
    for (int x = area.left; x < area.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      int changed = ChangedMapLayers(city, snapshot, tile, layers);
      if (changed) ProjectChangedMapCell(city, &projection, tile, changed, false);
    }
}

// Map Animation

// A bounds-only traversal records the original painter-order RNG state once.
// Every clipped repaint starts each animated overlay at that recorded state,
// avoiding dependence on how screen damage was split into rectangles.
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

static MapAnimation *FindMapAnimation(const Drawing *drawing, TileIndex tile) {
  int left = 0, right = drawing->animation.count;
  while (left < right) {
    int middle = left + ((right - left) >> 1);
    MapAnimation *record = &drawing->animation.records[middle];
    if (record->tile == tile) return record;
    if (record->tile < tile) left = middle + 1;
    else right = middle;
  }
  return NULL;
}

CityRect CapturedCityMapAnimationBounds(const Drawing *drawing, TileIndex tile) {
  MapAnimation *animation = FindMapAnimation(drawing, tile);
  return animation ? animation->bounds : (CityRect){0};
}

// Reuse peak visible-effect storage; its size is independent of the map area.
static bool ReserveMapAnimationStorage(City *city, void **records, int *capacity, int count, size_t size) {
  if (count <= *capacity) return true;
  int grown = IntMax(count, IntMax(64, *capacity * 2));
  void *storage = realloc(*records, grown * size);
  if (!storage) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    city->game->execution_failed = true;
    return false;
  }
  *records = storage;
  *capacity = grown;
  return true;
}

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
      if (!ReserveMapAnimationStorage(city, &records, &animations->capacity,
          animations->count + 1, sizeof(MapAnimation))) return false;
      animations->records = records;
      animation = &animations->records[animations->count++];
      *animation = (MapAnimation){.tile = tile, .random = city->random};
    }
    animations->saved_bounds = drawing->sprite_damage_bounds;
    drawing->sprite_damage_bounds = &animation->bounds;
  } else {
    MapAnimation *animation = FindMapAnimation(drawing, tile);
    if (!animation) return false;
    animations->saved_random = city->random;
    city->random = animation->random;
  }
  return true;
}

void EndCityMapOverlayAnimation(City *city, Drawing *drawing) {
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
  if (!ReserveMapAnimationStorage(city, &bounds_storage, &previous->capacity,
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
