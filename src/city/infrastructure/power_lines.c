// TileInfo storage for power lines/crossings (zero-based bits):
// TK_POWER_LINE[_WATER] is cable only; TK_POWER_LINE_TOWER[_WATER] adds a tower. Kind, not another bit, selects
// tower presence and permanent water. Ordinary lines store connections in terrain bits 4 west, 5 south, 6 east,
// 7 north. Tower shape/spacing is derived from connections and world position, not stored as a tower-type field.
// Road/power and rail/power crossings use terrain[4] for the other network's axis; wires are perpendicular.
// Highway/power uses terrain[4..5] for highway orientation, with perpendicular wires; bridge power underpasses
// use bridge kind+axis instead. Those crossings do not store a second independent four-bit connection mask.
// terrain[0..3] is ground cover on land, or underlying water kind minus TK_WATER on wet lines. altitude[5..9]
// remains water height on wet lines; zone[4] selects the bed/land slope in zone[0..3], otherwise zoning.
// zone[5..7] has no power payload. TF_POWERABLE/TF_POWERED are capability/supply; all other fields are shared
// as documented in tile_storage.h. Reconnecting changes topology without borrowing the region or overlay bits.

#include "city/infrastructure/power_lines.h"
#include "city/infrastructure/lines.h"
#include "city/maps/terrain.h"

#include <assert.h>

#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/surface_lines.h"
#include "assets/state.h"
#include "game/audio.h"
#include "city/maps/layers.h"
#include "city/maps/module.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "game/state.h"

// Keep tower spacing and appearance attached to the original world coordinates after a geography rotation.
static CityPoint PowerLinePatternPoint(const City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  int width = city->map_width, height = city->map_height;
  for (int rotation = city->geography.rotation; rotation > 0; --rotation) {
    int previous_x = point.x;
    point.x = point.y; point.y = width - 1 - previous_x;
    int previous_width = width;
    width = height; height = previous_width;
  }
  return point;
}

// Crossings have fixed wire axes. Ordinary power lines store the complete mask, including water spans, so rendering
// never has to rediscover their topology.
int CityPowerConnectionMask(const TileInfo *tp) {
  if (IsTileKindPlainPowerLine(tp->kind)) return ReadTransportConnections(tp);
  if (IsTileKindBridge(tp->kind))
    return ReadCityBridgeUnderpass(tp) != CITY_BRIDGE_UNDERPASS_POWER ? 0 :
        ReadCityBridgeAxis(tp) ? CONN_EW : CONN_NS;
  if (!IsTileKindPowerLine(tp->kind)) return 0;
  int axis = IsTileKindGroundCrossing(tp->kind) ? ReadCityCrossingAxis(tp) : ReadCityHighwayAxis(tp);
  return axis ? CONN_EW : CONN_NS;
}

// Only edits/imports discover topology. Water spans retain their construction axis even before their next cell exists;
// dry lines connect to capable neighbors.
int FindCityPowerConnectionMask(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  const TileInfo *tp = &city->tile[tile];
  if (!IsTileKindPlainPowerLine(tp->kind) || IsCityWaterPowerLine(tp)) return CityPowerConnectionMask(tp);
  int shape = ReadTerrainCode(tp);
  int directions = shape >= CITY_TERRAIN_SHAPE_1 && shape <= CITY_TERRAIN_SHAPE_4 ?
      (shape & 1 ? CONN_NS : CONN_EW) : CONN_ALL;
  int mask = 0;
  for (int direction = 0; direction < 4; ++direction) {
    if (!(directions & (1 << direction))) continue;
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
    if (neighbor == INVALID_TILE) continue;
    const TileInfo *other = &city->tile[neighbor];
    if (!(other->flags & TF_POWERABLE)) continue;
    if (IsTileKindBridge(other->kind) &&
        (ReadCityBridgeUnderpass(other) != CITY_BRIDGE_UNDERPASS_POWER ||
            (direction & 1) == ReadCityBridgeAxis(other))) continue;
    int other_shape = ReadTerrainCode(other);
    if (!(direction & 1 ? kLineVerticalConnectionAllowed[other_shape] :
        kLineHorizontalConnectionAllowed[other_shape])) continue;
    mask |= 1 << direction;
  }
  return mask;
}

// Update the stored mask and choose cable-only/tower kinds from local topology.
void UpdatePowerLineConnections(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  if (!IsTileKindPlainPowerLine(tp->kind)) return;
  int mask = FindCityPowerConnectionMask(city, tile);
  CityPoint point = PowerLinePatternPoint(city, tile);
  // Leave three cable-only tiles between straight-run towers. Endpoints, bends
  // and junctions always have a tower; water spans keep their fixed straight axis.
  bool tower = (point.x + point.y) % 4 == 0 || (mask != CONN_EW && mask != CONN_NS);
  for (int direction = 0; direction < 4 && !tower; ++direction) {
    if (!(mask & (1 << direction))) continue;
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
    if (neighbor == INVALID_TILE) continue;
    const TileInfo *np = &city->tile[neighbor];
    int kind = np->kind;
    bool crossing = IsTileKindBridge(kind) ?
        ReadCityBridgeUnderpass(np) == CITY_BRIDGE_UNDERPASS_POWER :
        IsTileKindPowerLine(kind) && !IsTileKindPlainPowerLine(kind);
    if (crossing && (CityPowerConnectionMask(np) & (1 << ((direction + 2) & 3)))) tower = true;
  }
  int kind = CityTileKindForWater(tower ? TK_POWER_LINE_TOWER : TK_POWER_LINE, HasTileWater(tp));
  if (tp->kind == kind && ReadTransportConnections(tp) == mask) return;
  if (tp->kind != kind) ReplaceCityTileAndStatistics(city, tile, kind);
  WriteTransportConnections(tp, mask);
  InvalidateTile(city, tile);
  // Each neighboring wire half also depends on this tile's tower and height.
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
    if (neighbor != INVALID_TILE && IsTileKindPowerLine(ReadTile(city, neighbor))) InvalidateTile(city, neighbor);
  }
}

// Call after a placement, capability or terrain edit, once all tile fields are set.
void RefreshCityPowerConnections(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  UpdatePowerLineConnections(city, tile);
  for (int direction = 0; direction < 4; ++direction) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[direction], kLineYStep[direction]);
    if (neighbor == INVALID_TILE) continue;
    UpdatePowerLineConnections(city, neighbor);
    if (IsTileKindPowerLine(ReadTile(city, neighbor))) InvalidateTile(city, neighbor);
  }
}

// File conversion has normalized kinds and preserved water-span axes. Run after complete native map conversion;
// derives stored topology once and must not inspect a partially decoded stock map.
void InitializeCityPowerConnections(City *city) {
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile)
    if (IsTileKindPlainPowerLine(ReadTile(city, tile))) UpdatePowerLineConnections(city, tile);
}

// Add a power-line cell and reconnect its four neighbors.
void PlaceCityPowerLineCell(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  if (tp->flags & TF_POWERABLE) return;
  int kind = tp->kind;
  int axis = ReadCityStraightLineAxis(city, tile, 0);
  int replacement;
  if (IsTileKindLandscape(kind)) replacement = TK_POWER_LINE;
  else if (kind == TK_ROAD && axis >= 0) replacement = TK_ROAD_POWER_CROSSING;
  else if (kind == TK_RAIL && axis >= 0) replacement = TK_RAIL_POWER_CROSSING;
  else if (kind == TK_HIGHWAY) replacement = TK_HIGHWAY_POWER;
  else return;
  replacement = CityTileKindForWater(replacement, HasTileWater(tp));
  ReplaceCityTileAndStatistics(city, tile, replacement);
  if (IsTileKindGroundCrossing(replacement)) WriteCityCrossingAxis(tp, axis);
  tp->flags |= TF_POWERABLE;
  // Placement reconnects north, south, west, east, matching the shared line-placement traversal.
  UpdatePowerLineConnections(city, tile);
  TileIndex row = city->map_width;
  int x = MapTileX(city, tile);
  if (tile >= row) UpdatePowerLineConnections(city, tile - row);
  if (tile + row < city->map_cell_count) UpdatePowerLineConnections(city, tile + row);
  if (x > 0) UpdatePowerLineConnections(city, tile - 1);
  if (x + 1 < city->map_width) UpdatePowerLineConnections(city, tile + 1);
}

// Water power lines use the same two kinds as dry lines. Removing one span follows its stored axis, preserving the
// banks and the water beneath the cables. Requires a valid origin; returns false for a different family and otherwise
// removes only the matching axis-aligned water line.
bool RemoveCityWaterPowerSpan(City *city, TileIndex origin, bool animate) {
  if (!IsCityWaterPowerLine(&city->tile[origin])) return false;
  int mask = ReadTransportConnections(&city->tile[origin]);
  if (mask != CONN_EW && mask != CONN_NS) return false;
  int axis = (mask & CONN_NS) != 0;
  TileIndex first = origin;
  for (;;) {
    TileIndex previous = GetRelativeTileOrInvalid(city, first, -!axis, -axis);
    if (previous == INVALID_TILE || !IsCityWaterPowerLine(&city->tile[previous]) ||
        ReadTransportConnections(&city->tile[previous]) != mask) break;
    first = previous;
  }
  if (animate) PlayCitySound(city, origin, CITY_SOUND_EXPLOSION);
  TileIndex tile = first;
  while (tile != INVALID_TILE) {
    TileInfo *tp = &city->tile[tile];
    if (!IsCityWaterPowerLine(tp) || ReadTransportConnections(tp) != mask) break;
    ReplaceCityTileAndStatistics(city, tile, ReadCityWaterGroundKind(tp));
    tp->flags &= ~(TF_POWERED | TF_POWERABLE);
    WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
    if (animate) ShowCityDemolitionEffect(city, tile, false);
    RefreshCityPowerConnections(city, tile);
    tile = GetRelativeTileOrInvalid(city, tile, !axis, axis);
  }
  return true;
}

int CityMapPowerLayout(int mask, int shape) {
  static const u8 kOffsets[16] = {0, 0, 1, 6, 0, 0, 7, 11, 1, 9, 1, 10, 8, 13, 12, 14};
  if (shape >= 1 && shape <= 4) return CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST + shape + 1;
  if (shape >= 9 && shape <= 12) return CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST + 1;
  return CITY_MAP_TILE_SPRITE_POWER_LINE_FIRST + kOffsets[mask];
}

// Cable drawing reads stored connections and only the adjoining endpoint heights.
static int PowerAnchorQuarter(const CityTileGeometry *geometry) {
  static const u8 kCorners[] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15};
  return 4 * geometry->surface_height + __builtin_popcount(kCorners[geometry->surface_shape]);
}

static bool DrawPowerTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) || !IsTileKindPowerLine(kind)) return false;
  CityTileGeometry geometry = *tile_geometry;
  if (IsTileKindPlainPowerLine(kind) && geometry.water == CITY_WATER_NONE) {
    CityMapSurface surface = {.shape = geometry.land_shape, .foundation = -1};
    DrawCityMapInfrastructureGround(city, tile, zoom, screen_x, ground_y, surface, &geometry);
  } else {
    DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, &geometry);
  }
  DrawCityMapPlacementTile(city, tile);
  if (!CityMapTileDetailsVisible(drawing)) return true;
  int rotation = CityMapCameraRotation(drawing);
  int mask = CityMapCameraMask(CityPowerConnectionMask(tp), rotation);
  bool tower = CityPowerLineHasTower(tp);
  int layout = IsTileKindPlainPowerLine(kind) && geometry.water == CITY_WATER_NONE ?
      CityMapPowerLayout(mask, geometry.land_shape) : CityMapViewTileSpriteIndex(city, tile);
  bool ground_crossing = IsTileKindGroundCrossing(kind);
  if ((!ground_crossing || IsCityMapGroundPass(drawing)) &&
      !IsTileKindPlainPowerLine(kind))
    DrawCityMapTransportArtwork(city, tile, layout, zoom, screen_x, ground_y, &geometry);
  int anchor = PowerAnchorQuarter(&geometry);
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  i64 baseline_q8 = (i64)ground_y * 256 -
      (anchor - 4 * geometry.surface_height) * CityMapHeightStep(zoom) * 64;
  for (int direction = 0; direction < 4; ++direction) {
    if (!(mask & (1 << direction))) continue;
    int grade = 0, other_support = 1;
    int world_direction = (direction + rotation) & 3;
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kLineXStep[world_direction], kLineYStep[world_direction]);
    if (neighbor != INVALID_TILE) {
      CityTileGeometry neighbor_geometry = ReadCityMapViewGeometry(city, neighbor);
      grade = PowerAnchorQuarter(&neighbor_geometry) - anchor;
      if (IsTileKindPowerLine(ReadTile(city, neighbor)))
        other_support = CityPowerLineHasTower(&city->tile[neighbor]);
    }
    grade = IntMax(-CITY_POWER_WIRE_GRADE_LIMIT, IntMin(grade, CITY_POWER_WIRE_GRADE_LIMIT));
    int sprite = first + CITY_LANDSCAPE_POWER_WIRE_FIRST +
        (direction * 4 + 2 * tower + other_support) * CITY_POWER_WIRE_GRADES +
        grade + CITY_POWER_WIRE_GRADE_LIMIT;
    DrawCityMapSpriteAtBaselineQ8(drawing, sprite, (i64)screen_x * 256, baseline_q8, 0);
  }
  if (tower) {
    int axis = mask == CONN_NS || mask == CONN_NORTH || mask == CONN_SOUTH;
    CityPoint point = PowerLinePatternPoint(city, tile);
    bool straight = mask == CONN_NS || mask == CONN_EW;
    int form = __builtin_popcount(mask) > 2 ? 5 : !straight ? 4 :
        axis + (((point.x * 13 + point.y * 7) & 4) ? 2 : 0);
    int sprite = IsCityWaterPowerLine(tp) ? CITY_LANDSCAPE_POWER_WATER_FIRST + axis :
        CITY_LANDSCAPE_POWER_TOWER_FIRST + geometry.surface_shape * CITY_POWER_TOWER_FORMS + form;
    DrawCityMapSpriteAtBaselineQ8(drawing, first + sprite, (i64)screen_x * 256, baseline_q8, 0);
  }
  if ((tp->flags & TF_POWER_SERVICE) == TF_POWERABLE)
    DrawCityMapSprite(drawing, CITY_SPRITE_CATALOG_SMALL_POWER_WARNING + zoom * 500,
        screen_x, ground_y - (4 << zoom), 0);
  return true;
}

static bool DrawPowerGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->game->drawing;
  DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, geometry);
  int kind = city->tile[tile].kind, layout = CityMapViewTileSpriteIndex(city, tile);
  if (IsTileKindPowerLine(kind) && IsTileKindGroundCrossing(kind))
    DrawCitySurfaceLineSprite(drawing,
        CitySurfaceLineSprite(city, tile, layout, zoom, CityMapCameraRotation(drawing)),
        screen_x, ground_y, CityMapViewTileFlip(city, tile));
  if (IsCityMapHighwayLayout(layout)) DrawCityHighwayCrossingGround(city, layout, zoom, screen_x, ground_y);
  return true;
}

const TileModule kCityPowerModule = {
  .tick = TickCitySurfaceLineGround, .draw = DrawPowerTile, .draw_ground = DrawPowerGround,
};
