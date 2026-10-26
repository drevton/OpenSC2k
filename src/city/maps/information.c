#include "city/maps/information.h"
#include "city/maps/labels.h"
#include "city/infrastructure/utilities.h"
#include "city/maps/terrain.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/map_types.h"
#include "city/state.h"
#include <string.h>

static int CityTileMetricRating(int metric) {
  static const u8 kRatingLimits[] = {0, 1, 60, 120, 180};
  int rating = 1;
  while (rating < 5 && kRatingLimits[rating] <= metric) ++rating;
  return rating;
}

// Capture tile facts without changing City or consulting local windows/tools.
CityTileInformation ReadCityTileInformation(const City *city, TileIndex tile) {
  CityTileInformation information = {.tile = tile};
  information.cell = city->tile[tile];
  CityPoint point = MapTilePt(city, tile);
  int kind = information.cell.kind;
  int facility = ReadCityFacilityOverlay(city, tile);
  if (city->individual_facilities && facility &&
      ReadCityFacilityRecord(city, CityFacilityOverlaySlot(facility))->type == kind) {
    information.has_facility_age = true;
    information.facility_years = city->annual_facilities.details[CityFacilityOverlaySlot(facility)].years;
  }
  information.x = point.x;
  information.y = point.y;
  CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
  const CityHalfMapStatistics *half = &city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)];
  const CityQuarterMapStatistics *quarter = &city->quarter[(point.y >> 2) * (city->map_width >> 2) + (point.x >> 2)];
  information.density = quarter->density;
  information.density_history = quarter->density_history;
  information.traffic_density = half->traffic_density;
  information.land_value = half->land_value;
  information.crime = half->crime;
  information.pollution = half->pollution;
  information.police = quarter->police_coverage;
  information.fire = quarter->fire_coverage;
  information.building_stage = IsTileKindDryBuilding(information.cell.kind);
  int zone = ReadCityZoneKind(&information.cell);
  information.show_traffic = zone == ZONE_NONE &&
      (IsTileKindRoadRoute(kind) || IsTileKindStraightHighwayFamily(kind) || IsTileKindMultiHighway(kind) ||
          (IsTileKindBridge(kind) && !IsTileKindRailBridge(kind)));
  if (information.show_traffic) {
    int traffic = 0;
    if (point.y > 0) traffic += ReadTrafficDensity(city, tile - city->map_width);
    if (point.x > 0) traffic += ReadTrafficDensity(city, tile - 1);
    if (point.y < city->map_height - 1) traffic += ReadTrafficDensity(city, tile + city->map_width);
    if (point.x < city->map_width - 1) traffic += ReadTrafficDensity(city, tile + 1);
    if (IsTileKindMultiHighway(kind) || IsTileKindStraightHighwayFamily(kind)) traffic *= 2;
    information.traffic = traffic >> 3;
  }
  int altitude = geometry.land_height;
  int sea_level = city->geography.edit_height;
  information.underwater = altitude < sea_level;
  bool water = geometry.water != CITY_WATER_NONE;
  information.rocky = information.underwater || water;
  information.altitude_feet = information.underwater ? 100 * (sea_level - altitude) - 50 :
      !water && geometry.land_shape != CITY_TERRAIN_FLAT ?
      100 * (altitude - sea_level + 1) : 100 * (altitude - sea_level) + 50;
  information.crime_rating = CityTileMetricRating(information.crime);
  information.pollution_rating = CityTileMetricRating(information.pollution);
  information.show_services = !IsTileKindUnbuilt(kind) && zone != ZONE_MILITARY && !information.rocky;
  information.powered = information.cell.flags & TF_POWERED;
  information.watered = (information.cell.flags & TF_WATER_SERVICE) == TF_WATER_SERVICE;
  if (kind == TK_WATER_PUMP) {
    information.water_supply = CITY_TILE_WATER_PRODUCTION;
    information.water = CityWaterPumpSupply(city, tile) * 720;
  } else if (kind == TK_WATER_TOWER) {
    information.water_supply = CITY_TILE_WATER_STORAGE;
    TileIndex start = GetBuildingStartTile(city, tile);
    if (start == INVALID_TILE) return information;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        TileIndex cell = GetRelativeTileOrInvalid(city, start, dx, dy);
        if (cell != INVALID_TILE && (city->tile[cell].flags & TF_WATERED)) information.water += 10000;
      }
  }
  return information;
}

// Tile descriptions follow native kind semantics, independently of replaceable artwork bindings.
int CityTileInformationCategory(int kind) {
  // The first building description follows these infrastructure and small-home groups.
  // Unassigned native kinds keep the clear-ground category.
  static const u8 kBaseCategories[TK_GAS_STATION_1X1_1] = {
    [TK_RUBBLE] = CITY_TILE_CATEGORY_RUBBLE,
    [TK_RADIOACTIVE_WASTE] = CITY_TILE_CATEGORY_RADIOACTIVE_WASTE,
    [TK_TREE_FIRST] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_7] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_8] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_MATURE_FIRST] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_10] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_DENSE_FIRST] = CITY_TILE_CATEGORY_TREES,
    [TK_TREE_LAST] = CITY_TILE_CATEGORY_TREES,
    [TK_SHORE_TREE_FIRST] = CITY_TILE_CATEGORY_TREES,
    [TK_SHORE_TREE_2] = CITY_TILE_CATEGORY_TREES,
    [TK_SHORE_TREE_3] = CITY_TILE_CATEGORY_TREES,
    [TK_SHORE_TREE_LAST] = CITY_TILE_CATEGORY_TREES,
    [TK_POWER_LINE] = CITY_TILE_CATEGORY_POWER_LINES,
    [TK_POWER_LINE_TOWER] = CITY_TILE_CATEGORY_POWER_LINES,
    [TK_POWER_LINE_WATER] = CITY_TILE_CATEGORY_POWER_LINES,
    [TK_POWER_LINE_TOWER_WATER] = CITY_TILE_CATEGORY_POWER_LINES,
    [TK_ROAD] = CITY_TILE_CATEGORY_ROAD,
    [TK_RAIL] = CITY_TILE_CATEGORY_RAIL,
    [TK_TUNNEL_ENTRANCE] = CITY_TILE_CATEGORY_TUNNEL,
    [TK_ROAD_POWER_CROSSING] = CITY_TILE_CATEGORY_ROAD_CROSSING,
    [TK_ROAD_RAIL_CROSSING] = CITY_TILE_CATEGORY_ROAD_CROSSING,
    [TK_RAIL_POWER_CROSSING] = CITY_TILE_CATEGORY_RAIL_CROSSING,
    [TK_HIGHWAY] = CITY_TILE_CATEGORY_HIGHWAY,
    [TK_HIGHWAY_RAIL] = CITY_TILE_CATEGORY_HIGHWAY,
    [TK_HIGHWAY_ROAD] = CITY_TILE_CATEGORY_HIGHWAY,
    [TK_HIGHWAY_POWER] = CITY_TILE_CATEGORY_HIGHWAY,
    [TK_ROAD_BRIDGE_SPAN] = CITY_TILE_CATEGORY_SUSPENSION_BRIDGE,
    [TK_ROAD_BRIDGE_RAIL] = CITY_TILE_CATEGORY_SUSPENSION_BRIDGE,
    [TK_ROAD_BRIDGE_ROAD] = CITY_TILE_CATEGORY_SUSPENSION_BRIDGE,
    [TK_ROAD_BRIDGE_POWER] = CITY_TILE_CATEGORY_SUSPENSION_BRIDGE,
    [TK_ROAD_BRIDGE_WATER] = CITY_TILE_CATEGORY_SUSPENSION_BRIDGE,
    [TK_RAIL_BRIDGE_SPAN] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_RAIL_BRIDGE_RAIL] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_RAIL_BRIDGE_ROAD] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_RAIL_BRIDGE_POWER] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_RAIL_BRIDGE_WATER] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_ROAD_BRIDGE_HEAD] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_RAIL_BRIDGE_HEAD] = CITY_TILE_CATEGORY_BRIDGE,
    [TK_HIGHWAY_ENTRANCE] = CITY_TILE_CATEGORY_HIGHWAY_ENTRANCE,
    [TK_HIGHWAY_INCLINE] = CITY_TILE_CATEGORY_HIGHWAY_MULTI,
    [TK_HIGHWAY_BEND] = CITY_TILE_CATEGORY_HIGHWAY_MULTI,
    [TK_HIGHWAY_JUNCTION] = CITY_TILE_CATEGORY_HIGHWAY_MULTI,
    [TK_HIGHWAY_BRIDGE_SPAN] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_HIGHWAY_BRIDGE_RAIL] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_HIGHWAY_BRIDGE_ROAD] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_HIGHWAY_BRIDGE_POWER] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_HIGHWAY_BRIDGE_WATER] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_HIGHWAY_BRIDGE_HEAD] = CITY_TILE_CATEGORY_HIGHWAY_BRIDGE,
    [TK_SUBWAY_TO_RAIL] = CITY_TILE_CATEGORY_SUBWAY_TO_RAIL,
    [TK_LOWER_CLASS_HOMES_FIRST] = CITY_TILE_CATEGORY_LOWER_CLASS_HOMES,
    [TK_LOWER_CLASS_HOMES_113] = CITY_TILE_CATEGORY_LOWER_CLASS_HOMES,
    [TK_LOWER_CLASS_HOMES_114] = CITY_TILE_CATEGORY_LOWER_CLASS_HOMES,
    [TK_LOWER_CLASS_HOMES_LAST] = CITY_TILE_CATEGORY_LOWER_CLASS_HOMES,
    [TK_MIDDLE_CLASS_HOMES_FIRST] = CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES,
    [TK_MIDDLE_CLASS_HOMES_117] = CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES,
    [TK_MIDDLE_CLASS_HOMES_118] = CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES,
    [TK_MIDDLE_CLASS_HOMES_LAST] = CITY_TILE_CATEGORY_MIDDLE_CLASS_HOMES,
    [TK_LUXURY_HOMES_FIRST] = CITY_TILE_CATEGORY_LUXURY_HOMES,
    [TK_LUXURY_HOMES_121] = CITY_TILE_CATEGORY_LUXURY_HOMES,
    [TK_LUXURY_HOMES_122] = CITY_TILE_CATEGORY_LUXURY_HOMES,
    [TK_LUXURY_HOMES_LAST] = CITY_TILE_CATEGORY_LUXURY_HOMES,
  };
  if (kind < TK_GAS_STATION_1X1_1) return kBaseCategories[kind];
  // The small park uses a former building slot but keeps its independent description.
  if (kind == TK_PARK) return CITY_TILE_CATEGORY_PARK;
  return kind - TK_GAS_STATION_1X1_1 + CITY_TILE_CATEGORY_COUNT;
}

void CalculateCityLandUse(const CityStatistics *statistics, u32 acres[CITY_LAND_USE_COUNT],
    u16 percent[CITY_LAND_USE_COUNT]) {
  // Select the first exclusive upper limit above the statistic; table order is significant.
  static const struct { u8 limit, category; } kLandUseGroups[] = {
    {TSTAT_POWER_LINE, CITY_LAND_USE_RECREATION},
    {TSTAT_ROAD, CITY_LAND_USE_POWER},
    {TK_RESIDENTIAL_FIRST, CITY_LAND_USE_TRANSPORTATION},
    {TK_GAS_STATION_1X1_1, CITY_LAND_USE_RESIDENTIAL},
    {TK_WAREHOUSE_1X1_2, CITY_LAND_USE_COMMERCIAL},
    {TK_CONSTRUCTION_1X1_1, CITY_LAND_USE_INDUSTRIAL},
    {TK_CHEAP_APARTMENTS, CITY_LAND_USE_NONE},
    {TK_SHOPPING_CENTER, CITY_LAND_USE_RESIDENTIAL},
    {TK_WAREHOUSE_2X2, CITY_LAND_USE_COMMERCIAL},
    {TK_CONSTRUCTION_2X2_1, CITY_LAND_USE_INDUSTRIAL},
    {TK_LARGE_APARTMENT_BUILDING_3X3_1, CITY_LAND_USE_NONE},
    {TK_OFFICE_PARK, CITY_LAND_USE_RESIDENTIAL},
    {TK_CHEMICAL_PROCESSING_3X3, CITY_LAND_USE_COMMERCIAL},
    {TK_CONSTRUCTION_3X3_1, CITY_LAND_USE_INDUSTRIAL},
    {TK_HYDRO_POWER, CITY_LAND_USE_NONE},
    {TK_CITY_HALL, CITY_LAND_USE_POWER},
    {TK_MUSEUM, CITY_LAND_USE_HEALTH_SAFETY},
    {TK_LARGE_PARK, CITY_LAND_USE_EDUCATION},
    {TK_SCHOOL, CITY_LAND_USE_RECREATION},
    {TK_STADIUM, CITY_LAND_USE_EDUCATION},
    {TK_PRISON, CITY_LAND_USE_RECREATION},
    {TK_COLLEGE, CITY_LAND_USE_HEALTH_SAFETY},
    {TK_ZOO, CITY_LAND_USE_EDUCATION},
    {TK_WATER_PUMP, CITY_LAND_USE_RECREATION},
    {TK_RUNWAY_FIRST, CITY_LAND_USE_WATER},
    {TK_SUBWAY_STATION, CITY_LAND_USE_PORTS_AIRPORTS},
    {TK_WATER_TOWER, CITY_LAND_USE_TRANSPORTATION},
    {TK_BUS_STATION, CITY_LAND_USE_WATER},
    {TK_PARKING_LOT_2X2_1, CITY_LAND_USE_TRANSPORTATION},
    {TK_MAYORS_HOUSE, CITY_LAND_USE_PORTS_AIRPORTS},
    {TK_WATER_TREATMENT_PLANT, CITY_LAND_USE_NONE},
    {TK_LIBRARY, CITY_LAND_USE_WATER},
    {TK_HANGAR_3X3, CITY_LAND_USE_EDUCATION},
    {TK_CHURCH, CITY_LAND_USE_PORTS_AIRPORTS},
    {TK_MARINA, CITY_LAND_USE_RESIDENTIAL},
    {TK_MISSILE_SILO, CITY_LAND_USE_RECREATION},
    {TK_DESALINATION_PLANT, CITY_LAND_USE_PORTS_AIRPORTS},
    {TK_PLYMOUTH_ARCOLOGY, CITY_LAND_USE_WATER},
    {TK_BRAUN_LLAMA_DOME, CITY_LAND_USE_ARCOLOGIES},
    {TK_CLEAR, CITY_LAND_USE_RECREATION},
  };
  enum { kGroupCount = sizeof(kLandUseGroups) / sizeof(kLandUseGroups[0]) };
  memset(acres, 0, CITY_LAND_USE_COUNT * sizeof(acres[0]));
  memset(percent, 0, CITY_LAND_USE_COUNT * sizeof(percent[0]));

  // These thresholds describe statistic slots. Building slots retain their established IDs after native kind remaps.
  for (int statistic = TSTAT_PARK; statistic < TK_COUNT; ++statistic) {
    i32 index = 0;
    while (kLandUseGroups[index].limit <= statistic) {
      if (++index >= kGroupCount)
        break;
    }
    if (index >= kGroupCount || kLandUseGroups[index].category == CITY_LAND_USE_NONE)
      continue;
    i32 group = kLandUseGroups[index].category - 1;
    acres[group] += statistics->count[statistic];
  }

  u64 total = 0;
  for (i32 group = 0; group < CITY_LAND_USE_COUNT; ++group)
    total += acres[group];
  if (total == 0)
    return;
  for (i32 group = 0; group < CITY_LAND_USE_COUNT; ++group)
    percent[group] = (u64)100 * acres[group] / total;
}
