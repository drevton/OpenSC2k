// TileInfo utility traversal state (zero-based bits):
// flags[3] (TF_VISITED) is the shared component mark, set while discovering and cleared while applying service.
// flags[7] (TF_POWERABLE) admits power traversal; flags[6] (TF_POWERED) is its resulting supply state.
// flags[5] (TF_PIPED) admits pipe traversal; flags[4] (TF_WATERED) is water supply, also stored water at towers.
// Section reset preserves a tower's stored water. Power and water traversals reuse the same mark/queue serially.
// flags[0] salinity and [1..2] permanent region are independent and must survive utility updates.
// kind and bridge axis/underpass metadata determine conductance; no connection mask is hidden in the flags.
// Supply totals/continuations are City state, not tile payload. Other bytes keep tile_storage.h/family meanings.

#include "city/engine/simulation_state.h"
#include "city/infrastructure/utilities.h"

#include "city/maps/terrain.h"
#include "city/maps/tile_queue.h"
#include "city/engine/random.h"
#include "city/infrastructure/bridges.h"
#include "city/engine/simulation.h"
#include "city/maps/layers.h"
#include "city/state.h"
#include <stdlib.h>

// The map scan and its connected component have independent stages. The scheduler retains both between batches.
typedef enum CityUtilityScanStage {
  CITY_UTILITY_SCAN_RESET, CITY_UTILITY_SCAN_SOURCES, CITY_UTILITY_SCAN_COMPONENT,
} CityUtilityScanStage;

// Preserve west/north/east/south insertion order in the shared FIFO, including when supply is limited.
static const i8 kUtilityNeighborX[4] = {-1, 0, 1, 0};
static const i8 kUtilityNeighborY[4] = {0, -1, 0, 1};

// Initialize caller-owned component traversal and seed the reusable route queue. The City map, queue and starting
// coordinate must be valid; the work record must remain alive through subsequent component steps.
static void BeginCityUtilityTraversal(City *city, CityUtilityTraversal *network, int x, int y) {
  *network = (CityUtilityTraversal){.source_x = x, .source_y = y, .stage = CITY_UTILITY_MEASURE};
  ResetCityTileQueueAndPush(city, x, y);
}

// Queue eligible neighbors of the current power-network cell. Requires a valid tile and initialized traversal;
// neighbor admission and duplicate suppression belong to the existing queue path.
static void PushPowerNeighbors(City *city, TileIndex cell, int x, int y, int visited) {
  u32 width = city->map_width, height = city->map_height;
  const TileInfo *tp = &city->tile[cell];
  int bridge_axis = -1;
  if (IsTileKindBridge(tp->kind)) {
    if (ReadCityBridgeUnderpass(tp) != CITY_BRIDGE_UNDERPASS_POWER) return;
    bridge_axis = ReadCityBridgeAxis(tp);
  }
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    if ((neighbor & 1) == bridge_axis) continue;
    u32 next_x = x + kUtilityNeighborX[neighbor], next_y = y + kUtilityNeighborY[neighbor];
    if (next_x >= width || next_y >= height) continue;
    TileIndex next = next_y * width + next_x;
    const TileInfo *np = &city->tile[next];
    if (IsTileKindBridge(np->kind) &&
        (ReadCityBridgeUnderpass(np) != CITY_BRIDGE_UNDERPASS_POWER ||
            (neighbor & 1) == ReadCityBridgeAxis(np))) continue;
    if ((np->flags & TF_VISITED) == visited) PushCityTileCoordinate(city, next_x, next_y);
  }
}

// Queue eligible neighbors of the current water-network cell. Requires a valid tile and initialized traversal; this
// shares the route queue and must not overlap another queue owner.
static void PushWaterNeighbors(City *city, int x, int y, int visited) {
  u32 width = city->map_width, height = city->map_height;
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    u32 next_x = x + kUtilityNeighborX[neighbor], next_y = y + kUtilityNeighborY[neighbor];
    if (next_x >= width || next_y >= height) continue;
    const TileInfo *np = &city->tile[next_y * width + next_x];
    if ((np->flags & TF_VISITED) == visited) PushCityTileCoordinate(city, next_x, next_y);
  }
}

// Count matching water tiles in the source's three-by-three area, including the source itself.
// The source must be valid; the scan clips at map edges and leaves network marks unchanged.
static u32 CountWaterSourceTiles(const City *city, TileIndex source, CityWaterKind required) {
  CityPoint point = MapTilePt(city, source);
  u32 count = 0;
  int left = IntMax(0, point.x - 1), right = IntMin(city->map_width, point.x + 2);
  int top = IntMax(0, point.y - 1), bottom = IntMin(city->map_height, point.y + 2);
  TileIndex row = top * city->map_width + left;
  for (int y = top; y < bottom; ++y, row += city->map_width) {
    TileIndex end = row + right - left;
    for (TileIndex tile = row; tile < end; ++tile) {
      if (ReadTileWaterKind(&city->tile[tile]) == required)
        ++count;
    }
  }
  return count;
}

// Supply uses simulation units; tile-information callers convert these to gallons per month.
int CityWaterPumpSupply(const City *city, TileIndex tile) {
  if (!(city->tile[tile].flags & TF_POWERED)) return 0;
  return 5 * city->geography.edit_height + (city->weather.rainfall >> 1) +
      10 * CountWaterSourceTiles(city, tile, CITY_WATER_FRESH);
}

// Count a connected power network, then assign its available supply. Uses the supplied budget to bound incremental
// work; false retains its cursor for the next call, true completes the pass.
static bool StepPowerNetwork(City *city, CityUtilityTraversal *network, int *budget) {
  const int width = city->map_width;
  TileInfo *tiles = city->tile;
  if (network->stage == CITY_UTILITY_MEASURE) {
    while (*budget > 0 && city->route_queue_cursor.read != city->route_queue_cursor.write) {
      --*budget;
      PointU16 point = DequeueCityTileCoordinate(city);
      int y = point.y, x = point.x;
      TileIndex cell = y * width + x;
      TileInfo *tp = &tiles[cell];
      if (tp->flags & TF_VISITED) continue;
      if (!(tp->flags & TF_POWERABLE)) continue;
      switch (tp->kind) {
      case TK_HYDRO_POWER:
        network->available += 40;
        break;
      case TK_WIND_POWER: {
        u32 random = Sc2kCrtRand(city);
        u32 divisor = (city->weather.wind >> 3) + 1;
        int altitude = ReadCityLandHeight(tp);
        network->available += (random % divisor + altitude) / 2;
        break;
      }
      case TK_GAS_POWER_PLANT:
        network->available += 11;
        break;
      case TK_OIL_POWER_PLANT:
        network->available += 48;
        break;
      case TK_NUCLEAR_POWER_PLANT:
        network->available += 111;
        break;
      case TK_SOLAR_POWER_PLANT: {
        u32 random = Sc2kCrtRand(city);
        u32 divisor = (100 - city->weather.rainfall) / 10;
        if (divisor == 0) divisor = 1;
        network->available += random % divisor + 5;
        break;
      }
      case TK_MICROWAVE_POWER_PLANT:
        network->available += 355;
        break;
      case TK_FUSION_POWER_PLANT:
        network->available += 555;
        break;
      case TK_COAL_POWER_PLANT:
        network->available += 44;
        break;
      default:
        if (IsTileKindBuilding(tp->kind)) ++network->demand;
        break;
      }
      tp->flags |= TF_VISITED;
      PushPowerNeighbors(city, cell, x, y, 0);
    }
    if (city->route_queue_cursor.read != city->route_queue_cursor.write) return false;
    city->infrastructure.power_total += network->available;
    if (city->unlocks.ordinance_flags & CITY_POLICY_FLAG_ENERGY_CONSERVATION)
      network->available += network->available / 12;
    if (network->demand > network->available) network->demand = network->available;
    city->infrastructure.power_percent += network->demand;
    ResetCityTileQueueAndPush(city, network->source_x, network->source_y);
    network->stage = CITY_UTILITY_DISTRIBUTE;
  }
  while (*budget > 0 && city->route_queue_cursor.read != city->route_queue_cursor.write) {
    --*budget;
    PointU16 point = DequeueCityTileCoordinate(city);
    int y = point.y, x = point.x;
    TileIndex cell = y * width + x;
    TileInfo *tp = &tiles[cell];
    if (!(tp->flags & TF_VISITED)) continue;
    if (network->available != 0) {
      if (IsTileKindBuilding(tp->kind)) --network->available;
      tp->flags |= TF_POWERED;
    }
    tp->flags &= ~TF_VISITED;
    PushPowerNeighbors(city, cell, x, y, TF_VISITED);
  }
  if (city->route_queue_cursor.read != city->route_queue_cursor.write) return false;
  network->stage = CITY_UTILITY_COMPLETE;
  return true;
}

// Count water supply and storage, then distribute water through the component. Uses the supplied budget to bound
// incremental work; false retains its cursor for the next call, true completes the pass.
static bool StepCityWaterComponent(City *city, CityUtilityTraversal *network, int *budget) {
  const int width = city->map_width;
  TileInfo *tiles = city->tile;
  if (network->stage == CITY_UTILITY_MEASURE) {
    while (*budget > 0 && city->route_queue_cursor.read != city->route_queue_cursor.write) {
      --*budget;
      PointU16 point = DequeueCityTileCoordinate(city);
      int y = point.y, x = point.x;
      TileIndex cell = y * width + x;
      TileInfo *tp = &tiles[cell];
      if (tp->flags & TF_VISITED) continue;
      if (!(tp->flags & TF_PIPED)) continue;
      if (IsTileKindBuilding(tp->kind)) {
        switch (tp->kind) {
        case TK_WATER_PUMP:
          network->available += CityWaterPumpSupply(city, cell);
          break;
        case TK_WATER_TOWER:
          network->transit += 100;
          if (tp->flags & TF_WATERED) network->available += 100;
          tp->flags &= ~TF_WATERED;
          break;
        case TK_WATER_TREATMENT_PLANT:
          break;
        case TK_DESALINATION_PLANT:
          if (tp->flags & TF_POWERED)
            network->available += 20 * CountWaterSourceTiles(city, cell, CITY_WATER_SALT);
          break;
        default:
          ++network->demand;
          break;
        }
      }
      tp->flags |= TF_VISITED;
      PushWaterNeighbors(city, x, y, 0);
    }
    if (city->route_queue_cursor.read != city->route_queue_cursor.write) return false;
    if (network->demand > network->available) network->demand = network->available;
    if (network->transit > network->available - network->demand)
      network->transit = network->available - network->demand;
    network->transit = (network->transit + 50) / 100;
    city->infrastructure.water_percent += network->demand;
    city->infrastructure.water_total += network->available;
    ResetCityTileQueueAndPush(city, network->source_x, network->source_y);
    network->stage = CITY_UTILITY_DISTRIBUTE;
  }
  while (*budget > 0 && city->route_queue_cursor.read != city->route_queue_cursor.write) {
    --*budget;
    PointU16 point = DequeueCityTileCoordinate(city);
    int y = point.y, x = point.x;
    TileIndex cell = y * width + x;
    TileInfo *tp = &tiles[cell];
    if (!(tp->flags & TF_VISITED)) continue;
    switch (tp->kind) {
    case TK_WATER_PUMP:
    case TK_WATER_TREATMENT_PLANT:
    case TK_DESALINATION_PLANT:
      if (tp->flags & TF_POWERED) tp->flags |= TF_WATERED;
      break;
    case TK_WATER_TOWER:
      if ((tp->flags & TF_POWERED) && network->transit != 0) {
        tp->flags |= TF_WATERED;
        --network->transit;
      }
      break;
    default:
      if (network->demand != 0) {
        tp->flags |= TF_WATERED;
        if (IsTileKindBuilding(tp->kind)) --network->demand;
      }
      break;
    }
    tp->flags &= ~TF_VISITED;
    PushWaterNeighbors(city, x, y, TF_VISITED);
  }
  if (city->route_queue_cursor.read != city->route_queue_cursor.write) return false;
  network->stage = CITY_UTILITY_COMPLETE;
  return true;
}

// Complete one connected water component using the reusable route queue. Source
// coordinates must be valid and the caller must own the utility workspace.
static void ScanCityWaterComponent(City *city, int source_x, int source_y) {
  CityUtilityTraversal network;
  BeginCityUtilityTraversal(city, &network, source_x, source_y);
  int budget = INT32_MAX;
  StepCityWaterComponent(city, &network, &budget);
}

// Advance the supplied power-grid work state within the requested budget. Returns false while work remains and true
// after final totals; the caller retains the same work and exclusive scratch/queue use between steps.
bool StepCityPowerGrid(City *city, CitySimulationWork *work, int budget) {
  const int cells = city->map_cell_count, width = city->map_width;
  TileInfo *tiles = city->tile;
  if (work->stage == CITY_UTILITY_SCAN_RESET) {
    if (!work->cursor) {
      InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
      city->infrastructure.power_total = city->infrastructure.power_percent = 0;
    }
    const int sections = cells / CITY_MAP_SECTION_CELL_COUNT;
    while (budget > 0 && work->cursor < sections) {
      ClearCityUtilitySection(city, work->cursor++, false);
      budget -= CITY_MAP_SECTION_CELL_COUNT;
    }
    if (work->cursor != sections) return false;
    work->stage = CITY_UTILITY_SCAN_SOURCES;
    work->cursor = 0;
  }
  while (budget > 0) {
    if (work->stage == CITY_UTILITY_SCAN_COMPONENT) {
      if (!StepPowerNetwork(city, &work->network, &budget)) return false;
      work->stage = CITY_UTILITY_SCAN_SOURCES;
    } else if (work->cursor < cells) {
      TileIndex cell = work->cursor++;
      --budget;
      const TileInfo *tp = &tiles[cell];
      if (IsTileKindPowerPlant(tp->kind) && !(tp->flags & TF_POWERED)) {
        BeginCityUtilityTraversal(city, &work->network, cell % width, cell / width);
        work->stage = CITY_UTILITY_SCAN_COMPONENT;
      }
    } else {
      u32 total = city->infrastructure.power_total;
      u32 percent = total ? 100 * city->infrastructure.power_percent / total : 100;
      city->infrastructure.power_percent = IntMin(percent, 100);
      return true;
    }
  }
  return false;
}

// Phase 1 power-grid scan. Requires bound utility/scratch layers and exclusive queue use; completes the whole phase
// rather than leaving incremental work pending.
void ScanPowerGridPhase(City *city) {
  CitySimulationWork work = {0};
  while (!StepCityPowerGrid(city, &work, INT32_MAX)) {}
}

// Compare two u32 source-order keys for sorting in ascending traversal order. Both arguments must point to readable
// keys; returns negative, zero or positive without subtracting unsigned values.
static int CompareWaterSourceOrder(const void *left, const void *right) {
  u32 a = *(const u32 *)left, b = *(const u32 *)right;
  return (a > b) - (a < b);
}

// Decode a linear traversal position into coordinates for the selected map rotation. Position must fit the map and
// direction must be a supported rotation; no map or cursor state changes.
static CityPoint WaterSourceAtPosition(const City *city, int position, int direction) {
  int width = city->map_width, height = city->map_height;
  int x = direction & 1 ? position % width : position / height;
  int y = direction & 1 ? position / width : position % height;
  if (direction == CITY_MAP_ROTATION_90 || direction == CITY_MAP_ROTATION_180) x = width - 1 - x;
  if (direction == CITY_MAP_ROTATION_180 || direction == CITY_MAP_ROTATION_270) y = height - 1 - y;
  return (CityPoint){x, y};
}

// Convert the completed water scan totals to percentage coverage and treatment capacity. Call only after
// source/component traversal; zero demand produces full percentage coverage.
static void FinishWaterNetwork(City *city) {
  u32 total = city->infrastructure.water_total;
  int statistic = city->statistics.count[TK_WATER_TREATMENT_PLANT];
  city->infrastructure.capacity = (u32)(2000 * (statistic / 4)) >= city->infrastructure.water_percent;
  city->infrastructure.water_percent = total ? 100 * city->infrastructure.water_percent / total : 100;
}

// Advance the section clearing, ordered source scan and connected-component water distribution. Budget controls
// incremental progress and may be consumed by a whole section; false retains the City's cursor and true finalizes
// coverage.
bool StepCityWaterNetwork(City *city, CitySimulationWork *work, int budget) {
  const int cells = city->map_cell_count, width = city->map_width;
  if (work->stage == CITY_UTILITY_SCAN_RESET) {
    if (!work->cursor) {
      InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
      InvalidateMapLayer(city, CITY_MAP_CHANGE_POLICE_COVERAGE);
      city->infrastructure.water_percent = city->infrastructure.water_total = 0;
    }
    const int sections = cells / CITY_MAP_SECTION_CELL_COUNT;
    while (budget > 0 && work->cursor < sections) {
      ClearCityUtilitySection(city, work->cursor++, true);
      budget -= CITY_MAP_SECTION_CELL_COUNT;
    }
    if (work->cursor != sections) return false;
    work->stage = CITY_UTILITY_SCAN_SOURCES;
    work->cursor = 0;
  }
  int direction = city->geography.rotation;
  while (budget > 0) {
    if (work->stage == CITY_UTILITY_SCAN_COMPONENT) {
      if (!StepCityWaterComponent(city, &work->network, &budget)) return false;
      work->stage = CITY_UTILITY_SCAN_SOURCES;
    } else if (work->cursor < cells && direction < CITY_MAP_ROTATION_COUNT) {
      int position = work->cursor++;
      --budget;
      CityPoint point = WaterSourceAtPosition(city, position, direction);
      int x = point.x, y = point.y;
      TileIndex cell = y * width + x;
      const TileInfo *tp = &city->tile[cell];
      int kind = tp->kind;
      if ((kind == TK_WATER_PUMP || kind == TK_DESALINATION_PLANT) &&
          (tp->flags & TF_POWERED) && !(tp->flags & TF_WATERED)) {
        BeginCityUtilityTraversal(city, &work->network, x, y);
        work->stage = CITY_UTILITY_SCAN_COMPONENT;
      }
    } else {
      FinishWaterNetwork(city);
      return true;
    }
  }
  return false;
}

// Use existing bounded workspace to visit sparse water sources in stock traversal order. Returns false when that
// workspace cannot represent the scan, allowing the ordinary traversal path; it does not allocate another map plane.
static bool ScanWaterSourcesInOrder(City *city, int direction) {
  if (!city->water_source_order) return false;
  // Bounded source-order workspace is disjoint from the route FIFO. Find
  // sparse sources contiguously, then retain the stored simulation order.
  u32 *sources = city->water_source_order;
  int count = 0, capacity = IntMin(city->map_cell_count / 2, CITY_WATER_SOURCE_SORT_LIMIT);
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    const TileInfo *tp = &city->tile[tile];
    if (tp->kind != TK_WATER_PUMP && tp->kind != TK_DESALINATION_PLANT) continue;
    if (!(tp->flags & TF_POWERED)) continue;
    // An unusually source-dense map uses the original scan without allocation.
    if (count == capacity) return false;
    CityPoint tile_point = MapTilePt(city, tile);
    if (direction == CITY_MAP_ROTATION_90 || direction == CITY_MAP_ROTATION_180)
      tile_point.x = city->map_width - 1 - tile_point.x;
    if (direction == CITY_MAP_ROTATION_180 || direction == CITY_MAP_ROTATION_270)
      tile_point.y = city->map_height - 1 - tile_point.y;
    sources[count++] = direction & 1 ? tile_point.y * city->map_width + tile_point.x :
        tile_point.x * city->map_height + tile_point.y;
  }
  if (count > 1) qsort(sources, count, sizeof(*sources), CompareWaterSourceOrder);
  for (int i = 0; i < count; ++i) {
    CityPoint point = WaterSourceAtPosition(city, sources[i], direction);
    int x = point.x, y = point.y;
    if (!(city->tile[y * city->map_width + x].flags & TF_WATERED))
      ScanCityWaterComponent(city, x, y);
  }
  return true;
}

// Visit water sources in the established map order, distribute supply and store final coverage metrics.
// Requires initialized water layers and exclusive route workspace; component work remains local to this pass.
void ScanCityWaterNetwork(City *city) {
  InvalidateMapLayer(city, CITY_MAP_CHANGE_FLAGS);
  InvalidateMapLayer(city, CITY_MAP_CHANGE_POLICE_COVERAGE);
  city->infrastructure.water_percent = 0;
  city->infrastructure.water_total = 0;
  for (int section = 0; section < city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT; ++section)
    ClearCityUtilitySection(city, section, true);
  // Keep the stock source order: limited water is assigned in traversal order.
  int direction = city->geography.rotation;
  if (direction < CITY_MAP_ROTATION_COUNT && !ScanWaterSourcesInOrder(city, direction)) {
    for (int position = 0; position < city->map_cell_count; ++position) {
      CityPoint point = WaterSourceAtPosition(city, position, direction);
      int x = point.x, y = point.y;
      const TileInfo *tp = &city->tile[y * city->map_width + x];
      int kind = tp->kind;
      if ((kind == TK_WATER_PUMP || kind == TK_DESALINATION_PLANT) &&
          (tp->flags & TF_POWERED) &&
          !(tp->flags & TF_WATERED))
        ScanCityWaterComponent(city, x, y);
    }
  }
  FinishWaterNetwork(city);
}
