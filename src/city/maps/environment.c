#include "city/economy/facilities.h"
#include "city/maps/buildings.h"
#include "city/infrastructure/power_lines.h"
#include "city/economy/types.h"
#include "city/events/news.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include <string.h>

#include "city/state.h"
#include "city/economy/economy.h"
#include "city/engine/simulation.h"
#include "city/events/events.h"
#include "city/growth/growth.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/layers.h"
#include "city/tools/tools.h"
#include "city/saveload/file.h"
#include "city/saveload/map_format.h"
#include "game/state.h"
#include "audio/state.h"
#include "assets/state.h"

// Read the low 16 bits of an initialized sector's funding rate for maintenance compatibility. Sector must index the
// economy table; the read does not clamp or change its stored rate.
static int ReadCityMaintenancePercent(const City *city, int sector) {
  // Stock reads only the low word of the current funding rate.
  return (u16)city->economy.sector[sector].current_rate;
}

// Clamp an integer metric to the unsigned-byte range. Returns an int from zero through 255 without narrowing
// wraparound or side effects.
static int ClampU8(int value) {
  return IntClamp(value, 0, 255);
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
            TrySpawnTile237Events(city, cell_index);
          else
            TrySpawnTile248Events(city, cell_index);
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
  bool large_city_effect = ((city->fixes & (1 << CITY_FIX_RESTORED_CHEATS)) && city->cheats.church == 2) ||
      (u32)(2500 * city->statistics.count[TK_CHURCH]) < city->indicators.population;
  ScanCityTileBatch(city, quarter_x, quarter_y, 0, city->map_cell_count / 16, large_city_effect);
}

// Saturating signed influence into one byte in a 32x32 map. Layer must be the supplied quarter-resolution backing;
// bounds are checked and contributions are accumulated in place.
void AddFacilityInfluenceCell(City *city, u8 *layer, int x, int y, int amount) {
  if (y < 0 || x < 0 || y >= city->map_height / 4 || x >= city->map_width / 4)
    return;
  int value = layer[(y * city->map_width / 4 + x) * sizeof(CityQuarterMapStatistics)];
  int result = ClampU8(value + amount);
  WriteByte(&layer[(y * city->map_width / 4 + x) * sizeof(CityQuarterMapStatistics)], result);
}

// Spread one facility's influence through the stock radius-three footprint. Requires the selected coarse layer and
// admitted facility radius; reuses caller storage and clips at map boundaries.
void SpreadFacilityInfluence(City *city, u8 *layer, int x, int y, int amount) {
  static const i8 offset_y[37] = {0, -1, 1, 0, 0, -1, 1, -1, 1, -1, 0, 1, -1, 0, 1, -2, -2, -2, 2, 2, 2, -1, 0, 1, -1,
      0, 1, -3, -3, -3, 3, 3, 3, -2, -2, 2, 2};
  static const i8 offset_x[37] = {0, 0, 0, -1, 1, -1, -1, 1, 1, -2, -2, -2, 2, 2, 2, -1, 0, 1, -1, 0, 1, -3, -3, -3, 3,
      3, 3, -1, 0, 1, -1, 0, 1, -2, 2, -2, 2};
  static const u8 amount_level[37] = {
      0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
  int level[5];
  level[0] = amount;
  level[1] = 4 * level[0] / 5;
  level[2] = 3 * level[1] / 4;
  level[3] = 2 * level[2] / 3;
  level[4] = level[3] / 2;
  for (int index = 0; index < 37; ++index)
    AddFacilityInfluenceCell(city, layer, x + offset_x[index], y + offset_y[index], level[amount_level[index]]);
}

enum {
  ENV_POLLUTION_INPUT, ENV_POLLUTION_SMOOTH, ENV_CENTER, ENV_INFLUENCE, ENV_LAND_VALUE, ENV_CLEAR, ENV_FACILITIES,
  ENV_DENSITY, ENV_CRIME_INPUT, ENV_CRIME_SMOOTH, ENV_DONE
};

// Accumulate raw pollution for one admitted environment section row. Requires the pollution pass's initialized work
// and map layers; writes its coarse field before the separate smoothing stage.
static inline void BuildPollutionSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 2);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 2; ++column) {
    int x = section_x + column;
    int value = city->half[y * width / 2 + x].pollution + city->half[y * width / 2 + x].traffic_density / 5;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        TileIndex cell = (2 * y + dy) * width + 2 * x + dx;
        int tile = CityTileStatisticAt(city, cell);
        if (IsTileKindBuilding(tile))
          value += kTilePollutionContribution[tile - TK_RESIDENTIAL_FIRST];
        if (tile == TK_RADIOACTIVE_WASTE)
          value += 200;
      }
    WriteScratch(city, x, y, value);
  }
}

// Both half-resolution smoothing passes read the same cardinal scratch cells. Coordinates must match the pass's
// scratch resolution; returns the local integer average without replacing the source field.
static int AverageEnvironmentScratch(const City *city, int x, int y, int center_weight, int divisor) {
  int total = center_weight * ReadScratch(city, x, y);
  if (y) {
    total += ReadScratch(city, x, y - 1);
    ++divisor;
  }
  if (y < city->map_height / 2 - 1) {
    total += ReadScratch(city, x, y + 1);
    ++divisor;
  }
  if (x) {
    total += ReadScratch(city, x - 1, y);
    ++divisor;
  }
  if (x < city->map_width / 2 - 1) {
    total += ReadScratch(city, x + 1, y);
    ++divisor;
  }
  return total / divisor;
}

// Smooth one pollution section row and accumulate the resulting metric in its scan state. Source and scratch stages
// must already be prepared; row and section coordinates use the pass's coarse resolution.
static inline void SmoothPollutionSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 2);
  int divisor_base = city->infrastructure.capacity - city->indicators.industrial_pollution_bonus +
      ((city->unlocks.ordinance_flags & CITY_POLICY_FLAG_POLLUTION_CONTROLS) ? 5 : 4);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 2; ++column) {
    int x = section_x + column;
    int value = AverageEnvironmentScratch(city, x, y, 2, IntMax(divisor_base, 1));
    value = IntMin(value, 255);
    city->half[y * width / 2 + x].pollution = (u8)value;
    city->indicators.city_pollution += (u32)value;
  }
}

// Accumulate developed-tile positions for the environment scan's city-center calculation. Requires valid
// full-resolution section/row coordinates and initialized scan sums; does not move the camera.
static inline void AccumulateCityCenterSectionRow(City *city, CityEnvironmentScan *scan, int section, int y) {
  const int width = city->map_width;
  int section_x = section * CITY_MAP_SECTION_SIZE;
  for (int column = 0; column < CITY_MAP_SECTION_SIZE; ++column) {
    int x = section_x + column;
    const TileIndex cell_index = y * width + x;
    int tile = CityTileStatisticAt(city, cell_index);
    if (IsTileKindBuilding(tile)) {
      scan->sum_y += y;
      scan->sum_x += x;
      WriteScratch(city, x, y, 40);
      ++scan->count;
      city->tile[cell_index].flags &= ~TF_VISITED;
    }
  }
}

// Four consecutive tiles share scratch cells. Preserve row and tile order: coarse visited writes can alias flags read
// later in this scan. Requires valid section/row coordinates and initialized scan totals; mutates coverage flags and
// live metrics in pass order.
static inline void AccumulateCityInfluenceSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  const int height = city->map_height;
  int section_x = section * CITY_MAP_SECTION_SIZE;
  int scratch_y = y / 4;
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 4; ++column) {
    int group_x = section_x + 4 * column;
    int scratch_x = group_x / 4;
    int land_influence = ReadScratch(city, scratch_x, scratch_y);
    int industrial_influence = ReadScratch(city, scratch_x, height / 4 + scratch_y);
    for (int x = group_x; x < group_x + 4; ++x) {
      const TileIndex cell_index = y * width + x;
      int tile = CityTileStatisticAt(city, cell_index);
      const TileInfo *tp = &city->tile[cell_index];
      int flags = tp->flags;
      if (tile != TK_CLEAR) {
        if (tile == TK_LARGE_PARK)
          land_influence += 40;
        else if (IsTileKindTreeOrPark(tile))
          land_influence += 20;
        else if (IsTileKindBareGround(tile))
          land_influence -= 20;
      } else if (HasTileWater(tp)) {
        land_influence += 12;
        industrial_influence += 12;
      } else
        land_influence += 4;
      if (IsTileKindTransportOrBuilding(tile) || ReadCityZoneKind(tp)) {
        city->tile[(y >> 1) * width + (x >> 1)].flags |= TF_VISITED;
        ++city->indicators.zone_count;
      }
      if (flags & TF_WATERED) {
        land_influence += 4;
        industrial_influence += 4;
      }
      if (ReadTerrainCode(tp) != CITY_TERRAIN_FLAT &&
          !HasTileWater(tp))
        land_influence += 12;
    }
    WriteScratch(city, scratch_x, scratch_y, land_influence);
    WriteScratch(city, scratch_x, height / 4 + scratch_y, industrial_influence);
  }
}

// Compute one coarse land-value row from the prepared environment metrics. Requires the matching scan state and prior
// source stages; writes the land-value layer in place.
static inline void UpdateLandValueSectionRow(City *city, const CityEnvironmentScan *scan, int section, int y) {
  const int width = city->map_width;
  const int height = city->map_height;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 2);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 2; ++column) {
    int x = section_x + column;
    int score = 0;
    if (city->tile[y * width + x].flags & TF_VISITED) {
      int tile_x = 2 * x;
      int tile_y = 2 * y;
      int category = ReadCityZoneKind(&city->tile[tile_y * width + tile_x]);
      if (category == ZONE_NONE)
        category = ReadCityZoneKind(&city->tile[(tile_y + 1) * width + tile_x + 1]);
      int scratch_x = x / 2;
      int scratch_y = y / 2;
      bool industrial = category == ZONE_INDUSTRIAL_LOW || category == ZONE_INDUSTRIAL_HIGH;
      int source_y = industrial ? height / 4 + scratch_y : scratch_y;
      int sum = ReadScratch(city, scratch_x, source_y);
      int divisor = 1;
      if (scratch_y) {
        sum += ReadScratch(city, scratch_x, scratch_y - 1);
        ++divisor;
      }
      if (scratch_y < height / 4 - 1) {
        sum += ReadScratch(city, scratch_x, scratch_y + 1);
        ++divisor;
      }
      if (scratch_x) {
        sum += ReadScratch(city, scratch_x - 1, source_y);
        ++divisor;
      }
      if (scratch_x < width / 4 - 1) {
        sum += ReadScratch(city, scratch_x + 1, source_y);
        ++divisor;
      }
      int dx = scan->center_x - x;
      int dy = scan->center_y - y;
      if (dy < 0)
        dy = -dy;
      if (dx < 0)
        dx = -dx;
      int distance = 64 - dy - dx;
      int pollution_value = city->half[y * width / 2 + x].pollution;
      int crime_value = city->half[y * width / 2 + x].crime;
      int density_value = city->quarter[scratch_y * width / 4 + scratch_x].density;
      if (category == ZONE_COMMERCIAL_LOW || category == ZONE_COMMERCIAL_HIGH) {
        score = sum / divisor;
        if (distance > 0)
          score += distance;
        score += density_value / 3 - (pollution_value >> 2) - crime_value / 3;
      } else if (category == ZONE_INDUSTRIAL_LOW || category == ZONE_INDUSTRIAL_HIGH) {
        score = sum / divisor + (category == ZONE_INDUSTRIAL_HIGH ? 21 : 0);
        if (distance / 4 > 0)
          score += distance / 4;
        score -= (pollution_value >> 4) + (crime_value >> 2);
      } else {
        score = sum / divisor + (density_value < 64 ? 21 : 0);
        if (distance / 2 > 0)
          score += distance / 2;
        score -= pollution_value / 5 + crime_value / 3;
      }
      int tile = city->tile[tile_y * width + tile_x].kind;
      if (IsTileKindBuilding(tile) &&
          kTileZoneCategory[tile - TK_RESIDENTIAL_FIRST] == CITY_BUILDING_ABANDONED)
        score -= score / 2;
      score = ClampU8(score);
      city->indicators.city_land_value += (u32)score;
    }
    city->half[y * width / 2 + x].land_value = (u8)score;
  }
}

// Clear one quarter-resolution environment scratch row before facility influence accumulation. Section and row must be
// valid for that coarse layer; other pass stages must not retain these scratch values.
static inline void ClearEnvironmentSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 4);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 4; ++column) {
    int x = section_x + column;
    WriteScratch(city, x, y, 0);
    city->quarter[y * width / 4 + x].police_coverage = 0;
    city->quarter[y * width / 4 + x].fire_coverage = 0;
  }
}

// Add facility and ordinance influence from one full-resolution section row to the environment workspace. Requires
// valid section coordinates and cleared scratch; repeated rows would count influences twice.
static inline void AccumulateFacilitySectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * CITY_MAP_SECTION_SIZE;
  u32 options = city->unlocks.ordinance_flags;
  for (int column = 0; column < CITY_MAP_SECTION_SIZE; ++column) {
    int x = section_x + column;
    if (x == 0 || x == width - 1) continue;
    const TileIndex cell_index = y * width + x;
    int tile = CityTileStatisticAt(city, cell_index);
    if (!IsTileKindBuilding(tile))
      continue;
    int scratch_x = x / 4;
    int scratch_y = y / 4;
    if (IsTileKindFacility(tile)) {
      int density = !IsTileKindArcology(tile) || tile == TK_BRAUN_LLAMA_DOME ? 2 : 12;
      WriteScratch(city, scratch_x, scratch_y, ReadScratch(city, scratch_x, scratch_y) + density);
      if ((tile == TK_POLICE_STATION || tile == TK_FIRE_STATION) &&
          IsCityBuildingCorner(city, cell_index, (CITY_BUILDING_CORNER_TOP_RIGHT - city->geography.rotation) & 3)) {
        int value;
        if (tile == TK_POLICE_STATION)
          value = (i32)((i64)(i32)city->economy.sector[CITY_ECONOMY_POLICE].current_rate *
              (city->infrastructure.facility_ratio + 5) / 2);
        else
          value = (i32)((i64)5 * (i32)city->economy.sector[CITY_ECONOMY_FIRE].current_rate / 2);
        if (!(city->tile[cell_index].flags & TF_POWERED))
          value /= 2;
        SpreadFacilityInfluence(
            city, tile == TK_POLICE_STATION ? &city->quarter[0].police_coverage : &city->quarter[0].fire_coverage,
            x / 4, y / 4, value);
      }
    } else {
      WriteScratch(city, scratch_x, scratch_y,
          ReadScratch(city, scratch_x, scratch_y) + kTileDensityContribution[tile - TK_RESIDENTIAL_FIRST]);
      if (options & CITY_POLICY_FLAG_NEIGHBORHOOD_WATCH) {
        int current = city->quarter[scratch_y * width / 4 + scratch_x].police_coverage;
        if (current < 254)
          city->quarter[scratch_y * width / 4 + scratch_x].police_coverage = current + 2;
      }
      if (options & CITY_POLICY_FLAG_VOLUNTEER_FIRE) {
        int current = city->quarter[scratch_y * width / 4 + scratch_x].fire_coverage;
        if (current < 254)
          city->quarter[scratch_y * width / 4 + scratch_x].fire_coverage = current + 2;
      }
    }
  }
}

// Convert accumulated environment values into one quarter-resolution density row. Requires the preceding accumulation
// stage; updates the density layer without allocating a separate plane.
static inline void UpdateDensitySectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 4);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 4; ++column) {
    int x = section_x + column;
    int value = 4 * ReadScratch(city, x, y);
    value = IntMin(value, 255);
    int change = value - city->quarter[y * width / 4 + x].density;
    city->quarter[y * width / 4 + x].density = (u8)value;
    int smoothed = (8 * change + 128 + 7 * city->quarter[y * width / 4 + x].density_history) / 8;
    city->quarter[y * width / 4 + x].density_history = ClampU8(smoothed);
  }
}

// Compute one raw crime row from current policies, population and coverage. Requires valid half-resolution section/row
// coordinates; the following smoothing stage consumes this prepared field.
static inline void BuildCrimeSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 2);
  u32 options = city->unlocks.ordinance_flags;
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 2; ++column) {
    int x = section_x + column;
    int value = 0;
    if (city->tile[y * width + x].flags & TF_VISITED) {
      value = city->quarter[(y / 2) * width / 4 + x / 2].density -
          (city->half[y * width / 2 + x].land_value >> 2) -
          (city->quarter[(y / 2) * width / 4 + x / 2].police_coverage >> 1);
      if (options & CITY_POLICY_FLAG_LEGALIZED_GAMBLING)
        value += 16;
    }
    WriteScratch(city, x, y, value);
  }
}

// Smooth one prepared crime row and retain its aggregate metric. Requires the raw crime stage and compatible scratch;
// section coordinates are in the coarse crime layer.
static inline void SmoothCrimeSectionRow(City *city, int section, int y) {
  const int width = city->map_width;
  int section_x = section * (CITY_MAP_SECTION_SIZE / 2);
  for (int column = 0; column < CITY_MAP_SECTION_SIZE / 2; ++column) {
    int x = section_x + column;
    int value = AverageEnvironmentScratch(city, x, y, 1, 1);
    value = ClampU8(value);
    city->half[y * width / 2 + x].crime = (u8)value;
    city->indicators.city_crime += (u32)value;
  }
}

// Update land value, pollution and crime. Scan must persist across bounded row budgets; false retains its stage/cursor
// and true finalizes the map metrics.
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows) {
  const int height = city->map_height;
  const int sections = city->map_width / CITY_MAP_SECTION_SIZE;
  while (rows > 0 && scan->stage != ENV_DONE) {
    int limit = height / 2;
    void (*update_row)(City *, int, int) = NULL;
    switch (scan->stage) {
    case ENV_POLLUTION_INPUT:
      if (scan->row == 0) {
        InvalidateMapLayer(city, CITY_MAP_CHANGE_POLLUTION);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_CRIME);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_LAND_VALUE);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_DENSITY);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_POLICE_COVERAGE);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_FIRE_COVERAGE);
        InvalidateMapLayer(city, CITY_MAP_CHANGE_DENSITY_HISTORY);
        city->indicators.city_pollution = 0;
      }
      update_row = BuildPollutionSectionRow;
      break;
    case ENV_POLLUTION_SMOOTH:
      update_row = SmoothPollutionSectionRow;
      break;
    case ENV_CENTER:
      if (scan->row == 0) {
        scan->sum_y = scan->sum_x = 0;
        scan->count = 1;
      }
      limit = height;
      break;
    case ENV_INFLUENCE:
      if (scan->row == 0) {
        scan->center_y = scan->sum_y / (2 * scan->count);
        scan->center_x = scan->sum_x / (2 * scan->count);
        city->geography.center_y = 2 * scan->center_y;
        city->geography.center_x = 2 * scan->center_x;
        city->indicators.zone_count = 0;
      }
      limit = height;
      update_row = AccumulateCityInfluenceSectionRow;
      break;
    case ENV_LAND_VALUE:
      if (scan->row == 0) {
        city->indicators.city_land_value = 0;
      }
      break;
    case ENV_CLEAR:
      limit = height / 4;
      update_row = ClearEnvironmentSectionRow;
      break;
    case ENV_FACILITIES:
      limit = height - 1;
      update_row = AccumulateFacilitySectionRow;
      break;
    case ENV_DENSITY:
      limit = height / 4;
      update_row = UpdateDensitySectionRow;
      break;
    case ENV_CRIME_INPUT:
      update_row = BuildCrimeSectionRow;
      break;
    case ENV_CRIME_SMOOTH:
      if (scan->row == 0) {
        city->indicators.city_crime = 0;
      }
      update_row = SmoothCrimeSectionRow;
      break;
    default: continue;
    }
    int end = IntMin(limit, scan->row + rows);
    for (int y = scan->row; y < end; ++y)
      for (int section = 0; section < sections; ++section) {
        if (scan->stage == ENV_CENTER) AccumulateCityCenterSectionRow(city, scan, section, y);
        else if (scan->stage == ENV_LAND_VALUE) UpdateLandValueSectionRow(city, scan, section, y);
        else update_row(city, section, y);
      }
    rows -= end - scan->row;
    scan->row = end;
    if (end == limit) {
      scan->row = scan->stage == ENV_CLEAR ? 1 : 0;
      ++scan->stage;
    }
  }
  return scan->stage == ENV_DONE;
}

// Complete the environment-map pipeline using a fresh local scan record. Requires initialized map/scratch layers;
// performs all stages synchronously through StepCityEnvironment and updates their metrics/damage.
void UpdateCityEnvironmentMaps(City *city) {
  CityEnvironmentScan scan = {0};
  StepCityEnvironment(city, &scan, INT32_MAX);
}
