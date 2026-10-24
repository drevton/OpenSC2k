#include "city/events/events.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/sprites/surface.h"
#include "drawing/maps/map_sprites.h"
#include "drawing/maps/map_data.h"
#include "city/maps/terrain.h"
#include "city/presentation.h"
#include <math.h>

#include "assets/graphics.h"
#include "assets/state.h"
#include "city/infrastructure/bridges.h"
#include "city/maps/layers.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/state.h"

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
  Drawing *drawing = city->drawing;
  CityTileGeometry geometry = ReadCityMapViewGeometry(city, tile);
  int ground_y = screen_y - CityMapHeightStep(zoom) * geometry.surface_height;
  // Faces below a tile can intersect the clip even when its top is above it.
  if (ground_y > drawing->map_state.scan_clip.bottom) return;

  CityMapDataMode mode = drawing->map_state.data_mode;
  int sprite = -1;
  if (CityMapDryGroundShape(city, tile, &geometry) == CITY_TERRAIN_FLAT) {
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
  Drawing *drawing = city->drawing;
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
  Drawing *drawing = city->drawing;
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
        int facing = (ReadTileVariant(tp) + CityMapCameraRotation(city->drawing)) & 3;
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
  Drawing *drawing = city->drawing;
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
  Drawing *drawing = city->drawing;
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
      DrawCityMapOverlay(city, cell, zoom, (CityPoint){screen_x, baseline - (4 << zoom)});
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
    DrawCityMapOverlay(city, cell, zoom, (CityPoint){screen_x, baseline - (4 << zoom)});
}

// Bind underground artwork's palette before visiting visible tiles. This pass preserves the caller's scan clip.
void DrawCityMapUnderground(City *city, int zoom) {
  Drawing *drawing = city->drawing;
  drawing->map_depth.underworld_palettes = city->game->resources->underworld_palettes;
  CityRect saved_clip = drawing->map_state.scan_clip;
  drawing->map_state.scan_clip.left -= 8 << zoom;
  drawing->map_state.scan_clip.bottom += 8 << zoom;
  PaintVisibleCityMapTiles(city, drawing->default_map_view, zoom);
  drawing->map_state.scan_clip = saved_clip;
}

// Native data previews use semantic colors independently of the compatibility palette.
// Growth history deliberately uses the rubble color for trees while the other previews use green.
static u32 CityMapPreviewGround(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  if (HasTileWater(tp)) return 0xFF285A80;
  if (IsTileKindTree(tp->kind)) return 0xFF304E38;
  if (!IsTileKindUnbuilt(tp->kind)) return 0xFF505860;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  int shade = 40 + IntMin(geometry.land_height, 31);
  return 0xFF000000 | shade << 16 | (shade + 8) << 8 | shade;
}

static u32 CityMapPreviewZone(const TileInfo *tp, u32 ground) {
  static const u32 kZoneColors[ZONE_RESERVED_FIRST] = {
    [ZONE_RESIDENTIAL_LOW] = 0xFF54B85C, [ZONE_RESIDENTIAL_HIGH] = 0xFF278C39,
    [ZONE_COMMERCIAL_LOW] = 0xFF559DE0, [ZONE_COMMERCIAL_HIGH] = 0xFF286DBD,
    [ZONE_INDUSTRIAL_LOW] = 0xFFF0D05A, [ZONE_INDUSTRIAL_HIGH] = 0xFFCBA527,
    [ZONE_MILITARY] = 0xFFCB6556, [ZONE_AIRPORT] = 0xFFAD7BC9, [ZONE_SEAPORT] = 0xFF55BFC5
  };
  int zone = ReadCityZoneKind(tp);
  return zone > ZONE_NONE && zone < ZONE_RESERVED_FIRST ? kZoneColors[zone] : ground;
}

// Preserve the sixteen data levels; zero leaves the background visible.
static u32 CityMapPreviewMetric(int value, u32 low, u32 high, u32 ground) {
  int level = value >> 4;
  if (!level) return ground;
  u32 color = 0xFF000000;
  for (int shift = 0; shift < 24; shift += 8)
    color |= (((low >> shift & 255) * (15 - level) + (high >> shift & 255) * (level - 1)) / 14) << shift;
  return color;
}

static u32 CityMapPreviewColor(const City *city, TileIndex tile, int selection) {
  const TileInfo *tp = &city->tile[tile];
  u32 ground = CityMapPreviewGround(city, tile), classes = ReadCityTileClasses(tp->kind);
  const u32 highlight = 0xFFF4F0DC, supplied = 0xFF65D67B, missing = 0xFFE65C50;
  const u32 low_warning = 0xFFC9B85B, high_warning = 0xFFE74232;
  const u32 low_benefit = 0xFF344D3C, high_benefit = 0xFF65D67B;
  switch (selection) {
  case CITY_DATA_SELECTION_STRUCTURES: return ground;
  case CITY_DATA_SELECTION_ZONES: return CityMapPreviewZone(tp, ground);
  case CITY_DATA_SELECTION_ROADS:
    return tp->kind != TK_CLEAR && ((classes & TCLASS_ROAD) ||
        ((classes & TCLASS_NATIVE_BRIDGE) && !(classes & TCLASS_RAIL_BRIDGE)) ||
        ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_ROAD ||
        IsTileKindMultiHighway(tp->kind) || IsTileKindStraightHighwayFamily(tp->kind)) ? highlight : ground;
  case CITY_DATA_SELECTION_RAIL:
    return (classes & (TCLASS_RAIL | TCLASS_RAIL_BRIDGE)) ||
        ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_RAIL ? highlight : ground;
  case CITY_DATA_SELECTION_TRAFFIC: {
    u32 background = classes & (TCLASS_ROAD | TCLASS_RAIL | TCLASS_NATIVE_BRIDGE | TCLASS_HIGHWAY) ? highlight : ground;
    return CityMapPreviewMetric(ReadTrafficDensity(city, tile), low_warning, high_warning, background);
  }
  case CITY_DATA_SELECTION_POWER:
    if (classes & TCLASS_POWER) return highlight;
    if (tp->kind != TK_CLEAR && !IsTileKindExposedWater(tp->kind)) {
      if (tp->flags & TF_POWERED) return supplied;
      if (tp->flags & TF_POWERABLE) return missing;
    }
    return ground;
  case CITY_DATA_SELECTION_WATER: {
    int underground = ReadUnderground(city, tile);
    if (underground >= TUK_PIPE_FIRST && underground <= TUK_SUBWAY_ACCESS) return highlight;
    if ((tp->flags & TF_WATER_SERVICE) == TF_WATER_SERVICE) return 0xFF55BFC5;
    return tp->flags & TF_PIPED ? missing : ground;
  }
  case CITY_DATA_SELECTION_DENSITY:
    return CityMapPreviewMetric(ReadDensity(city, tile), 0xFF3B5474, 0xFF9FDBFF, ground);
  case CITY_DATA_SELECTION_GROWTH: {
    int history = ReadDensityHistory(city, tile);
    if (history <= 124) return missing;
    if (history > 130) return supplied;
    return IsTileKindTree(tp->kind) ? 0xFF505860 : ground;
  }
  case CITY_DATA_SELECTION_CRIME:
    return CityMapPreviewMetric(ReadCrime(city, tile), low_warning, high_warning, ground);
  case CITY_DATA_SELECTION_POLLUTION:
    return CityMapPreviewMetric(ReadPollution(city, tile), low_warning, high_warning, ground);
  case CITY_DATA_SELECTION_LAND_VALUE:
    return CityMapPreviewMetric(ReadLandValue(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_POLICE_COVERAGE:
    return CityMapPreviewMetric(ReadPoliceCoverage(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_FIRE_COVERAGE:
    return CityMapPreviewMetric(ReadFireCoverage(city, tile), low_benefit, high_benefit, ground);
  case CITY_DATA_SELECTION_POLICE_STATIONS: return tp->kind == TK_POLICE_STATION ? highlight : ground;
  case CITY_DATA_SELECTION_FIRE_STATIONS: return tp->kind == TK_FIRE_STATION ? highlight : ground;
  case CITY_DATA_SELECTION_SCHOOLS: return tp->kind == TK_SCHOOL ? highlight : ground;
  case CITY_DATA_SELECTION_COLLEGES: return tp->kind == TK_COLLEGE ? highlight : ground;
  default: return ground;
  }
}

bool BuildCityMapDataPreview(const City *city, int selection, u32 *pixels) {
  bool changed = false;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      TileIndex tile = MapTileXY(city, y * city->map_width / 128, x * city->map_height / 128);
      u32 color = CityMapPreviewColor(city, tile, selection);
      int index = y * 128 + x;
      changed |= pixels[index] != color;
      pixels[index] = color;
    }
  return changed;
}
