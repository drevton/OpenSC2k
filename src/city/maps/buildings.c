// TileInfo storage for buildings/facilities, including airport surfaces and the small park (zero-based bits):
// kind identifies the completed building or explicit construction/abandoned kind; every footprint cell has that kind.
// terrain[4..5] is facing 0..3 relative to the stored map. terrain[6] means continue toward the anchor in -X;
// terrain[7] means continue in -Y. Both links are clear at the top-left anchor; one-cell buildings need neither.
// terrain[0..3] keeps dry ground/coverage, or the underlying water kind minus TK_WATER for wet hydro/pier/marina.
// zone[0..3] is zoning unless zone[4] marks a slope instead. Wind power alone uses zone[5] for the modern form;
// zone[6..7] has no building payload. The other building kinds have no zone[5..7] payload.
// Dry building kinds except TK_PARK use altitude[5..9] for construction stage 0..31 (0 inactive), not water height.
// Wet facilities use that field for actual water height; TK_PARK is excluded by IsTileKindBuilding.
// altitude[0..4] remains land/bed height. Footprint/facility identities are City records, not overlay byte IDs.
// Utility flags, climate region, sign/effect overlay, tunnel depth and underground retain tile_storage.h meanings.

#include "drawing/maps/map_sprites.h"
#include "drawing/sprites/sources.h"
#include "city/maps/labels.h"
#include "city/events/events.h"
#include "city/infrastructure/networks.h"
#include "city/infrastructure/utilities.h"
#include "city/maps/tile_queue.h"
#include "city/saveload/map_format.h"
#include "city/engine/random.h"
#include "city/economy/facilities.h"
#include "city/maps/buildings.h"
#include "city/maps/terrain.h"

#include <assert.h>

#include "assets/state.h"
#include "game/audio.h"
#include "city/economy/economy.h"
#include "city/engine/commands.h"
#include "city/growth/growth.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/layers.h"
#include "city/maps/module.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "drawing/maps/map_animation.h"
#include "drawing/maps/map_effects.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "game/observation.h"
#include "game/state.h"

// Author each building property once; emit compact columns for simulation hot loops.
#define CITY_ZONED_BUILDING_PROPERTIES(X) \
  X(TK_LOWER_CLASS_HOMES_FIRST, 1, 1, 0, 0, 1) \
  X(TK_LOWER_CLASS_HOMES_113, 1, 1, 0, 0, 1) \
  X(TK_LOWER_CLASS_HOMES_114, 1, 1, 0, 0, 1) \
  X(TK_LOWER_CLASS_HOMES_LAST, 1, 1, 0, 0, 1) \
  X(TK_MIDDLE_CLASS_HOMES_FIRST, 1, 1, 0, 0, 1) \
  X(TK_MIDDLE_CLASS_HOMES_117, 1, 1, 0, 0, 1) \
  X(TK_MIDDLE_CLASS_HOMES_118, 1, 1, 0, 0, 1) \
  X(TK_MIDDLE_CLASS_HOMES_LAST, 1, 1, 0, 0, 1) \
  X(TK_LUXURY_HOMES_FIRST, 1, 1, 0, 0, 1) \
  X(TK_LUXURY_HOMES_121, 1, 1, 0, 0, 1) \
  X(TK_LUXURY_HOMES_122, 1, 1, 0, 0, 1) \
  X(TK_LUXURY_HOMES_LAST, 1, 1, 0, 0, 1) \
  X(TK_GAS_STATION_1X1_1, 1, 1, 0, 0, 1) \
  X(TK_BED_BREAKFAST_INN, 1, 1, 0, 0, 1) \
  X(TK_CONVENIENCE_STORE, 1, 1, 0, 0, 1) \
  X(TK_GAS_STATION_1X1_2, 1, 1, 0, 0, 1) \
  X(TK_SMALL_OFFICE_BUILDING, 1, 1, 0, 0, 1) \
  X(TK_OFFICE_BUILDING_1X1, 1, 1, 0, 0, 1) \
  X(TK_WAREHOUSE_1X1_1, 1, 1, 0, 0, 1) \
  X(TK_CASSIDY_S_TOY_STORE, 1, 1, 0, 0, 1) \
  X(TK_WAREHOUSE_1X1_2, 1, 1, 0, 6, 1) \
  X(TK_CHEMICAL_STORAGE, 1, 1, 0, 6, 1) \
  X(TK_WAREHOUSE_1X1_3, 1, 1, 0, 6, 1) \
  X(TK_INDUSTRIAL_SUBSTATION, 1, 1, 0, 6, 1) \
  X(TK_CONSTRUCTION_1X1_1, 1, 1, CITY_BUILDING_CONSTRUCTION, 0, 1) \
  X(TK_CONSTRUCTION_1X1_2, 1, 1, CITY_BUILDING_CONSTRUCTION, 0, 1) \
  X(TK_ABANDONED_BUILDING_1X1_1, 1, 1, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_ABANDONED_BUILDING_1X1_2, 1, 1, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_CHEAP_APARTMENTS, 2, 2, 0, 0, 2) \
  X(TK_APARTMENTS_2X2_1, 2, 2, 0, 0, 2) \
  X(TK_APARTMENTS_2X2_2, 2, 2, 0, 0, 2) \
  X(TK_NICE_APARTMENTS_2X2_1, 2, 2, 0, 0, 2) \
  X(TK_NICE_APARTMENTS_2X2_2, 2, 3, 0, 0, 3) \
  X(TK_CONDOMINIUM_2X2_1, 2, 3, 0, 0, 3) \
  X(TK_CONDOMINIUM_2X2_2, 2, 3, 0, 0, 3) \
  X(TK_CONDOMINIUM_2X2_3, 2, 3, 0, 0, 3) \
  X(TK_SHOPPING_CENTER, 2, 2, 0, 0, 2) \
  X(TK_GROCERY_STORE, 2, 2, 0, 0, 2) \
  X(TK_OFFICE_BUILDING_2X2_1, 2, 2, 0, 0, 2) \
  X(TK_RESORT_HOTEL, 2, 2, 0, 0, 2) \
  X(TK_OFFICE_BUILDING_2X2_2, 2, 2, 0, 0, 2) \
  X(TK_OFFICE_RETAIL, 2, 3, 0, 0, 3) \
  X(TK_OFFICE_BUILDING_2X2_3, 2, 3, 0, 0, 3) \
  X(TK_OFFICE_BUILDING_2X2_4, 2, 3, 0, 0, 3) \
  X(TK_OFFICE_BUILDING_2X2_5, 2, 3, 0, 0, 3) \
  X(TK_OFFICE_BUILDING_2X2_6, 2, 3, 0, 0, 3) \
  X(TK_WAREHOUSE_2X2, 2, 2, 0, 12, 2) \
  X(TK_CHEMICAL_PROCESSING_2X2, 2, 2, 0, 12, 2) \
  X(TK_FACTORY_2X2_1, 2, 2, 0, 12, 2) \
  X(TK_FACTORY_2X2_2, 2, 2, 0, 12, 2) \
  X(TK_FACTORY_2X2_3, 2, 3, 0, 18, 3) \
  X(TK_FACTORY_2X2_4, 2, 3, 0, 18, 3) \
  X(TK_FACTORY_2X2_5, 2, 3, 0, 18, 3) \
  X(TK_FACTORY_2X2_6, 2, 3, 0, 18, 3) \
  X(TK_CONSTRUCTION_2X2_1, 2, 2, CITY_BUILDING_CONSTRUCTION, 0, 2) \
  X(TK_CONSTRUCTION_2X2_2, 2, 2, CITY_BUILDING_CONSTRUCTION, 0, 2) \
  X(TK_CONSTRUCTION_2X2_3, 2, 3, CITY_BUILDING_CONSTRUCTION, 0, 3) \
  X(TK_CONSTRUCTION_2X2_4, 2, 3, CITY_BUILDING_CONSTRUCTION, 0, 3) \
  X(TK_ABANDONED_BUILDING_2X2_1, 2, 2, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_ABANDONED_BUILDING_2X2_2, 2, 2, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_ABANDONED_BUILDING_2X2_3, 2, 3, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_ABANDONED_BUILDING_2X2_4, 2, 3, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_LARGE_APARTMENT_BUILDING_3X3_1, 3, 4, 0, 0, 4) \
  X(TK_LARGE_APARTMENT_BUILDING_3X3_2, 3, 4, 0, 0, 4) \
  X(TK_CONDOMINIUM_3X3_1, 3, 4, 0, 0, 4) \
  X(TK_CONDOMINIUM_3X3_2, 3, 4, 0, 0, 4) \
  X(TK_OFFICE_PARK, 3, 4, 0, 0, 4) \
  X(TK_OFFICE_TOWER_3X3_1, 3, 4, 0, 0, 4) \
  X(TK_MINI_MALL, 3, 4, 0, 0, 4) \
  X(TK_THEATRE_SQUARE, 3, 4, 0, 0, 4) \
  X(TK_DRIVE_IN_THEATER, 3, 4, 0, 0, 4) \
  X(TK_OFFICE_TOWER_3X3_2, 3, 4, 0, 0, 4) \
  X(TK_OFFICE_TOWER_3X3_3, 3, 4, 0, 0, 4) \
  X(TK_PARKING_LOT, 3, 4, 0, 0, 4) \
  X(TK_HISTORIC_OFFICE_BUILDING, 3, 4, 0, 0, 4) \
  X(TK_CORPORATE_HEADQUARTERS, 3, 4, 0, 0, 4) \
  X(TK_CHEMICAL_PROCESSING_3X3, 3, 4, 0, 24, 4) \
  X(TK_LARGE_FACTORY, 3, 4, 0, 24, 4) \
  X(TK_INDUSTRIAL_THINGAMAJIG, 3, 4, 0, 24, 4) \
  X(TK_FACTORY_3X3, 3, 4, 0, 24, 4) \
  X(TK_LARGE_WAREHOUSE, 3, 4, 0, 24, 4) \
  X(TK_WAREHOUSE_3X3, 3, 4, 0, 24, 4) \
  X(TK_CONSTRUCTION_3X3_1, 3, 4, CITY_BUILDING_CONSTRUCTION, 0, 4) \
  X(TK_CONSTRUCTION_3X3_2, 3, 4, CITY_BUILDING_CONSTRUCTION, 0, 4) \
  X(TK_ABANDONED_BUILDING_3X3_1, 3, 4, CITY_BUILDING_ABANDONED, 0, 0) \
  X(TK_ABANDONED_BUILDING_3X3_2, 3, 4, CITY_BUILDING_ABANDONED, 0, 0)
#define CITY_OTHER_BUILDING_PROPERTIES(X) \
  X(TK_HYDRO_POWER, 1, 0, 0, 0, 0) \
  X(TK_WIND_POWER, 1, 0, 112, 0, 0) \
  X(TK_GAS_POWER_PLANT, 4, 0, 0, 10, 0) \
  X(TK_OIL_POWER_PLANT, 4, 0, 140, 25, 0) \
  X(TK_NUCLEAR_POWER_PLANT, 4, 0, 0, 2, 0) \
  X(TK_SOLAR_POWER_PLANT, 4, 0, 144, 0, 0) \
  X(TK_MICROWAVE_POWER_PLANT, 4, 0, 0, 0, 0) \
  X(TK_FUSION_POWER_PLANT, 4, 0, 174, 2, 0) \
  X(TK_COAL_POWER_PLANT, 4, 0, 0, 50, 0) \
  X(TK_CITY_HALL, 3, 0, 124, 0, 0) \
  X(TK_HOSPITAL, 3, 0, 0, 0, 0) \
  X(TK_POLICE_STATION, 3, 0, 148, 0, 0) \
  X(TK_FIRE_STATION, 3, 0, 0, 0, 0) \
  X(TK_MUSEUM, 3, 257, 153, 0, 0) \
  X(TK_PARK, 1, 0, 0, 0, 0) \
  X(TK_LARGE_PARK, 3, 514, 0, 0, 0) \
  X(TK_SCHOOL, 3, 0, 178, 0, 0) \
  X(TK_STADIUM, 4, 0, 0, 4, 0) \
  X(TK_PRISON, 4, 0, 132, 10, 0) \
  X(TK_COLLEGE, 4, 0, 0, 0, 0) \
  X(TK_ZOO, 4, 0, 158, 0, 0) \
  X(TK_STATUE, 1, 0, 0, 0, 0) \
  X(TK_WATER_PUMP, 1, 0, 162, 2, 0) \
  X(TK_RUNWAY_FIRST, 1, 0, 0, 10, 0) \
  X(TK_RUNWAY_LAST, 1, 0, 188, 10, 0) \
  X(TK_PIER, 1, 0, 0, 10, 0) \
  X(TK_CRANE, 1, 0, 136, 5, 0) \
  X(TK_CONTROL_TOWER_1X1_1, 1, 0, 0, 0, 0) \
  X(TK_CONTROL_TOWER_1X1_2, 1, 0, 166, 0, 0) \
  X(TK_WAREHOUSE_1X1_4, 1, 257, 0, 5, 0) \
  X(TK_BUILDING_1X1_1, 1, 257, 168, 5, 0) \
  X(TK_BUILDING_1X1_2, 1, 514, 0, 5, 0) \
  X(TK_TARMAC, 1, 514, 194, 10, 0) \
  X(TK_F_15B, 1, 0, 0, 10, 0) \
  X(TK_HANGAR_1X1, 1, 0, 138, 0, 0) \
  X(TK_SUBWAY_STATION, 1, 0, 0, 5, 0) \
  X(TK_RADAR, 1, 0, 170, 0, 0) \
  X(TK_WATER_TOWER, 2, 0, 0, 0, 0) \
  X(TK_BUS_STATION, 2, 0, 172, 3, 0) \
  X(TK_RAIL_STATION, 2, 0, 0, 4, 0) \
  X(TK_PARKING_LOT_2X2_1, 2, 0, 196, 2, 0) \
  X(TK_PARKING_LOT_2X2_2, 2, 0, 0, 2, 0) \
  X(TK_LOADING_BAY, 2, 0, 12, 2, 0) \
  X(TK_TOP_SECRET, 2, 257, 0, 2, 0) \
  X(TK_CARGO_YARD, 2, 514, 4, 10, 0) \
  X(TK_MAYORS_HOUSE, 2, 0, 0, 0, 0) \
  X(TK_WATER_TREATMENT_PLANT, 2, 112, 4, 10, 0) \
  X(TK_LIBRARY, 2, 140, 0, 0, 0) \
  X(TK_HANGAR_3X3, 2, 144, 4, 5, 0) \
  X(TK_CHURCH, 2, 174, 0, 0, 0) \
  X(TK_MARINA, 3, 124, 8, 0, 0) \
  X(TK_MISSILE_SILO, 3, 148, 0, 0, 0) \
  X(TK_DESALINATION_PLANT, 3, 153, 5, 0, 0) \
  X(TK_PLYMOUTH_ARCOLOGY, 4, 178, 0, 25, 0) \
  X(TK_FOREST_ARCOLOGY, 4, 132, 5, 10, 0) \
  X(TK_DARCO_ARCOLOGY, 4, 158, 0, 12, 0) \
  X(TK_LAUNCH_ARCOLOGY, 4, 162, 10, 15, 0) \
  X(TK_BRAUN_LLAMA_DOME, 4, 188, 0, 0, 0)

#define BUILDING_PROPERTY(kind, footprint, population, category, pollution, density) \
  [kind - TK_RESIDENTIAL_FIRST] = footprint,
const u8 kCityBuildingFootprintSize[TK_COUNT - TK_RESIDENTIAL_FIRST] = {
  CITY_ZONED_BUILDING_PROPERTIES(BUILDING_PROPERTY)
  CITY_OTHER_BUILDING_PROPERTIES(BUILDING_PROPERTY)
};
#undef BUILDING_PROPERTY

#define BUILDING_PROPERTY(kind, footprint, population, category, pollution, density) \
  [kind - TK_RESIDENTIAL_FIRST] = population,
const u16 kTilePopulationSize[TK_COUNT - TK_RESIDENTIAL_FIRST] = {
  CITY_ZONED_BUILDING_PROPERTIES(BUILDING_PROPERTY)
  CITY_OTHER_BUILDING_PROPERTIES(BUILDING_PROPERTY)
};
#undef BUILDING_PROPERTY

#define BUILDING_PROPERTY(kind, footprint, population, category, pollution, density) \
  [kind - TK_RESIDENTIAL_FIRST] = category,
const u8 kTileZoneCategory[TK_COUNT - TK_RESIDENTIAL_FIRST] = {
  CITY_ZONED_BUILDING_PROPERTIES(BUILDING_PROPERTY)
  CITY_OTHER_BUILDING_PROPERTIES(BUILDING_PROPERTY)
};
#undef BUILDING_PROPERTY

#define BUILDING_PROPERTY(kind, footprint, population, category, pollution, density) \
  [kind - TK_RESIDENTIAL_FIRST] = pollution,
const u8 kTilePollutionContribution[TK_COUNT - TK_RESIDENTIAL_FIRST] = {
  CITY_ZONED_BUILDING_PROPERTIES(BUILDING_PROPERTY)
  CITY_OTHER_BUILDING_PROPERTIES(BUILDING_PROPERTY)
};
#undef BUILDING_PROPERTY

#define BUILDING_PROPERTY(kind, footprint, population, category, pollution, density) \
  [kind - TK_RESIDENTIAL_FIRST] = density,
const u8 kTileDensityContribution[TK_HYDRO_POWER - TK_RESIDENTIAL_FIRST] = {
  CITY_ZONED_BUILDING_PROPERTIES(BUILDING_PROPERTY)
};
#undef BUILDING_PROPERTY

#undef CITY_ZONED_BUILDING_PROPERTIES
#undef CITY_OTHER_BUILDING_PROPERTIES

// One layout for all native rectangular building footprints. Existing building definitions still supply square
// dimensions; links do not encode a size. Requires the complete writable rectangle within the map and a supported
// facing; installs links only after its tile kinds exist.
void InitializeCityBuildingFootprint(City *city, TileIndex top_left, int width, int height, int facing) {
  int kind = ReadTile(city, top_left);
  if (IsTileKindSingleTileFootprint(kind)) {
    WriteTileZoneByte(&city->tile[top_left], ReadTileZoneByte(&city->tile[top_left]) & CITY_ZONE_KIND_MASK);
    return;
  }
  if (!IsTileKindBuilding(kind)) return;
  for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
    TileIndex tile = top_left + dy * city->map_width + dx;
    TileInfo *tp = &city->tile[tile];
    WriteTileZoneByte(tp, ReadCityZoneKind(tp) |
        (dx ? CITY_BUILDING_CONTINUE_X : 0) | (dy ? CITY_BUILDING_CONTINUE_Y : 0) |
        EncodeBuildingOrientation(facing));
  }
}

// Buildings currently have square footprints. All building-range kinds use the same immutable size table, shared with
// stock codecs. Kind must be supported by the building-size table; returns zero dimensions for non-footprints and
// currently ignores orientation.
CityPoint GetBuildingFootprintDimensions(int kind, int orientation) {
  (void)orientation;
  int size = kind == TK_PARK ? 1 : 0;
  if (IsTileKindBuilding(kind))
    size = kCityBuildingFootprintSize[kind - TK_RESIDENTIAL_FIRST];
  return (CityPoint){size, size};
}

// Follow building continuation links to the native footprint anchor. Requires a valid starting tile; malformed links
// or mismatched kinds return INVALID_TILE rather than selecting an unrelated footprint.
TileIndex GetBuildingStartTile(const City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  int kind = ReadTile(city, tile);
  if (IsTileKindHighwayFootprint(kind) || IsTileKindHighwayBridge(kind))
    return CityHighwayAnchor(city, tile);
  if (!IsTileKindBuilding(kind)) return tile;
  int x = MapTileX(city, tile);
  while (ReadTileZoneByte(&city->tile[tile]) & CITY_BUILDING_CONTINUE_X) {
    if (!x || ReadTile(city, tile - 1) != kind) return INVALID_TILE;
    --x;
    --tile;
  }
  while (ReadTileZoneByte(&city->tile[tile]) & CITY_BUILDING_CONTINUE_Y) {
    if (tile < city->map_width || ReadTile(city, tile - city->map_width) != kind) return INVALID_TILE;
    tile -= city->map_width;
  }
  return tile;
}

// Check that a valid building cell's continuation metadata stays within a consistent footprint. Rejects broken links
// left by decline rather than treating an interior remnant as a drawable corner.
static bool HasValidCityBuildingLinks(const City *city, TileIndex tile, int zone) {
  const TileInfo *tiles = city->tile;
  int kind = tiles[tile].kind;
  // Decline can replace a large building's perimeter while leaving its center.
  // Broken continuation links must not turn that interior tile into a new corner.
  if ((zone & CITY_BUILDING_CONTINUE_X) &&
      (!MapTileX(city, tile) || tiles[tile - 1].kind != kind)) return false;
  if ((zone & CITY_BUILDING_CONTINUE_Y) &&
      (tile < city->map_width || tiles[tile - city->map_width].kind != kind)) return false;
  return true;
}

// Test a valid building cell against the requested footprint corner after validating links. Corner must use the
// supported corner enum; result is read-only and malformed footprints return false.
SC2K_PRESERVE_MOST bool IsCityBuildingCorner(const City *city, TileIndex tile, int corner) {
  const TileInfo *tp = &city->tile[tile];
  if (!HasValidCityBuildingLinks(city, tile, ReadTileZoneByte(tp))) return false;
  bool right = (corner & 2) != 0;
  bool bottom = corner == CITY_BUILDING_CORNER_BOTTOM_LEFT || corner == CITY_BUILDING_CORNER_BOTTOM_RIGHT;
  if (!right && (ReadTileZoneByte(tp) & CITY_BUILDING_CONTINUE_X)) return false;
  if (!bottom && (ReadTileZoneByte(tp) & CITY_BUILDING_CONTINUE_Y)) return false;
  if (right && MapTileX(city, tile) + 1 < city->map_width &&
      city->tile[tile + 1].kind == tp->kind &&
      (ReadTileZoneByte(&city->tile[tile + 1]) & CITY_BUILDING_CONTINUE_X)) return false;
  TileIndex down = tile + city->map_width;
  if (bottom && down < city->map_cell_count && city->tile[down].kind == tp->kind &&
      (ReadTileZoneByte(&city->tile[down]) & CITY_BUILDING_CONTINUE_Y)) return false;
  return true;
}

// Compute visible corner bits from a valid building tile and matching strided zone metadata. Source must describe the
// same map and footprint; invalid continuation links return an empty mask.
SC2K_PRESERVE_MOST int CityBuildingCornerMask(const City *city, TileIndex tile, const u8 *zones, int stride) {
  const TileInfo *tp = &city->tile[tile];
  int zone = zones[tile * stride], width = city->map_width;
  if (!HasValidCityBuildingLinks(city, tile, zone)) return 0;
  bool left = !(zone & CITY_BUILDING_CONTINUE_X), top = !(zone & CITY_BUILDING_CONTINUE_Y);
  bool right = MapTileX(city, tile) + 1 >= width || city->tile[tile + 1].kind != tp->kind ||
      !(zones[(tile + 1) * stride] & CITY_BUILDING_CONTINUE_X);
  TileIndex down = tile + width;
  bool bottom = down >= city->map_cell_count || city->tile[down].kind != tp->kind ||
      !(zones[down * stride] & CITY_BUILDING_CONTINUE_Y);
  int mask = ((left ? CITY_FOOTPRINT_CORNERS_WEST : 0) | (right ? CITY_FOOTPRINT_CORNERS_EAST : 0)) &
      ((top ? CITY_FOOTPRINT_CORNERS_NORTH : 0) | (bottom ? CITY_FOOTPRINT_CORNERS_SOUTH : 0));
  int rotation = city->geography.rotation;
  return (((mask << rotation) | (mask >> (4 - rotation))) << 4) & CITY_ZONE_CORNER_MASK;
}

// Decode construction progress from a native tile that supports it. Requires a readable tile record; unsupported
// categories return zero rather than interpreting water-height bits as progress.
int ReadCityBuildingConstructionStage(const TileInfo *tp) {
  if (!CityTileCanStoreBuildingStage(tp)) return 0;
  return (tp->altitude & CITY_ALTITUDE_BUILDING_STAGE_MASK) >> CITY_ALTITUDE_BUILDING_STAGE_SHIFT;
}

// Return the temporary construction artwork/statistic kind for an unfinished developed building. Requires a readable
// native tile; returns zero for completed or unsupported categories and never changes the stored building kind.
int CityBuildingConstructionKind(const TileInfo *tp) {
  int kind = tp->kind;
  if (!IsTileKindZoneBuilding(kind) ||
      kTileZoneCategory[kind - TK_RESIDENTIAL_FIRST] != CITY_BUILDING_DEVELOPED ||
      !ReadCityBuildingConstructionStage(tp)) return 0;
  int size = kTilePopulationSize[kind - TK_RESIDENTIAL_FIRST];
  return kGrowthBaseTable[12 + size] + (kind & 1);
}

// Set a supported tile's bounded progress field and keep visible-kind statistics consistent. Returns false for invalid
// tile/stage/category; admitted changes invalidate the tile and preserve unrelated altitude bits.
bool SetCityBuildingConstructionStage(City *city, TileIndex tile, int stage) {
  if (stage < 0 || stage > CITY_BUILDING_CONSTRUCTION_STAGE_MAX || !IsMapTileValid(city, tile)) return false;
  TileInfo *tp = &city->tile[tile];
  if (!CityTileCanStoreBuildingStage(tp)) return false;
  int old_kind = CityBuildingConstructionKind(tp);
  if (!old_kind) old_kind = tp->kind;
  int altitude = tp->altitude;
  int next = (altitude & CITY_ALTITUDE_CLEAR_WATER) | (stage << CITY_ALTITUDE_BUILDING_STAGE_SHIFT);
  if (next != altitude) {
    tp->altitude = next;
    int new_kind = CityBuildingConstructionKind(tp);
    if (!new_kind) new_kind = tp->kind;
    if (old_kind != new_kind && ReadCityZoneKind(tp) != ZONE_MILITARY) {
      --city->statistics.count[old_kind];
      ++city->statistics.count[new_kind];
    }
    InvalidateTile(city, tile);
  }
  return true;
}

// Apply one progress value to every valid cell of the selected building footprint. Starting tile must be valid;
// malformed anchors are ignored and each cell uses SetCityBuildingConstructionStage for statistics and damage.
void SetCityBuildingFootprintStage(City *city, TileIndex tile, int stage) {
  TileIndex anchor = GetBuildingStartTile(city, tile);
  if (anchor == INVALID_TILE) return;
  CityPoint dimensions = GetBuildingFootprintDimensions(ReadTile(city, tile), ReadBuildingOrientation(city, tile));
  int width = dimensions.x, height = dimensions.y;
  for (int row = 0; row < height; ++row)
    for (int column = 0; column < width; ++column)
      SetCityBuildingConstructionStage(city, anchor + row * city->map_width + column, stage);
}

// Advance a valid building footprint toward completion by one scheduled progress step. Reads the current stage and
// updates the entire footprint; repeated calls are simulation effects, not presentation refreshes.
void AdvanceCityBuildingConstruction(City *city, TileIndex tile) {
  int stage = ReadCityBuildingConstructionStage(&city->tile[tile]);
  SetCityBuildingFootprintStage(city, tile, IntMax(0, stage - 8));
}

const CityBuildingDefinition kCityBuildings[TK_COUNT] = {
  [TK_HYDRO_POWER] = {.cost = 400, .size = 1},
  [TK_WIND_POWER] = {.cost = 100, .size = 1},
  [TK_COAL_POWER_PLANT] = {.cost = 4000, .size = 4},
  [TK_OIL_POWER_PLANT] = {.cost = 6600, .size = 4},
  [TK_GAS_POWER_PLANT] = {.cost = 2000, .size = 4},
  [TK_NUCLEAR_POWER_PLANT] = {.cost = 15000, .size = 4},
  [TK_SOLAR_POWER_PLANT] = {.cost = 1300, .size = 4},
  [TK_MICROWAVE_POWER_PLANT] = {.cost = 28000, .size = 4},
  [TK_FUSION_POWER_PLANT] = {.cost = 40000, .size = 4},
  [TK_WATER_PUMP] = {.cost = 100, .size = 1},
  [TK_WATER_TOWER] = {.cost = 250, .size = 2},
  [TK_WATER_TREATMENT_PLANT] = {.cost = 500, .size = 2},
  [TK_DESALINATION_PLANT] = {.cost = 1000, .size = 3},
  [TK_MAYORS_HOUSE] = {.cost = 0, .size = 2, .reward = 1},
  [TK_CITY_HALL] = {.cost = 0, .size = 3, .reward = 2},
  [TK_STATUE] = {.cost = 0, .size = 1, .reward = 3},
  [TK_BRAUN_LLAMA_DOME] = {.cost = 0, .size = 4, .reward = 4},
  [TK_PLYMOUTH_ARCOLOGY] = {.cost = 100000, .size = 4},
  [TK_FOREST_ARCOLOGY] = {.cost = 120000, .size = 4},
  [TK_DARCO_ARCOLOGY] = {.cost = 150000, .size = 4},
  [TK_LAUNCH_ARCOLOGY] = {.cost = 200000, .size = 4},
  [TK_BUS_STATION] = {.cost = 250, .size = 2},
  [TK_RAIL_STATION] = {.cost = 500, .size = 2},
  [TK_SUBWAY_STATION] = {.cost = 250, .size = 1},
  [TK_SCHOOL] = {.cost = 250, .size = 3},
  [TK_COLLEGE] = {.cost = 1000, .size = 4},
  [TK_LIBRARY] = {.cost = 500, .size = 2},
  [TK_MUSEUM] = {.cost = 1000, .size = 3},
  [TK_POLICE_STATION] = {.cost = 500, .size = 3},
  [TK_FIRE_STATION] = {.cost = 500, .size = 3},
  [TK_HOSPITAL] = {.cost = 500, .size = 3},
  [TK_PRISON] = {.cost = 3000, .size = 4},
  [TK_PARK] = {.cost = 20, .size = 1},
  [TK_LARGE_PARK] = {.cost = 150, .size = 3},
  [TK_ZOO] = {.cost = 3000, .size = 4},
  [TK_STADIUM] = {.cost = 5000, .size = 4},
  [TK_MARINA] = {.cost = 1000, .size = 3},
};

// The picked tile is the northwest cell for 2x2 buildings, and one cell inset for larger footprints.
// Preserve the required border margin while checking columns against width and rows against height.
static bool IsCityBuildingAtMapEdge(const City *city, CityPoint point, int size) {
  if (size == 2)
    return point.x < 1 || point.y < 1 || point.x + 3 > city->map_width || point.y + 3 > city->map_height;
  return size > 2 && (point.x < 2 || point.y < 2 || point.x + size > city->map_width ||
      point.y + size > city->map_height);
}

// Check every cell of a square facility footprint with side length 1..4. Returns zero when admitted, one for an
// ordinary obstruction or a notice code for a specific rejection; does not allocate records or edit the map.
static int GetCityFacilityPlacementError(const City *city, TileIndex origin_tile, int replacement, int size) {
  if (!IsMapTileValid(city, origin_tile) || size < 1 || size > 4) return 1;
  CityPoint origin_point = MapTilePt(city, origin_tile);
  if (size > 2) {
    --origin_point.y;
    --origin_point.x;
  }
  int wet_cells = 0;
  bool military_growth = city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH);
  for (int cell_y = origin_point.y; cell_y < origin_point.y + size; ++cell_y)
    for (int cell_x = origin_point.x; cell_x < origin_point.x + size; ++cell_x) {
      TileIndex cell_index = MapTileXYOrInvalid(city, cell_x, cell_y);
      if (cell_index == INVALID_TILE) return 1;
      const TileInfo *tp = &city->tile[cell_index];
      bool military = ReadCityZoneKind(tp) == ZONE_MILITARY;
      int margin = size > 1 && !(military_growth && military) ? 1 : 0;
      if (cell_x < margin || cell_x >= city->map_width - margin ||
          cell_y < margin || cell_y >= city->map_height - margin)
        return 1;
      int tile = tp->kind;
      if (IsTileKindTransportOrBuilding(tile) || tile == TK_RADIOACTIVE_WASTE || tile == TK_PARK ||
          (military && !military_growth))
        return 1;
      if (replacement == TK_MARINA && HasTileWater(tp)) {
        ++wet_cells;
        continue;
      }
      if (ReadTerrainCode(tp) != CITY_TERRAIN_FLAT ||
          HasTileWater(tp))
        return 1;
    }
  if (replacement == TK_MARINA && (wet_cells == 0 || wet_cells == 9))
    return CITY_NOTICE_MARINA_REQUIRES_SHORELINE;
  return 0;
}

// Quote the raw building price using its footprint, terrain and uniqueness rules.
// Treasury and editor policy belong to the shared command boundary; no callbacks or RNG draws occur.
CityCommandCost GetCityBuildingCost(const City *city, TileIndex tile, int kind) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, tile) || kind < 0 || kind >= TK_COUNT || !kCityBuildings[kind].size) return quote;
  quote.error = CITY_COMMAND_COST_BLOCKED;
  if (kind == TK_HYDRO_POWER) {
    if (!IsTileKindExposedWater(ReadTile(city, tile)) ||
        ReadCityTileGeometry(city, tile).water_surface != CITY_WATER_SURFACE_RAPIDS) return quote;
  } else {
    int size = kCityBuildings[kind].size;
    CityPoint point = MapTilePt(city, tile);
    if (IsCityBuildingAtMapEdge(city, point, size)) return quote;
    if (kind == TK_MAYORS_HOUSE && city->statistics.count[kind]) {
      quote.error = CITY_COMMAND_COST_ALREADY_BUILT;
      return quote;
    }
    int error = GetCityFacilityPlacementError(city, tile, kind, size);
    if (error) {
      quote.error = error == 1 ? CITY_COMMAND_COST_BLOCKED : error;
      return quote;
    }
  }
  quote.cost = kCityBuildings[kind].cost;
  quote.error = CITY_COMMAND_COST_OK;
  return quote;
}

// Place a square special-zone footprint with side length 1..4, without charging.
i64 PlaceSpecialZoneFootprint(City *city, TileIndex origin_tile, u8 replacement, int size) {
  assert(IsMapTileValid(city, origin_tile));
  int error = GetCityFacilityPlacementError(city, origin_tile, replacement, size);
  if (error) {
    return error == 1 ? CITY_COMMAND_COST_BLOCKED : -error;
  }
  TileIndex anchor = size > 2 ? origin_tile - city->map_width - 1 : origin_tile;
  int water_height = 0, water_flags = 0, water_ground = TK_WATER;
  if (replacement == TK_MARINA) {
    bool found = false;
    for (int dy = 0; dy < size && !found; ++dy) for (int dx = 0; dx < size; ++dx) {
      const TileInfo *tp = &city->tile[anchor + dy * city->map_width + dx];
      if (!HasTileWater(tp)) continue;
      water_height = ReadCityWaterHeight(tp);
      water_flags = tp->flags & TF_SALTWATER;
      water_ground = ReadCityWaterGroundKind(tp);
      found = true;
      break;
    }
  }
  int overlay = CreateFacilityInstance(city, anchor, replacement);
  if (city->individual_facilities && IsTileKindFacility(replacement) && !overlay) return CITY_COMMAND_COST_BLOCKED;
  int facing = ReadBuildingOrientation(city, anchor);
  int utility_flags = replacement == TK_LARGE_PARK || replacement == TK_PARK
      ? TF_PIPED
      : TF_UTILITY_CAPABILITY;
  for (int row = 0; row < size; ++row)
    for (TileIndex tile_index = anchor + row * city->map_width, end = tile_index + size;
        tile_index < end; ++tile_index) {
      TileInfo *tp = &city->tile[tile_index];
      int ground = ReadCityWaterGroundKind(tp);
      tp->flags = (tp->flags &
          ~(TF_PIPED | TF_POWERED | TF_POWERABLE)) | utility_flags;
      if (replacement == TK_MARINA) {
        tp->altitude = (tp->altitude & ~(CITY_ALTITUDE_LAND_MASK | CITY_ALTITUDE_WATER_MASK)) |
          IntMin(ReadCityLandHeight(tp), water_height) | water_height << CITY_ALTITUDE_WATER_SHIFT;
        tp->flags = (tp->flags & ~TF_SALTWATER) | water_flags;
      }
      ReplaceCityTileAndStatistics(city, tile_index, replacement);
      if (replacement == TK_MARINA)
        tp->terrain = (tp->terrain & 240) | ((ground ? ground : water_ground) - TK_WATER);
      WriteTileZoneByte(tp, ZONE_NONE);
      RefreshCityPowerConnections(city, tile_index);
      if (overlay != CITY_OVERLAY_NONE)
        WriteOverlay(city, tile_index, overlay);
    }
  InitializeCityBuildingFootprint(city, anchor, size, size, facing);
  if (replacement == TK_WIND_POWER &&
      city->geography.start_year + city->clock.year >= CITY_WIND_MODERN_YEAR)
    city->tile[origin_tile].zone |= CITY_WIND_MODERN_MASK;
  InvalidateTileRange(city, anchor, size, size);
  return true;
}

// Runways connect to both runway kinds; piers connect to piers and cranes. Replace each queued cell before inspecting
// neighbors. North/south/west/east order is significant because runway rubble consumes RNG in dequeue order.
void RemoveCityConnectedRunwayOrPier(City *city, TileIndex start, int kind, bool animate) {
  static const i8 kNeighborX[4] = {0, 0, -1, 1};
  static const i8 kNeighborY[4] = {-1, 1, 0, 0};
  bool runway = IsTileKindRunway(kind);
  int first_kind = runway ? TK_RUNWAY_FIRST : TK_PIER;
  int last_kind = runway ? TK_RUNWAY_LAST : TK_CRANE;
  CityPoint start_point = MapTilePt(city, start);
  ResetCityTileQueueAndPush(city, start_point.x, start_point.y);
  while (CityTileQueueLength(city) != 0) {
    PointU16 point = PopCityTileCoordinate(city);
    TileIndex tile = MapTileXY(city, point.x, point.y);
    TileInfo *tp = &city->tile[tile];
    if (runway)
      ReplaceCityTileVariant(city, tile, TK_RUBBLE, Sc2kCrtRand(city) & 3);
    else
      ReplaceCityTileAndStatistics(city, tile, ReadCityWaterGroundKind(tp));
    if (animate) ShowCityDemolitionEffect(city, tile, false);
    if (tp->kind != TK_RUBBLE) WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
    tp->flags &= ~(TF_POWERED | TF_POWERABLE);
    RefreshCityPowerConnections(city, tile);

    for (int direction = 0; direction < 4; ++direction) {
      TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kNeighborX[direction], kNeighborY[direction]);
      if (neighbor == INVALID_TILE) continue;
      int neighbor_kind = city->tile[neighbor].kind;
      if (neighbor_kind == first_kind || neighbor_kind == last_kind) {
        CityPoint neighbor_point = MapTilePt(city, neighbor);
        PushCityTileCoordinate(city, neighbor_point.x, neighbor_point.y);
      }
    }
  }
}

// Place the specified building kind with automatic orientation (-1) or an explicit orientation (0 through 3). Rejects
// invalid tiles, unsupported kinds and nonzero reserved input; TEST quotes admission, while execution preserves the
// placement result and invalidates the footprint.
i64 CmdPlaceBuilding(City *city, u32 flags, TileIndex tile, int kind, int orientation, int reserved) {
  if (!IsMapTileValid(city, tile) || kind < 0 || kind >= TK_COUNT || !kCityBuildings[kind].size ||
      orientation < -1 || orientation > 3 || reserved ||
      (orientation >= 0 && (!IsTileKindBuilding(kind) || kind == TK_HYDRO_POWER)))
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(GetCityBuildingCost(city, tile, kind));
  return PlaceCityBuilding(city, tile, kind);
}

// Place an explicit building footprint without charging. The command pays before calling
// FinishCityBuildingPlacement; failed footprint/protest decisions keep their original result and RNG.
i64 PlaceCityBuilding(City *city, TileIndex origin_tile, int kind) {
  if (kind < 0 || kind >= TK_COUNT || !kCityBuildings[kind].size) return CITY_COMMAND_COST_INVALID;
  if (kind == TK_HYDRO_POWER) return PlaceCityWaterPowerFacility(city, origin_tile);
  CityPoint origin_point = MapTilePt(city, origin_tile);
  const TileIndex tile_index = origin_tile;
  if (kind == TK_MAYORS_HOUSE && city->statistics.count[TK_MAYORS_HOUSE] != 0) {
    return CITY_COMMAND_COST_ALREADY_BUILT;
  }
  int size = kCityBuildings[kind].size;
  if (IsCityBuildingAtMapEdge(city, origin_point, size)) return -CITY_NOTICE_PLACEMENT_AT_MAP_EDGE;
  if (kind == TK_WATER_TREATMENT_PLANT || kind == TK_COAL_POWER_PLANT ||
      kind == TK_OIL_POWER_PLANT || kind == TK_NUCLEAR_POWER_PLANT || kind == TK_GAS_POWER_PLANT ||
      kind == TK_PRISON) {
    int nearby_zones = 0;
    for (int zone_y = origin_point.y - 8; zone_y < origin_point.y + size + 8; ++zone_y)
      for (int zone_x = origin_point.x - 8; zone_x < origin_point.x + size + 8; ++zone_x) {
        TileIndex nearby = MapTileXYOrInvalid(city, zone_x, zone_y);
        if (nearby == INVALID_TILE) continue;
        int zone = ReadCityZoneKind(&city->tile[nearby]);
        if (zone == ZONE_RESIDENTIAL_LOW || zone == ZONE_RESIDENTIAL_HIGH) ++nearby_zones;
      }
    if (NextCityMapRandomModulo(city, 200) < nearby_zones) {
      StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
      PlayCitySound(city, tile_index, CITY_SOUND_DEMOLITION);
      return -CITY_NOTICE_PLACEMENT_PROTEST;
    }
  }
  return PlaceSpecialZoneFootprint(city, origin_tile, kind, size);
}

// Run facility/utility effects after the command has paid. The stadium callback may retain
// this caller through nested commands; keep the captured kind and resume with live City state.
void FinishCityBuildingPlacement(City *city, TileIndex tile_index, int kind) {
  if (kind == TK_HYDRO_POWER) return;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  switch (kind) {
  case TK_HOSPITAL:
    ++city->economy.sector[CITY_ECONOMY_HEALTH].current_amount;
    break;
  case TK_POLICE_STATION:
    ++city->economy.sector[CITY_ECONOMY_POLICE].current_amount;
    break;
  case TK_FIRE_STATION:
    ++city->economy.sector[CITY_ECONOMY_FIRE].current_amount;
    break;
  case TK_SCHOOL:
    ++city->economy.sector[CITY_ECONOMY_SCHOOLS].current_amount;
    break;
  case TK_STADIUM:
    if (ReadOverlay(city, tile_index) != 0) {
      if (city->game->observation && city->game->observation->show_stadium_placement)
        city->game->observation->show_stadium_placement(city, tile_index);
      else
        ReportCityStadiumPlacement(city, tile_index);
    }
    break;
  case TK_COLLEGE:
    ++city->economy.sector[CITY_ECONOMY_COLLEGES].current_amount;
    break;
  case TK_STATUE:
    if (IsMapTileValid(city, tile_index)) {
      city->tile[tile_index].flags &= ~TF_POWERABLE;
      RefreshCityPowerConnections(city, tile_index);
    }
    break;
  case TK_WATER_PUMP:
    PlaceCityWaterPipeCell(city, tile_index);
    break;
  case TK_SUBWAY_STATION:
    PlaceCitySubwayCell(city, tile_index);
    SetUndergroundTile(city, tile_index, TUK_SUBWAY_ACCESS);
    if (IsMapTileValid(city, tile_index))
      city->tile[tile_index].flags &= ~TF_PIPED;
    InvalidateTile(city, tile_index);
    break;
  }
  if (IsMapTileValid(city, tile_index)) {
    if ((city->tile[tile_index].flags & TF_POWERABLE) != 0 && city->indicators.population < 50000)
      ScanPowerGridPhase(city);
    if ((city->tile[tile_index].flags & TF_PIPED) != 0 && city->indicators.population < 50000)
      ScanCityWaterNetwork(city);
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
}

// Finish the paid, successful building command using its captured kind and orientation. Facility callbacks
// run first and may retain the caller; orientation, train spawning and reward consumption use live state afterward.
void FinishCityBuildingCommand(City *city, TileIndex tile, int kind, int orientation) {
  FinishCityBuildingPlacement(city, tile, kind);
  if (orientation >= 0) {
    int size = kCityBuildings[kind].size;
    CityPoint point = MapTilePt(city, tile);
    TileIndex anchor = MapTileXY(city, point.x - (size > 2), point.y - (size > 2));
    InvalidateTileRange(city, anchor, size, size);
    for (int dy = 0; dy < size; ++dy) for (int dx = 0; dx < size; ++dx) {
      TileIndex building_tile = anchor + dy * city->map_width + dx;
      TileInfo *tp = &city->tile[building_tile];
      WriteZoneByte(city, building_tile,
          (ReadTileZoneByte(tp) & ~CITY_BUILDING_ORIENTATION_MASK) | EncodeBuildingOrientation(orientation));
    }
    InvalidateTileRange(city, anchor, size, size);
  }
  if (kind == TK_RAIL_STATION) TrySpawnCityStationTrains(city, tile);
  if (kCityBuildings[kind].reward) SetSpecialFacilityAvailability(city, kCityBuildings[kind].reward - 1, 0);
}

// Place the special power facility on water at one map cell.
i64 PlaceCityWaterPowerFacility(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  static const u8 kHydroTileVariantByElevationMask[16] = {1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1};
  if (ReadCityTileGeometry(city, tile_index).water_surface != CITY_WATER_SURFACE_RAPIDS) {
    return -CITY_NOTICE_HYDRO_REQUIRES_WATERFALL;
  }
  if (!IsTileKindExposedWater(ReadTile(city, tile_index))) {
    return CITY_COMMAND_COST_BLOCKED;
  }

  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);

  int current_height = ReadCityLandHeight(&city->tile[tile_index]);
  int power_mask = 0;
  if (point.x > 0 && ReadCityLandHeight(&city->tile[tile_index - 1]) > current_height)
    power_mask |= CONN_WEST;
  if (point.y > 0 && point.y < city->map_height - 1 &&
      ReadCityLandHeight(&city->tile[tile_index + city->map_width]) > current_height)
    power_mask |= CONN_SOUTH;
  if (point.x < city->map_width - 1 && ReadCityLandHeight(&city->tile[tile_index + 1]) > current_height)
    power_mask |= CONN_EAST;
  if (point.y > 0 && point.y < city->map_height - 1 &&
      ReadCityLandHeight(&city->tile[tile_index - city->map_width]) > current_height)
    power_mask |= CONN_NORTH;

  int orientation = (kHydroTileVariantByElevationMask[power_mask] ^ (city->geography.rotation & 1)) << 1;
  city->tile[tile_index].flags |= TF_POWERABLE;
  ReplaceCityTileAndStatistics(city, tile_index, TK_HYDRO_POWER);
  WriteZoneByte(city, tile_index, EncodeBuildingOrientation(orientation + city->geography.rotation));
  RefreshCityPowerConnections(city, tile_index);
  InvalidateTile(city, tile_index);

  int facility_instance = CreateFacilityInstance(city, tile_index, TK_HYDRO_POWER);
  if (facility_instance != 0)
    WriteOverlay(city, tile_index, facility_instance);
  if (city->indicators.population < 50000)
    ScanPowerGridPhase(city);
  return true;
}

// Choose the nearest authored construction height; midpoint ties retain the lower profile.
static int BuildingConstructionProfile(int height) {
  int profile = 0;
  while (profile + 1 < CITY_CONSTRUCTION_HEIGHTS &&
      (kCityConstructionHeights[profile] + kCityConstructionHeights[profile + 1]) / 2 < height) ++profile;
  return profile;
}

int CityBuildingSpriteFirst(int kind) {
  return CityBuildingArtworkSourceFirst(kCityMapKindSprites[kind]);
}

int CityBuildingSprite(const City *city, int kind, int orientation) {
  int sprite = kCityMapKindSprites[kind], first = CityBuildingArtworkSourceFirst(sprite);
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || first < 0) return -1;
  return first + (orientation & 3);
}

int CityBuildingSmokeIndex(int kind) {
  switch (kind) {
  case TK_OIL_POWER_PLANT: return 0;
  case TK_NUCLEAR_POWER_PLANT: return 1;
  case TK_COAL_POWER_PLANT: return 2;
  default: return -1;
  }
}

CityPoint CityBuildingSpriteExtent(const City *city, int sprite, int zoom) {
  const Drawing *drawing = city->drawing;
  CityPoint extent = CityMapSpriteExtent(drawing, 500 * zoom + sprite);
  int original_width = extent.x;
  int first = CityBuildingArtworkSourceFirst(sprite);
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || first < 0) return extent;
  int smoke_kind = sprite == CITY_MAP_TILE_SPRITE_OIL_POWER_PLANT ? 0 :
      sprite == CITY_MAP_TILE_SPRITE_NUCLEAR_POWER_PLANT ? 1 : sprite == CITY_MAP_TILE_SPRITE_COAL_POWER_PLANT ? 2 : -1;
  for (int view = 0; view < CITY_CIVIC_VIEWS; ++view) {
    const CityLandscapeSprite *art = GetTileSpriteSource(city->game->resources, sprite, view);
    if (!art || !art->sprite) continue;
    int divisor = art->scale << (2 - zoom);
    extent.x = IntMax(extent.x, (art->sprite->width + divisor - 1) / divisor);
    extent.y = IntMax(extent.y, (art->sprite->height + divisor - 1) / divisor);
    if (smoke_kind >= 0) {
      const CityPowerSmoke *smoke = &city->game->resources->power_smoke[smoke_kind][view];
      for (int emitter = 0; emitter < smoke->count; ++emitter) {
        const CityPowerSmokeEmitter *source = &smoke->emitters[emitter];
        const CityLandscapeSprite *plume = &city->game->resources->landscape[CITY_LANDSCAPE_POWER_SMOKE_FIRST +
            source->type * CITY_POWER_SMOKE_FRAMES];
        if (!plume->sprite) continue;
        int height = art->baseline - source->y + plume->baseline;
        extent.y = IntMax(extent.y, (height + divisor - 1) / divisor);
      }
    }
  }
  if (sprite == CITY_MAP_TILE_SPRITE_WIND_POWER) {
    const CityLandscapeSprite *rotor = &city->game->resources->landscape[CITY_LANDSCAPE_WIND_ROTOR_FIRST];
    if (rotor->sprite) {
      int divisor = rotor->scale << (2 - zoom);
      extent.y = IntMax(extent.y, (rotor->baseline + divisor - 1) / divisor);
    }
  }
  if (sprite >= CITY_BUILDING_ART_FIRST && sprite < CITY_BUILDING_ART_FIRST + CITY_BUILDING_ART_COUNT) {
    int divisor = 4 << (2 - zoom);
    const CityBuildingArtwork *building = &city->game->resources->buildings[sprite - CITY_BUILDING_ART_FIRST];
    int profile = BuildingConstructionProfile(building->structure_height);
    const RunSprite *front = city->game->resources->landscape[first].sprite;
    int width = front ? front->width : original_width * divisor;
    extent.y = IntMax(extent.y, (kCityConstructionHeights[profile] + width / 2 + 64 + divisor - 1) / divisor);
  }
  return extent;
}

// Per-kind drawing policy. Buildings default to AFTER so their painted sites
// cannot erase the marker. Parks retain ground selection beneath their trees.
static const u8 kCityBuildingSelectionOrder[TK_COUNT] = {
  [TK_PARK] = CITY_BUILDING_SELECTION_BEFORE,
  [TK_LARGE_PARK] = CITY_BUILDING_SELECTION_BEFORE,
};

void DrawCityBuildingSelection(City *city, TileIndex anchor, CityBuildingSelectionPhase order, u32 options) {
  if (!(options & CITY_MAP_DRAW_PLACEMENT)) return;
  int kind = ReadTile(city, anchor);
  if (kCityBuildingSelectionOrder[kind] != order) return;
  CityPoint dimensions = GetBuildingFootprintDimensions(kind, CityMapViewOrientation(city, anchor));
  int width = dimensions.x, height = dimensions.y;
  for (int u = 0; u < width; ++u) for (int v = 0; v < height; ++v) {
    TileIndex tile = CityMapViewNeighbor(city, anchor, -u, v);
    if (tile != INVALID_TILE) DrawCityMapPlacementTile(city, tile);
  }
}

static bool DrawBuildingConstruction(City *city, TileIndex cell, int zoom, int screen_x, int ground_y,
    int size) {
  Drawing *drawing = city->drawing;
  CityResourceState *assets = city->game->resources;
  int stage = ReadCityBuildingConstructionStage(&city->tile[cell]);
  int kind = ReadTile(city, cell);
  const CityBuildingArtwork *building = &assets->buildings[
      kCityMapKindSprites[kind] - CITY_BUILDING_ART_FIRST];
  int first = CITY_LANDSCAPE_CONSTRUCTION_FIRST + (size - 1) * CITY_CONSTRUCTION_PIECES;
  int profile = BuildingConstructionProfile(building->structure_height);
  int phase = stage > 15 ? 0 : stage > 7 ? 1 : 2;
  int frame = first + 1 + (building->family * CITY_CONSTRUCTION_HEIGHTS + profile) * CITY_CONSTRUCTION_PHASES + phase;
  if (!assets->landscape[first].sprite || (stage <= 23 && !assets->landscape[frame].sprite)) return false;
  int base = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST;
  if (stage > 23 || !(building->ground & CITY_BUILDING_GROUND_CONSTRUCTION_BAKED))
    DrawCityMapSpriteAtBaseline(drawing, base + first, screen_x, ground_y, 0);
  if (stage <= 23) DrawCityMapSpriteAtBaseline(drawing, base + frame, screen_x, ground_y, 0);
  return true;
}

static int ReadBuildingGround(const City *city, int kind) {
  int layout = kCityMapKindSprites[kind];
  const CityResourceState *assets = city->game->resources;
  if (layout == CITY_MAP_TILE_SPRITE_PARK || layout >= CITY_CIVIC_KIND_FIRST) {
    int index = layout == CITY_MAP_TILE_SPRITE_PARK ? CITY_CIVIC_KIND_COUNT : layout - CITY_CIVIC_KIND_FIRST;
    return assets->civic_ground[index];
  }
  if (layout >= CITY_BUILDING_ART_FIRST && layout < CITY_BUILDING_ART_FIRST + CITY_BUILDING_ART_COUNT)
    return assets->buildings[layout - CITY_BUILDING_ART_FIRST].ground;
  return CITY_BUILDING_GROUND_TERRAIN;
}

static bool BuildingGroundCoversTile(const City *city, TileIndex tile, int kind, int ground,
    const CityTileGeometry *geometry) {
  int baked = CityBuildingConstructionKind(&city->tile[tile]) ?
      CITY_BUILDING_GROUND_CONSTRUCTION_BAKED : CITY_BUILDING_GROUND_BAKED;
  if (!(ground & baked) ||
      geometry->water != CITY_WATER_NONE || geometry->land_shape ||
      (kind == TK_PARK && !CityMapTileDetailsVisible(city->drawing))) return false;
  TileIndex start = GetBuildingStartTile(city, tile);
  if (start == INVALID_TILE) return false;
  CityPoint size = GetBuildingFootprintDimensions(kind, ReadBuildingOrientation(city, tile));
  if (size.x == 1 && size.y == 1) return true;
  TileIndex end = GetRelativeTileOrInvalid(city, start, size.x - 1, size.y - 1);
  if (end == INVALID_TILE) return false;
  CityPoint first = CityMapViewTilePoint(city, start);
  CityPoint last = CityMapViewTilePoint(city, end);
  TileIndex anchor = CityMapCameraTile(city, CityMapCameraRotation(city->drawing),
      IntMax(first.x, last.x), IntMin(first.y, last.y));
  return anchor != INVALID_TILE &&
      ReadCitySurfaceHeight(&city->tile[anchor]) == geometry->surface_height;
}

// The material/baked-ground flags are resolved once for a whole footprint. Each tile still supplies its own geometry
// and construction stage, so slopes, water and unequal terrain expose their existing ground and skirts.
static void DrawBuildingGroundTile(City *city, TileIndex tile, int kind, int ground, int zoom,
    int screen_x, int ground_y, const CityTileGeometry *geometry) {
  if (BuildingGroundCoversTile(city, tile, kind, ground, geometry)) {
    DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y, geometry->surface_height);
    return;
  }
  int material = ground & ~CITY_BUILDING_GROUND_FLAGS;
  int sprite = material ? MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
      CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_BUILDING_GROUND_FIRST + material - 1 : -1;
  DrawCityMapTerrainTile(city, tile, zoom, sprite, screen_x, ground_y, geometry);
}

static bool DrawBuildingGround(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  int kind = city->tile[tile].kind;
  if ((!IsTileKindBuilding(kind) && kind != TK_PARK) ||
      CityBuildingSprite(city, kind, CityMapViewOrientation(city, tile)) < 0) return false;
  DrawBuildingGroundTile(city, tile, kind, ReadBuildingGround(city, kind), zoom, screen_x, ground_y, geometry);
  return true;
}

// Shared/custom building artwork retains its ground, selection and warning order.
void DrawCityBuildingFallback(City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->drawing;
  u32 options = CityMapDrawOptions(drawing);
  const TileInfo *tp = &city->tile[tile];
  int zone = ReadCityZoneKind(tp), first = 500 * zoom;
  if (!(options & CITY_MAP_DRAW_BUILDINGS) || (!(options & CITY_MAP_DRAW_ZONE_GROUND) && zone)) {
    int ground = (options & CITY_MAP_DRAW_ZONE_GROUND) || !zone ?
        first + 300 + CityZoneGroundSpriteOffset(zone) : first + 290 + zone;
    // Hidden buildings expose the real terrain beside slopes and water.
    if ((city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) &&
        (geometry->water != CITY_WATER_NONE || geometry->surface_shape)) ground = -1;
    DrawCityMapTerrainTile(city, tile, zoom, ground, screen_x, ground_y, geometry);
    DrawCityMapPlacementTile(city, tile);
  } else if (IsCityMapViewFootprintAnchor(city, tile)) {
    CityPoint extent = CityMapSpriteExtent(drawing, sprite);
    int flags = tp->flags;
    int flipped = CityMapViewOrientation(city, tile) & 1;
    DrawCityBuildingSelection(city, tile, CITY_BUILDING_SELECTION_BEFORE, options);
    DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, ground_y + (extent.x >> 2) - (2 << zoom), flipped);
    DrawCityBuildingSelection(city, tile, CITY_BUILDING_SELECTION_AFTER, options);
    if ((flags & TF_POWER_SERVICE) == TF_POWERABLE)
      DrawCityMapSprite(drawing, first + CITY_SPRITE_CATALOG_SMALL_POWER_WARNING,
          screen_x + (extent.x >> 1) - (4 << zoom), ground_y - (4 << zoom), 0);
  }
}

static bool DrawBuildingTile(City *city, TileIndex cell, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->drawing;
  u32 options = CityMapDrawOptions(drawing);
  CityTileGeometry geometry = *tile_geometry;
  const TileInfo *tp = &city->tile[cell];
  int kind = tp->kind;
  bool small_park = kind == TK_PARK;
  int orientation = small_park ? (city->geography.rotation + CityMapCameraRotation(city->drawing)) & 3 :
      CityMapViewOrientation(city, cell);
  // The dam spans the river with spillways facing downhill. View 0 drains
  // east (shape 2); the camera-relative slope supplies all four directions.
  if (kind == TK_HYDRO_POWER &&
      geometry.surface_shape >= CITY_TERRAIN_SHAPE_1 && geometry.surface_shape <= CITY_TERRAIN_SHAPE_4)
    orientation = (2 - geometry.surface_shape) & 3;
  int id = CityBuildingSprite(city, kind, orientation);
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) || id < 0) return false;
  if (small_park ? !(options & CITY_MAP_DRAW_DETAILS) || !(options & CITY_MAP_DRAW_BUILDINGS) :
      !(options & CITY_MAP_DRAW_BUILDINGS) ||
      (!(options & CITY_MAP_DRAW_ZONE_GROUND) && ReadCityZoneKind(tp))) return false;
  int view = (id - CityBuildingSpriteFirst(kind)) & 3;
  bool wind = kind == TK_WIND_POWER;
  int wind_form = wind && (tp->zone & CITY_WIND_MODERN_MASK) ? 1 : 0;
  if (wind_form) id = CITY_LANDSCAPE_WIND_MODERN_FIRST + view;
  if (!small_park && !IsCityMapViewFootprintAnchor(city, cell)) return true;
  CityPoint dimensions = GetBuildingFootprintDimensions(kind, orientation);
  int width = dimensions.x, height = dimensions.y;
  int size = IntMax(width, height);
  int step_x = 4 << zoom, step_y = 2 << zoom;
  int anchor_height = geometry.surface_height;
  int ground = ReadBuildingGround(city, kind);
  // This is the footprint's leftmost tile. Paint its bounded ground union once,
  // before the full building, so transparent landscaping reveals real terrain
  // without later footprint cells painting over the building.
  if (IsCityMapGroundPass(drawing)) for (int u = 0; u < width; ++u) {
    for (int v = 0; v < height; ++v) {
      TileIndex tile = CityMapViewNeighbor(city, cell, -u, v);
      if (tile == INVALID_TILE) continue;
      CityTileGeometry ground_geometry = tile == cell ? geometry : ReadCityMapViewGeometry(city, tile);
      int ground_height = ground_geometry.surface_height;
      int tile_x = screen_x + (u + v) * step_x;
      int tile_y = ground_y + (v - u) * step_y - (ground_height - anchor_height) * CityMapHeightStep(zoom);
      DrawBuildingGroundTile(city, tile, kind, ground, zoom, tile_x, tile_y, &ground_geometry);
    }
  }
  int cached = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
      id - CITY_LANDSCAPE_GROUND_FIRST;
  DrawCityBuildingSelection(city, cell, CITY_BUILDING_SELECTION_BEFORE, options);
  int baseline = ground_y + (height - 1) * step_y;
  if (CityBuildingConstructionKind(tp)) {
    if (!DrawBuildingConstruction(city, cell, zoom, screen_x, baseline, size)) return false;
  } else DrawCityMapSpriteAtBaseline(drawing, cached, screen_x, baseline, 0);
  DrawCityBuildingSelection(city, cell, CITY_BUILDING_SELECTION_AFTER, options);
  if (wind) {
    int wind_frame = UseCityMapSpriteEffect(drawing, CITY_MAP_EFFECT_WIND);
    bool animation = BeginCityMapSpriteAnimation(city, cell);
    int frame = (wind_frame + cell) % CITY_WIND_FRAMES;
    int rotor = CITY_LANDSCAPE_WIND_ROTOR_FIRST + (wind_form * CITY_CIVIC_VIEWS + view) * CITY_WIND_FRAMES + frame;
    int cached_rotor = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        rotor - CITY_LANDSCAPE_GROUND_FIRST;
    DrawCityMapSpriteAtBaseline(drawing, cached_rotor, screen_x, ground_y, 0);
    if (animation) EndCityMapSpriteAnimation(city);
  }
  int smoke_kind = CityBuildingSmokeIndex(kind);
  if (smoke_kind >= 0) {
    const CityPowerSmoke *smoke = &city->game->resources->power_smoke[smoke_kind][view];
    if (smoke->count) {
      int smoke_frame = UseCityMapSpriteEffect(drawing, CITY_MAP_EFFECT_POWER_SMOKE);
      bool animation = BeginCityMapSpriteAnimation(city, cell);
      // Emitter coordinates belong to the default painting. An authored zoom
      // variant may have a different resolution and crop around the same origin.
      const CityLandscapeSprite *painting = GetResourceSprite(city->game->resources,
          LandscapeSpriteResourceIndex(city->game->resources, id));
      int divisor = painting->scale << (2 - zoom);
      int sprite_baseline = painting->baseline;
      for (int emitter = 0; emitter < smoke->count; ++emitter) {
        const CityPowerSmokeEmitter *source = &smoke->emitters[emitter];
        int anchor_x = source->type == CITY_POWER_SMOKE_STEAM ?
            CITY_POWER_STEAM_ANCHOR_X : CITY_POWER_SMOKE_ANCHOR_X;
        if (source->flipped) anchor_x = CITY_POWER_SMOKE_WIDTH - anchor_x;
        i64 x_q8 = (i64)screen_x * 256 + (source->x - anchor_x) * 256 / divisor;
        i64 baseline_q8 = (i64)(ground_y + (height - 1) * step_y) * 256 +
            (source->y - sprite_baseline) * 256 / divisor;
        int frame = (smoke_frame + cell + emitter * 5) % CITY_POWER_SMOKE_FRAMES;
        int id = CITY_LANDSCAPE_POWER_SMOKE_FIRST + source->type * CITY_POWER_SMOKE_FRAMES + frame;
        if (source->type == CITY_POWER_SMOKE_STEAM)
          id = CITY_LANDSCAPE_NUCLEAR_STEAM_FIRST +
              (view * CITY_POWER_SMOKE_EMITTERS + emitter) * CITY_POWER_SMOKE_FRAMES + frame;
        int cached_smoke = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
            id - CITY_LANDSCAPE_GROUND_FIRST;
        DrawCityMapSpriteAtBaselineQ8(drawing, cached_smoke, x_q8, baseline_q8, source->flipped);
      }
      if (animation) EndCityMapSpriteAnimation(city);
    }
  }
  if ((tp->flags & TF_POWER_SERVICE) == TF_POWERABLE)
    DrawCityMapSprite(drawing, CITY_SPRITE_CATALOG_SMALL_POWER_WARNING + 500 * zoom,
        screen_x + (width + height - 2) * step_x / 2, ground_y - step_x, 0);
  return true;
}

const TileModule kCityBuildingModule = {.draw = DrawBuildingTile, .draw_ground = DrawBuildingGround};
