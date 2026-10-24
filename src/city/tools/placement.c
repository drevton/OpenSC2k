#include "city/infrastructure/lines.h"
#include "city/infrastructure/spans.h"
#include "city/maps/terrain.h"
#include "city/saveload/map_format.h"
#include "city/tools/tools.h"
#include "city/engine/commands.h"
#include "city/fixes.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"

// Map explicit construction tool choices to a building kind. Unlisted and picker entries return TK_CLEAR.
// This does not check placement or mutate City.
int GetCityFacilityTile(int group, int variant) {
  if (group < 0 || group >= CITY_TOOL_GROUP_QUERY || variant < 0 || variant >= CITY_TOOL_VARIANT_CAPACITY)
    return TK_CLEAR;
  static const u8 kTiles[CITY_TOOL_GROUP_QUERY][CITY_TOOL_VARIANT_CAPACITY] = {
      [CITY_TOOL_GROUP_POWER] = {
          [CITY_POWER_COAL] = TK_COAL_POWER_PLANT,
          [CITY_POWER_OIL] = TK_OIL_POWER_PLANT,
          [CITY_POWER_GAS] = TK_GAS_POWER_PLANT,
          [CITY_POWER_NUCLEAR] = TK_NUCLEAR_POWER_PLANT,
          [CITY_POWER_WIND] = TK_WIND_POWER,
          [CITY_POWER_SOLAR] = TK_SOLAR_POWER_PLANT,
          [CITY_POWER_MICROWAVE] = TK_MICROWAVE_POWER_PLANT,
          [CITY_POWER_FUSION] = TK_FUSION_POWER_PLANT},
      [CITY_TOOL_GROUP_WATER] = {
          [CITY_WATER_PUMP] = TK_WATER_PUMP,
          [CITY_WATER_TOWER] = TK_WATER_TOWER,
          [CITY_WATER_TREATMENT] = TK_WATER_TREATMENT_PLANT,
          [CITY_WATER_DESALINATION] = TK_DESALINATION_PLANT},
      [CITY_TOOL_GROUP_REWARDS] = {
          [CITY_REWARD_MAYORS_HOUSE] = TK_MAYORS_HOUSE,
          [CITY_REWARD_CITY_HALL] = TK_CITY_HALL,
          [CITY_REWARD_STATUE] = TK_STATUE,
          [CITY_REWARD_LLAMA_DOME] = TK_BRAUN_LLAMA_DOME,
          [CITY_REWARD_PLYMOUTH_ARCOLOGY] = TK_PLYMOUTH_ARCOLOGY,
          [CITY_REWARD_FOREST_ARCOLOGY] = TK_FOREST_ARCOLOGY,
          [CITY_REWARD_DARCO_ARCOLOGY] = TK_DARCO_ARCOLOGY,
          [CITY_REWARD_LAUNCH_ARCOLOGY] = TK_LAUNCH_ARCOLOGY},
      [CITY_TOOL_GROUP_ROAD] = {[CITY_ROAD_BUS_DEPOT] = TK_BUS_STATION},
      [CITY_TOOL_GROUP_RAIL] = {
          [CITY_RAIL_DEPOT] = TK_RAIL_STATION,
          [CITY_RAIL_SUBWAY_STATION] = TK_SUBWAY_STATION},
      [CITY_TOOL_GROUP_EDUCATION] = {
          [CITY_EDUCATION_SCHOOL] = TK_SCHOOL,
          [CITY_EDUCATION_COLLEGE] = TK_COLLEGE,
          [CITY_EDUCATION_LIBRARY] = TK_LIBRARY,
          [CITY_EDUCATION_MUSEUM] = TK_MUSEUM},
      [CITY_TOOL_GROUP_CIVIC] = {
          [CITY_CIVIC_POLICE] = TK_POLICE_STATION,
          [CITY_CIVIC_FIRE] = TK_FIRE_STATION,
          [CITY_CIVIC_HOSPITAL] = TK_HOSPITAL,
          [CITY_CIVIC_PRISON] = TK_PRISON},
      [CITY_TOOL_GROUP_RECREATION] = {
          [CITY_RECREATION_SMALL_PARK] = TK_PARK,
          [CITY_RECREATION_BIG_PARK] = TK_LARGE_PARK,
          [CITY_RECREATION_ZOO] = TK_ZOO,
          [CITY_RECREATION_STADIUM] = TK_STADIUM,
          [CITY_RECREATION_MARINA] = TK_MARINA}};
  return kTiles[group][variant];
}

// Zoning and de-zoning share these rules in quotes, previews and execution.
// The caller supplies an existing tile; this only inspects its current contents.
static bool CanZoneCityTile(const TileInfo *tp) {
  return !HasTileWater(tp) && !IsTileKindTransportOrBuilding(tp->kind) &&
      tp->kind != TK_RADIOACTIVE_WASTE && tp->kind != TK_PARK &&
      ReadTerrainCode(tp) == CITY_TERRAIN_FLAT && ReadCityZoneKind(tp) != ZONE_MILITARY;
}

// Read-only admission checks: previews must not execute commands or consume RNG. Invalid tile/tool combinations return
// false; the result is admission only and native commands use resolved domain predicates.
bool CanPlaceCityToolCell(const City *city, TileIndex tile, int group, int variant, int terrain_options) {
  if (!IsMapTileValid(city, tile) || group < 0 || group > CITY_TOOL_GROUP_SIGN ||
      variant < 0 || variant >= CITY_TOOL_VARIANT_CAPACITY) return false;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  int terrain = ReadTerrainCode(tp);
  bool water = HasTileWater(tp);
  int zone = ReadCityZoneKind(tp);
  bool military = zone == ZONE_MILITARY;
  CityPoint point = MapTilePt(city, tile);
  if (group == CITY_TOOL_GROUP_SIGN) return true;
  if (group == CITY_TOOL_GROUP_DEMOLISH) {
    if (variant == CITY_DEMOLISH_RAISE || variant == CITY_DEMOLISH_LOWER)
      return GetCityTerrainCost(city, tile, variant == CITY_DEMOLISH_RAISE, variant,
          terrain_options).error == CITY_COMMAND_COST_OK;
    if (variant == CITY_DEMOLISH_CLEAR) return CanBulldozeCityTile(city, tile, false, false);
    if (variant != CITY_DEMOLISH_DEZONE || zone == ZONE_NONE) return true;
  }
  if (group == CITY_TOOL_GROUP_TREE_WATER) {
    if (variant == CITY_TREE_WATER_WATER)
      return CanPlaceCityCanal(city, tile);
    bool shore = IsTileKindShoreBank(kind) &&
        ReadCityTileGeometry(city, tile).water_surface == CITY_WATER_SURFACE_SHORE;
    return (!water || shore) && IsTileKindUnbuilt(kind) && kind != TK_RADIOACTIVE_WASTE;
  }
  if (group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_HYDRO)
    return IsTileKindExposedWater(kind) &&
        ReadCityTileGeometry(city, tile).water_surface == CITY_WATER_SURFACE_RAPIDS;
  if ((group == CITY_TOOL_GROUP_WATER && variant == CITY_WATER_PIPES) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY)) {
    int depth = (tp->altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
    if (military || depth == 1 || depth == 2) return false;
    int underground = tp->underground & CITY_UNDERGROUND_KIND_MASK;
    if (group == CITY_TOOL_GROUP_WATER)
      return underground == TUK_CLEAR || underground == TUK_SUBWAY_EW ||
          underground == TUK_SUBWAY_NS ||
          (underground >= TUK_PIPE_FIRST && underground <= TUK_SUBWAY_NS_PIPE_EW);
    return underground == TUK_CLEAR || underground == TUK_PIPE_EW ||
        underground == TUK_PIPE_NS ||
        IsSubwayTile(underground);
  }
  bool road = group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_ROAD;
  bool rail = group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_RAIL;
  bool power = group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_LINES;
  if (road || rail || power) {
    if (IsTileKindBridge(kind))
      return (road || rail) && CanPlaceCityBridgeUnderpass(city, tile,
          rail ? TK_RAIL : TK_ROAD, -1);
    if (military || kind == TK_RADIOACTIVE_WASTE || kind == TK_PARK) return false;
    if (water)
      return FindCityInfrastructureSpan(city, tile, city->geography.rotation, true).end != INVALID_TILE;
    if (IsTileKindLandscape(kind)) return true;
    int axis = ReadCityStraightLineAxis(city, tile, 0);
    if (road)
      return IsTileKindRoadRoute(kind) || axis >= 0;
    if (rail)
      return IsTileKindRailRoute(kind) || axis >= 0;
    return (tp->flags & TF_POWERABLE) ||
        (!IsTileKindPlainPowerLine(kind) && axis >= 0);
  }
  if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_HIGHWAY) {
    point.x &= ~1; point.y &= ~1;
    return point.x + 1 < city->map_width && point.y + 1 < city->map_height &&
        GetTransportSpanCompatibilityMask(city, MapTileXYOrInvalid(city, point.x, point.y)) !=
            CITY_SPAN_COMPATIBILITY_INVALID;
  }
  if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_TUNNEL)
    return !water && IsTileKindLandscape(kind) && kind != TK_RADIOACTIVE_WASTE &&
        (tp->underground & CITY_UNDERGROUND_KIND_MASK) == TUK_CLEAR &&
        terrain >= CITY_TERRAIN_SHAPE_1 && terrain <= CITY_TERRAIN_SHAPE_4 &&
        !(tp->altitude & CITY_ALTITUDE_TUNNEL_MASK);
  if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_ONRAMP)
    return CanPlaceCityOnramp(city, tile);
  if (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY_RAIL_CONNECTION)
    return CanPlaceCitySubwayRailConnection(city, tile);
  bool zoning = (group >= CITY_TOOL_GROUP_PORT && group <= CITY_TOOL_GROUP_INDUSTRIAL) ||
      (group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_DEZONE);
  if (zoning) return CanZoneCityTile(tp);
  bool marina = group == CITY_TOOL_GROUP_RECREATION && variant == CITY_RECREATION_MARINA;
  int size = kCityToolFootprintSizes[group * CITY_TOOL_VARIANT_CAPACITY + variant];
  if (size > 1 && (point.x == 0 || point.y == 0 || point.x == city->map_width - 1 || point.y == city->map_height - 1))
    return false;
  if (group == CITY_TOOL_GROUP_REWARDS && variant == CITY_REWARD_MAYORS_HOUSE &&
      city->statistics.count[TK_MAYORS_HOUSE]) return false;
  if (IsTileKindTransportOrBuilding(kind) || kind == TK_RADIOACTIVE_WASTE || kind == TK_PARK ||
      (military && !(city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH))))
    return false;
  return (marina && water) || (!water && terrain == CITY_TERRAIN_FLAT);
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

// Translate a retained UI tool group and variant to a domain ZoneKind. Unsupported combinations use the routine's
// no-zone result; native commands carry ZoneKind directly instead of calling this adapter.
int CityToolZoneCode(int group, int variant) {
  if (group < 0 || group >= CITY_TOOL_GROUP_COUNT || variant < 0 || variant > CITY_ZONE_DENSE) return ZONE_NONE;
  static const u8 kZoneCodes[CITY_TOOL_GROUP_COUNT][2] = {
      [CITY_TOOL_GROUP_PORT] = {ZONE_SEAPORT, ZONE_AIRPORT},
      [CITY_TOOL_GROUP_RESIDENTIAL] = {ZONE_RESIDENTIAL_LOW, ZONE_RESIDENTIAL_HIGH},
      [CITY_TOOL_GROUP_COMMERCIAL] = {ZONE_COMMERCIAL_LOW, ZONE_COMMERCIAL_HIGH},
      [CITY_TOOL_GROUP_INDUSTRIAL] = {ZONE_INDUSTRIAL_LOW, ZONE_INDUSTRIAL_HIGH}};
  return kZoneCodes[group][variant];
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
static bool CityZoneRectangleContainsZone(const City *city, TileIndex start, TileIndex end, int zone) {
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

// Quote raw zoning work over an inclusive rectangle for an explicit supported ZoneKind.
// Rejects invalid endpoints or unavailable kinds without consulting treasury or changing City.
static CityCommandCost GetCityZoneRectangleCost(const City *city, TileIndex start, TileIndex end, int zone) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      zone < 0 || zone >= ZONE_RESERVED_FIRST || kCityZoneCosts[zone] < 0) return quote;
  quote.cost = CalculateCityZoneCost(city, start, end, zone, kCityZoneCosts[zone]);
  quote.error = !quote.cost && !CityZoneRectangleContainsZone(city, start, end, zone) ? CITY_COMMAND_COST_BLOCKED :
      CITY_COMMAND_COST_OK;
  return quote;
}

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
      if (city->construction.zone_code == ZONE_NONE && IsLegacyRubbleKind(tp->kind))
        ReplaceCityTileAndStatistics(city, tile, TK_CLEAR);
      WriteCityZoneKind(tp, city->construction.zone_code);
      InvalidateTile(city, tile);
    }
  }
  return true;
}

// Prepare and commit zoning from resolved endpoints and a domain ZoneKind. Caller admission must validate indices and
// kind; updates construction state and returns the actual placement/error result.
static i64 PlaceCityZoneRectangleAt(City *city, TileIndex start, TileIndex end, int zone) {
  city->construction.stage = CITY_PLACEMENT_ZONE;
  city->construction.zone_code = zone;
  city->construction.cost = CalculateCityZoneCost(city, start, end, zone, kCityZoneCosts[zone]);
  return ApplyPreparedCityZoneRectangle(city, start, end);
}

// Apply the supplied ZoneKind to an inclusive rectangle with resolved endpoints. Invalid endpoints and zone codes are
// rejected; TEST only quotes cost, and execution updates the construction working state before committing eligible
// cells.
i64 CmdPlaceZoneRect(City *city, u32 flags, TileIndex tile, int zone, TileIndex end) {
  if (!IsMapTileValid(city, tile) || !IsMapTileValid(city, end) ||
      zone < 0 || zone >= ZONE_RESERVED_FIRST || kCityZoneCosts[zone] < 0) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(GetCityZoneRectangleCost(city, tile, end, zone));
  return PlaceCityZoneRectangleAt(city, tile, end, zone);
}

// Tables

const u8 kCityToolFootprintSizes[CITY_TOOL_FOOTPRINT_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY] = {
  1, 1, 1, 1, 1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 0, 4, 1, 4, 4, 4, 1, 4, 4, 4, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, 2, 2, 3, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  2, 3, 1, 4, 0, 4, 4, 4, 4, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 2, 1, 1, 2, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, 2, 1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  3, 4, 2, 3, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  3, 3, 3, 4, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, 3, 4, 4, 3, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, 1, 1, 1, 1, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE, CITY_TOOL_FOOTPRINT_UNAVAILABLE,
  CITY_TOOL_FOOTPRINT_UNAVAILABLE
};

const u32 kCityToolCosts[CITY_TOOL_GROUP_COUNT * CITY_TOOL_VARIANT_CAPACITY] = {
  1, 25, 25, 25, 1, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  3, 100, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  0, 0, 0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  2, 0, 4000, 400, 6600, 2000, 15000, 100, 1300, 28000, 40000, CITY_TOOL_COST_UNAVAILABLE,
  3, 100, 250, 500, 1000, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  0, 0, 0, 0, 0, 100000, 120000, 150000, 200000, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE,
  10, 100, 150, 25, 250, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  25, 100, 500, 250, 250, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  150, 250, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  5, 10, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  5, 10, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  5, 10, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  250, 1000, 500, 1000, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE,
  500, 500, 500, 3000, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE,
  20, 150, 3000, 5000, 1000, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  0, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE,
  CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE, CITY_TOOL_COST_UNAVAILABLE
};

const u8 kZoneTerrainSurchargeFlags[16] = {
  0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0
};

const u8 kPowerPlantToolVariants[TK_COAL_POWER_PLANT - TK_GAS_POWER_PLANT + 1] = {
  [TK_GAS_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_GAS,
  [TK_OIL_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_OIL,
  [TK_NUCLEAR_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_NUCLEAR,
  [TK_SOLAR_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_SOLAR,
  [TK_MICROWAVE_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_MICROWAVE,
  [TK_FUSION_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_FUSION,
  [TK_COAL_POWER_PLANT - TK_GAS_POWER_PLANT] = CITY_POWER_COAL,
};

static CityCommandCost PlanCityConstruction(const City *city, TileIndex start, TileIndex end, int group,
    int variant, CityInfrastructureSpan *planned_span) {
  if (planned_span) *planned_span = (CityInfrastructureSpan){.end = INVALID_TILE, .error_tile = INVALID_TILE};
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) || group < 0 ||
      group > CITY_TOOL_GROUP_SIGN || variant < 0 || variant >= CITY_TOOL_VARIANT_CAPACITY) return quote;
  int unit_cost = kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + variant];
  if (unit_cost == (int)CITY_TOOL_COST_UNAVAILABLE) return quote;
  quote.error = CITY_COMMAND_COST_BLOCKED;
  bool highway = group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_HIGHWAY;
  bool road = group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_ROAD;
  bool rail = group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_RAIL;
  bool power = group == CITY_TOOL_GROUP_POWER && variant == CITY_POWER_LINES;
  bool pipe = group == CITY_TOOL_GROUP_WATER && variant == CITY_WATER_PIPES;
  bool subway = group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY;
  if (road || rail || power || pipe || subway || highway) {
    CityInfrastructureKind kind = highway ? CITY_INFRASTRUCTURE_HIGHWAY :
        road ? CITY_INFRASTRUCTURE_ROAD : rail ? CITY_INFRASTRUCTURE_RAIL :
        power ? CITY_INFRASTRUCTURE_POWER : pipe ? CITY_INFRASTRUCTURE_PIPE : CITY_INFRASTRUCTURE_SUBWAY;
    return GetCityInfrastructureCost(city, start, end, kind, -1, planned_span);
  } else if ((group >= CITY_TOOL_GROUP_PORT && group <= CITY_TOOL_GROUP_INDUSTRIAL) ||
      (group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_DEZONE)) {
    int zone = CityToolZoneCode(group, variant);
    quote.cost = CalculateCityZoneCost(city, start, end, zone, unit_cost);
    if (quote.cost < 0 || (quote.cost == 0 && !CityZoneRectangleContainsZone(city, start, end, zone)))
      return quote;
  } else if (group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_TUNNEL) {
    CityInfrastructureSpan tunnel = FindCityRoadTunnel(city, start);
    if (tunnel.end == INVALID_TILE) {
      if (tunnel.error) quote.error = tunnel.error;
      return quote;
    }
    quote.cost = (i64)tunnel.length * unit_cost;
  } else if (group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_CLEAR) {
    int error = GetCityBulldozeTileError(city, start, false, false);
    if (error) {
      if (error == CITY_COMMAND_COST_CLEAR_DEEP_WATER) quote.error = error;
      return quote;
    }
    quote.cost = GetCityBulldozeTileCost(city, start, false);
  } else if (group == CITY_TOOL_GROUP_DEMOLISH &&
      (variant == CITY_DEMOLISH_RAISE || variant == CITY_DEMOLISH_LOWER)) {
    quote = GetCityTerrainCost(city, start, variant == CITY_DEMOLISH_RAISE, variant, 0);
    if (quote.error) return quote;
  } else if (group == CITY_TOOL_GROUP_DEMOLISH && variant == CITY_DEMOLISH_LEVEL) {
    int height = ReadCityLandHeight(&city->tile[start]);
    int target = ReadCityLandHeight(&city->tile[end]);
    if (height == target) quote.cost = 0;
    else {
      quote = GetCityTerrainCost(city, start, height < target, variant, 0);
      if (quote.error) return quote;
    }
  } else {
    int replacement = GetCityFacilityTile(group, variant);
    if (replacement != TK_CLEAR) {
      return GetCityBuildingCost(city, start, replacement);
    } else if (!CanPlaceCityToolCell(city, start, group, variant, 0)) {
      return quote;
    }
    quote.cost = group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_SUBWAY_RAIL_CONNECTION ? 0 : unit_cost;
  }
  if (quote.cost < 0) return quote;
  quote.error = CITY_COMMAND_COST_OK;
  return quote;
}

CityInfrastructureSpan GetCityConstructionSpan(const City *city, TileIndex start, TileIndex end, int group, int variant) {
  CityInfrastructureSpan span;
  PlanCityConstruction(city, start, end, group, variant, &span);
  return span;
}

CityCommandCost PriceCityConstruction(const City *city, TileIndex start, TileIndex end, int group, int variant) {
  return ApplyCityCommandCostPolicy(city, PlanCityConstruction(city, start, end, group, variant, NULL));
}

// The preview retains the requested price even when the treasury cannot afford it.
CityCommandCost GetCityConstructionCost(const City *city, TileIndex start, TileIndex end, int group, int variant) {
  if ((group == CITY_TOOL_GROUP_ROAD && variant == CITY_ROAD_BRIDGE) ||
      (group == CITY_TOOL_GROUP_RAIL && variant == CITY_RAIL_BRIDGE)) {
    if (IsMapTileValid(city, start) && start == end)
      return (CityCommandCost){.error = CITY_COMMAND_COST_BRIDGE_SAME_SPOT, .error_tile = INVALID_TILE};
    CityInfrastructureSpan span = FindCityBridgeSpan(city, start, end, group == CITY_TOOL_GROUP_RAIL);
    i64 cost = CityInfrastructureSpanCost(span, span.styles);
    return ApplyCityCommandCostPolicy(city, (CityCommandCost){.cost = cost < 0 ? 0 : cost,
        .error = cost < 0 ? CITY_COMMAND_COST_BLOCKED : CITY_COMMAND_COST_OK, .error_tile = span.error_tile});
  }
  return PriceCityConstruction(city, start, end, group, variant);
}
