#include "city/maps/buildings.h"
#include "city/maps/map.h"
#include "city/maps/layers.h"
#include "city/saveload/map_format.h"
#include "city/infrastructure/infrastructure.h"
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
  information.density = ReadDensity(city, tile);
  information.density_history = ReadDensityHistory(city, tile);
  information.traffic_density = ReadTrafficDensity(city, tile);
  information.land_value = ReadLandValue(city, tile);
  information.crime = ReadCrime(city, tile);
  information.pollution = ReadPollution(city, tile);
  information.police = ReadPoliceCoverage(city, tile);
  information.fire = ReadFireCoverage(city, tile);
  information.building_stage = CityTileCanStoreBuildingStage(&information.cell);
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

void CalculateCityLandUse(const i16 *tile_counts, u16 acres[CITY_LAND_USE_COUNT],
    u16 percent[CITY_LAND_USE_COUNT]) {
  static const u8 kThreshold[40] = {
    TKL_POWER_LINE_EAST_WEST, TKL_ROAD_EAST_WEST, TK_RESIDENTIAL_FIRST, TK_GAS_STATION_1X1_1,
    TK_WAREHOUSE_1X1_2, TK_CONSTRUCTION_1X1_1, TK_CHEAP_APARTMENTS, TK_SHOPPING_CENTER,
    TK_WAREHOUSE_2X2, TK_CONSTRUCTION_2X2_1, TK_LARGE_APARTMENT_BUILDING_3X3_1,
        TK_OFFICE_PARK,
    TK_CHEMICAL_PROCESSING_3X3, TK_CONSTRUCTION_3X3_1, TK_HYDRO_POWER, TK_CITY_HALL,
    TK_MUSEUM, TK_LARGE_PARK, TK_SCHOOL, TK_STADIUM,
    TK_PRISON, TK_COLLEGE, TK_ZOO, TK_WATER_PUMP,
    TK_RUNWAY_FIRST, TK_SUBWAY_STATION, TK_WATER_TOWER, TK_BUS_STATION,
    TK_PARKING_LOT_2X2_1, TK_MAYORS_HOUSE, TK_WATER_TREATMENT_PLANT, TK_LIBRARY,
    TK_HANGAR_3X3, TK_CHURCH, TK_MARINA, TK_MISSILE_SILO,
    TK_DESALINATION_PLANT, TK_PLYMOUTH_ARCOLOGY, TK_BRAUN_LLAMA_DOME, TK_CLEAR
  };
  static const u8 kCategory[40] = {
    CITY_LAND_USE_RECREATION, CITY_LAND_USE_POWER, CITY_LAND_USE_TRANSPORTATION,
    CITY_LAND_USE_RESIDENTIAL, CITY_LAND_USE_COMMERCIAL, CITY_LAND_USE_INDUSTRIAL,
    CITY_LAND_USE_NONE, CITY_LAND_USE_RESIDENTIAL, CITY_LAND_USE_COMMERCIAL,
    CITY_LAND_USE_INDUSTRIAL, CITY_LAND_USE_NONE, CITY_LAND_USE_RESIDENTIAL,
    CITY_LAND_USE_COMMERCIAL, CITY_LAND_USE_INDUSTRIAL, CITY_LAND_USE_NONE,
    CITY_LAND_USE_POWER, CITY_LAND_USE_HEALTH_SAFETY, CITY_LAND_USE_EDUCATION,
    CITY_LAND_USE_RECREATION, CITY_LAND_USE_EDUCATION, CITY_LAND_USE_RECREATION,
    CITY_LAND_USE_HEALTH_SAFETY, CITY_LAND_USE_EDUCATION, CITY_LAND_USE_RECREATION,
    CITY_LAND_USE_WATER, CITY_LAND_USE_PORTS_AIRPORTS, CITY_LAND_USE_TRANSPORTATION,
    CITY_LAND_USE_WATER, CITY_LAND_USE_TRANSPORTATION, CITY_LAND_USE_PORTS_AIRPORTS,
    CITY_LAND_USE_NONE, CITY_LAND_USE_WATER, CITY_LAND_USE_EDUCATION,
    CITY_LAND_USE_PORTS_AIRPORTS, CITY_LAND_USE_RESIDENTIAL, CITY_LAND_USE_RECREATION,
    CITY_LAND_USE_PORTS_AIRPORTS, CITY_LAND_USE_WATER, CITY_LAND_USE_ARCOLOGIES,
    CITY_LAND_USE_RECREATION
  };
  memset(acres, 0, CITY_LAND_USE_COUNT * sizeof(acres[0]));
  memset(percent, 0, CITY_LAND_USE_COUNT * sizeof(percent[0]));

  for (i32 tile = TKL_PARK; tile < TK_COUNT; ++tile) {
    i32 index = 0;
    while (kThreshold[index] <= tile) {
      if (++index >= 40)
        break;
    }
    if (index >= 40 || kCategory[index] == CITY_LAND_USE_NONE)
      continue;
    i32 group = kCategory[index] - 1;
    acres[group] += tile_counts[tile];
  }

  u32 total = 0;
  for (i32 group = 0; group < CITY_LAND_USE_COUNT; ++group)
    total += acres[group];
  if (total == 0)
    return;
  for (i32 group = 0; group < CITY_LAND_USE_COUNT; ++group)
    percent[group] = 100 * acres[group] / total;
}
