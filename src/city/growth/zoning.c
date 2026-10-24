// TileInfo zoning/growth contract (zero-based bits):
// zone[0..3] is ZONE_ only when zone[4] is clear; a sloped tile uses it for terrain shape and has no zoning.
// Trees on flat zoned land keep their kind/species/growth until development replaces them. Building metadata
// belongs to terrain[4..7]: facing [4..5], anchor continuation toward -X in bit 6 and -Y in bit 7.
// A dry building's altitude[5..9] is construction progress, not water height; water-class facilities retain an
// actual water plane there. Ground cover/region and utility supply are read in their shared meanings.
// No extra growth-counter field is assigned here: tree/clear timing in zone[5..7] stays with those families,
// and retained growth work stays in City. See tile_storage.h and buildings.c for the full shared/building layouts.

#include "city/maps/labels.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/networks.h"
#include "city/infrastructure/transport.h"
#include "city/saveload/map_format.h"
#include "city/engine/random.h"
#include "city/growth/growth.h"

#include "city/economy/facilities.h"
#include "city/events/news.h"
#include "city/presentation.h"
#include "city/maps/buildings.h"
#include "city/infrastructure/power_lines.h"
#include "city/military.h"
#include "city/events/events.h"
#include "city/economy/types.h"
#include "city/infrastructure/types.h"
#include "city/maps/tile_storage.h"
#include <assert.h>

#include "city/state.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/tools/tools.h"
#include "city/maps/map_types.h"

static void GrowCityZonedTile(City *city, TileIndex tile, bool large_city_effect_enabled);
static void PlaceGrowthTiles(City *city, TileIndex tile_index, int size, int growth_type);
static void LargeCityTileEffect(City *city, TileIndex tile_index);

// Growth selection tables

enum { CITY_GROWTH_TRANSPORT_WEIGHT_COUNT = 515 };

static const u8 kGrowthDivisorTable[21] = {
  196, 12, 4, 4, 4, 8, 5, 5, 10, 4, 4, 4, 6, 2, 2, 2,
  2, 2, 2, 2, 2
};

static const i8 kSpecialZoneCorridorYStep[4] = {
  0, 1, 0, -1
};

static const i8 kSpecialZoneCorridorXStep[4] = {
  1, 0, -1, 0
};

// The stock scan can select adjacent constant words for unusual zoned tiles.
// Include every size admitted by the population/category lookups. Unselected
// holes are zero; they are not snapshots of adjacent mutable UI state.
static const u16 kTransportValueBySize[CITY_GROWTH_TRANSPORT_WEIGHT_COUNT] = {
  [0] = 0,
  [1] = 1,
  [2] = 8,
  [3] = 12,
  [4] = 36,
  [140] = 313,
  [148] = 305,
  [158] = 315,
  [174] = 315,
  [178] = 318,
  [188] = 305,
  [257] = 12849,
  [514] = 3341,
};

// Scheduled growth and maintenance

// Read the low 16 bits of an initialized sector's funding rate for maintenance compatibility. Sector must index the
// economy table; the read does not clamp or change its stored rate.
static int ReadCityMaintenancePercent(const City *city, int sector) {
  // Stock reads only the low word of the current funding rate.
  return (u16)city->economy.sector[sector].current_rate;
}

// Process one scheduled row of every-fourth-tile growth candidates within a section. Coordinates must fit the
// scheduled section; may consume RNG and mutate zoning, buildings and local effects in simulation order.
static void ScanCityTileSectionRow(City *city, int first_x, int y,
    bool large_city_effect_enabled) {
  const int width = city->map_width;
  TileIndex row = y * width;
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 4; ++column) {
    int x = first_x + column * 4;
    const TileIndex cell_index = row + x;
    TileInfo *tp = &city->tile[cell_index];
    int zone = ReadCityZoneKind(tp);
    if (zone) {
      if (zone >= ZONE_RESERVED_FIRST) {
        if (NextCityMapRandomModulo(city, 128) == 0) {
          int underground = ReadUnderground(city, cell_index);
          if (IsSubwayTile(underground)) {
            int maintenance = ReadCityMaintenancePercent(city, CITY_ECONOMY_SUBWAY);
            if (maintenance != 100 && Sc2kCrtRandRange(city, 100) >= maintenance) {
              if (underground == TUK_SUBWAY_ACCESS) {
                BulldozeCityTile(city, cell_index, false);
              } else {
                SetUndergroundTile(city, cell_index, TUK_CLEAR);
              }
            }
          }
        }
        continue;
      }
      GrowCityZonedTile(city, cell_index, large_city_effect_enabled);
    } else if (CityTileStatisticAt(city, cell_index) >= TKL_ROAD_EAST_WEST) {
      // Water pylons also take a maintenance draw, despite sharing land-line kinds.
      u32 seed = city->random.map;
      int next = AdvanceCityMapSeed(seed);
      city->random.map = next;
      int tile = tp->kind;
      int decay_sector = -1;
      if (IsTileKindRoadRoute(tile))
        decay_sector = CITY_ECONOMY_ROADS;
      else if (IsTileKindRailRoute(tile))
        decay_sector = CITY_ECONOMY_RAIL;
      if (!IsTileKindBuilding(tile) && (next & 0x7F) == 0 &&
          (IsTileKindBridge(tile) ||
              IsCityWaterPowerLine(tp))) {
        int maintenance = ReadCityMaintenancePercent(city, CITY_ECONOMY_BRIDGES);
        if (maintenance != 100 && (u32)city->weather.wind + Sc2kCrtRandRange(city, 50) >= maintenance) {
          CenterCityMapOnEvent(city, x, y);
          BulldozeCityTile(city, cell_index, true);
          InsertRankedCityEvent(city, CITY_NEWS_BRIDGE_COLLAPSE, 0);
        }
      } else if (!IsTileKindBuilding(tile) && (next & 0x7F) == 0 && decay_sector != -1) {
        int percentage = ReadCityMaintenancePercent(city, decay_sector);
        if (percentage == 100 || Sc2kCrtRandRange(city, 100) < percentage)
          continue;
        ReplaceCityTileVariant(city, cell_index, TK_RUBBLE, Sc2kCrtRand(city) & 3);
        tp->flags &= ~TF_POWERABLE;
        RefreshCityPowerConnections(city, cell_index);
      } else if (!IsTileKindBuilding(tile) && (next & 0x7F) == 0 &&
          (tile == TK_HIGHWAY || tile == TK_HIGHWAY_POWER ||
              IsTileKindMultiHighway(tile)) &&
          !(y & 1) && !(x & 1)) {
        int percentage = ReadCityMaintenancePercent(city, CITY_ECONOMY_HIGHWAYS);
        if (percentage == 100 || Sc2kCrtRandRange(city, 100) < percentage)
          continue;
        static const u8 cell_y_offsets[4] = {0, 1, 0, 1};
        static const u8 cell_x_offsets[4] = {0, 0, 1, 1};
        for (int cell = 0; cell < 4; ++cell) {
          int cell_x = x + cell_x_offsets[cell];
          int cell_y = y + cell_y_offsets[cell];
          TileIndex cell_tile = cell_y * width + cell_x;
          if (HasTileWater(&city->tile[cell_tile]))
            ReplaceCityTileAndStatistics(city, cell_tile, ReadCityWaterGroundKind(&city->tile[cell_tile]));
          else ReplaceCityTileVariant(city, cell_tile, TK_RUBBLE, Sc2kCrtRand(city) & 3);
          RefreshCityPowerConnections(city, cell_tile);
        }
      }
      if ((tile == TK_RAIL_STATION || tile == TK_MARINA) &&
          (tp->flags & TF_POWERED)) {
        if (NextCityMapRandomModulo(city, 4) == 0 &&
            city->statistics.count[tile] / (tile == TK_RAIL_STATION ? 4 : 9) >
                (tile == TK_RAIL_STATION ? city->disasters.tile237_count : city->disasters.tile248_count)) {
          if (tile == TK_RAIL_STATION)
            TrySpawnCityStationTrains(city, cell_index);
          else
            TrySpawnCityMarinaSailboats(city, cell_index);
        }
      } else if (IsTileKindArcology(tile) && tile != TK_BRAUN_LLAMA_DOME &&
          ReadFootprintCorners(city, cell_index) == CITY_ZONE_CORNER_3) {
        int overlay = ReadCityFacilityOverlay(city, cell_index);
        if (IsCityFacilityOverlay(overlay)) {
          CityAnnualFacilityRecord *facility = CityFacilityRecordAt(city, CityFacilityOverlaySlot(overlay));
          if (IsTileKindArcology(facility->type) && facility->type != TK_BRAUN_LLAMA_DOME) {
            int sx = x / 2;
            int sy = y / 2;
            int score = (city->half[sy * width / 2 + sx].land_value >> 5) -
                (city->half[sy * width / 2 + sx].crime >> 5) -
                (city->half[sy * width / 2 + sx].pollution >> 5) + 12;
            if (!(tp->flags & TF_POWERED))
              score /= 2;
            if (!(tp->flags & TF_WATERED))
              score /= 2;
            score = IntClamp(score, 0, 12);
            facility->age = (u8)score;
          }
        }
      }
    }
  }
}

// Quarter-grid scan, growth, tile decay, and special-zone underlay decay. Batch offsets/count must fit the scheduled
// quarter; growth effects and random draws follow its stable traversal order.
void ScanCityTileBatch(City *city, int quarter_x, int quarter_y,
    int first, int count, bool large_city_effect_enabled) {
  InvalidateMapLayer(city, CITY_MAP_CHANGE_TILE);
  InvalidateMapLayer(city, CITY_MAP_CHANGE_ZONE);
  InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
  InvalidateMapLayer(city, CITY_MAP_CHANGE_TRAFFIC_DENSITY);
  int direction_mask = kTileScanMaskTable[city->geography.rotation];
  SetCityTileScanPresentation(city, direction_mask);
  const int width = city->map_width;
  const int columns = width / 4;
  enum { kSectionColumns = CITY_MAP_SECTION_SIZE / 4 };
  for (int position = first; position < first + count; position += kSectionColumns) {
    int first_x = position % columns * 4 + quarter_x;
    int y = position / columns * 4 + quarter_y;
    ScanCityTileSectionRow(city, first_x, y, large_city_effect_enabled);
  }
}

// Scan the selected interleaved quarter of City tiles for growth and decline. Quarter offsets must be supported; this
// executes simulation effects and cannot be substituted for a read-only map query.
void ScanQuarterCityTiles(City *city, int quarter_x, int quarter_y) {
  bool large_city_effect = ((city->fixes & (1ull << CITY_FIX_RESTORED_CHEATS)) && city->cheats.church == 2) ||
      (u32)(2500 * city->statistics.count[TK_CHURCH]) < city->indicators.population;
  ScanCityTileBatch(city, quarter_x, quarter_y, 0, city->map_cell_count / 16, large_city_effect);
}

// Zoning

// Read-only admission queries for a valid tile; preserve the transport probe order and power edge rules.
// Neither changes tiles, consumes RNG nor calls presentation.
// 24-neighbor road, rail and highway access probe.
static bool TileHasTransportAccess(const City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  for (int i = 0; i < 24; ++i) {
    int nx = point.x + kTransportNeighborOffsets.offset[i].x;
    int ny = point.y + kTransportNeighborOffsets.offset[i].y;
    if (!IsMapCoordinateValid(city, nx, ny)) continue;
    TileIndex neighbor = MapTileXY(city, nx, ny);
    int kind = ReadTile(city, neighbor);
    if (IsTileKindRoadRoute(kind) ||
        kind == TK_BUS_STATION || kind == TK_RAIL_STATION || kind == TK_SUBWAY_STATION)
      return true;
  }
  return false;
}

// A powered cell at the given position or immediately beside it.
static bool TileHasNearbyPower(const City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  const TileInfo *tp = &city->tile[tile_index];
  int width = city->map_width, height = city->map_height;
  return (tp->flags & TF_POWERED) != 0 ||
      (point.y > 1 && (tp[-width].flags & TF_POWERED) != 0) ||
      (point.x > 1 && (tp[-1].flags & TF_POWERED) != 0) ||
      (point.y < height - 1 && (tp[width].flags & TF_POWERED) != 0) ||
      (point.x < width - 1 && (tp[1].flags & TF_POWERED) != 0);
}

// Select a seaport kind from current counts without mutation or RNG. The growth caller places its footprint.
static int SelectCitySeaportGrowthKind(const CityTileCounts *counts) {
  int existing = counts->count[TK_CRANE];
  if (counts->count[TK_CARGO_YARD] / 4 >= existing)
    return TK_CRANE;
  if (counts->count[TK_LOADING_BAY] / 4 < existing)
    return TK_LOADING_BAY;
  return counts->count[TK_WAREHOUSE_1X1_4] / 3 >= existing ? TK_CARGO_YARD : TK_WAREHOUSE_1X1_4;
}

// Select an airport kind, including runway tiles, from current counts. Placement stays with the growth caller.
static int SelectCityAirportGrowthKind(const CityTileCounts *counts) {
  int target = (counts->count[TK_RUNWAY_FIRST] + counts->count[TK_RUNWAY_LAST]) / 5;
  if (counts->count[TK_PARKING_LOT_2X2_1] / 4 >= target)
    return TK_RUNWAY_FIRST;
  if (2 * counts->count[TK_CONTROL_TOWER_1X1_1] < target)
    return TK_CONTROL_TOWER_1X1_1;
  if (2 * counts->count[TK_RADAR] < target)
    return TK_RADAR;
  if (counts->count[TK_TARMAC] < target)
    return TK_TARMAC;
  if (counts->count[TK_BUILDING_1X1_1] / 2 < target)
    return TK_BUILDING_1X1_1;
  if (counts->count[TK_BUILDING_1X1_2] / 2 < target)
    return TK_BUILDING_1X1_2;
  return counts->count[TK_HANGAR_3X3] / 4 >= target ? TK_PARKING_LOT_2X2_1 : TK_HANGAR_3X3;
}

// Process one admitted zone tile. Military growth stays in its own domain; airport and seaport growth also
// schedule their vehicles. Ordinary zones account population and apply construction, growth or decline.
static void GrowCityZonedTile(City *city, TileIndex tile, bool large_city_effect_enabled) {
  TileInfo *tp = &city->tile[tile];
  int kind = tp->kind, zone = ReadCityZoneKind(tp);
  if (zone == ZONE_MILITARY) {
    GrowCityMilitaryBaseTile(city, tile);
    return;
  }
  if (zone == ZONE_SEAPORT) {
    if ((Sc2kCrtRand(city) & 3) == 0) {
      if (TileHasNearbyPower(city, tile)) {
        int replacement = SelectCitySeaportGrowthKind(&city->statistics);
        if (!ReplaceSpecialZoneTile(city, tile, replacement, zone))
          ReplaceSpecialZoneTile(city, tile, TK_WAREHOUSE_1X1_4, zone);
      }
      return;
    }
    if (kind == TK_CRANE && (Sc2kCrtRand(city) & 3) == 0) {
      SpawnBorderWaterEvent(city, tile);
    }
    return;
  }
  if (zone == ZONE_AIRPORT) {
    if ((Sc2kCrtRand(city) & 3) == 0) {
      if (TileHasNearbyPower(city, tile)) {
        ReplaceSpecialZoneTile(city, tile, SelectCityAirportGrowthKind(&city->statistics), zone);
      }
      return;
    }
    if (kind == TK_RUNWAY_FIRST && Sc2kCrtRandRange(city, 30) == 0 &&
        (tp->flags & TF_POWERED)) {
      int event_draw = Sc2kCrtRandRange(city, 10);
      if (event_draw < 4) {
        TrySpawnCityHelicopter(city, tile);
      } else {
        int axis = 2 * (ReadBuildingOrientation(city, tile) & 1);
        TrySpawnCityAirplane(city, tile, axis);
      }
    }
    return;
  }
  CityPoint point = MapTilePt(city, tile);
  int x = point.x, y = point.y, width = city->map_width, height = city->map_height;
  bool building = IsTileKindBuilding(kind);
  bool active = building ? IsCityBuildingCorner(city, tile, CITY_BUILDING_CORNER_TOP_RIGHT) :
      !IsTileKindTransportOrBuilding(kind) && TileHasTransportAccess(city, tile);
  if (active) {
    int size = building ? kTilePopulationSize[kind - TK_RESIDENTIAL_FIRST] : 0;
    bool has_route = false;
    bool powered = (tp->flags & TF_POWERED) ||
        (y > 1 && (tp[-width].flags & TF_POWERED)) ||
        (x > 1 && (tp[-1].flags & TF_POWERED)) ||
        (y < height - 1 && (tp[width].flags & TF_POWERED)) ||
        (x < width - 1 && (tp[1].flags & TF_POWERED));
    // The route search itself returns without consuming RNG when no access exists.
    if (powered)
      has_route = FindTransportRouteAndApplyTraffic(city, x, y, zone, size, 100);
    int category = kTileZoneCategory[building ? kind - TK_RESIDENTIAL_FIRST : 0];
    if (category == CITY_BUILDING_DEVELOPED && ReadCityBuildingConstructionStage(tp))
      category = CITY_BUILDING_CONSTRUCTION;
    int route_value = has_route ? 2000 + city->economic_tables.sector_rates.value[(zone - 1) / 2] : 0;
    if (size > 0 && category == CITY_BUILDING_DEVELOPED) {
      int weight = kTransportValueBySize[size];
      city->population.value[zone] += weight;
      int growth_budget = 4000 - route_value;
      if (Sc2kCrtRand(city) < (u32)(growth_budget / size)) {
        int orientation = Sc2kCrtRand(city) & 1;
        GrowActiveZoneTiles(city, tile, size, orientation);
        return;
      }
    }
    if (size > 0 && category == CITY_BUILDING_CONSTRUCTION) {
      if (Sc2kCrtRand(city) < (u32)(0x4000 / size)) {
        if (large_city_effect_enabled && (size & 2) && zone < ZONE_COMMERCIAL_LOW) {
          LargeCityTileEffect(city, tile);
          return;
        }
        if (CityBuildingConstructionKind(tp)) {
          AdvanceCityBuildingConstruction(city, tile);
        } else if (city->fixes & (1ull << CITY_FIX_BUILDING_CONSTRUCTION)) {
          PlaceGrowthTiles(city, tile, size, 3);
        } else {
          int growth_type = (zone - 1) / 2;
          PlaceGrowthTiles(city, tile, size, growth_type);
        }
        return;
      }
    }
    if (size > 0 && category == CITY_BUILDING_ABANDONED) {
      int weight = kTransportValueBySize[size];
      city->population.value[CITY_POPULATION_ABANDONED] += weight;
      if (Sc2kCrtRand(city) < (u32)(15 * route_value / size)) {
        int growth_type = (zone - 1) / 2;
        PlaceGrowthTiles(city, tile, size, growth_type);
      }
      return;
    }
    if (size != 4 && (!(zone & 1) || size <= 0)) {
      bool eligible = zone >= ZONE_INDUSTRIAL_LOW;
      if (!eligible) {
        int land_value = city->half[(y / 2) * width / 2 + x / 2].land_value;
        eligible = (size != 1 || land_value >= 0x20) && (size != 2 || land_value >= 0x60) &&
            (size != 3 || land_value >= 0xC0);
      }
      if (eligible) {
        int draw = Sc2kCrtRand(city);
        if (3 * route_value / (size + 1) > draw) {
          GrowZoneFromTraffic(city, tile, size, zone);
        }
      }
    }
  }
}

// Choose one growth tile and place a single cell or shared-tile footprint. Economic growth admission happens in its
// callers. Types 0..2 place completed R/C/I buildings.
static void PlaceGrowthTiles(City *city, TileIndex tile_index, int size, int growth_type) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  int zone = ReadCityZoneKind(&city->tile[tile_index]);
  bool construction = growth_type == 3 && zone >= ZONE_RESIDENTIAL_LOW && zone <= ZONE_INDUSTRIAL_HIGH &&
      !HasTileWater(&city->tile[tile_index]) &&
      (city->fixes & (1ull << CITY_FIX_BUILDING_CONSTRUCTION));
  if (construction) growth_type = (zone - 1) / 2;
  int new_tile;
  if (size == 1) {
    if (growth_type == 0) {
      int band = ReadLandValue(city, tile_index) >> 6;
      if (band == 3)
        band = 2;
      new_tile = kGrowthBaseTable[1] + (Sc2kCrtRand(city) & 3) + 4 * band;
    } else {
      int table_index = 4 * growth_type + 1;
      int divisor = kGrowthDivisorTable[table_index];
      int base = kGrowthBaseTable[table_index];
      new_tile = base + Sc2kCrtRandRange(city, divisor);
    }
    int facing = ReadBuildingOrientation(city, tile_index);
    ReplaceCityTileAndStatistics(city, tile_index, new_tile);
    InitializeCityBuildingFootprint(city, tile_index, 1, 1, facing);
    city->tile[tile_index].flags |= TF_UTILITY_CAPABILITY;
    RefreshCityPowerConnections(city, tile_index);
    if (construction) SetCityBuildingFootprintStage(city, tile_index, CITY_BUILDING_CONSTRUCTION_STAGE_MAX);
    return;
  }

  int table_index = size + 4 * growth_type;
  int divisor = kGrowthDivisorTable[table_index];
  int base = kGrowthBaseTable[table_index];
  new_tile = base + Sc2kCrtRandRange(city, divisor);
  int half = size / 2;
  if (point.y < 2 || point.x < 2 || point.y > city->map_height - 1 - 1 - half ||
      point.x > city->map_width - 1 - 1 - half)
    return;

  TileIndex anchor = tile_index - half;
  int facing = ReadBuildingOrientation(city, anchor);
  for (int row = 0; row <= half; ++row) {
    for (TileIndex cell_index = anchor + row * city->map_width, end = cell_index + half;
        cell_index <= end; ++cell_index) {
      TileInfo *tp = &city->tile[cell_index];
      ReplaceCityTileAndStatistics(city, cell_index, new_tile);
      WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
      tp->flags |= TF_UTILITY_CAPABILITY;
      RefreshCityPowerConnections(city, cell_index);
    }
  }
  InitializeCityBuildingFootprint(city, anchor, half + 1, half + 1, facing);
  if (construction) SetCityBuildingFootprintStage(city, anchor, CITY_BUILDING_CONSTRUCTION_STAGE_MAX);
}

// Place the next active-zone footprint while retaining its stock CRT draw order.
void GrowActiveZoneTiles(City *city, TileIndex tile_index, int size, i32 rotated_layout) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  switch (size) {
  case 1:
    PlaceGrowthTiles(city, tile_index, 1, 4);
    return;
  case 2: {
    if (!rotated_layout) {
      PlaceGrowthTiles(city, tile_index, 2, 4);
      return;
    }
    static const i8 offsets[4][2] = {{0, 0}, {0, 1}, {-1, 1}, {-1, 0}};
    for (int cell = 0; cell < 4; ++cell)
      PlaceGrowthTiles(city, MapTileXY(city, point.x + offsets[cell][0], point.y + offsets[cell][1]), 1, 4);
    return;
  }
  case 3:
    PlaceGrowthTiles(city, tile_index, rotated_layout ? 2 : 3, 4);
    return;
  case 4: {
    if (!rotated_layout) {
      PlaceGrowthTiles(city, tile_index, 4, 4);
      return;
    }
    static const i8 perimeter[8][2] = {{0, 0}, {0, 1}, {0, 2}, {-1, 2}, {-2, 2}, {-2, 1}, {-2, 0}, {-1, 0}};
    for (int cell = 0; cell < 8; ++cell)
      PlaceGrowthTiles(city, MapTileXY(city, point.x + perimeter[cell][0], point.y + perimeter[cell][1]), 1, 4);
    int orientation = Sc2kCrtRand(city) & 3;
    point.y += orientation & 1;
    point.x -= orientation / 2;
    PlaceGrowthTiles(city, MapTileXY(city, point.x, point.y), 3, 4);
    return;
  }
  }
}

// Replace an interior 2x2 zone with TK_CHURCH, retaining its facing and reconnecting power.
static void LargeCityTileEffect(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  if (point.y < 1 || point.x < 1 || point.y >= city->map_height - 1 || point.x >= city->map_width - 1)
    return;
  TileIndex anchor = tile_index - 1;
  int facing = ReadBuildingOrientation(city, anchor);
  for (int row = 0; row < 2; ++row)
    for (TileIndex cell_index = anchor + row * city->map_width, end = cell_index + 2; cell_index < end; ++cell_index) {
      TileInfo *tp = &city->tile[cell_index];
      ReplaceCityTileAndStatistics(city, cell_index, TK_CHURCH);
      WriteTileZoneByte(tp, ZONE_NONE);
      tp->flags |= TF_UTILITY_CAPABILITY;
      RefreshCityPowerConnections(city, cell_index);
    }
  InitializeCityBuildingFootprint(city, anchor, 2, 2, facing);
  return;
}

// Terrain, zone and tile check used by GrowZoneFromTraffic.
static SC2K_NOINLINE SC2K_PRESERVE_MOST bool DeclineNeighborSuitable(
    const TileInfo *tp, int terrain, int zone, int tile_limit) {
  int tile = tp->kind;
  // Both decline sizes exclude unfinished buildings larger than one cell.
  if (ReadCityBuildingConstructionStage(tp) &&
      !IsTileKindFacility(tile) && kTilePopulationSize[tile - TK_RESIDENTIAL_FIRST] > 1)
    return false;
  if (tile >= tile_limit || ReadCityLandHeight(tp) != terrain ||
      ReadCityZoneKind(tp) != zone)
    return false;
  return !(ReadCityTileClasses(tile) & TCLASS_GROWTH_OBSTRUCTION);
}

// Clear a 2x2 developed zone before replacing its footprint. Requires a valid developed footprint; removes its
// category/statistics before its replacement is installed.
static void ClearDecliningZone(City *city, TileIndex tile) {
  int corners = ReadFootprintCorners(city, tile);
  if (corners != CITY_ZONE_CORNER_0 && corners != CITY_ZONE_CORNER_1 &&
      corners != CITY_ZONE_CORNER_2 && corners != CITY_ZONE_CORNER_3) return;
  TileIndex anchor = GetBuildingStartTile(city, tile);
  if (anchor == INVALID_TILE) return;
  // Keep the original clockwise order: top right, bottom right, bottom left, top left.
  const TileIndex cells[] = {anchor + 1, anchor + city->map_width + 1, anchor + city->map_width, anchor};
  for (int corner = 0; corner < 4; ++corner) PlaceGrowthTiles(city, cells[corner], 1, 4);
}

// Advance or decline a zone using its original caller boundary. Requires an admitted zone origin and current traffic
// inputs; may consume RNG, change footprints and apply decline effects.
void GrowZoneFromTraffic(City *city, TileIndex tile_index, int size, u8 zone) {
  assert(IsMapTileValid(city, tile_index));
  if (size == 0) {
    PlaceGrowthTiles(city, tile_index, 1, 3);
    return;
  }
  if (size == 2) {
    PlaceGrowthTiles(city, tile_index, 3, 3);
    return;
  }

  CityPoint point = MapTilePt(city, tile_index);
  const TileInfo *tp = &city->tile[tile_index];
  int width = city->map_width, height = city->map_height;
  if (size == 1) {
    int terrain = ReadCityLandHeight(tp);
    bool south = point.y + 1 < height &&
        DeclineNeighborSuitable(tp + width, terrain, zone, TK_CHEAP_APARTMENTS);
    bool east = point.x + 1 < width &&
        DeclineNeighborSuitable(tp + 1, terrain, zone, TK_CHEAP_APARTMENTS);
    bool southeast = point.y + 1 < height && point.x + 1 < width &&
        DeclineNeighborSuitable(tp + width + 1, terrain, zone, TK_CHEAP_APARTMENTS);
    if (south && east && southeast) {
      PlaceGrowthTiles(city, tile_index + 1, 2, 3);
      return;
    }
    bool west = point.x > 0 &&
        DeclineNeighborSuitable(tp - 1, terrain, zone, TK_CHEAP_APARTMENTS);
    bool southwest = point.y + 1 < height && point.x > 0 &&
        DeclineNeighborSuitable(tp + width - 1, terrain, zone, TK_CHEAP_APARTMENTS);
    if (south && west && southwest) {
      PlaceGrowthTiles(city, tile_index, 2, 3);
      return;
    }
    bool north = point.y > 0 &&
        DeclineNeighborSuitable(tp - width, terrain, zone, TK_CHEAP_APARTMENTS);
    bool northeast = point.y > 0 && point.x + 1 < width &&
        DeclineNeighborSuitable(tp - width + 1, terrain, zone, TK_CHEAP_APARTMENTS);
    if (east && north && northeast) {
      PlaceGrowthTiles(city, tile_index - width + 1, 2, 3);
      return;
    }
    bool northwest = point.y > 0 && point.x > 0 &&
        DeclineNeighborSuitable(tp - width - 1, terrain, zone, TK_CHEAP_APARTMENTS);
    if (west && north && northwest)
      PlaceGrowthTiles(city, tile_index - width, 2, 3);
    return;
  }

  static const u8 perimeter_y[8] = {0, 0, 0, 1, 2, 2, 2, 1};
  static const i8 perimeter_x[8] = {0, -1, -2, -2, -2, -1, 0, 0};
  static const i8 corner_y[4] = {-1, -1, 3, 3};
  static const i8 corner_x[4] = {1, -3, -3, 1};
  int terrain = ReadCityLandHeight(tp);
  for (int variant = 0; variant < 4; ++variant) {
    int top = point.y - (variant & 1), right = point.x + variant / 2;
    if (top < 0 || top + 2 >= height || right < 2 || right >= width) continue;
    TileIndex origin = tile_index - (variant & 1) * width + variant / 2;
    bool suitable = true;
    for (int i = 0; i < 8; ++i) {
      const TileInfo *np = &city->tile[origin + perimeter_y[i] * width + perimeter_x[i]];
      if (!DeclineNeighborSuitable(np, terrain, zone, TK_LARGE_APARTMENT_BUILDING_3X3_1)) {
        suitable = false;
        break;
      }
    }
    if (!suitable)
      continue;
    bool corner_found = false;
    for (int i = 0; i < 4 && !corner_found; ++i) {
      int cell_y = top + corner_y[i];
      int cell_x = right + corner_x[i];
      TileIndex cell = MapTileXYOrInvalid(city, cell_x, cell_y);
      if (cell == INVALID_TILE)
        continue;
      static const u8 kCornerConnections[] = {3, 6, 12, 9};
      const TileInfo *tp = &city->tile[cell];
      corner_found = tp->kind == TK_ROAD &&
          !kLineTerrainOverrideBySlope[ReadTerrainCode(tp)] &&
          (ReadTransportConnections(tp) & kCornerConnections[i]) == kCornerConnections[i];
    }
    if (!corner_found)
      continue;
    for (int i = 0; i < 8; ++i) {
      TileIndex cell = origin + perimeter_y[i] * width + perimeter_x[i];
      if (ReadTile(city, cell) >= TK_CHEAP_APARTMENTS) ClearDecliningZone(city, cell);
    }
    PlaceGrowthTiles(city, origin, 4, 3);
    return;
  }
}

// Remove an existing special-zone tile or footprint. Requires valid footprint geometry; clears its statistics,
// power and zone metadata in row order before the caller installs the replacement.
static void ClearSpecialZoneFootprint(City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (!IsTileKindFacility(kind)) return;
  int size = kind >= TK_STATUE && kind <= TK_RADAR ? 1 : 2;
  if (size == 2) {
    CityPoint point = MapTilePt(city, tile);
    tile -= (point.y & 1) * city->map_width + (point.x & 1);
  }
  for (int row = 0; row < size; ++row)
    for (TileIndex cell = tile + row * city->map_width, end = cell + size; cell < end; ++cell) {
      ReplaceCityTileAndStatistics(city, cell, ReadCityWaterGroundKind(&city->tile[cell]));
      city->tile[cell].flags &= ~(TF_POWERED | TF_POWERABLE);
      RefreshCityPowerConnections(city, cell);
      WriteZoneByte(city, cell, ReadZoneByte(city, cell) & CITY_ZONE_KIND_MASK);
    }
}

// Extend a runway through five new cells, joining perpendicular runways as crossings. Admission scans the whole
// extension before any writes and retains the selected scan direction.
static bool GrowSpecialZoneRunway(City *city, TileIndex origin, int zone_kind) {
  assert(IsMapTileValid(city, origin));
  CityPoint origin_point = MapTilePt(city, origin);
  int runways = zone_kind == ZONE_MILITARY && (city->fixes & (1ull << CITY_FIX_MILITARY_RUNWAYS)) ?
      city->special_tile_counts.count[CITY_SPECIAL_TILE_RUNWAY_FIRST] : city->statistics.count[TK_RUNWAY_FIRST];
  // Even/even starts are excluded; odd/odd starts alternate orientation with the runway count.
  if (!((origin_point.x | origin_point.y) & 1)) return false;
  int step_y = (origin_point.x & 1) && (!(origin_point.y & 1) || (runways & 1));
  int step_x = !step_y;
  int stride = step_y * city->map_width + step_x;
  TileIndex probe = origin;
  int new_cells = 0;
  for (int probe_y = origin_point.y, probe_x = origin_point.x; probe_y < city->map_height &&
      probe_x < city->map_width;
      probe_y += step_y, probe_x += step_x, probe += stride) {
    const TileInfo *tp = &city->tile[probe];
    if (ReadCityZoneKind(tp) != zone_kind) return false;
    int kind = tp->kind;
    if (zone_kind == ZONE_MILITARY && (city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH)) &&
        (kind == TK_ROAD || IsTileKindBridge(kind) || kind == TK_CRANE || kind == TK_MISSILE_SILO ||
            ReadTerrainCode(tp) || ReadUnderground(city, probe) || HasTileWater(tp)))
      return false;
    if (kind == TK_RUNWAY_FIRST || kind == TK_RUNWAY_LAST) continue;
    if (++new_cells == 5) break;
  }
  if (new_cells < 5) return false;
  int facing = (step_y != (city->geography.rotation & 1)) + city->geography.rotation;
  TileIndex tile = origin;
  for (int placed = 0; placed < 5; tile += stride) {
    int kind = ReadTile(city, tile);
    bool crossing = kind == TK_RUNWAY_FIRST;
    if (kind == TK_RUNWAY_LAST || (crossing && (ReadBuildingOrientation(city, tile) & 1) == step_y)) continue;
    if (!crossing && !IsTileKindUnbuilt(kind)) ClearSpecialZoneFootprint(city, tile);
    ReplaceCityTileAndStatistics(city, tile, crossing ? TK_RUNWAY_LAST : TK_RUNWAY_FIRST);
    if (zone_kind != ZONE_MILITARY) {
      city->tile[tile].flags |= TF_POWER_SERVICE;
      RefreshCityPowerConnections(city, tile);
    }
    InitializeCityBuildingFootprint(city, tile, 1, 1, crossing ? city->geography.rotation : facing);
    if (!crossing) ++placed;
  }
  return true;
}

// Grow a crane and four pier cells along an admitted water corridor. Five exposed water cells and depth at the far end
// are checked before clearing the shore occupant.
static bool GrowSpecialZonePier(City *city, TileIndex tile_index, int zone_kind) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  int direction;
  for (direction = 0; direction < 4; ++direction) {
    int next_y = point.y + kSpecialZoneCorridorYStep[direction];
    int next_x = point.x + kSpecialZoneCorridorXStep[direction];
    TileIndex next = MapTileXYOrInvalid(city, next_x, next_y);
    if (next != INVALID_TILE && HasTileWater(&city->tile[next]))
      break;
  }
  if (direction == 4 || (kSpecialZoneCorridorXStep[direction] != 0 && (point.y & 1)) ||
      (kSpecialZoneCorridorYStep[direction] != 0 && (point.x & 1)))
    return false;
  int end_y = point.y;
  int end_x = point.x;
  TileIndex end_tile = tile_index;
  for (int distance = 0; distance < 5; ++distance) {
    end_y += kSpecialZoneCorridorYStep[direction];
    end_x += kSpecialZoneCorridorXStep[direction];
    end_tile = MapTileXYOrInvalid(city, end_x, end_y);
    if (end_tile == INVALID_TILE) return false;
    const TileInfo *tp = &city->tile[end_tile];
    if (!IsTileKindExposedWater(tp->kind))
      return false;
  }
  const TileInfo *end = &city->tile[end_tile];
  if (ReadCityWaterHeight(end) < ReadCityLandHeight(end) + 2)
    return false;
  if (!IsTileKindUnbuilt(ReadTile(city, tile_index)))
    ClearSpecialZoneFootprint(city, tile_index);
  if (PlaceSpecialZoneFootprint(city, tile_index, TK_CRANE, 1) < 0 &&
      (city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH)))
    return false;
  WriteZoneByte(city, tile_index, (ReadZoneByte(city, tile_index) & CITY_ZONE_METADATA_MASK) | zone_kind);
  if (zone_kind == ZONE_MILITARY) {
    city->tile[tile_index].flags &= ~(TF_WATERED | TF_PIPED | TF_POWERED | TF_POWERABLE);
    RefreshCityPowerConnections(city, tile_index);
  }
  int facing = ((kSpecialZoneCorridorYStep[direction] == 0) == ((city->geography.rotation & 1) != 0)) +
      city->geography.rotation;
  int stride = kSpecialZoneCorridorYStep[direction] * city->map_width + kSpecialZoneCorridorXStep[direction];
  for (int distance = 0; distance < 4; ++distance) {
    tile_index += stride;
    ReplaceCityTileAndStatistics(city, tile_index, TK_PIER);
    InitializeCityBuildingFootprint(city, tile_index, 1, 1, facing);
  }
  return true;

}

// Apply one special-zone growth choice for the scan's selected zone. False permits the caller's fallback;
// an occupied single-cell request or unrecognized kind succeeds unchanged.
bool ReplaceSpecialZoneTile(City *city, TileIndex tile, int replacement, int zone_kind) {
  assert(IsMapTileValid(city, tile));
  if (zone_kind != ZONE_MILITARY && !TileHasNearbyPower(city, tile)) return false;
  int size;
  switch (replacement) {
  case TK_RUNWAY_FIRST: return GrowSpecialZoneRunway(city, tile, zone_kind);
  case TK_CRANE: return GrowSpecialZonePier(city, tile, zone_kind);
  case TK_MISSILE_SILO: return GrowSpecialZoneSilos(city, tile, zone_kind);
  case TK_CONTROL_TOWER_1X1_1: case TK_CONTROL_TOWER_1X1_2: case TK_WAREHOUSE_1X1_4:
  case TK_BUILDING_1X1_1: case TK_BUILDING_1X1_2: case TK_TARMAC: case TK_F_15B: case TK_HANGAR_1X1: case TK_RADAR:
    size = 1;
    break;
  case TK_PARKING_LOT_2X2_1: case TK_PARKING_LOT_2X2_2: case TK_LOADING_BAY:
  case TK_TOP_SECRET: case TK_CARGO_YARD: case TK_HANGAR_3X3:
    size = 2;
    break;
  default: return true;
  }

  if (size == 1) {
    if (!IsTileKindUnbuilt(ReadTile(city, tile))) return true;
  } else {
    CityPoint point = MapTilePt(city, tile);
    tile -= (point.y & 1) * city->map_width + (point.x & 1);
    if (ReadTile(city, tile) >= TK_WATER_TOWER) return false;
    for (int row = 0; row < 2; ++row)
      for (TileIndex cell = tile + row * city->map_width, end = cell + 2; cell < end; ++cell) {
        const TileInfo *tp = &city->tile[cell];
        int kind = tp->kind;
        if (zone_kind == ZONE_MILITARY && (city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH)) &&
            (kind == TK_ROAD || IsTileKindBridge(kind) || kind == TK_MISSILE_SILO || ReadUnderground(city, cell)))
          return false;
        if (kind == TK_RUNWAY_FIRST || kind == TK_RUNWAY_LAST || kind == TK_CRANE || ReadCityZoneKind(tp) != zone_kind)
          return false;
      }
    for (int row = 0; row < 2; ++row)
      for (TileIndex cell = tile + row * city->map_width, end = cell + 2; cell < end; ++cell)
        if (!IsTileKindUnbuilt(ReadTile(city, cell))) ClearSpecialZoneFootprint(city, cell);
  }

  // Single-cell military growth restores the pre-placement flags; larger footprints keep each cell's live flags.
  int single_cell_flags = size == 1 ? city->tile[tile].flags : 0;
  if (PlaceSpecialZoneFootprint(city, tile, replacement, size) < 0 &&
      (city->fixes & (1ull << CITY_FIX_MILITARY_GROWTH)))
    return false;
  for (int row = 0; row < size; ++row)
    for (TileIndex cell = tile + row * city->map_width, end = cell + size; cell < end; ++cell) {
      TileInfo *tp = &city->tile[cell];
      WriteZoneByte(city, cell, (ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK) | zone_kind);
      if (zone_kind == ZONE_MILITARY) {
        int flags = size == 1 ? single_cell_flags : tp->flags;
        tp->flags = flags & ~(TF_WATERED | TF_PIPED | TF_POWERED | TF_POWERABLE);
        RefreshCityPowerConnections(city, cell);
      }
    }
  return true;
}

// Tables

const u8 kGrowthBaseTable[21] = {
  TK_CLEAR, TK_LOWER_CLASS_HOMES_FIRST,
  TK_CHEAP_APARTMENTS, TK_NICE_APARTMENTS_2X2_2,
  TK_LARGE_APARTMENT_BUILDING_3X3_1, TK_GAS_STATION_1X1_1,
  TK_SHOPPING_CENTER, TK_OFFICE_RETAIL,
  TK_OFFICE_PARK, TK_WAREHOUSE_1X1_2,
  TK_WAREHOUSE_2X2, TK_FACTORY_2X2_3,
  TK_CHEMICAL_PROCESSING_3X3, TK_CONSTRUCTION_1X1_1,
  TK_CONSTRUCTION_2X2_1, TK_CONSTRUCTION_2X2_3,
  TK_CONSTRUCTION_3X3_1, TK_ABANDONED_BUILDING_1X1_1,
  TK_ABANDONED_BUILDING_2X2_1, TK_ABANDONED_BUILDING_2X2_3,
  TK_ABANDONED_BUILDING_3X3_1,
};

const u8 kTileScanMaskTable[4] = {
  128, 16, 32, 64
};
