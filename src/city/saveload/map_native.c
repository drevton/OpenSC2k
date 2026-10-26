#include "city/saveload/map_native.h"

#include "city/maps/tile_storage.h"
#include "city/saveload/types.h"
#include "city/state.h"
#include "city/maps/module.h"
#include "city/maps/ground.h"
#include "city/maps/terrain.h"
#include "city/maps/trees.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/power_lines.h"
#include "city/terrain/climate.h"
#include "base/byte_order.h"
#include <string.h>

// Native map packet: six byte planes and the existing weather lattice. Stock base chunks remain projections.
u32 CityGroundExtensionSize(const City *city) {
  if (!city->native_map_storage) return 0;
  return 6 * city->map_cell_count + CITY_SAVE_CLIMATE_HEADER_SIZE +
      TerrainSnowNoiseSize(city->map_width, city->map_height);
}

bool EncodeCityGroundExtension(const City *city, u8 *bytes, u32 size) {
  int cells = city->map_cell_count;
  if (!city->native_map_storage || size < CityGroundExtensionSize(city) || size > CITY_SAVE_EXTENSION_LIMIT)
    return false;
  for (int tile = 0; tile < cells; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    bytes[tile] = tp->terrain;
    bytes[cells + tile] = tp->zone;
    bytes[2 * cells + tile] = tp->flags;
    bytes[3 * cells + tile] = tp->kind;
    WriteLE16(bytes + 4 * cells + 2 * tile, tp->altitude);
  }
  u32 controls = TerrainSnowNoiseSize(city->map_width, city->map_height);
  u8 *header = bytes + 6 * cells;
  memcpy(header, "CLIM", 4);
  WriteLE32(header + 4, controls);
  memcpy(header + CITY_SAVE_CLIMATE_HEADER_SIZE, city->snow.points, controls);
  return true;
}

// Validate native tile discriminators and footprint topology directly, without a stock projection or staging map.
bool ValidateCityTileRecords(const City *city) {
  TileIndex cells = city->map_cell_count;
  bool valid = true;
  // Validate local discriminators before inspecting neighboring geometry.
  for (TileIndex tile = 0; tile < cells && valid; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    valid = (tp->zone & CITY_TILE_SLOPED) ? ReadTerrainCode(tp) >= 1 &&
        ReadTerrainCode(tp) <= CITY_TERRAIN_SHAPE_13 : ReadCityZoneKind(tp) <= ZONE_SEAPORT;
    if (!(ReadCityTileClasses(tp->kind) & ~CITY_TILE_MODULE_MASK) && tp->kind != TK_CLEAR &&
        !IsTileKindExposedWater(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE) valid = false;
    if (IsCityBridgeHead(tp)) {
      int style = ReadTileBridgeStyle(tp);
      if (IsTileKindHighwayBridge(tp->kind)) {
        valid = valid && (style == HIGHWAY_BRIDGE_STYLE_STANDARD || style == HIGHWAY_BRIDGE_STYLE_REINFORCED);
      } else {
        valid = valid && style < BRIDGE_STYLE_COUNT &&
            (style != BRIDGE_STYLE_DRAWBRIDGE || tp->kind == TK_ROAD_BRIDGE_HEAD) &&
            !(tp->altitude & CITY_BRIDGE_HIGHWAY_PHASE_MASK);
      }
    }
  }
  for (TileIndex tile = 0; tile < cells && valid; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    int kind = tp->kind, ground = ReadGroundKind(tp), metadata = tp->terrain & 240;
    bool bridge = IsTileKindBridge(kind), head = IsCityBridgeHead(tp), wet = HasTileWater(tp);
    if (IsTileKindShoreBank(kind)) {
      valid = valid && ground <= CITY_GROUND_SHORE;
    } else if (wet) {
      valid = valid && (tp->terrain & 15) <= TK_WATER_BANK - TK_WATER &&
          (!IsTileKindExposedWater(kind) || !(tp->terrain & 15));
    } else {
      valid = valid && ground <= CITY_GROUND_SHORE;
      if (ground >= CITY_GROUND_ROUGH && ground <= CITY_GROUND_RECOVERY_LAST &&
          kind != TK_CLEAR && !IsTileKindTree(kind) && !bridge && !IsTileKindGroundTransport(kind)) valid = false;
    }
    if (IsTileKindShoreTree(kind)) {
      CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
      valid = valid && geometry.land_height == geometry.water_height && geometry.land_shape != CITY_TERRAIN_FLAT;
    }
    if (IsTileKindTree(kind)) {
      valid = valid && ReadTreeSpecies(tp) < CityTreeSpeciesCount(ReadCityTileBaseClimate(city, tp)) &&
          ReadTreeGrowth(tp) <= CITY_TREE_DEAD && ground != CITY_GROUND_ROCKS;
    } else if (kind != TK_CLEAR && kind != TK_WATER_BANK)
      valid = valid && !(tp->zone & (CITY_TILE_GROWTH_MASK & ~(kind == TK_WIND_POWER ? CITY_WIND_MODERN_MASK : 0)));
    if (bridge) {
      int allowed = CITY_BRIDGE_AXIS_MASK | (head ? CITY_BRIDGE_HEAD_FACING_MASK : 0) |
          (IsTileKindHighwayBridge(kind) ? CITY_BRIDGE_HIGHWAY_RIGHT : 0);
      valid = valid && !ReadCityZoneKind(tp) && !(metadata & ~allowed) && ValidateCityBridgeSpan(city, tile);
    } else if (kind == TK_CLEAR || IsTileKindExposedWater(kind)) valid = valid && !metadata;
    else if (IsTileKindGroundCrossing(kind)) valid = valid && !(metadata & ~CITY_CROSSING_AXIS_MASK);
    else if (IsTileKindVariant(kind)) valid = valid && !(metadata & ~(CITY_TILE_VARIANT_MASK |
        (kind == TK_HIGHWAY_ENTRANCE ? CITY_HIGHWAY_RAMP_MIRROR_MASK : 0)));
    else if (IsTileKindSingleTileFootprint(kind)) valid = valid && !metadata;
    if (IsTileKindPlainPowerLine(kind) && !wet)
      valid = valid && ReadTransportConnections(tp) == FindCityPowerConnectionMask(city, tile);
    if (!valid) break;
    if (IsTileKindHighwayFootprint(kind)) {
      CityRect bounds = CityHighwayFootprintBounds(city, tile);
      int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
      bool rectangle = width == 2 && height == 2;
      if (IsTileKindStraightHighwayFamily(kind)) rectangle |= ReadCityHighwayAxis(tp) ?
          width == 2 && height == 1 : width == 1 && height == 2;
      valid = valid && rectangle && bounds.left >= 0 && bounds.top >= 0 &&
          bounds.right <= city->map_width && bounds.bottom <= city->map_height;
      if (valid) for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
        const TileInfo *other = &city->tile[MapTileXY(city, bounds.left + dx, bounds.top + dy)];
        int expected = (dx ? CITY_BUILDING_CONTINUE_X : 0) | (dy ? CITY_BUILDING_CONTINUE_Y : 0);
        valid = valid && (IsTileKindStraightHighwayFamily(kind) ?
            IsTileKindStraightHighwayFamily(other->kind) : other->kind == kind) &&
            (other->terrain & CITY_BUILDING_CONTINUATION_MASK) == expected &&
            !((other->terrain ^ tp->terrain) & CITY_HIGHWAY_ORIENTATION_MASK);
      }
    }
    if (IsTileKindBuilding(kind)) {
      CityPoint point = MapTilePt(city, tile);
      if (metadata & CITY_BUILDING_CONTINUE_X) valid = valid && point.x > 0 &&
          city->tile[tile - 1].kind == kind &&
          !((city->tile[tile - 1].terrain ^ tp->terrain) & CITY_BUILDING_ORIENTATION_MASK);
      if (metadata & CITY_BUILDING_CONTINUE_Y) valid = valid && point.y > 0 &&
          city->tile[tile - city->map_width].kind == kind &&
          !((city->tile[tile - city->map_width].terrain ^ tp->terrain) & CITY_BUILDING_ORIENTATION_MASK);
    }
  }
  return valid;
}
