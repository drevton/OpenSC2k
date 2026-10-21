#include "city/maps/buildings.h"
#include "city/infrastructure/surface_lines.h"
#include "city/infrastructure/power_lines.h"
#include "city/military.h"
#include "city/engine/command_types.h"
#include "city/engine/simulation.h"
#include "city/events/news.h"
#include "city/growth/growth.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/tools/tools.h"
#include <assert.h>

// Store the diagnostic reply consumed by the military reward offer. The comparison caller supplies the original answer
// value; this does not open or dismiss presentation.
void Sc2kQueueUnlockBonusAnswer(City *city, i32 answer) { city->unlock_bonus_answer = answer; }

// Native equivalents of sc2kfix military restoration. Bounded searches use City RNG;
// presentation rotation and host rand() cannot alter the selected military site.
enum { MILITARY_SITE_ATTEMPTS = 10, MILITARY_PLOT_ATTEMPTS = 24, MILITARY_SILO_COUNT = 6 };

// Check whether a valid map tile is flat, dry, unzoned and free of buildings and underground construction. This is
// read-only; callers must validate the tile index before the probe.
static bool MilitaryLandAvailable(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  return IsFlatDryUnbuiltTile(tp) &&
      !ReadCityZoneKind(tp) && !ReadUnderground(city, tile);
}

// Convert one valid selected tile to military zoning and update its statistics and damage. The caller has already
// admitted the site; existing utility flags and ground state are reset for the category change.
static void ZoneMilitaryTile(City *city, TileIndex tile) {
  InvalidateTile(city, tile);
  TileInfo *tp = &city->tile[tile];
  --city->statistics.count[tp->kind];
  WriteTileZoneByte(tp, (ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK) | ZONE_MILITARY);
  tp->flags &= ~(TF_WATERED | TF_PIPED | TF_POWERED | TF_POWERABLE);
  RefreshCityPowerConnections(city, tile);
  ++city->special_tile_counts.count[CITY_SPECIAL_TILE_OTHER];
}

// Zone the inclusive straight strip between two valid map tiles. Endpoints must describe the selected base strip; each
// tile receives the same military conversion and invalidation.
static void FormArmyBaseStrip(City *city, TileIndex first, TileIndex last) {
  assert(IsMapTileValid(city, first) && IsMapTileValid(city, last));
  CityPoint point = MapTilePt(city, first), end = MapTilePt(city, last);
  assert(point.x == end.x || point.y == end.y);
  int dx = end.x > point.x ? 1 : end.x < point.x ? -1 : 0;
  int dy = end.y > point.y ? 1 : end.y < point.y ? -1 : 0;
  int stride = dy * city->map_width + dx;
  for (TileIndex tile = first;; tile += stride) {
    const TileInfo *tp = &city->tile[tile];
    if (ReadCityZoneKind(tp) == ZONE_MILITARY &&
        IsTileKindUnbuilt(tp->kind) && tp->kind != TK_RADIOACTIVE_WASTE &&
        !HasTileWater(tp))
      PlaceCityRoadCell(city, tile);
    if (tile == last) break;
  }
  const TileIndex endpoints[] = {first, last};
  for (int i = 0; i < 2; ++i) {
    TileIndex tile = endpoints[i];
    const TileInfo *tp = &city->tile[tile];
    if (ReadCityZoneKind(tp) == ZONE_MILITARY &&
        ReadTerrainCode(tp) == CITY_TERRAIN_FLAT &&
        ReadCityStraightLineAxis(city, tile, TK_ROAD) >= 0) {
      ReplaceCityTileAndStatistics(city, tile, TK_RUNWAY_LAST);
      InitializeCityBuildingFootprint(city, tile, 1, 1, city->geography.rotation);
    }
  }
}

// Attempt a randomized naval site only when ocean geography permits it. Returns the selected notice or zero if no yard
// was placed; failed searches may still consume City RNG.
static int PlaceMilitaryNavalYard(City *city) {
  if (!city->geography.ocean || !(Sc2kCrtRand(city) & 1)) return 0;
  static const int kDirectionX[] = {1, 0, -1, 0}, kDirectionY[] = {0, 1, 0, -1};
  for (int attempt = 0; attempt < 256; ++attempt) {
    int x = NextEventOrientationRandom(city, city->map_width);
    int y = NextEventOrientationRandom(city, city->map_height);
    int direction = Sc2kCrtRand(city) & 3;
    int dx = kDirectionX[direction], dy = kDirectionY[direction];
    int tx = -dy, ty = dx;
    int end_x = x + 14 * tx + 3 * dx, end_y = y + 14 * ty + 3 * dy;
    if (!IsMapCoordinateValid(city, end_x, end_y) || !IsMapCoordinateValid(city, x - dx, y - dy)) continue;
    int level = ReadCityLandHeight(&city->tile[MapTileXY(city, x, y)]);
    bool valid = true;
    for (int along = 0; along < 15 && valid; ++along) {
      int shore_x = x + along * tx, shore_y = y + along * ty;
      TileIndex sea = MapTileXYOrInvalid(city, shore_x - dx, shore_y - dy);
      if (sea == INVALID_TILE) { valid = false; break; }
      if (ReadTileWaterKind(&city->tile[sea]) != CITY_WATER_SALT) { valid = false; break; }
      for (int inland = 0; inland < 4; ++inland) {
        TileIndex tile = MapTileXY(city, shore_x + inland * dx, shore_y + inland * dy);
        if (!MilitaryLandAvailable(city, tile) ||
            ReadCityLandHeight(&city->tile[tile]) != level) { valid = false; break; }
      }
    }
    if (!valid) continue;
    city->unlocks.military_base = CITY_MILITARY_BASE_NAVY;
    for (int along = 0; along < 15; ++along)
      for (int inland = 0; inland < 4; ++inland)
        ZoneMilitaryTile(city, MapTileXY(city, x + along * tx + inland * dx, y + along * ty + inland * dy));
    CenterCityMapOnEvent(city, x + 7 * tx, y + 7 * ty);
    return CITY_NOTICE_MILITARY_BASE_NAVY;
  }
  return 0;
}

// Search for the military silo sites using deterministic randomized attempts. Returns the placement outcome; search
// and placement are gameplay effects and must not be used as a preview.
static int PlaceMilitarySiloPlots(City *city) {
  CityPoint sites[MILITARY_SILO_COUNT];
  for (int site = 0; site < MILITARY_SILO_COUNT; ++site) {
    bool found = false;
    for (int attempt = 0; attempt < 640 && !found; ++attempt) {
      int x = NextEventOrientationRandom(city, city->map_width - 3);
      int y = NextEventOrientationRandom(city, city->map_height - 3);
      int elevation = ReadCityLandHeight(&city->tile[MapTileXY(city, x, y)]);
      if (elevation <= 5) continue;
      bool valid = true;
      for (int i = 0; i < site; ++i)
        if (x < sites[i].x + 4 && x + 4 > sites[i].x && y < sites[i].y + 4 && y + 4 > sites[i].y)
          valid = false;
      for (int dy = 0; dy < 3 && valid; ++dy)
        for (int dx = 0; dx < 3; ++dx) {
          TileIndex tile = MapTileXY(city, x + dx, y + dy);
          if (!MilitaryLandAvailable(city, tile) ||
              ReadCityLandHeight(&city->tile[tile]) != elevation) { valid = false; break; }
        }
      if (valid) { sites[site] = (CityPoint){x, y}; found = true; }
    }
    if (!found) return 0;
  }
  city->unlocks.military_base = CITY_MILITARY_BASE_MISSILE_SILOS;
  for (int site = 0; site < MILITARY_SILO_COUNT; ++site)
    for (int dy = 0; dy < 3; ++dy)
      for (int dx = 0; dx < 3; ++dx)
        ZoneMilitaryTile(city, MapTileXY(city, sites[site].x + dx, sites[site].y + dy));
  CenterCityMapOnEvent(city, sites[5].x, sites[5].y);
  return CITY_NOTICE_MILITARY_BASE_MISSILE_SILOS;
}

// Search the stock sequence of military base candidates and select its result notice. Searches consume deterministic
// RNG; callers retain the resulting continuation rather than retrying after a dialog.
static int PlaceRestoredMilitaryBase(City *city) {
  for (int retry = 0; retry < MILITARY_SITE_ATTEMPTS; ++retry) {
    int notice = PlaceMilitaryNavalYard(city);
    if (notice) return notice;
    int chosen_x = 0, chosen_y = 0, available = 0, level_tiles = 0;
    for (int attempt = 0; attempt < MILITARY_PLOT_ATTEMPTS; ++attempt) {
      chosen_x = NextEventOrientationRandom(city, city->map_width - 8);
      chosen_y = NextEventOrientationRandom(city, city->map_height - 8);
      int level = ReadCityLandHeight(&city->tile[MapTileXY(city, chosen_x, chosen_y)]);
      available = level_tiles = 0;
      for (int dy = 0; dy < 8; ++dy)
        for (int dx = 0; dx < 8; ++dx) {
          TileIndex tile = MapTileXY(city, chosen_x + dx, chosen_y + dy);
          if (MilitaryLandAvailable(city, tile)) {
            ++available;
            if (ReadCityLandHeight(&city->tile[tile]) == level) ++level_tiles;
          }
        }
      if (available >= 40) break;
    }
    if (available < 40) {
      notice = PlaceMilitarySiloPlots(city);
      if (notice) return notice;
      continue;
    }
    bool army = available != level_tiles;
    if (!army && level_tiles < 40) continue;
    city->unlocks.military_base = army ? CITY_MILITARY_BASE_ARMY : CITY_MILITARY_BASE_AIR_FORCE;
    for (int dy = 0; dy < 8; ++dy)
      for (int dx = 0; dx < 8; ++dx) {
        TileIndex tile = MapTileXY(city, chosen_x + dx, chosen_y + dy);
        if (MilitaryLandAvailable(city, tile)) ZoneMilitaryTile(city, tile);
      }
    if (army) {
      for (int offset = 2; offset <= 5; offset += 3) {
        FormArmyBaseStrip(city, MapTileXY(city, chosen_x + offset, chosen_y),
            MapTileXY(city, chosen_x + offset, chosen_y + 7));
        FormArmyBaseStrip(city, MapTileXY(city, chosen_x, chosen_y + offset),
            MapTileXY(city, chosen_x + 7, chosen_y + offset));
      }
    }
    CenterCityMapOnEvent(city, chosen_x + 4, chosen_y + 4);
    return army ? CITY_NOTICE_MILITARY_BASE_ARMY : CITY_NOTICE_MILITARY_BASE_AIR_FORCE;
  }
  return CITY_NOTICE_MILITARY_BASE_UNSUITABLE;
}

// Offer the population-unlock bonus and place the selected sites. Uses an explicit retained answer or observation
// boundary; unsupported/unanswered interaction must not fabricate an accepted military offer.
void OfferDisasterUnlockBonus(City *city) {
  if (city->unlock_bonus_answer == -1) {
    PresentCityDisasterUnlockOffer(city);
    if (city->unlock_bonus_answer == -1) return;
  }
  int answer = city->unlock_bonus_answer;
  city->unlock_bonus_answer = -1;
  CityMilitaryPlacement placement = BeginCityMilitaryPlacement(city, answer != CITY_DIALOG_NO);
  if (placement.notice) ShowCityMessage(city, placement.notice);
  FinishCityMilitaryPlacement(city, placement);
}

// Searches and pre-notice effects run once, with the explicit offer decision. Accept is an already resolved decision;
// returned placement captures only the deferred tail and any result notice must precede that tail.
CityMilitaryPlacement BeginCityMilitaryPlacement(City *city, bool accept) {
  if (!accept) {
    city->unlocks.military_base = CITY_MILITARY_BASE_UNAVAILABLE;
    return (CityMilitaryPlacement){0};
  }
  if (city->fixes & (1 << CITY_FIX_MILITARY_PLACEMENT)) {
    int notice = PlaceRestoredMilitaryBase(city);
    return (CityMilitaryPlacement){.notice = notice,
        .after_notice = notice == CITY_NOTICE_MILITARY_BASE_UNSUITABLE ? CITY_MILITARY_FINISH_UNAVAILABLE :
            CITY_MILITARY_FINISH_DONE};
  }
  int chosen_x = 0;
  int chosen_y = 0;
  int open_cells = 0;
  int same_elevation_cells = 0;
  int elevation = 0;
  for (int attempt = 0; attempt < 24; ++attempt) {
    chosen_y = NextEventOrientationRandom(city, 119);
    chosen_x = NextEventOrientationRandom(city, 119);
    elevation = ReadCityLandHeight(&city->tile[MapTileXY(city, chosen_x, chosen_y)]);
    open_cells = 0;
    same_elevation_cells = 0;
    for (int y = chosen_y; y < chosen_y + 8; ++y)
      for (int x = chosen_x; x < chosen_x + 8; ++x) {
        TileIndex tile = MapTileXY(city, x, y);
        const TileInfo *tp = &city->tile[tile];
        if (IsFlatDryUnbuiltTile(tp)) {
          ++open_cells;
          if (ReadCityLandHeight(tp) == elevation) ++same_elevation_cells;
        }
      }
    if (open_cells >= 40)
      break;
  }
  if (same_elevation_cells < 40) {
    if (open_cells < 40) {
      CityPoint selected_sites[6];
      int selected_count = 0;
      for (int attempt = 0; attempt < 40 && selected_count < 6; ++attempt) {
        int y0 = NextEventOrientationRandom(city, city->map_height - 4);
        int x0 = NextEventOrientationRandom(city, city->map_width - 4);
        bool repaired_silos = city->fixes & (1 << CITY_FIX_SILO_PLOTS);
        int site_elevation = repaired_silos ?
            ReadCityLandHeight(&city->tile[MapTileXY(city, x0, y0)]) : elevation;
        bool overlaps = false;
        if (repaired_silos)
          for (int site = 0; site < selected_count; ++site)
            if (x0 < selected_sites[site].x + 3 && x0 + 3 > selected_sites[site].x &&
                y0 < selected_sites[site].y + 3 && y0 + 3 > selected_sites[site].y)
              overlaps = true;
        if (overlaps) continue;
        int available = 0;
        TileIndex site = MapTileXY(city, x0, y0);
        for (int y = y0; y < y0 + 3; ++y)
          for (int x = x0; x < x0 + 3; ++x) {
            TileIndex tile = MapTileXY(city, x, y);
            const TileInfo *tp = &city->tile[tile];
            if (IsFlatDryUnbuiltTile(tp) &&
                ReadCityLandHeight(tp) == site_elevation &&
                ReadCityZoneKind(tp) != ZONE_MILITARY &&
                ReadUnderground(city, repaired_silos ? tile : site) == TUK_CLEAR)
              ++available;
          }
        if (available == 9) {
          selected_sites[selected_count++] = (CityPoint){x0, y0};
        }
      }
      if (selected_count == 6) {
        city->unlocks.military_base = CITY_MILITARY_BASE_MISSILE_SILOS;
        for (int site = 0; site < 6; ++site)
          for (int y = selected_sites[site].y; y < selected_sites[site].y + 3; ++y)
            for (int x = selected_sites[site].x; x < selected_sites[site].x + 3; ++x) {
              const TileIndex cell_index = MapTileXY(city, x, y);
              TileInfo *tp = &city->tile[cell_index];
              --city->statistics.count[tp->kind];
              WriteTileZoneByte(tp, (ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK) |
                  ZONE_MILITARY);
              ++city->special_tile_counts.count[CITY_SPECIAL_TILE_OTHER];
            }
        CenterCityMapOnEvent(city, selected_sites[5].x, selected_sites[5].y);
        return (CityMilitaryPlacement){.notice = CITY_NOTICE_MILITARY_BASE_MISSILE_SILOS};
      }
    }
    return (CityMilitaryPlacement){.notice = CITY_NOTICE_MILITARY_BASE_UNSUITABLE,
        .after_notice = CITY_MILITARY_FINISH_UNAVAILABLE};
  }
  city->unlocks.military_base =
      open_cells == same_elevation_cells ? CITY_MILITARY_BASE_AIR_FORCE : CITY_MILITARY_BASE_ARMY;
  return (CityMilitaryPlacement){.site = MapTileXY(city, chosen_x, chosen_y),
      .notice = city->unlocks.military_base == CITY_MILITARY_BASE_AIR_FORCE ?
          CITY_NOTICE_MILITARY_BASE_AIR_FORCE : CITY_NOTICE_MILITARY_BASE_ARMY,
      .after_notice = CITY_MILITARY_FINISH_ZONE};
}

// Apply only the deferred effects selected before the military result notice. The placement value belongs to that
// retained caller; live City state is used after dismissal and the search is not restarted.
void FinishCityMilitaryPlacement(City *city, CityMilitaryPlacement placement) {
  if (placement.after_notice == CITY_MILITARY_FINISH_UNAVAILABLE) {
    city->unlocks.military_base = CITY_MILITARY_BASE_UNAVAILABLE;
    return;
  }
  if (placement.after_notice != CITY_MILITARY_FINISH_ZONE) return;
  CityPoint site = MapTilePt(city, placement.site);
  for (int row = 0; row < 8; ++row)
    for (TileIndex cell_index = placement.site + row * city->map_width, end = cell_index + 8;
        cell_index < end; ++cell_index) {
      TileInfo *tp = &city->tile[cell_index];
      int tile = tp->kind;
      if (IsFlatDryUnbuiltTile(tp) &&
          ReadCityZoneKind(tp) == ZONE_NONE &&
          ReadUnderground(city, placement.site) == TUK_CLEAR) {
        --city->statistics.count[tile];
        WriteTileZoneByte(tp, (ReadTileZoneByte(tp) & CITY_ZONE_METADATA_MASK) |
            ZONE_MILITARY);
        ++city->special_tile_counts.count[CITY_SPECIAL_TILE_OTHER];
      }
    }
  CenterCityMapOnEvent(city, site.x + 4, site.y + 4);
}

// Find the silo block's origin and replace its 3x3 surface and underground footprint. The military-growth option
// validates the complete site first, while its disabled path retains bounded anchor steps.
bool GrowSpecialZoneSilos(City *city, TileIndex origin, int zone_kind) {
  assert(IsMapTileValid(city, origin));
  CityPoint point = MapTilePt(city, origin);
  if (city->fixes & (1 << CITY_FIX_MILITARY_GROWTH)) {
    while (point.y > 0 && ReadCityZoneKind(&city->tile[MapTileXY(city, point.x, point.y - 1)]) == zone_kind)
      --point.y;
    while (point.x > 0 && ReadCityZoneKind(&city->tile[MapTileXY(city, point.x - 1, point.y)]) == zone_kind)
      --point.x;
    if (point.x + 3 > city->map_width || point.y + 3 > city->map_height) return false;
    for (int dy = 0; dy < 3; ++dy)
      for (int dx = 0; dx < 3; ++dx) {
        TileIndex tile = MapTileXY(city, point.x + dx, point.y + dy);
        const TileInfo *tp = &city->tile[tile];
        int surface = tp->kind;
        if (ReadCityZoneKind(tp) != zone_kind ||
            IsTileKindTransportOrBuilding(surface) || surface == TK_PARK || surface == TK_RADIOACTIVE_WASTE ||
            ReadTerrainCode(tp) || ReadUnderground(city, tile) ||
            HasTileWater(tp))
          return false;
      }
  } else {
    if (ReadCityZoneKind(&city->tile[MapTileXY(city, point.x, point.y - 1)]) == zone_kind)
      --point.y;
    if (ReadCityZoneKind(&city->tile[MapTileXY(city, point.x, point.y - 1)]) == zone_kind)
      --point.y;
    if (ReadCityZoneKind(&city->tile[MapTileXY(city, point.x - 1, point.y)]) == zone_kind)
      --point.x;
    if (ReadCityZoneKind(&city->tile[MapTileXY(city, point.x - 1, point.y)]) == zone_kind)
      --point.x;
  }
  TileIndex anchor = MapTileXY(city, point.x, point.y);
  int facing = ReadBuildingOrientation(city, anchor);
  for (int cell_y = point.y; cell_y < point.y + 3; ++cell_y)
    for (int cell_x = point.x; cell_x < point.x + 3; ++cell_x) {
      TileIndex tile = MapTileXY(city, cell_x, cell_y);
      ReplaceCityTileAndStatistics(city, tile, TK_MISSILE_SILO);
      SetUndergroundTile(city, tile, TUK_MISSILE_SILO);
      if (city->fixes & (1 << CITY_FIX_MILITARY_GROWTH)) {
        city->tile[tile].flags &= ~(TF_WATERED | TF_PIPED | TF_POWERED | TF_POWERABLE);
        RefreshCityPowerConnections(city, tile);
      }
    }
  InitializeCityBuildingFootprint(city, anchor, 3, 3, facing);
  return true;
}

// Advance one scheduled military-zone tile using the base's counters and City RNG.
// The scheduler admits the tile; each call attempts only that base's next growth action.
void GrowCityMilitaryBaseTile(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  assert(ReadCityZoneKind(&city->tile[tile_index]) == ZONE_MILITARY);
  int zone = ZONE_MILITARY, tile = ReadTile(city, tile_index);
  CitySpecialTileCounts *counts = &city->special_tile_counts;
  int replacement;
  if (city->unlocks.military_base == CITY_MILITARY_BASE_ARMY) {
    if ((Sc2kCrtRand(city) & 3) != 0)
      return;
    if (city->fixes & (1 << CITY_FIX_MILITARY_GROWTH)) {
      int parking_lots = counts->count[CITY_SPECIAL_TILE_PARKING_LOT] / 4;
      replacement = TK_PARKING_LOT_2X2_2;
      if (counts->count[CITY_SPECIAL_TILE_TOP_SECRET] / 4 < parking_lots) {
        replacement = counts->count[CITY_SPECIAL_TILE_HANGAR_SMALL] / 8 > parking_lots
            ? TK_TOP_SECRET : TK_HANGAR_1X1;
      }
    } else {
      replacement =
          counts->count[CITY_SPECIAL_TILE_PARKING_LOT] / 4 > counts->count[CITY_SPECIAL_TILE_HANGAR_SMALL] / 12
          ? TK_HANGAR_1X1 : TK_PARKING_LOT_2X2_2;
    }
    if (!ReplaceSpecialZoneTile(city, tile_index, replacement, zone))
      ReplaceSpecialZoneTile(city, tile_index, TK_HANGAR_1X1, zone);
    return;
  }
  if (city->unlocks.military_base == CITY_MILITARY_BASE_AIR_FORCE) {
    if ((Sc2kCrtRand(city) & 3) != 0)
      return;
    int target =
        (counts->count[CITY_SPECIAL_TILE_RUNWAY_FIRST] + counts->count[CITY_SPECIAL_TILE_RUNWAY_LAST]) / 5;
    if (counts->count[CITY_SPECIAL_TILE_PARKING_LOT] / 4 >= target)
      replacement = TK_RUNWAY_FIRST;
    else if (2 * counts->count[CITY_SPECIAL_TILE_CONTROL_TOWER] < target)
      replacement = TK_CONTROL_TOWER_1X1_2;
    else if (2 * counts->count[CITY_SPECIAL_TILE_RADAR] < target)
      replacement = TK_RADAR;
    else if (counts->count[CITY_SPECIAL_TILE_F_15B] < target)
      replacement = TK_F_15B;
    else if (counts->count[CITY_SPECIAL_TILE_BUILDING_FIRST] / 2 < target)
      replacement = TK_BUILDING_1X1_1;
    else if (counts->count[CITY_SPECIAL_TILE_BUILDING_SECOND] / 2 < target)
      replacement = TK_BUILDING_1X1_2;
    else
      replacement = counts->count[CITY_SPECIAL_TILE_HANGAR_LARGE] / 4 < target ?
          TK_HANGAR_3X3 : TK_PARKING_LOT_2X2_2;
    ReplaceSpecialZoneTile(city, tile_index, replacement, zone);
    return;
  }
  if (city->unlocks.military_base == CITY_MILITARY_BASE_NAVY) {
    if ((Sc2kCrtRand(city) & 3) != 0)
      return;
    int target = counts->count[CITY_SPECIAL_TILE_CRANE];
    if (counts->count[CITY_SPECIAL_TILE_CARGO_YARD] / 4 >= target)
      replacement = TK_CRANE;
    else if (counts->count[CITY_SPECIAL_TILE_TOP_SECRET] / 4 < target)
      replacement = TK_TOP_SECRET;
    else if (counts->count[CITY_SPECIAL_TILE_WAREHOUSE] / 3 >= target)
      replacement = TK_CARGO_YARD;
    else
      replacement = TK_WAREHOUSE_1X1_4;
    if (!ReplaceSpecialZoneTile(city, tile_index, replacement, zone))
      ReplaceSpecialZoneTile(city, tile_index, TK_WAREHOUSE_1X1_4, zone);
    return;
  }
  if (city->unlocks.military_base == CITY_MILITARY_BASE_MISSILE_SILOS && tile != TK_MISSILE_SILO) {
    ReplaceSpecialZoneTile(city, tile_index, TK_MISSILE_SILO, zone);
  }
}
