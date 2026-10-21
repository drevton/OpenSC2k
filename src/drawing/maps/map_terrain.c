#include "city/maps/layers.h"
#include "city/maps/buildings.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/maps/water.h"
#include "city/maps/ground.h"
#include "city/maps/module.h"
#include "drawing/maps/map_terrain.h"
#include "city/maps/trees.h"
#include "assets/graphics.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "city/saveload/map_format.h"
#include "city/infrastructure/infrastructure.h"
#include "city/tools/tools.h"
#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"

bool IsCityMapGroundPass(const Drawing *drawing) {
  return drawing->map_depth.phase != MAP_DEPTH_OBJECTS;
}

// Original diagnostic layout is selected at runtime, independently of the build.
static bool NativeMapArtworkEnabled(const City *city) {
  return (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE));
}

// Terrain faces join cells inside the map; the outside boundary has no wall.
// The prepared sprite's left and right halves face x+1 and y+1 respectively.
enum { kTerrainFaceLeft = 1, kTerrainFaceRight = 2, kTerrainFaceBoth = 3 };

// A matching foreground slope covers the shared edge, but its bitmap does not
// cover a wall extending below that edge. Such a wall leaks at the map border.
int CityMapSlopeFaceSides(const City *city, TileIndex tile, int shape, int height) {
  static const u8 kCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15, 15, 0};
  if (shape == CITY_TERRAIN_FLAT) return 0;
  int sides = 0;
  int corners = kCorners[shape];
  for (int side = 0; side < 2; ++side) {
    TileIndex neighbor = CityMapViewNeighbor(city, tile, !side, side);
    if (neighbor == INVALID_TILE) continue;
    CityTileGeometry next = ReadCityMapViewGeometry(city, neighbor);
    int adjacent = kCorners[next.surface_shape];
    int near = side ? 1 : 3, far = side ? 3 : 1;
    if (height + ((corners >> near) & 1) > next.surface_height + (adjacent & 1) ||
        height + ((corners >> 2) & 1) > next.surface_height + ((adjacent >> far) & 1))
      sides |= side ? kTerrainFaceRight : kTerrainFaceLeft;
  }
  return sides;
}

void DrawCityMapTerrainFace(City *city, TileIndex tile, int zoom, int sprite, int screen_x, int ground_y,
    int sides) {
  Drawing *drawing = city->game->drawing;
  if (!IsCityMapGroundPass(drawing)) return;
  CityPoint point = CityMapViewTilePoint(city, tile), size = CityMapCameraSize(city, CityMapCameraRotation(city->game->drawing));
  if (point.x == size.x - 1) sides &= ~kTerrainFaceLeft;
  if (point.y == size.y - 1) sides &= ~kTerrainFaceRight;
  if (!sides) return;
  CityRect clip = drawing->map_target.canvas.clip;
  if (sides != kTerrainFaceBoth) {
    int center = drawing->target_x +
        (int)(((i64)screen_x * 256 + drawing->sprite_offset.x) * drawing->target_scale / 25600) +
        ((4 << zoom) * drawing->target_scale / 100);
    if (!(sides & kTerrainFaceLeft)) drawing->map_target.canvas.clip.left = IntMax(clip.left, center);
    if (!(sides & kTerrainFaceRight)) drawing->map_target.canvas.clip.right = IntMin(clip.right, center);
  }
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, ground_y, 0);
  drawing->map_target.canvas.clip = clip;
}

// SC2 stores independent cell heights. Close exposed land and water edges
// with the existing prepared vertical face, one level per blit.
void DrawCityMapTerrainSkirt(City *city, TileIndex tile, int zoom, int screen_x, int ground_y, int base) {
  if (!IsCityMapGroundPass(city->game->drawing)) return;
  static const u8 kCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15, 15, 0};
  int bottom[2] = {base, base};
  for (int side = 0; side < 2; ++side) {
    TileIndex next = CityMapViewNeighbor(city, tile, side, 1 - side);
    if (next == INVALID_TILE) continue;
    CityTileGeometry geometry = ReadCityMapViewGeometry(city, next);
    int height = geometry.surface_height, shape = geometry.surface_shape;
    // Compare the shared edge, not the low corner on the far side of a slope.
    // x+1 shares the neighbor's north/east corners; y+1 its north/west corners.
    int edge = side ? 3 : 9;
    height += (kCorners[shape] & edge) == edge;
    bottom[side] = IntMin(base, height);
  }
  int face = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
      CITY_LANDSCAPE_TERRAIN_FACE_FIRST + CITY_TERRAIN_SHAPE_13;
  for (int level = base; level > IntMin(bottom[0], bottom[1]); --level) {
    int sides = (level > bottom[1] ? kTerrainFaceLeft : 0) | (level > bottom[0] ? kTerrainFaceRight : 0);
    DrawCityMapTerrainFace(city, tile, zoom, face, screen_x,
        ground_y + (base - level + 1) * CityMapHeightStep(zoom), sides);
  }
}

static int TerrainTileSprite(const City *city, TileIndex tile, int zoom,
    const CityTileGeometry *geometry) {
  if (!NativeMapArtworkEnabled(city)) return CityTerrainSprite(
      CityMapViewTerrainCode(city, tile)) + 500 * zoom;
  if (geometry->water == CITY_WATER_NONE) return CityGroundSpriteForShape(city, tile, zoom, geometry->land_shape);
  return CityWaterSprite(city, tile, zoom, geometry);
}

void DrawCityMapTerrainTile(City *city, TileIndex tile, int zoom, int sprite, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->game->drawing;
  if (!IsCityMapGroundPass(drawing)) return;
  if (sprite < 0) sprite = TerrainTileSprite(city, tile, zoom, geometry);
  if (NativeMapArtworkEnabled(city)) {
    // Elevated structures still need complete terrain underneath: the top
    // diamond alone leaves holes beside slopes and lower foreground cells.
    DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y, geometry->surface_height);
    // Mixed shores have exposed sloping ground too. Close their sides using
    // the visible surface, rather than omitting every tile that contains water.
    int shape = geometry->surface_shape;
    if (shape > CITY_TERRAIN_FLAT && shape < CITY_TERRAIN_SHAPE_13) {
      int face = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
          CITY_LANDSCAPE_TERRAIN_FACE_FIRST + shape;
      DrawCityMapTerrainFace(city, tile, zoom, face, screen_x, ground_y,
          CityMapSlopeFaceSides(city, tile, shape, geometry->surface_height));
    }
  }
  // Zone sprites carry only the colored wash, dots and grid. Always use the
  // real ground underneath, including snow, desert, rough and recovering dirt.
  int layout = sprite % 500;
  if (geometry->water == CITY_WATER_NONE && !geometry->land_shape &&
      sprite < CITY_MAP_SPRITE_COUNT && layout >= 291 && layout <= 304) {
    int ground = TerrainTileSprite(city, tile, zoom, geometry);
    if (layout == 303) sprite = ground;
    else DrawCityMapSpriteAtBaseline(drawing, ground, screen_x, ground_y, 0);
  }
  if (DrawCityWaterBank(city, tile, zoom, screen_x, ground_y, geometry)) return;
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, ground_y, 0);
}

// Ground roads and airport surfaces draw during object collection. Parked planes
// belong to those surface sprites, so adjacent hangars must paint over their overhang.
// Raised tile artwork is deferred, including its baked base. Bridges submit front structures separately.
static bool IsDepthObjectTile(const City *city, TileIndex tile) {
  const Drawing *drawing = city->game->drawing;
  int kind = ReadTile(city, tile);
  if (IsTileKindBareGround(kind) || IsTileKindPlainTransport(kind)) return false;
  if (ReadCityTileClasses(kind) & TCLASS_GROUND_ARTWORK) return false;
  if (IsTileKindBuilding(kind) || kind == TK_PARK)
    return drawing->map_state.buildings_enabled &&
        (drawing->map_state.zone_ground_mode || !ReadCityZoneKind(&city->tile[tile]));
  if (!drawing->map_state.tile_details_enabled) return false;
  if (IsTileKindTree(kind)) return drawing->map_state.trees_enabled;
  if (!drawing->map_state.networks_enabled) return false;
  return IsTileKindPowerLine(kind) || IsTileKindBridge(kind) || IsTileKindMultiHighway(kind) ||
      IsTileKindStraightHighwayFamily(kind) || kind == TK_TUNNEL_ENTRANCE ||
      kind == TK_HIGHWAY_ENTRANCE || kind == TK_SUBWAY_TO_RAIL;
}

static bool IsDepthObjectAnchor(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  return (!IsTileKindBuilding(kind) && !IsTileKindHighwayBridge(kind) &&
      (!IsTileKindMultiHighway(kind) || IsTileKindHighwayIncline(kind))) ||
      IsCityMapViewFootprintAnchor(city, tile);
}

static i32 DepthTilePosition(const City *city, TileIndex tile) {
  Drawing *drawing = city->game->drawing;
  CityPoint point = CityMapViewTilePoint(city, tile);
  int kind = ReadTile(city, tile), x = point.x * 4096, y = point.y * 4096;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  int height = geometry.surface_height;
  if (IsTileKindBuilding(kind) || kind == TK_PARK) {
    CityPoint size = GetBuildingFootprintDimensions(kind, CityMapViewOrientation(city, tile));
    x -= (size.x - 1) * 2048;
    y += (size.y - 1) * 2048;
  } else if ((IsTileKindMultiHighway(kind) && !IsTileKindHighwayIncline(kind)) || IsTileKindHighwayBridge(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
    CityPoint a = CityMapCameraPoint(city, CityMapCameraRotation(drawing), bounds.left, bounds.top);
    CityPoint b = CityMapCameraPoint(city, CityMapCameraRotation(drawing), bounds.right - 1, bounds.bottom - 1);
    x = (a.x + b.x) * 2048;
    y = (a.y + b.y) * 2048;
  }
  if (IsTileKindBridge(kind) && !IsTileKindHighwayBridge(kind)) return CityMapBridgeArtworkDepth(city, tile, false);
  if (IsTileKindBridge(kind)) height = ReadCityBridgeDeckHeight(city, tile);
  else if (IsTileKindStraightHighwayFamily(kind) || IsTileKindMultiHighway(kind))
    height = CityHighwayDrawHeight(city, tile, &geometry) + 3;
  return CityMapObjectDepth(x, y, height * 4096);
}

// Zooms share tile traversal, visibility, module dispatch and overlays.
void DrawCityMapTile(City *city, int zoom, TileIndex cell, int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[cell];
  if (drawing->map_depth.phase == MAP_DEPTH_COLLECT) {
    if (IsDepthObjectTile(city, cell) && IsDepthObjectAnchor(city, cell)) {
      drawing->map_depth.phase = MAP_DEPTH_OBJECTS;
      drawing->map_depth.depth = DepthTilePosition(city, cell);
      DrawCityMapTile(city, zoom, cell, screen_x, screen_y);
    }
    int overlay = ReadOverlay(city, cell);
    if (IsCityEventOverlay(overlay) || (overlay >= CITY_OVERLAY_POLLUTION && overlay < 256)) {
      drawing->map_depth.phase = MAP_DEPTH_OBJECTS;
      drawing->map_depth.depth = CityMapOverlayDepth(city, cell);
      int ground_y = screen_y - ReadCitySurfaceHeight(&city->tile[cell]) * CityMapHeightStep(zoom);
      DrawCityMapOverlay(city, zoom, MapTileX(city, cell), MapTileY(city, cell), screen_x, ground_y);
    }
    // Plain roads and ground fall straight through to painting; only raised
    // objects above used the preparing path. The indexed caller restores it.
    drawing->map_depth.phase = MAP_DEPTH_GROUND;
    drawing->map_depth.preparing = false;
  }
  if (!NativeMapArtworkEnabled(city)) {
    int sprite_base = 500 * zoom;
    CityMapSprite *catalog = CityDrawingSpriteCatalog(drawing);
    CityPoint camera = CityMapViewTilePoint(city, cell);
    CityPoint size = CityMapCameraSize(city, CityMapCameraRotation(drawing));
    if (camera.x == size.x - 1 || camera.y == size.y - 1) {
      int edge_elevation = tp->altitude;
      int edge_y = screen_y - catalog[(sprite_base + CITY_SPRITE_CATALOG_SMALL_LAND_EDGE)].height;
      int wall_height = edge_elevation & CITY_ALTITUDE_LAND_MASK;
      while (wall_height-- > 0) {
        if (drawing->map_state.scan_clip.top > edge_y)
          return;
        if (drawing->map_state.scan_clip.bottom > edge_y)
          DrawCityMapSpriteAtBaseline(drawing, (sprite_base + CITY_SPRITE_CATALOG_SMALL_LAND_EDGE), screen_x,
              edge_y + catalog[(sprite_base + CITY_SPRITE_CATALOG_SMALL_LAND_EDGE)].height, 0);
        edge_y -= CityMapHeightStep(zoom);
      }
      if (drawing->map_state.water_enabled && HasTileWater(tp)) {
        wall_height = ((edge_elevation & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT) -
            (edge_elevation & CITY_ALTITUDE_LAND_MASK);
        while (wall_height-- > 0) {
          if (drawing->map_state.scan_clip.top > edge_y)
            return;
          if (drawing->map_state.scan_clip.bottom > edge_y)
            DrawCityMapSpriteAtBaseline(drawing, (sprite_base + CITY_SPRITE_CATALOG_SMALL_WATER_EDGE), screen_x,
              edge_y + catalog[(sprite_base + CITY_SPRITE_CATALOG_SMALL_WATER_EDGE)].height, 0);
          edge_y -= CityMapHeightStep(zoom);
        }
      }
    }
  }

  CityTileGeometry geometry = ReadCityMapViewGeometry(city, cell);
  int height = geometry.surface_height;
  int ground_y = screen_y - CityMapHeightStep(zoom) * height;
  int kind = tp->kind;
  u32 classes = kCityTileClasses[kind];
  const TileModule *module = kCityTileModules[(classes & CITY_TILE_MODULE_MASK) >> CITY_TILE_MODULE_SHIFT];
  if (drawing->map_depth.phase == MAP_DEPTH_GROUND && IsDepthObjectTile(city, cell)) {
    if (!module->draw_ground || !module->draw_ground(city, cell, zoom, screen_x, ground_y, &geometry))
      DrawCityMapTerrainTile(city, cell, zoom, -1, screen_x, ground_y, &geometry);
    goto overlay;
  }
  bool hidden = (!drawing->map_state.networks_enabled &&
      !IsTileKindLandscape(kind) && !IsTileKindBuilding(kind)) ||
      (!drawing->map_state.trees_enabled && IsTileKindTree(kind)) ||
      (!drawing->map_state.buildings_enabled && kind == TK_PARK);
  if (hidden) {
    if (!DrawCityGroundSurface(city, cell, zoom, screen_x, ground_y, &geometry))
      DrawCityMapTerrainTile(city, cell, zoom, TerrainTileSprite(city, cell, zoom, &geometry),
          screen_x, ground_y, &geometry);
    DrawCityMapPlacementTile(city, cell);
    goto overlay;
  }
  if (module->draw && module->draw(city, cell, zoom, screen_x, ground_y, &geometry)) goto overlay;
  int tile = CityMapViewTileSpriteIndex(city, cell);
  int tile_sprite = MapSpriteZoomIndex(tile, zoom);
  if (tile < CITY_MAP_TILE_SPRITE_RESIDENTIAL_FIRST && ground_y < drawing->map_state.scan_clip.top)
    return;

  if (tile >= CITY_MAP_TILE_SPRITE_RESIDENTIAL_FIRST) {
    DrawCityBuildingFallback(city, cell, tile_sprite, zoom, screen_x, ground_y, &geometry);
    goto overlay;
  }

  if (tile < CITY_MAP_TILE_SPRITE_ROAD_FIRST) {
    DrawCityGroundTileFallback(city, cell, tile, zoom, screen_x, ground_y, &geometry);
    goto overlay;
  }

  if (drawing->map_state.tile_details_enabled == 0) {
    DrawCityMapTerrainTile(city, cell, zoom, -1, screen_x, ground_y, &geometry);
    DrawCityMapPlacementTile(city, cell);
    goto overlay;
  }

  DrawCityTransportTileFallback(city, cell, tile, zoom, screen_x, ground_y, &geometry);
overlay:
  if (drawing->map_depth.phase == MAP_DEPTH_OBJECTS) return;
  if (drawing->map_depth.phase == MAP_DEPTH_GROUND) {
    int overlay = ReadOverlay(city, cell);
    if (IsCityEventOverlay(overlay)) return;
    if (overlay >= CITY_OVERLAY_POLLUTION && overlay < 256) return;
  }
  if (ReadOverlay(city, cell) != 0)
    DrawCityMapOverlay(city, zoom, MapTileX(city, cell), MapTileY(city, cell), screen_x, ground_y);
}

// The caller prepares the active map target and logical clip. Both sorted objects and direct ground painting use
// the same clip-derived tile traversal; no map-size-specific fallback or repaint-from-tile cursor is retained.
void DrawCityMapSurface(City *city, const CityMapViewConfig *view, int zoom) {
  Drawing *drawing = city->game->drawing;
  drawing->map_depth.bridge_palettes = city->game->resources->bridge_palettes;
  drawing->map_depth.underworld_palettes = city->game->resources->underworld_palettes;
  drawing->map_depth.car_palettes = city->game->resources->road_vehicle_palettes;
  CityRect saved_clip = drawing->map_state.scan_clip;
  if (NativeMapArtworkEnabled(city) && drawing->label_style && drawing->animation.mode != ANIMATION_CAPTURE) {
    drawing->map_state.scan_clip.top -= 8 << zoom;
    drawing->map_state.scan_clip.bottom += 55 << zoom;
    DrawDepthSortedCityMap(city, view, zoom);
  } else {
    static const struct { u8 left, bottom, top; } kScanPadding[] = {
      {24, 16, 8}, {48, 32, 16}, {128, 220, 32}
    };
    drawing->map_state.scan_clip.left -= kScanPadding[zoom].left;
    drawing->map_state.scan_clip.bottom += kScanPadding[zoom].bottom;
    drawing->map_state.scan_clip.top -= kScanPadding[zoom].top;
    PaintVisibleCityMapTiles(city, view, zoom);
  }
  drawing->map_state.scan_clip = saved_clip;
}
