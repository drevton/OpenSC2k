// Retain map values and sprite heights, then project City changes into camera-space damage.
// Event interpolation and effect capture are owned by map_events.c and map_animation.c.

#include "city/maps/labels.h"
#include "city/maps/terrain.h"
#include "drawing/maps/map_damage.h"
#include "drawing/maps/map_labels.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_sprites.h"

#include <stdlib.h>
#include <string.h>

#include "assets/graphics.h"
#include "assets/state.h"
#include "city/events/events.h"
#include "city/fixes.h"
#include "city/maps/buildings.h"
#include "city/maps/map.h"
#include "city/state.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_events.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/state.h"

// Map Damage

enum {
  kSnapshotTileLayers = (1 << CITY_MAP_CHANGE_TILE) | (1 << CITY_MAP_CHANGE_FLAGS) | (1 << CITY_MAP_CHANGE_ZONE) |
      (1 << CITY_MAP_CHANGE_TERRAIN) | (1 << CITY_MAP_CHANGE_UNDERGROUND) | (1 << CITY_MAP_CHANGE_OVERLAY) |
      (1 << CITY_MAP_CHANGE_ALTITUDE),
  kSnapshotHalfLayers = (1 << CITY_MAP_CHANGE_POLLUTION) | (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY) |
      (1 << CITY_MAP_CHANGE_CRIME) | (1 << CITY_MAP_CHANGE_LAND_VALUE),
  kSnapshotQuarterLayers = (1 << CITY_MAP_CHANGE_DENSITY) | (1 << CITY_MAP_CHANGE_POLICE_COVERAGE) |
      (1 << CITY_MAP_CHANGE_FIRE_COVERAGE) | (1 << CITY_MAP_CHANGE_DENSITY_HISTORY),
};
_Static_assert((kSnapshotTileLayers | kSnapshotHalfLayers | kSnapshotQuarterLayers) == (1 << CITY_MAP_CHANGE_COUNT) - 1,
    "snapshot capture and comparison cover every map change layer");

// One conservative height per kind covers every footprint tile and camera rotation.
static int DrawingTileHeight(const City *city, TileIndex tile) {
  const CityResourceState *assets = city->game->resources;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return 0;
  if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || IsTileKindPlainTransport(kind)) return 4;
  return assets->kind_sprite_heights[kind];
}

bool PrepareCityMapDrawingSnapshot(const City *city, CityMapDrawingSnapshot *snapshot) {
  if (snapshot->width == city->map_width && snapshot->height == city->map_height && snapshot->storage)
    return true;
  size_t count = city->map_cell_count;
  CityMapDrawingValues *values = &snapshot->values;
  const struct { u8 **destination; size_t count; } planes[] = {
    {&values->tiles, count}, {&values->flags, count}, {&values->zones, count}, {&values->terrain, count},
    {&values->underground, count}, {&values->overlays, count},
    {&values->pollution, count >> 2}, {&values->traffic_density, count >> 2},
    {&values->crime, count >> 2}, {&values->land_value, count >> 2},
    {&values->density, count >> 4}, {&values->police_coverage, count >> 4},
    {&values->fire_coverage, count >> 4}, {&values->density_history, count >> 4},
    {&values->heights, count},
  };
  size_t altitude_bytes = count * sizeof(*values->altitude), bytes = altitude_bytes;
  for (size_t plane = 0; plane < sizeof(planes) / sizeof(planes[0]); ++plane)
    bytes += planes[plane].count;
  if (snapshot->cell_count != city->map_cell_count || !snapshot->storage) {
    void *storage = calloc(bytes, 1);
    if (!storage) return false;
    free(snapshot->storage);
    snapshot->storage = storage;
  }
  values->altitude = snapshot->storage;
  u8 *next = (u8 *)snapshot->storage + altitude_bytes;
  for (size_t plane = 0; plane < sizeof(planes) / sizeof(planes[0]); ++plane) {
    *planes[plane].destination = next;
    next += planes[plane].count;
  }
  snapshot->width = city->map_width;
  snapshot->height = city->map_height;
  snapshot->cell_count = city->map_cell_count;
  snapshot->valid = false;
  return true;
}

bool PrepareCityMapDrawingStorage(const City *city) {
  Drawing *drawing = city->drawing;
  bool resized = drawing->map_snapshot.width != city->map_width ||
      drawing->map_snapshot.height != city->map_height;
  if (!PrepareDrawingEventStorage(drawing, city->events.capacity) ||
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
  ReleaseDrawingEventStorage(drawing);
  free(drawing->map_snapshot.storage);
  drawing->map_snapshot = (CityMapDrawingSnapshot){0};
  ReleaseCityMapAnimationStorage(drawing);
}

// Compare only requested layers of a valid tile against the prepared snapshot. Source records are borrowed
// once per selected resolution; absent backing contributes zero, as it does during snapshot capture.
static int ChangedMapLayers(const City *city, const CityMapDrawingSnapshot *snapshot, TileIndex tile, int layers) {
  const CityMapDrawingValues *values = &snapshot->values;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  if (layers & kSnapshotTileLayers) {
    int previous = y * snapshot->width + x;
    const TileInfo *tp = city->tile ? &city->tile[tile] : NULL;
    if ((layers & (1 << CITY_MAP_CHANGE_TILE)) && values->tiles[previous] == (tp ? tp->kind : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_TILE);
    if ((layers & (1 << CITY_MAP_CHANGE_FLAGS)) &&
        (values->flags[previous] & ~TF_VISITED) == (tp ? tp->flags & ~TF_VISITED : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_FLAGS);
    if ((layers & (1 << CITY_MAP_CHANGE_ZONE)) && values->zones[previous] == (tp ? tp->zone : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_ZONE);
    if ((layers & (1 << CITY_MAP_CHANGE_TERRAIN)) && values->terrain[previous] == (tp ? tp->terrain : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_TERRAIN);
    if ((layers & (1 << CITY_MAP_CHANGE_UNDERGROUND)) &&
        values->underground[previous] == (tp ? tp->underground & CITY_UNDERGROUND_KIND_MASK : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_UNDERGROUND);
    if ((layers & (1 << CITY_MAP_CHANGE_OVERLAY)) && values->overlays[previous] == (tp ? tp->overlay : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_OVERLAY);
    if ((layers & (1 << CITY_MAP_CHANGE_ALTITUDE)) && values->altitude[previous] == (tp ? tp->altitude : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_ALTITUDE);
  }
  if (layers & kSnapshotHalfLayers) {
    int previous = (y / 2) * (snapshot->width / 2) + x / 2;
    int cell = (y / 2) * (city->map_width / 2) + x / 2;
    const CityHalfMapStatistics *stats = city->half ? &city->half[cell] : NULL;
    if ((layers & (1 << CITY_MAP_CHANGE_POLLUTION)) &&
        values->pollution[previous] == (stats ? stats->pollution : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_POLLUTION);
    if ((layers & (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY)) &&
        values->traffic_density[previous] == (stats ? stats->traffic_density : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY);
    if ((layers & (1 << CITY_MAP_CHANGE_CRIME)) && values->crime[previous] == (stats ? stats->crime : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_CRIME);
    if ((layers & (1 << CITY_MAP_CHANGE_LAND_VALUE)) &&
        values->land_value[previous] == (stats ? stats->land_value : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_LAND_VALUE);
  }
  if (layers & kSnapshotQuarterLayers) {
    int previous = (y / 4) * (snapshot->width / 4) + x / 4;
    int cell = (y / 4) * (city->map_width / 4) + x / 4;
    const CityQuarterMapStatistics *stats = city->quarter ? &city->quarter[cell] : NULL;
    if ((layers & (1 << CITY_MAP_CHANGE_DENSITY)) && values->density[previous] == (stats ? stats->density : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_DENSITY);
    if ((layers & (1 << CITY_MAP_CHANGE_POLICE_COVERAGE)) &&
        values->police_coverage[previous] == (stats ? stats->police_coverage : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_POLICE_COVERAGE);
    if ((layers & (1 << CITY_MAP_CHANGE_FIRE_COVERAGE)) &&
        values->fire_coverage[previous] == (stats ? stats->fire_coverage : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_FIRE_COVERAGE);
    if ((layers & (1 << CITY_MAP_CHANGE_DENSITY_HISTORY)) &&
        values->density_history[previous] == (stats ? stats->density_history : 0))
      layers &= ~(1 << CITY_MAP_CHANGE_DENSITY_HISTORY);
  }
  return layers;
}

void FilterDrawingMapChanges(const City *city, SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot) {
  if (!snapshot->valid || snapshot->city != city) changes->full = true;
}

// Capture selected layers in one pass per map resolution. The bounds already intersect the map; sources stay
// unchanged throughout this synchronous copy. Cache the source and destination bindings before writing byte planes.
static void StoreSnapshotLayers(const City *city, CityMapDrawingSnapshot *snapshot, CityRect area, int layers) {
  const CityMapDrawingValues values = snapshot->values;
  int width = city->map_width;
  const TileInfo *tiles = city->tile;
  const CityHalfMapStatistics *half = city->half;
  const CityQuarterMapStatistics *quarter = city->quarter;
  if (layers & kSnapshotTileLayers) {
    for (int y = area.top; y < area.bottom; ++y) {
      TileIndex row = y * width;
      for (int x = area.left; x < area.right; ++x) {
        TileIndex tile = row + x;
        const TileInfo *tp = tiles ? &tiles[tile] : NULL;
        if (layers & (1 << CITY_MAP_CHANGE_TILE)) values.tiles[tile] = tp ? tp->kind : 0;
        if (layers & (1 << CITY_MAP_CHANGE_FLAGS)) values.flags[tile] = tp ? tp->flags & ~TF_VISITED : 0;
        if (layers & (1 << CITY_MAP_CHANGE_ZONE)) values.zones[tile] = tp ? tp->zone : 0;
        if (layers & (1 << CITY_MAP_CHANGE_TERRAIN)) values.terrain[tile] = tp ? tp->terrain : 0;
        if (layers & (1 << CITY_MAP_CHANGE_UNDERGROUND))
          values.underground[tile] = tp ? tp->underground & CITY_UNDERGROUND_KIND_MASK : 0;
        if (layers & (1 << CITY_MAP_CHANGE_OVERLAY)) values.overlays[tile] = tp ? tp->overlay : 0;
        if (layers & (1 << CITY_MAP_CHANGE_ALTITUDE)) values.altitude[tile] = tp ? tp->altitude : 0;
      }
    }
  }
  if (layers & kSnapshotHalfLayers) {
    CityRect cells = {area.left / 2, area.top / 2, (area.right + 1) / 2, (area.bottom + 1) / 2};
    for (int y = cells.top; y < cells.bottom; ++y) {
      int row = y * (width / 2);
      for (int x = cells.left; x < cells.right; ++x) {
        int cell = row + x;
        const CityHalfMapStatistics *stats = half ? &half[cell] : NULL;
        if (layers & (1 << CITY_MAP_CHANGE_POLLUTION)) values.pollution[cell] = stats ? stats->pollution : 0;
        if (layers & (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY))
          values.traffic_density[cell] = stats ? stats->traffic_density : 0;
        if (layers & (1 << CITY_MAP_CHANGE_CRIME)) values.crime[cell] = stats ? stats->crime : 0;
        if (layers & (1 << CITY_MAP_CHANGE_LAND_VALUE)) values.land_value[cell] = stats ? stats->land_value : 0;
      }
    }
  }
  if (layers & kSnapshotQuarterLayers) {
    CityRect cells = {area.left / 4, area.top / 4, (area.right + 3) / 4, (area.bottom + 3) / 4};
    for (int y = cells.top; y < cells.bottom; ++y) {
      int row = y * (width / 4);
      for (int x = cells.left; x < cells.right; ++x) {
        int cell = row + x;
        const CityQuarterMapStatistics *stats = quarter ? &quarter[cell] : NULL;
        if (layers & (1 << CITY_MAP_CHANGE_DENSITY)) values.density[cell] = stats ? stats->density : 0;
        if (layers & (1 << CITY_MAP_CHANGE_POLICE_COVERAGE))
          values.police_coverage[cell] = stats ? stats->police_coverage : 0;
        if (layers & (1 << CITY_MAP_CHANGE_FIRE_COVERAGE))
          values.fire_coverage[cell] = stats ? stats->fire_coverage : 0;
        if (layers & (1 << CITY_MAP_CHANGE_DENSITY_HISTORY))
          values.density_history[cell] = stats ? stats->density_history : 0;
      }
    }
  }
}

static void StoreDrawingTileHeights(const City *city, CityMapDrawingSnapshot *snapshot, CityRect area) {
  for (int y = area.top; y < area.bottom; ++y)
    for (int x = area.left; x < area.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      snapshot->values.heights[tile] = DrawingTileHeight(city, tile);
    }
}

// Newly exposed cells capture live values once. Stable viewports advance only
// notified cells/layers, so an idle frame never scans the visible map again.
void UpdateDrawingMapSnapshot(const City *city, CityMapDrawingSnapshot *snapshot,
    int viewport, CityMapCamera region) {
  if (!snapshot->valid || snapshot->city != city)
    memset(snapshot->regions, 0, sizeof(snapshot->regions));
  bool moved = memcmp(&snapshot->regions[viewport], &region, sizeof(region)) != 0;
  const SimulationMapChanges *changes = &city->map_changes;
  int layers = moved || changes->full || changes->tiles_changed ?
      (1 << CITY_MAP_CHANGE_COUNT) - 1 : changes->invalidated_layers;
  if (layers) {
    CityRect area = VisibleCityMapTileBounds(city, region, (CityRect){0, 0, region.width, region.height});
    StoreSnapshotLayers(city, snapshot, area, layers);
    int height_layers = (1 << CITY_MAP_CHANGE_TILE) | (1 << CITY_MAP_CHANGE_ZONE) |
        (1 << CITY_MAP_CHANGE_TERRAIN) | (1 << CITY_MAP_CHANGE_FLAGS) | (1 << CITY_MAP_CHANGE_ALTITUDE);
    if (layers & height_layers) {
      CityMapCamera previous = snapshot->regions[viewport];
      bool changed = changes->full || changes->tiles_changed || (changes->invalidated_layers & height_layers);
      if (moved && !changed && previous.width > 0 && previous.height > 0) {
        CityRect retained = VisibleCityMapTileBounds(city, previous, (CityRect){0, 0, previous.width, previous.height});
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

// Match ReadCityWaterHeight: construction progress and bridge-head style are metadata, not water planes.
static int MaximumElevation(int altitude, int kind) {
  int land = altitude & CITY_ALTITUDE_LAND_MASK;
  int water = IsTileKindDryBuilding(kind) || IsTileKindBridgeHead(kind) ? land :
      (altitude >> CITY_ALTITUDE_WATER_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  int tunnel = (altitude >> CITY_ALTITUDE_TUNNEL_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  int height = IntMax(land, water);
  return IntMax(height, tunnel);
}

static int PreviousSurfaceElevation(const CityMapDrawingSnapshot *snapshot, TileIndex tile) {
  int altitude = snapshot->values.altitude[tile];
  int land = altitude & CITY_ALTITUDE_LAND_MASK;
  int kind = snapshot->values.tiles[tile];
  int water = IsTileKindDryBuilding(kind) || IsTileKindBridgeHead(kind) ? land :
      (altitude >> CITY_ALTITUDE_WATER_SHIFT) & CITY_ALTITUDE_LAND_MASK;
  return IntMax(land, water);
}

typedef struct {
  const CityMapDrawingSnapshot *snapshot;
  CityMapCamera camera;
  bool data_overlay;
  CityCanvasVisiblePaint mark;
  void *context;
} MapDamageProjection;

static bool ProjectTileHeightBounds(const City *city, const MapDamageProjection *projection,
    TileIndex tile, int screen_x, int screen_y, CityRect *bounds) {
  const CityMapDrawingSnapshot *snapshot = projection->snapshot;
  if (!snapshot || !(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || projection->data_overlay) return false;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind, previous = snapshot->values.tiles[tile];
  // These animated and suspended objects have independent extents outside a
  // tile's vertical column. Retain their existing conservative damage bounds.
  if (CityBuildingSmokeIndex(kind) >= 0 || CityBuildingSmokeIndex(previous) >= 0 ||
      kind == TK_WIND_POWER || previous == TK_WIND_POWER ||
      (tp->flags & TF_POWER_SERVICE) == TF_POWERABLE ||
      (snapshot->values.flags[tile] & TF_POWER_SERVICE) == TF_POWERABLE ||
      IsCityEventOverlay(tp->overlay) || IsCityEventOverlay(snapshot->values.overlays[tile]) ||
      (tp->overlay & CITY_TILE_EFFECT_MASK) >= CITY_TILE_POLLUTION_EFFECT ||
      (snapshot->values.overlays[tile] & CITY_TILE_EFFECT_MASK) >= CITY_TILE_POLLUTION_EFFECT)
    return false;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  if (geometry.surface_shape != CITY_TERRAIN_FLAT || tp->terrain != snapshot->values.terrain[tile]) return false;
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
  int scale = 1 << projection->camera.zoom;
  int top = IntMax(height + elevation * CITY_MAP_HEIGHT_UNIT, old_height + old_elevation * CITY_MAP_HEIGHT_UNIT);
  *bounds = (CityRect){screen_x, screen_y - top * scale,
      screen_x + 8 * scale, screen_y - lowest * CITY_MAP_HEIGHT_UNIT * scale + 1};
  return true;
}

static void ProjectChangedMapCell(const City *city, const MapDamageProjection *projection,
    TileIndex tile, int layers, bool forced) {
  const CityMapDrawingSnapshot *snapshot = projection->snapshot;
  const TileInfo *tp = &city->tile[tile];
  int scale = 1 << projection->camera.zoom;
  int origin_x = projection->camera.origin_x, origin_y = projection->camera.origin_y;
  int zoom = projection->camera.zoom, width = projection->camera.width, height = projection->camera.height;
  bool data_overlay = projection->data_overlay;
  CityCanvasVisiblePaint mark = projection->mark;
  void *context = projection->context;
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  int sprite = CityMapTileSpriteIndex(city, tile);
  int old_sprite = snapshot ? kCityMapKindSprites[snapshot->values.tiles[tile]] : sprite;
  // Moving cars publish their own damage; density alone affects only data maps.
  if (!data_overlay && !forced && layers == (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY)) return;
  int altitude = MaximumElevation(tp->altitude, tp->kind);
  int old_altitude = snapshot ? MaximumElevation(snapshot->values.altitude[tile], snapshot->values.tiles[tile]) :
      CITY_ALTITUDE_LAND_MASK;
  altitude = IntMax(altitude, old_altitude);
  CityPoint point = CityMapCameraPoint(city, projection->camera.rotation, x, y);
  int screen_x = origin_x + (point.y - point.x) * 4 * scale;
  int screen_y = origin_y + (point.x + point.y) * 2 * scale;
  CityRect rect;
  if (!ProjectTileHeightBounds(city, projection, tile, screen_x, screen_y, &rect)) {
    CityPoint current_extent = CityBuildingSpriteExtent(city,
        CityMapCameraSprite(city->game->resources, sprite, projection->camera.rotation), zoom);
    CityPoint old_extent = CityBuildingSpriteExtent(city,
        CityMapCameraSprite(city->game->resources, old_sprite, projection->camera.rotation), zoom);
    int sprite_width = IntMax(IntMax(current_extent.x, old_extent.x), 8 * scale);
    // Include slope faces, suspended networks and ground edges.
    int sprite_height = IntMax(IntMax(current_extent.y, old_extent.y), 32 * scale);
    if ((sprite >= CITY_MAP_TILE_SPRITE_SUSPENSION_BRIDGE_FIRST &&
        sprite <= CITY_MAP_TILE_SPRITE_RAIL_BRIDGE_LAST) ||
        (old_sprite >= CITY_MAP_TILE_SPRITE_SUSPENSION_BRIDGE_FIRST &&
            old_sprite <= CITY_MAP_TILE_SPRITE_RAIL_BRIDGE_LAST))
      sprite_height = IntMax(sprite_height, 80 * scale);
    rect = (CityRect){screen_x - 20 * scale,
        screen_y - altitude * CITY_MAP_HEIGHT_UNIT * scale - sprite_height - 20 * scale,
        screen_x + sprite_width + 20 * scale, screen_y + (sprite_width >> 2) + 20 * scale};
  }
  int sign = ReadCitySignOverlay(city, tile);
  if (city->drawing->map_state.label_display && sign) {
    // Sign edits invalidate before and after changing text/location, preserving both extents.
    const char *text = (const char *)ReadCityOverlayName(city, sign)->character;
    CityRect bounds = CitySignMapBounds(city, zoom,
        (CityPoint){screen_x, screen_y - altitude * CITY_MAP_HEIGHT_UNIT * scale}, text);
    rect = UnionCityRects(rect, bounds);
  }
  rect = IntersectCityRects(rect, (CityRect){0, 0, width, height});
  if (rect.left < rect.right && rect.top < rect.bottom) mark(rect, context);
}

void ProjectCityMapTileDamage(const City *city, TileIndex tile, CityMapCamera region,
    CityCanvasVisiblePaint mark, void *context) {
  Drawing *drawing = city->drawing;
  const CityMapDrawingSnapshot *snapshot = &drawing->map_snapshot;
  if (!snapshot->valid || snapshot->city != city || snapshot->width != city->map_width ||
      snapshot->height != city->map_height) snapshot = NULL;
  bool data = drawing->map_state.data_overlay_enabled && drawing->map_state.data_mode != CITY_DATA_MODE_NONE;
  MapDamageProjection projection = {snapshot, region, data, mark, context};
  if (!data) {
    ProjectChangedMapCell(city, &projection, tile, (1 << CITY_MAP_CHANGE_COUNT) - 1, true);
    // Cable endpoints depend only on this tile and their immediate neighbor's
    // height/support. Repaint those spans when either endpoint changes.
    {
      static const i8 kX[] = {-1, 0, 1, 0}, kY[] = {0, 1, 0, -1};
      for (int direction = 0; direction < 4; ++direction) {
        TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kX[direction], kY[direction]);
        if (neighbor == INVALID_TILE) continue;
        if (IsTileKindPowerLine(city->tile[neighbor].kind))
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

void ProjectSimulationMapDamage(const City *city, const SimulationMapChanges *changes,
    const CityMapDrawingSnapshot *snapshot,
    CityMapCamera region, bool data_overlay, CityCanvasVisiblePaint mark, void *context) {
  int width = region.width, height = region.height;
  if (changes->full) {
    mark((CityRect){0, 0, width, height}, context);
    return;
  }
  int visible_layers = data_overlay ? (1 << CITY_MAP_CHANGE_COUNT) - 1 :
      ((1 << CITY_MAP_CHANGE_POLLUTION) - 1) | (1 << CITY_MAP_CHANGE_TRAFFIC_DENSITY);
  MapDamageProjection projection = {snapshot, region, data_overlay, mark, context};
  int layers = changes->invalidated_layers;
  // Without immediate tile damage, deferred publishers discover tile edits from
  // observed layer differences. Native viewports already received rectangles.
  if (changes->tiles_changed && !city->drawing->tile_damage_enabled)
    layers |= (1 << CITY_MAP_CHANGE_COUNT) - 1;
  layers &= visible_layers;
  if (!layers) return;
  CityRect area = VisibleCityMapTileBounds(city, region, (CityRect){0, 0, width, height});
  for (int y = area.top; y < area.bottom; ++y)
    for (int x = area.left; x < area.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      int changed = ChangedMapLayers(city, snapshot, tile, layers);
      if (changed) ProjectChangedMapCell(city, &projection, tile, changed, false);
    }
}
