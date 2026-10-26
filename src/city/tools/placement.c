#include "city/tools/tools.h"

#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/maps/terrain.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"

// Zoning and de-zoning share these rules in quotes, previews and execution.
// The caller supplies an existing tile; this only inspects its current contents.
bool CanZoneCityTile(const TileInfo *tp) {
  return !HasTileWater(tp) && !IsTileKindTransportOrBuilding(tp->kind) &&
      tp->kind != TK_RADIOACTIVE_WASTE && tp->kind != TK_PARK &&
      ReadTerrainCode(tp) == CITY_TERRAIN_FLAT && ReadCityZoneKind(tp) != ZONE_MILITARY;
}

// Placement

// Invalidate the changed zone footprint and count eligible new cells.
void UpdateCityZoneRectanglePreview(City *city, TileIndex origin, TileIndex previous, TileIndex next) {
  city->construction.terrain_surcharge_count = 0;
  city->construction.cell_count = 0;
  CityRect before = MapTileRectangle(city, origin, previous);
  CityRect after = MapTileRectangle(city, origin, next);
  CityRect bounds = UnionCityRects(before, after);
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile_index = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile_index) {
      const TileInfo *tp = &city->tile[tile_index];
      if (!CanZoneCityTile(tp)) continue;
      int zone = ReadCityZoneKind(tp);
      bool in_previous = y >= before.top && y < before.bottom && x >= before.left && x < before.right;
      bool in_next = y >= after.top && y < after.bottom && x >= after.left && x < after.right;
      if (zone != city->construction.zone_code && in_next)
        ++city->construction.cell_count;
      if (in_previous != in_next)
        InvalidateTile(city, tile_index);
    }
  }
}

// Sum the cost of eligible cells in an inclusive rectangle for an explicit zone and unit charge. Invalid endpoints
// return -1; no zoning, construction working state, treasury or RNG changes occur.
i64 CalculateCityZoneCost(const City *city, TileIndex start, TileIndex end, int zone, int unit_cost) {
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return -1;
  CityRect bounds = MapTileRectangle(city, start, end);
  i64 cost = 0;
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    const TileInfo *tp = &city->tile[tile];
    for (int x = bounds.left; x < bounds.right; ++x, ++tp)
      if (CanZoneCityTile(tp) && ReadCityZoneKind(tp) != zone)
        cost += unit_cost;
  }
  return cost;
}

// Check whether any tile in a resolved inclusive rectangle already has the requested zone. Invalid endpoints return
// false; this query neither applies zoning nor changes construction state.
bool CityZoneRectangleContainsZone(const City *city, TileIndex start, TileIndex end, int zone) {
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end)) return false;
  CityRect bounds = MapTileRectangle(city, start, end);
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile)
      if (ReadCityZoneKind(&city->tile[tile]) == zone) return true;
  }
  return false;
}

const i16 kCityZoneCosts[ZONE_RESERVED_FIRST] = {
  [ZONE_NONE] = 1, [ZONE_RESIDENTIAL_LOW] = 5, [ZONE_RESIDENTIAL_HIGH] = 10,
  [ZONE_COMMERCIAL_LOW] = 5, [ZONE_COMMERCIAL_HIGH] = 10,
  [ZONE_INDUSTRIAL_LOW] = 5, [ZONE_INDUSTRIAL_HIGH] = 10,
  [ZONE_MILITARY] = -1, [ZONE_AIRPORT] = 250, [ZONE_SEAPORT] = 150,
};

// Apply the live prepared zone/cost state to valid endpoints. The caller owns pricing and payment; nested callers
// may have overwritten construction state, so this operation does not reconstruct it from an earlier selection.
i64 ApplyPreparedCityZoneRectangle(City *city, TileIndex origin_tile, TileIndex endpoint) {
  if (city->construction.cost == 0 &&
      CityZoneRectangleContainsZone(city, origin_tile, endpoint, city->construction.zone_code)) {
    return true;
  }
  if (!city->construction.cost) return CITY_COMMAND_COST_BLOCKED;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  CityRect bounds = MapTileRectangle(city, origin_tile, endpoint);
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile) {
      TileInfo *tp = &city->tile[tile];
      if (!CanZoneCityTile(tp)) continue;
      if (city->construction.zone_code == ZONE_NONE && tp->kind == TK_RUBBLE)
        ReplaceCityTileAndStatistics(city, tile, TK_CLEAR);
      WriteCityZoneKind(tp, city->construction.zone_code);
      InvalidateTile(city, tile);
    }
  }
  return true;
}

// Apply the supplied ZoneKind to an inclusive rectangle with resolved endpoints. Invalid endpoints and zone codes are
// rejected; TEST only quotes cost, and execution updates the construction working state before committing eligible
// cells.
i64 CmdPlaceZoneRect(City *city, u32 flags, TileIndex tile, int zone, TileIndex end) {
  if (!IsMapTileValid(city, tile) || !IsMapTileValid(city, end) ||
      zone < 0 || zone >= ZONE_RESERVED_FIRST || kCityZoneCosts[zone] < 0) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    i64 cost = CalculateCityZoneCost(city, tile, end, zone, kCityZoneCosts[zone]);
    return cost || CityZoneRectangleContainsZone(city, tile, end, zone) ? cost : CITY_COMMAND_COST_BLOCKED;
  }
  city->construction.stage = CITY_PLACEMENT_ZONE;
  city->construction.zone_code = zone;
  city->construction.cost = CalculateCityZoneCost(city, tile, end, zone, kCityZoneCosts[zone]);
  return ApplyPreparedCityZoneRectangle(city, tile, end);
}

// Tool costs retain all twelve variant slots, including unavailable entries.

const u32 kCityToolCosts[CITY_TOOL_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY] = {
  [CITY_TOOL_GROUP_DEMOLISH * CITY_TOOL_VARIANT_CAPACITY] = 1, 25, 25, 25, 1, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_TREE_WATER * CITY_TOOL_VARIANT_CAPACITY] = 3, 100, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_DISPATCH * CITY_TOOL_VARIANT_CAPACITY] = 0, 0, 0, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_POWER * CITY_TOOL_VARIANT_CAPACITY] = 2, 0, 4000, 400, 6600, 2000, 15000, 100, 1300, 28000, 40000,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_WATER * CITY_TOOL_VARIANT_CAPACITY] = 3, 100, 250, 500, 1000, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_REWARDS * CITY_TOOL_VARIANT_CAPACITY] = 0, 0, 0, 0, 0, 100000, 120000, 150000, 200000,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_ROAD * CITY_TOOL_VARIANT_CAPACITY] = 10, 100, 150, 25, 250, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_RAIL * CITY_TOOL_VARIANT_CAPACITY] = 25, 100, 500, 250, 250, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_PORT * CITY_TOOL_VARIANT_CAPACITY] = 150, 250, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_RESIDENTIAL * CITY_TOOL_VARIANT_CAPACITY] = 5, 10, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_COMMERCIAL * CITY_TOOL_VARIANT_CAPACITY] = 5, 10, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_INDUSTRIAL * CITY_TOOL_VARIANT_CAPACITY] = 5, 10, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_EDUCATION * CITY_TOOL_VARIANT_CAPACITY] = 250, 1000, 500, 1000, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_CIVIC * CITY_TOOL_VARIANT_CAPACITY] = 500, 500, 500, 3000, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_RECREATION * CITY_TOOL_VARIANT_CAPACITY] = 20, 150, 3000, 5000, 1000, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_SIGN * CITY_TOOL_VARIANT_CAPACITY] = 0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_QUERY * CITY_TOOL_VARIANT_CAPACITY] = 0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
  [CITY_TOOL_GROUP_CENTER * CITY_TOOL_VARIANT_CAPACITY] = 0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
      CITY_TOOL_COST_UNAVAILABLE,
};
