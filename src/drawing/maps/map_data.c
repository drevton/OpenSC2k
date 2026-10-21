#include "city/maps/layers.h"
#include "assets/graphics.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include <math.h>

#include "drawing/drawing_internal.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"
#include "assets/state.h"

// Data overlays select artwork independently of the tile painter. Quantitative layers use eight intensity bands;
// service and growth layers supply their explicit presence/absence artwork. A negative result leaves natural ground.
static int DataOverlaySprite(const City *city, TileIndex tile, CityMapDataMode mode) {
  int value;
  switch (mode) {
  case CITY_DATA_MODE_TRAFFIC: value = ReadTrafficDensity(city, tile); break;
  case CITY_DATA_MODE_DENSITY: value = ReadDensity(city, tile); break;
  case CITY_DATA_MODE_DENSITY_HISTORY: value = ReadDensityHistory(city, tile); break;
  case CITY_DATA_MODE_CRIME: value = ReadCrime(city, tile); break;
  case CITY_DATA_MODE_POLICE_COVERAGE: value = ReadPoliceCoverage(city, tile); break;
  case CITY_DATA_MODE_POLLUTION: value = ReadPollution(city, tile); break;
  case CITY_DATA_MODE_LAND_VALUE: value = ReadLandValue(city, tile); break;
  case CITY_DATA_MODE_FIRE_COVERAGE: value = ReadFireCoverage(city, tile); break;
  case CITY_DATA_MODE_POWER: {
    int flags = city->tile[tile].flags;
    if (!(flags & TF_POWERABLE)) return -1;
    return flags & TF_POWERED ? CITY_SPRITE_CATALOG_SERVICE_PRESENT : CITY_SPRITE_CATALOG_SERVICE_MISSING;
  }
  case CITY_DATA_MODE_WATER: {
    int flags = city->tile[tile].flags;
    if (!(flags & TF_PIPED)) return -1;
    return flags & TF_WATERED ? CITY_SPRITE_CATALOG_SERVICE_PRESENT : CITY_SPRITE_CATALOG_SERVICE_MISSING;
  }
  case CITY_DATA_MODE_GROWTH:
    value = ReadDensityHistory(city, tile);
    if (value <= 124) return CITY_SPRITE_CATALOG_GROWTH_NEGATIVE;
    return value >= 131 ? CITY_SPRITE_CATALOG_GROWTH_POSITIVE : -1;
  default: return -1;
  }
  return value >= 16 ? CITY_SPRITE_CATALOG_DATA_LEVEL_FIRST + (value >> 5) : -1;
}

void DrawCityMapDataTile(City *city, int zoom, TileIndex tile, int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
  int ground_y = screen_y - CityMapHeightStep(zoom) * geometry.surface_height;
  // Faces below a tile can intersect the clip even when its top is above it.
  if (ground_y > drawing->map_state.scan_clip.bottom) return;

  CityMapDataMode mode = drawing->map_state.data_mode;
  int sprite = -1;
  if (CityTerrainSprite(CityMapViewTerrainCode(city, tile)) == CITY_SPRITE_CATALOG_FLAT_TERRAIN) {
    sprite = DataOverlaySprite(city, tile, mode);
    if (sprite >= 0) sprite = MapSpriteZoomIndex(sprite, zoom);
  }
  DrawCityMapTerrainTile(city, tile, zoom, sprite, screen_x, ground_y, &geometry);
  DrawCityMapPlacementTile(city, tile);
  if (mode == CITY_DATA_MODE_LAND_VALUE && (drawing->options.fixes & (1 << DRAWING_FIX_CITY_CENTER)) &&
      MapTileX(city, tile) == city->geography.center_x && MapTileY(city, tile) == city->geography.center_y) {
    int scale = drawing->target_scale;
    int px = drawing->target_x + (screen_x + (4 << zoom)) * scale / 100;
    int py = drawing->target_y + (ground_y - (2 << zoom)) * scale / 100;
    int radius = IntMax(2, 3 * scale / 100), stroke = IntMax(1, scale / 100);
    FillSpriteSurface(&drawing->map_target, (CityRect){px - radius, py, px + radius + stroke, py + stroke}, 255);
    FillSpriteSurface(&drawing->map_target, (CityRect){px, py - radius, px + stroke, py + radius + stroke}, 255);
  }
}

// Render the selected city data layer at the active map zoom.
void DrawCityMapData(City *city, int zoom) {
  Drawing *drawing = city->game->drawing;
  CityRect saved_clip = drawing->map_state.scan_clip;
  drawing->map_state.scan_clip.left -= 8 << zoom;
  drawing->map_state.scan_clip.bottom += 8 << zoom;
  PaintVisibleCityMapTiles(city, drawing->default_map_view, zoom);
  drawing->map_state.scan_clip = saved_clip;
}

// Exposed height steps need soil down to the neighboring buried land, even
// where the surface view hides that drop with water or a bridge deck.
static void DrawUnderworldGroundSkirt(City *city, TileIndex cell, int style, int zoom,
    int screen_x, int baseline, int base) {
  Drawing *drawing = city->game->drawing;
  int bottom = base;
  for (int side = 0; side < 2; ++side) {
    TileIndex neighbor = CityMapViewNeighbor(city, cell, side, 1 - side);
    if (neighbor == INVALID_TILE) continue;
    int height = CityMapUnderworldGround(city, neighbor).elevation;
    bottom = IntMin(bottom, height);
  }
  int face = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
      CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_BAKED_FACE_FIRST;
  for (int level = base; level > bottom; --level)
    DrawCityMapPaletteSpriteAtBaseline(drawing, face, screen_x,
        baseline + (base - level + 1) * CityMapHeightStep(zoom), SPRITE_FIXED_PALETTE, MAP_PALETTE_UNDERWORLD_FIRST + style);
}

// The legacy reconnectors force a climbing kind from terrain, even when the
// neighboring utility runs across that slope. Native artwork follows those
// actual neighbors; crossing tiles still keep their two utility axes separate.
static int UnderworldUtilityConnections(const City *city, TileIndex tile, bool pipes) {
  static const u8 kUtilityConnections[] = {
      CONN_EW, CONN_NS, CONN_NS, CONN_EW,
      CONN_NS, CONN_EW, CONN_WS, CONN_SE,
      CONN_NE, CONN_NW, CONN_ALL ^ CONN_EAST,
      CONN_ALL ^ CONN_NORTH, CONN_ALL ^ CONN_WEST,
      CONN_ALL ^ CONN_SOUTH, CONN_ALL,
  };
  int first = pipes ? TUK_PIPE_FIRST : TUK_SUBWAY_FIRST;
  int last = pipes ? TUK_PIPE_LAST : TUK_SUBWAY_LAST;
  int kind = CityMapViewUnderground(city, tile);
  if (kind == TUK_SUBWAY_EW_PIPE_NS) return pipes ? CONN_NS : CONN_EW;
  if (kind == TUK_SUBWAY_NS_PIPE_EW) return pipes ? CONN_EW : CONN_NS;
  bool regular = kind >= first && kind <= last;
  if (!regular && (pipes || (kind != TUK_SUBWAY_ACCESS && kind != TUK_MISSILE_SILO)))
    return CONN_NONE;
  static const struct { i8 x, y; u8 connection, opposite; } kNeighbors[] = {
      {-1, 0, CONN_WEST, CONN_EAST},
      {0, 1, CONN_SOUTH, CONN_NORTH},
      {1, 0, CONN_EAST, CONN_WEST},
      {0, -1, CONN_NORTH, CONN_SOUTH},
  };
  int mask = CONN_NONE;
  for (int side = 0; side < 4; ++side) {
    TileIndex next = CityMapViewNeighbor(city, tile, kNeighbors[side].x, kNeighbors[side].y);
    if (next == INVALID_TILE) continue;
    int neighbor = CityMapViewUnderground(city, next);
    bool connected = neighbor >= first && neighbor <= last;
    if (!pipes && (neighbor == TUK_SUBWAY_ACCESS || neighbor == TUK_MISSILE_SILO))
      connected = true;
    if (neighbor == TUK_SUBWAY_EW_PIPE_NS)
      connected = ((pipes ? CONN_NS : CONN_EW) & kNeighbors[side].opposite) != 0;
    else if (neighbor == TUK_SUBWAY_NS_PIPE_EW)
      connected = ((pipes ? CONN_EW : CONN_NS) & kNeighbors[side].opposite) != 0;
    if (connected) mask |= kNeighbors[side].connection;
  }
  return mask ? mask : regular ? kUtilityConnections[kind - first] : CONN_ALL;
}

static int UnderworldPipeDiagonalEnds(const City *city, TileIndex tile, int connections) {
  if (connections != CONN_WS && connections != CONN_SE &&
      connections != CONN_NE && connections != CONN_NW) return 0;
  static const i8 kX[] = {-1, 0, 1, 0}, kY[] = {0, 1, 0, -1};
  int ends = 0, end = 0;
  for (int side = 0; side < 4; ++side) {
    if (!(connections & (1 << side))) continue;
    TileIndex next = CityMapViewNeighbor(city, tile, kX[side], kY[side]);
    if (next != INVALID_TILE &&
        UnderworldUtilityConnections(city, next, true) == (connections ^ CONN_ALL))
      ends |= 1 << end;
    ++end;
  }
  return ends;
}

static int UnderworldRoadTunnelAxis(const City *city, TileIndex cell, int floor) {
  static const i8 kX[] = {-1, 1, 0, 0}, kY[] = {0, 0, -1, 1};
  int connections[2] = {0};
  for (int side = 0; side < 4; ++side) {
    TileIndex next = CityMapViewNeighbor(city, cell, kX[side], kY[side]);
    if (next == INVALID_TILE) continue;
    int altitude = ReadAltitudeWord(city, next);
    int depth = (altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
    if (depth && (altitude & CITY_ALTITUDE_LAND_MASK) - depth + 1 == floor) ++connections[side >> 1];
  }
  if (connections[0] != connections[1]) return connections[1] > connections[0];
  // Adjacent parallel tunnels can make local connectivity ambiguous. Follow
  // only connected straight spans to their portals; no map-wide search/cache.
  for (int side = 0; side < 4; ++side) {
    TileIndex next = cell;
    for (;;) {
      next = CityMapViewNeighbor(city, next, kX[side], kY[side]);
      if (next == INVALID_TILE) break;
      const TileInfo *tp = &city->tile[next];
      int altitude = tp->altitude;
      int depth = (altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
      if (!depth || (altitude & CITY_ALTITUDE_LAND_MASK) - depth + 1 != floor) break;
      if (depth == CITY_TUNNEL_DEPTH_ENTRANCE) {
        int facing = (ReadTileVariant(tp) + CityMapCameraRotation(city->game->drawing)) & 3;
        if (tp->kind == TK_TUNNEL_ENTRANCE &&
            1 - (facing & 1) == (side >> 1)) return side >> 1;
        break;
      }
    }
  }
  return connections[1] != 0;
}

static void DrawBakedUnderworldSprite(Drawing *drawing, int asset, int zoom, int x, int baseline, double height) {
  int id = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
      CITY_LANDSCAPE_GROUND_FIRST + asset;
  CityPoint saved = drawing->sprite_offset;
  drawing->sprite_offset.y -= height * CityMapHeightStep(zoom) * 256;
  DrawCityMapSpriteAtBaseline(drawing, id, x, baseline, 0);
  drawing->sprite_offset = saved;
}

static bool UnderworldPipeEasesAtEdge(const City *city, TileIndex tile, int side,
    const CityMapUnderworldPatch *patch, double edge) {
  static const i8 kX[] = {-1, 0, 1, 0}, kY[] = {0, 1, 0, -1};
  TileIndex neighbor = CityMapViewNeighbor(city, tile, kX[side], kY[side]);
  if (neighbor == INVALID_TILE) return false;
  CityMapUnderworldPatch next = CityMapUnderworldSurface(city, neighbor);
  return next.elevation + CityMapUnderworldHeight(&next, 0.5, 0.5) == patch->elevation + edge;
}

// Common slopes use two independently selected baked halves. Nonstandard
// slope junctions reuse plain half-grade arms and a small central fitting.
static void DrawBakedUnderworldPipe(City *city, TileIndex tile, const CityMapUnderworldPatch *patch,
    int connections, int zoom, int screen_x, int baseline) {
  if (!connections) return;
  Drawing *drawing = city->game->drawing;
  double center = CityMapUnderworldHeight(patch, 0.5, 0.5);
  bool flat = patch->coefficient[1][0] == 0 && patch->coefficient[0][1] == 0 &&
      patch->coefficient[1][1] == 0;
  if (flat) {
    int piece = connections - 1;
    if (connections == CONN_NW || connections == CONN_SE ||
        connections == CONN_WS || connections == CONN_NE) {
      int diagonal = UnderworldPipeDiagonalEnds(city, tile, connections);
      if (diagonal) {
        int first = connections == CONN_NW || connections == CONN_SE ?
            31 + (connections == CONN_SE ? 3 : 0) :
            CITY_UNDERWORLD_PIPE_VERTICAL_DIAGONAL_FIRST + (connections == CONN_NE ? 3 : 0);
        piece = first + diagonal - 1;
      }
    }
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_PIPE_FIRST + piece, zoom, screen_x, baseline, center);
    return;
  }
  static const double kEnds[4][2] = {{0, 0.5}, {0.5, 1}, {1, 0.5}, {0.5, 0}};
  static const u8 kAscendingDirection[] = {1, 2, 3, 0};
  bool straight = connections == CONN_EW || connections == CONN_NS;
  bool ordinary = (patch->shape == 1 || patch->shape == 3) ? connections == CONN_NS :
      (patch->shape == 2 || patch->shape == 4) && connections == CONN_EW;
  int count = 0;
  for (int side = 0; side < 4; ++side) {
    if (!(connections & (1 << side))) continue;
    ++count;
    double edge = CityMapUnderworldHeight(patch, kEnds[side][0], kEnds[side][1]);
    double delta = edge - center;
    if (delta == 0) {
      DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_PIPE_FIRST + (1 << side) - 1,
          zoom, screen_x, baseline, center);
      continue;
    }
    int direction = kAscendingDirection[delta > 0 ? side : (side + 2) & 3];
    int half = delta > 0;
    double grade = fabs(delta) * 2;
    int piece;
    if (grade == 0.5) piece = 37 + direction * 2 + half;
    else {
      bool eased = ordinary && UnderworldPipeEasesAtEdge(city, tile, side, patch, edge);
      piece = 15 + direction * 4 + half * 2 + eased;
    }
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_PIPE_FIRST + piece,
        zoom, screen_x, baseline, center - grade * 0.5);
  }
  if (!straight && count > 1) {
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_PIPE_FIRST + CITY_UNDERWORLD_PIPE_FITTING,
        zoom, screen_x, baseline, center);
  } else if (count == 1) {
    // The endpoint face stays at the tile center.
    CityRect saved = drawing->map_target.canvas.clip;
    int width = (8 << zoom) * drawing->target_scale / 100;
    int px = drawing->target_x + ((i64)screen_x * 256 + drawing->sprite_offset.x) * drawing->target_scale / 25600;
    int py = drawing->target_y + ((i64)baseline * 256 + drawing->sprite_offset.y -
        (int)(center * CityMapHeightStep(zoom) * 256)) * drawing->target_scale / 25600 - width / 4;
    int radius = (width * 10 + 127) / 128;
    drawing->map_target.canvas.clip = (CityRect){IntMax(saved.left, px + width / 2 - radius),
        IntMax(saved.top, py - radius), IntMin(saved.right, px + width / 2 + radius),
        IntMin(saved.bottom, py + radius)};
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_PIPE_FIRST +
        connections - 1, zoom, screen_x, baseline, center);
    drawing->map_target.canvas.clip = saved;
  }
}

// Ground, utilities, highlights and shadows are immutable offline artwork.
void DrawCityMapUndergroundTile(City *city, int zoom, TileIndex cell, int screen_x, int screen_y) {
  Drawing *drawing = city->game->drawing;
  CityMapUnderworldPatch patch = CityMapUnderworldSurface(city, cell);
  int baseline = screen_y - patch.elevation * CityMapHeightStep(zoom);
  const TileInfo *tp = &city->tile[cell];
  int flags = tp->flags;
  int style = HasTileWater(tp) ? CITY_UNDERWORLD_SUBMERGED : CITY_UNDERWORLD_SOIL;
  if (flags & TF_PIPED)
    style = flags & TF_WATERED ? CITY_UNDERWORLD_WATER_SERVICE : CITY_UNDERWORLD_DRY_SERVICE;
  DrawUnderworldGroundSkirt(city, cell, style, zoom, screen_x, baseline, patch.elevation);
  int ground = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
      CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_BAKED_UNDERWORLD_FIRST + patch.shape;
  DrawCityMapPaletteSpriteAtBaseline(drawing, ground, screen_x, baseline,
      SPRITE_FIXED_PALETTE, MAP_PALETTE_UNDERWORLD_FIRST + style);
  if (!drawing->map_state.networks_enabled) {
    DrawCityMapPlacementTile(city, cell);
    if (ReadOverlay(city, cell) != CITY_OVERLAY_NONE)
      DrawCityMapOverlay(city, zoom, MapTileX(city, cell), MapTileY(city, cell), screen_x, baseline - (4 << zoom));
    return;
  }
  int altitude = tp->altitude;
  int depth = (altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
  if (depth > CITY_TUNNEL_DEPTH_ENTRANCE) {
    int floor = (altitude & CITY_ALTITUDE_LAND_MASK) - depth + 1;
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_TUNNEL_FIRST +
        UnderworldRoadTunnelAxis(city, cell, floor), zoom, screen_x, baseline,
        CityMapUnderworldHeight(&patch, 0.5, 0.5));
  }
  DrawBakedUnderworldPipe(city, cell, &patch, UnderworldUtilityConnections(city, cell, true), zoom, screen_x, baseline);
  int subway = UnderworldUtilityConnections(city, cell, false);
  if (subway) DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_SUBWAY_FIRST +
      (subway - 1) * CITY_UNDERWORLD_SHAPES + patch.shape, zoom, screen_x, baseline, 0);
  DrawCityMapPlacementTile(city, cell);
  int kind = CityMapViewUnderground(city, cell);
  if (kind == TUK_SUBWAY_ACCESS || kind == TUK_MISSILE_SILO)
    DrawBakedUnderworldSprite(drawing, CITY_LANDSCAPE_BAKED_COVER_FIRST +
        (kind == TUK_MISSILE_SILO ? CITY_UNDERWORLD_SHAPES : 0) + patch.shape,
        zoom, screen_x, baseline, 0);
  if (depth == CITY_TUNNEL_DEPTH_ENTRANCE)
    DrawCityMapSpriteAtBaseline(drawing, MapSpriteZoomIndex(CityMapViewTileSpriteIndex(city, cell), zoom),
        screen_x, baseline, CityMapViewTileFlip(city, cell));
  if (ReadOverlay(city, cell) != CITY_OVERLAY_NONE)
    DrawCityMapOverlay(city, zoom, MapTileX(city, cell), MapTileY(city, cell), screen_x, baseline - (4 << zoom));
}

// Bind underground artwork's palette before visiting visible tiles. This pass preserves the caller's scan clip.
void DrawCityMapUnderground(City *city, int zoom) {
  Drawing *drawing = city->game->drawing;
  drawing->map_depth.underworld_palettes = city->game->resources->underworld_palettes;
  CityRect saved_clip = drawing->map_state.scan_clip;
  drawing->map_state.scan_clip.left -= 8 << zoom;
  drawing->map_state.scan_clip.bottom += 8 << zoom;
  PaintVisibleCityMapTiles(city, drawing->default_map_view, zoom);
  drawing->map_state.scan_clip = saved_clip;
}
