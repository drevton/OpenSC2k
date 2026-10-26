// Environment passes derive coarse pollution, land value, density and service coverage from City tiles.
// Their stages share City scratch storage; an incremental scan must finish before another scratch user runs.
#include "city/maps/environment.h"
#include "city/maps/map.h"

#include "city/economy/types.h"
#include "city/maps/buildings.h"
#include "city/maps/kinds.h"
#include "city/maps/terrain.h"
#include "city/state.h"

// Environment passes interpret the shared words as signed 16-bit samples, with map_width words per row.
// Active rows use only half/quarter that width. Land and industrial influence occupy separate vertical bands;
// preserve the full stride when switching stages. Writes keep their existing 16-bit wrapping.
static inline int ReadScratch(const City *city, int x, int y) {
  const i16 *cells = (const i16 *)city->scratch;
  return cells[y * city->map_width + x];
}

static inline void WriteScratch(City *city, int x, int y, int value) {
  city->scratch[y * city->map_width + x] = value;
}

// Add signed influence to an admitted coverage record, saturating at byte bounds.
// The spreading loop clips coordinates and supplies a constant police/fire choice.
static inline void AddFacilityInfluenceCell(CityQuarterMapStatistics *stats, bool police, int amount) {
  if (police)
    stats->police_coverage = IntClamp(stats->police_coverage + amount, 0, 255);
  else
    stats->fire_coverage = IntClamp(stats->fire_coverage + amount, 0, 255);
}

// Spread police or fire coverage through the stock radius-three footprint, in quarter-resolution coordinates.
// Select the member once so each loop uses typed record writes. Negative offsets wrap to unsigned for clipping.
static void SpreadFacilityInfluence(City *city, bool police, int x, int y, int amount) {
  typedef struct { i8 dx, dy; u8 level; } InfluenceOffset;
  static const InfluenceOffset kInfluenceOffsets[] = {
    {0, 0, 0}, {0, -1, 1}, {0, 1, 1}, {-1, 0, 1},
    {1, 0, 1}, {-1, -1, 2}, {-1, 1, 2}, {1, -1, 2},
    {1, 1, 2}, {-2, -1, 3}, {-2, 0, 3}, {-2, 1, 3},
    {2, -1, 3}, {2, 0, 3}, {2, 1, 3}, {-1, -2, 3},
    {0, -2, 3}, {1, -2, 3}, {-1, 2, 3}, {0, 2, 3},
    {1, 2, 3}, {-3, -1, 4}, {-3, 0, 4}, {-3, 1, 4},
    {3, -1, 4}, {3, 0, 4}, {3, 1, 4}, {-1, -3, 4},
    {0, -3, 4}, {1, -3, 4}, {-1, 3, 4}, {0, 3, 4},
    {1, 3, 4}, {-2, -2, 4}, {2, -2, 4}, {-2, 2, 4},
    {2, 2, 4},
  };
  int level[5];
  level[0] = amount;
  level[1] = 4 * level[0] / 5;
  level[2] = 3 * level[1] / 4;
  level[3] = 2 * level[2] / 3;
  level[4] = level[3] / 2;
  u32 width = city->map_width / 4, height = city->map_height / 4;
  CityQuarterMapStatistics *quarter = city->quarter;
  const InfluenceOffset *end = kInfluenceOffsets + sizeof(kInfluenceOffsets) / sizeof(kInfluenceOffsets[0]);
  if (police) {
    for (const InfluenceOffset *offset = kInfluenceOffsets; offset != end; ++offset) {
      u32 cell_x = x + offset->dx, cell_y = y + offset->dy;
      if (cell_y >= height || cell_x >= width) continue;
      CityQuarterMapStatistics *stats = &quarter[cell_y * width + cell_x];
      AddFacilityInfluenceCell(stats, true, level[offset->level]);
    }
  } else {
    for (const InfluenceOffset *offset = kInfluenceOffsets; offset != end; ++offset) {
      u32 cell_x = x + offset->dx, cell_y = y + offset->dy;
      if (cell_y >= height || cell_x >= width) continue;
      CityQuarterMapStatistics *stats = &quarter[cell_y * width + cell_x];
      AddFacilityInfluenceCell(stats, false, level[offset->level]);
    }
  }
}

enum CityEnvironmentStage {
  ENV_POLLUTION_INPUT, ENV_POLLUTION_SMOOTH, ENV_CENTER, ENV_INFLUENCE, ENV_LAND_VALUE, ENV_CLEAR, ENV_FACILITIES,
  ENV_DENSITY, ENV_CRIME_INPUT, ENV_CRIME_SMOOTH, ENV_DONE
};

// Accumulate raw pollution for one half-resolution row. Requires initialized map layers;
// the following smoothing stage consumes this scratch field.
static inline void BuildPollutionRow(City *city, int y) {
  const int width = city->map_width;
  for (int x = 0; x < width / 2; ++x) {
    const CityHalfMapStatistics *stats = &city->half[y * width / 2 + x];
    int value = stats->pollution + stats->traffic_density / 5;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        TileIndex cell = (2 * y + dy) * width + 2 * x + dx;
        const TileInfo *tp = &city->tile[cell];
        if (IsTileKindBuilding(tp->kind)) {
          int statistic = ReadCityTileStatisticsKey(city, tp);
          value += kTilePollutionContribution[statistic - TK_RESIDENTIAL_FIRST];
        }
        if (tp->kind == TK_RADIOACTIVE_WASTE)
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

// Smooth one half-resolution pollution row and accumulate the city metric.
// The raw pollution stage must have finished before any row is smoothed.
static inline void SmoothPollutionRow(City *city, int y) {
  const int width = city->map_width;
  int divisor_base = city->infrastructure.capacity - city->indicators.industrial_pollution_bonus +
      ((city->unlocks.ordinance_flags & CITY_POLICY_FLAG_POLLUTION_CONTROLS) ? 5 : 4);
  for (int x = 0; x < width / 2; ++x) {
    int value = AverageEnvironmentScratch(city, x, y, 2, IntMax(divisor_base, 1));
    value = IntMin(value, 255);
    city->half[y * width / 2 + x].pollution = (u8)value;
    city->indicators.city_pollution += (u32)value;
  }
}

// Accumulate developed-tile positions for the environment scan's city-center calculation. Requires valid
// full-resolution row coordinates and initialized scan sums; does not move the camera.
static inline void AccumulateCityCenterRow(City *city, CityEnvironmentScan *scan, int y) {
  const int width = city->map_width;
  for (int x = 0; x < width; ++x) {
    const TileIndex cell_index = y * width + x;
    TileInfo *tp = &city->tile[cell_index];
    if (IsTileKindBuilding(tp->kind)) {
      scan->sum_y += y;
      scan->sum_x += x;
      WriteScratch(city, x, y, 40);
      ++scan->count;
      tp->flags &= ~TF_VISITED;
    }
  }
}

// Four consecutive tiles share scratch cells. Preserve row and tile order: coarse visited writes can alias flags read
// later in this scan. Requires a valid full-resolution row and initialized scan totals; mutates coverage flags and
// live metrics in pass order.
static inline void AccumulateCityInfluenceRow(City *city, int y) {
  const int width = city->map_width;
  const int height = city->map_height;
  int scratch_y = y / 4;
  for (int scratch_x = 0; scratch_x < width / 4; ++scratch_x) {
    int group_x = 4 * scratch_x;
    int land_influence = ReadScratch(city, scratch_x, scratch_y);
    int industrial_influence = ReadScratch(city, scratch_x, height / 4 + scratch_y);
    for (int x = group_x; x < group_x + 4; ++x) {
      const TileIndex cell_index = y * width + x;
      const TileInfo *tp = &city->tile[cell_index];
      int kind = tp->kind;
      int flags = tp->flags;
      if (kind != TK_CLEAR && !IsTileKindExposedWater(kind)) {
        if (kind == TK_LARGE_PARK)
          land_influence += 40;
        else if (IsTileKindTreeOrPark(kind))
          land_influence += 20;
        else if (IsTileKindBareGround(kind))
          land_influence -= 20;
      } else if (IsTileKindExposedWater(kind)) {
        land_influence += 12;
        industrial_influence += 12;
      } else
        land_influence += 4;
      if (IsTileKindTransportOrBuilding(kind) || IsTileKindWaterPowerLine(tp->kind) || ReadCityZoneKind(tp)) {
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

// Reuse the scheduled pass's tile order and contribution rules when rebuilding loaded-map influence.
// Existing scratch values are inputs; only the occupied/zoned tile count is reset.
void AccumulateCityLandInfluence(City *city) {
  city->indicators.zone_count = 0;
  for (int y = 0; y < city->map_height; ++y) AccumulateCityInfluenceRow(city, y);
}

// Compute one coarse land-value row from the prepared environment metrics. Requires the matching scan state and prior
// source stages; writes the land-value layer in place.
static inline void UpdateLandValueRow(City *city, const CityEnvironmentScan *scan, int y) {
  const int width = city->map_width;
  const int height = city->map_height;
  for (int x = 0; x < width / 2; ++x) {
    CityHalfMapStatistics *stats = &city->half[y * width / 2 + x];
    int score = 0;
    if (city->tile[y * width + x].flags & TF_VISITED) {
      int tile_x = 2 * x;
      int tile_y = 2 * y;
      const TileInfo *tp = &city->tile[tile_y * width + tile_x];
      int zone = ReadCityZoneKind(tp);
      if (zone == ZONE_NONE)
        zone = ReadCityZoneKind(&city->tile[(tile_y + 1) * width + tile_x + 1]);
      int scratch_x = x / 2;
      int scratch_y = y / 2;
      bool industrial = zone == ZONE_INDUSTRIAL_LOW || zone == ZONE_INDUSTRIAL_HIGH;
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
      int pollution_value = stats->pollution;
      int crime_value = stats->crime;
      int density_value = city->quarter[scratch_y * width / 4 + scratch_x].density;
      if (zone == ZONE_COMMERCIAL_LOW || zone == ZONE_COMMERCIAL_HIGH) {
        score = sum / divisor;
        if (distance > 0)
          score += distance;
        score += density_value / 3 - (pollution_value >> 2) - crime_value / 3;
      } else if (zone == ZONE_INDUSTRIAL_LOW || zone == ZONE_INDUSTRIAL_HIGH) {
        score = sum / divisor + (zone == ZONE_INDUSTRIAL_HIGH ? 21 : 0);
        if (distance / 4 > 0)
          score += distance / 4;
        score -= (pollution_value >> 4) + (crime_value >> 2);
      } else {
        score = sum / divisor + (density_value < 64 ? 21 : 0);
        if (distance / 2 > 0)
          score += distance / 2;
        score -= pollution_value / 5 + crime_value / 3;
      }
      int kind = tp->kind;
      if (IsTileKindBuilding(kind) &&
          kTileZoneCategory[kind - TK_RESIDENTIAL_FIRST] == CITY_BUILDING_ABANDONED)
        score -= score / 2;
      score = IntClamp(score, 0, 255);
      city->indicators.city_land_value += (u32)score;
    }
    stats->land_value = (u8)score;
  }
}

// Clear one quarter-resolution scratch and service-coverage row before facility influence accumulation.
// Prior stages must have finished consuming these scratch values.
static inline void ClearEnvironmentRow(City *city, int y) {
  const int width = city->map_width;
  for (int x = 0; x < width / 4; ++x) {
    WriteScratch(city, x, y, 0);
    CityQuarterMapStatistics *stats = &city->quarter[y * width / 4 + x];
    stats->police_coverage = 0;
    stats->fire_coverage = 0;
  }
}

// Add facility and ordinance influence from one full-resolution interior row to the cleared workspace.
// Skip the outer map border; repeated rows would count influences twice.
static inline void AccumulateFacilityRow(City *city, int y) {
  const int width = city->map_width;
  u32 options = city->unlocks.ordinance_flags;
  for (int x = 1; x < width - 1; ++x) {
    const TileIndex cell_index = y * width + x;
    const TileInfo *tp = &city->tile[cell_index];
    int kind = tp->kind;
    if (!IsTileKindBuilding(kind))
      continue;
    int scratch_x = x / 4;
    int scratch_y = y / 4;
    if (IsTileKindFacility(kind)) {
      int density = !IsTileKindArcology(kind) || kind == TK_BRAUN_LLAMA_DOME ? 2 : 12;
      WriteScratch(city, scratch_x, scratch_y, ReadScratch(city, scratch_x, scratch_y) + density);
      if ((kind == TK_POLICE_STATION || kind == TK_FIRE_STATION) &&
          IsCityBuildingCorner(city, cell_index, (CITY_BUILDING_CORNER_TOP_RIGHT - city->geography.rotation) & 3)) {
        int value;
        if (kind == TK_POLICE_STATION)
          value = (i32)((i64)(i32)city->economy.sector[CITY_ECONOMY_POLICE].current_rate *
              (city->infrastructure.facility_ratio + 5) / 2);
        else
          value = (i32)((i64)5 * (i32)city->economy.sector[CITY_ECONOMY_FIRE].current_rate / 2);
        if (!(tp->flags & TF_POWERED))
          value /= 2;
        SpreadFacilityInfluence(city, kind == TK_POLICE_STATION, x / 4, y / 4, value);
      }
    } else {
      int statistic = ReadCityTileStatisticsKey(city, tp);
      WriteScratch(city, scratch_x, scratch_y,
          ReadScratch(city, scratch_x, scratch_y) + kTileDensityContribution[statistic - TK_RESIDENTIAL_FIRST]);
      CityQuarterMapStatistics *stats = &city->quarter[scratch_y * width / 4 + scratch_x];
      if (options & CITY_POLICY_FLAG_NEIGHBORHOOD_WATCH) {
        int current = stats->police_coverage;
        if (current < 254)
          stats->police_coverage = current + 2;
      }
      if (options & CITY_POLICY_FLAG_VOLUNTEER_FIRE) {
        int current = stats->fire_coverage;
        if (current < 254)
          stats->fire_coverage = current + 2;
      }
    }
  }
}

// Convert accumulated environment values into one quarter-resolution density row. Requires the preceding accumulation
// stage; updates the density layer without allocating a separate plane.
static inline void UpdateDensityRow(City *city, int y) {
  const int width = city->map_width;
  for (int x = 0; x < width / 4; ++x) {
    int value = 4 * ReadScratch(city, x, y);
    value = IntMin(value, 255);
    CityQuarterMapStatistics *stats = &city->quarter[y * width / 4 + x];
    int change = value - stats->density;
    stats->density = (u8)value;
    int smoothed = (8 * change + 128 + 7 * stats->density_history) / 8;
    stats->density_history = IntClamp(smoothed, 0, 255);
  }
}

// Compute one half-resolution crime row from current policies, population and coverage.
// The following smoothing stage consumes this scratch field.
static inline void BuildCrimeRow(City *city, int y) {
  const int width = city->map_width;
  u32 options = city->unlocks.ordinance_flags;
  for (int x = 0; x < width / 2; ++x) {
    int value = 0;
    if (city->tile[y * width + x].flags & TF_VISITED) {
      const CityQuarterMapStatistics *stats = &city->quarter[(y / 2) * width / 4 + x / 2];
      value = stats->density - (city->half[y * width / 2 + x].land_value >> 2) - (stats->police_coverage >> 1);
      if (options & CITY_POLICY_FLAG_LEGALIZED_GAMBLING)
        value += 16;
    }
    WriteScratch(city, x, y, value);
  }
}

// Smooth one half-resolution crime row and accumulate the city metric.
// The raw crime stage must have finished before any row is smoothed.
static inline void SmoothCrimeRow(City *city, int y) {
  const int width = city->map_width;
  for (int x = 0; x < width / 2; ++x) {
    int value = AverageEnvironmentScratch(city, x, y, 1, 1);
    value = IntClamp(value, 0, 255);
    city->half[y * width / 2 + x].crime = (u8)value;
    city->indicators.city_crime += (u32)value;
  }
}

// Update land value, pollution and crime. Scan must persist across bounded row budgets; false retains its stage/cursor
// and true finalizes the map metrics.
bool StepCityEnvironment(City *city, CityEnvironmentScan *scan, int rows) {
  const int height = city->map_height;
  while (rows > 0 && scan->stage != ENV_DONE) {
    int limit = height / 2;
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
      break;
    case ENV_POLLUTION_SMOOTH:
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
      break;
    case ENV_LAND_VALUE:
      if (scan->row == 0) {
        city->indicators.city_land_value = 0;
      }
      break;
    case ENV_CLEAR:
      limit = height / 4;
      break;
    case ENV_FACILITIES:
      limit = height - 1;
      break;
    case ENV_DENSITY:
      limit = height / 4;
      break;
    case ENV_CRIME_INPUT:
      break;
    case ENV_CRIME_SMOOTH:
      if (scan->row == 0) {
        city->indicators.city_crime = 0;
      }
      break;
    default: continue;
    }
    int end = IntMin(limit, scan->row + rows);
    // Each stage visits rows north to south and columns west to east. The influence pass also retains its
    // four-tile groups because its coarse visited writes can affect subsequent tile reads.
    for (int y = scan->row; y < end; ++y) {
      switch (scan->stage) {
      case ENV_POLLUTION_INPUT: BuildPollutionRow(city, y); break;
      case ENV_POLLUTION_SMOOTH: SmoothPollutionRow(city, y); break;
      case ENV_CENTER: AccumulateCityCenterRow(city, scan, y); break;
      case ENV_INFLUENCE: AccumulateCityInfluenceRow(city, y); break;
      case ENV_LAND_VALUE: UpdateLandValueRow(city, scan, y); break;
      case ENV_CLEAR: ClearEnvironmentRow(city, y); break;
      case ENV_FACILITIES: AccumulateFacilityRow(city, y); break;
      case ENV_DENSITY: UpdateDensityRow(city, y); break;
      case ENV_CRIME_INPUT: BuildCrimeRow(city, y); break;
      case ENV_CRIME_SMOOTH: SmoothCrimeRow(city, y); break;
      }
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
