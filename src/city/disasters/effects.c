#include "city/disasters/disasters.h"

#include "base/integer.h"
#include "game/timing.h"
#include "city/maps/labels.h"
#include "city/engine/random.h"
#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/maps/terrain.h"
#include "city/maps/water.h"
#include "city/maps/tables.h"
#include "game/audio.h"
#include "city/presentation.h"
#include <assert.h>

#include "game/observation.h"
#include "city/events/events.h"
#include "city/maps/map.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/saveload/file.h"
#include "game/state.h"
#include "city/maps/map_types.h"

static const u8 kDisasterDamagePriority[67] = {
  0, 1, 1, 1, 1, 1, 1, 1, 4, 8, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 3, 4, 4, 4, 2, 2, 3, 2, 4, 3, 2, 3, 2,
  8, 8, 2, 4, 8, 1, 4, 1, 1, 8, 1, 1, 3, 8, 2, 2,
  2, 1, 1, 4, 1, 1, 2, 2, 2, 1, 3, 2, 5, 3, 6, 6,
  6, 6, 6,
};

static const i8 kMicrowaveDisasterStepY[8] = {
  0, 1, 1, 1, 0, -1, -1, -1,
};

static const i8 kMicrowaveDisasterStepX[8] = {
  -1, -1, 0, 1, 1, 1, 0, -1,
};

enum CityDisasterWorkPhase {
  CITY_DISASTER_WORK_READY, CITY_DISASTER_WORK_WAIT,
  CITY_DISASTER_WORK_DAMAGE,
};

// Cancel the current resumable disaster and perform its captured cleanup. Restores temporary cash, auto-goto or label
// state for the relevant disaster and stops its audio before clearing the work record.
void CancelCityDisasterWork(City *city) {
  CityDisasterWork *work = &city->disaster_work;
  if (work->type == CITY_DISASTER_VOLCANO) {
    city->indicators.cash = work->cash_restore;
    if (city->settings.auto_goto) RestoreCityVolcanoPresentation(city, work->label_restore);
    StopAllCitySound(city->game);
    StopCityMusicSequence(city->game);
  } else if (work->type == CITY_DISASTER_MAJOR_FLOOD || work->type == CITY_DISASTER_MASS_RIOTS) {
    city->settings.auto_goto = work->update_gate;
  }
  *work = (CityDisasterWork){0};
}

// Continue the fire/firestorm spiral and its fallback target attempts using City's retained disaster work. Calls must
// preserve the same work lifetime; deterministic draws and successful strikes are not repeated after suspension.
i64 ApplyCityFireDisaster(City *city, bool fire_storm) {
  enum CityFireWorkPhase {
    CITY_FIRE_SPIRAL_MOVE = 1, CITY_FIRE_SPIRAL_STRIKE = 2,
    CITY_FIRE_RANDOM_TARGET = 3, CITY_FIRE_RANDOM_STRIKE = 4,
  };
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    *work = (CityDisasterWork){.type = fire_storm ? CITY_DISASTER_FIRE_STORM : CITY_DISASTER_FIRE,
        .run_length = 1, .remaining = 65, .phase = CITY_FIRE_SPIRAL_MOVE};
    if (fire_storm) {
      work->y = city->disasters.location.y;
      work->x = city->disasters.location.x;
    } else {
      work->y = city->geography.center_y + Sc2kCrtRandRange(city, 40) - 20;
      work->x = city->geography.center_x + Sc2kCrtRandRange(city, 40) - 20;
    }
  }
  while (work->phase < CITY_FIRE_RANDOM_TARGET && work->run_length < (fire_storm ? city->map_width : 64) &&
      work->remaining) {
    if (work->phase == CITY_FIRE_SPIRAL_MOVE) {
      work->y += kCardinalNeighborY[work->direction];
      work->x += kCardinalNeighborX[work->direction];
      TileIndex tile = MapTileXYOrInvalid(city, work->x, work->y);
      if (tile != INVALID_TILE && (fire_storm || IsTileKindBuilding(city->tile[tile].kind))) {
        work->phase = CITY_FIRE_SPIRAL_STRIKE;
      }
    }
    if (work->phase == CITY_FIRE_SPIRAL_STRIKE) {
      i64 result = ApplyDisasterCellEffect(city, MapTileXYOrInvalid(city, work->x, work->y), fire_storm);
      if (result) {
        work->applied = true;
        if (!fire_storm) break;
        --work->remaining;
        SetDisasterLocation(&city->disasters.location, work->x, work->y);
      }
    }
    work->phase = CITY_FIRE_SPIRAL_MOVE;
    if (++work->run_position >= work->run_length) {
      work->run_position = 0;
      if (work->direction & 1) ++work->run_length;
      work->direction = (work->direction + 1) & 3;
    }
  }
  if (!fire_storm && !work->applied) {
    if (work->phase < CITY_FIRE_RANDOM_TARGET) work->phase = CITY_FIRE_RANDOM_TARGET;
    while (work->attempts < 200) {
      if (work->phase == CITY_FIRE_RANDOM_TARGET) {
        work->y = NextCityMapRandomModulo(city, city->map_height);
        work->x = NextCityMapRandomModulo(city, city->map_width);
        work->phase = CITY_FIRE_RANDOM_STRIKE;
      }
      i64 result = ApplyDisasterCellEffect(city, MapTileXYOrInvalid(city, work->x, work->y), 0);
      if (result) { work->applied = true; break; }
      ++work->attempts;
      work->phase = CITY_FIRE_RANDOM_TARGET;
    }
  }
  bool applied = work->applied;
  if (applied || fire_storm) {
    if (city->settings.auto_goto && applied) CenterCityMapOnEvent(city, work->x, work->y);

    if (!fire_storm) SetDisasterLocation(&city->disasters.location, work->x, work->y);
  }
  *work = (CityDisasterWork){0};
  return applied;
}

// Search outward from the scheduled location for a dry road and start one riot marker.
// Returns whether a suitable road was found; successful placement consumes one CRT draw.
bool ApplyCityRiotDisaster(City *city) {
  int y = city->disasters.location.y;
  int x = city->disasters.location.x;
  int direction = 0;
  int run_length = 1;
  int run_position = 0;
  int origin_y = y;
  int origin_x = x;
  TileIndex tile_index;

  for (;;) {
    y += kCardinalNeighborY[direction];
    x += kCardinalNeighborX[direction];
    tile_index = MapTileXYOrInvalid(city, x, y);
    if (tile_index != INVALID_TILE) {
      const TileInfo *tp = &city->tile[tile_index];
      int tile = tp->kind;
      if (IsTileKindRoadRoute(tile) &&
          !HasTileWater(tp) && ReadOverlay(city, tile_index) == 0)
        break;
    }
    if (++run_position >= run_length) {
      run_position = 0;
      if ((direction & 1) != 0)
        ++run_length;
      direction = (direction + 1) & 3;
    }
    if (run_length >= city->map_width)
      return false;
  }

  WriteOverlay(city, tile_index, (u8)((Sc2kCrtRand(city) & 1) - 3));
  InvalidateTile(city, tile_index);
  if (city->settings.auto_goto != 0)
    CenterCityMapOnEvent(city, origin_x, origin_y);
  PlayCitySound(city, tile_index, CITY_SOUND_DEMOLITION);
  SetDisasterLocation(&city->disasters.location, x, y);
  return true;
}

// Continue the population-scaled random strikes for the selected immediate or delayed disaster. Preserves per-strike
// work across waits and restores the temporary update gate at the real completion/cancellation boundary.
i64 ApplyCityRandomDisasterStrikes(City *city, bool delayed) {
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    *work = (CityDisasterWork){.type = delayed ? CITY_DISASTER_MAJOR_FLOOD : CITY_DISASTER_MASS_RIOTS,
        .origin_y = city->disasters.location.y, .origin_x = city->disasters.location.x,
        .attempts = (i16)(city->indicators.population / 0x2710 + 5), .update_gate = city->settings.auto_goto};
    city->settings.auto_goto = 0;
  }
  if (work->phase) {
    if (!StepGameDelay(city->game, &city->game->animation_delay)) return CITY_OPERATION_PENDING;
    work->phase = CITY_DISASTER_WORK_READY;
    --work->attempts;
  }
  while (work->attempts > 0) {
    int y = (Sc2kCrtRand(city) & 0x1F) + work->origin_y - 16;
    int x = (Sc2kCrtRand(city) & 0x1F) + work->origin_x - 16;
    if (IsMapCoordinateValid(city, x, y)) {
      SetDisasterLocation(&city->disasters.location, x, y);
      if (delayed ? ApplyCityFloodDisaster(city) : ApplyCityRiotDisaster(city)) work->applied = true;
      if (delayed) {
        work->phase = CITY_DISASTER_WORK_WAIT;
        BeginGameDelay(city->game, &city->game->animation_delay, 20, true);
        return CITY_OPERATION_PENDING;
      }
    }
    --work->attempts;
  }
  city->settings.auto_goto = work->update_gate;
  if (work->applied && work->update_gate)
    CenterCityMapOnEvent(city, work->origin_x, work->origin_y);

  bool applied = work->applied;
  *work = (CityDisasterWork){0};
  return applied;
}

// Search complete map storage in row order, without RNG or callbacks. The first matching tile wins,
// including a footprint continuation; callers resolve any footprint after selecting their target.
static TileIndex FindDisasterPlant(const City *city, TileKind kind) {
  const TileInfo *tp = city->tile;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile, ++tp)
    if (tp->kind == kind) return tile;
  return INVALID_TILE;
}

// Find the first microwave power plant and damage a random eight-direction path from it.
// Calls share City disaster work; each path step consumes its direction draw once.
i64 ApplyCityMicrowaveDisaster(City *city) {
  enum CityMicrowaveWorkPhase {
    CITY_MICROWAVE_PREPARE = 0, CITY_MICROWAVE_STRIKE = 1,
  };
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    TileIndex plant = FindDisasterPlant(city, TK_MICROWAVE_POWER_PLANT);
    if (plant == INVALID_TILE) return false;
    CityPoint point = MapTilePt(city, plant);
    int x = point.x, y = point.y;

    if (city->settings.auto_goto != 0)
      CenterCityMapOnEvent(city, x, y);
    *work = (CityDisasterWork){.type = CITY_DISASTER_MICROWAVE, .x = x, .y = y,
        .remaining = 39, .direction = Sc2kCrtRand(city) & 7};
  }
  while (work->remaining > 0) {
    TileIndex tile = MapTileXYOrInvalid(city, work->x, work->y);
    if (tile == INVALID_TILE) break;
    if (work->phase != CITY_MICROWAVE_PREPARE || city->tile[tile].kind != TK_MICROWAVE_POWER_PLANT) {
      if (work->phase == CITY_MICROWAVE_PREPARE) {
        if (HasTileWater(&city->tile[tile])) {
          WriteOverlay(city, tile, CITY_OVERLAY_POLLUTION);
          InvalidateTile(city, tile);
        }
        if (work->remaining % 10 == 0 && city->settings.auto_goto)
          CenterCityMapOnEvent(city, work->x, work->y);
        work->phase = CITY_MICROWAVE_STRIKE;
      }
      ApplyDisasterCellEffect(city, tile, 1);
      PlayCitySound(city, tile, CITY_SOUND_POWER_LINES);
      work->phase = CITY_MICROWAVE_PREPARE;
    }
    --work->remaining;
    work->y += kMicrowaveDisasterStepY[work->direction];
    work->x += kMicrowaveDisasterStepX[work->direction];
    work->direction = Sc2kCrtRand(city) & 7;
  }
  *work = (CityDisasterWork){0};
  return true;
}

// Find a shoreline target and flood nearby dry tiles. If no shore is found, try random flat tiles.
// Returns whether flooding started; failed random probes still consume their draws.
bool ApplyCityFloodDisaster(City *city) {
  int origin_y = city->disasters.location.y;
  int origin_x = city->disasters.location.x;
  int target_y = 0;
  int target_x = 0;
  int target_offset_y = 0;
  int target_offset_x = 0;
  bool found = false;

  for (int radius = 0; radius < IntMax(city->map_width, city->map_height) && !found; ++radius) {
    for (int offset_y = -radius; offset_y <= radius && !found; ++offset_y) {
      int y = origin_y + offset_y;
      if (y < 0 || y >= city->map_height) continue;
      // Earlier radii already proved the interior empty. Visit this square's
      // perimeter in the same order without rescanning it on large maps.
      int step = offset_y == -radius || offset_y == radius ? 1 : 2 * radius;
      for (int offset_x = -radius; offset_x <= radius; offset_x += step) {
        int x = origin_x + offset_x;
        TileIndex tile = MapTileXYOrInvalid(city, x, y);
        if (tile == INVALID_TILE) continue;
        const TileInfo *tp = &city->tile[tile];
        // Hydro spillways can also seed a flood.
        if (tp->kind != TK_HYDRO_POWER && ReadCityWaterGroundKind(tp) != TK_WATER_BANK)
          continue;
        target_y = y;
        target_x = x;
        target_offset_y = offset_y;
        target_offset_x = offset_x;
        found = true;
        break;
      }
    }
  }

  if (!found) {
    for (int attempts = 0; attempts < 200; ++attempts) {
      int y = NextCityMapRandomModulo(city, city->map_height);
      int x = NextCityMapRandomModulo(city, city->map_width);
      TileIndex tile_index = MapTileXY(city, x, y);
      if (!IsFlatDryTile(&city->tile[tile_index])) continue;
      WriteOverlay(city, tile_index, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, tile_index);
      if (city->settings.auto_goto != 0)
        CenterCityMapOnEvent(city, x, y);
      city->disasters.event_decay_ticks = 60;
      PlayCitySound(city, MapTileXY(city, origin_x, origin_y), CITY_SOUND_WATER);
      SetDisasterLocation(&city->disasters.location, x, y);
      return true;
    }
    return false;
  }

  int y = target_y;
  int x = target_x;
  InvalidateTile(city, MapTileXY(city, x, y));
  if (y > 0 && x < city->map_width && target_offset_y > 0) {
    TileIndex neighbor = MapTileXY(city, x, y - 1);
    if (!HasTileWater(&city->tile[neighbor])) {
      WriteOverlay(city, neighbor, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, neighbor);
    }
  }
  if (x > 0 && y < city->map_height && target_offset_x > 0) {
    TileIndex neighbor = MapTileXY(city, x - 1, y);
    if (!HasTileWater(&city->tile[neighbor])) {
      WriteOverlay(city, neighbor, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, neighbor);
    }
  }
  if (y + 1 < city->map_height && x < city->map_width && target_offset_y < city->map_height - 1) {
    TileIndex neighbor = MapTileXY(city, x, y + 1);
    if (!HasTileWater(&city->tile[neighbor])) {
      WriteOverlay(city, neighbor, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, neighbor);
    }
  }
  if (x + 1 < city->map_width && target_offset_x < city->map_width - 1) {
    TileIndex neighbor = MapTileXY(city, x + 1, y);
    if (!HasTileWater(&city->tile[neighbor])) {
      WriteOverlay(city, neighbor, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, neighbor);
    }
  }
  if (city->settings.auto_goto != 0)
    CenterCityMapOnEvent(city, target_x, target_y);
  city->disasters.event_decay_ticks = 60;
  PlayCitySound(city, MapTileXY(city, origin_x, origin_y), CITY_SOUND_WATER);
  SetDisasterLocation(&city->disasters.location, target_x, target_y);
  return true;
}

// Mark the scheduled tile with pollution and optionally center the map on it. The scheduled location must
// be valid; this effect always succeeds and consumes no random draws.
bool ApplyCityPollutionDisaster(City *city) {
  int y = city->disasters.location.y;
  int x = city->disasters.location.x;
  if (city->settings.auto_goto != 0)
    CenterCityMapOnEvent(city, x, y);
  TileIndex tile = MapTileXY(city, x, y);
  WriteOverlay(city, tile, CITY_OVERLAY_POLLUTION);
  InvalidateTile(city, tile);
  SetDisasterLocation(&city->disasters.location, x, y);
  return true;
}

// Place population-scaled pollution markers around the scheduled location. Each attempt draws Y then X;
// returns one if any in-map tile was marked, otherwise zero.
int ApplyCityChemicalSpillDisaster(City *city) {
  int center_y = city->disasters.location.y;
  int center_x = city->disasters.location.x;
  int remaining = city->indicators.population / 10000 + 5;
  bool changed = false;

  while (remaining > 0) {
    int y = (Sc2kCrtRand(city) & 7) + center_y - 4;
    int x = (Sc2kCrtRand(city) & 7) + center_x - 4;
    TileIndex tile = MapTileXYOrInvalid(city, x, y);
    if (tile != INVALID_TILE) {
      changed = true;
      WriteOverlay(city, tile, CITY_OVERLAY_POLLUTION);
      InvalidateTile(city, tile);
      SetDisasterLocation(&city->disasters.location, x, y);
    }
    --remaining;
  }
  if (changed && city->settings.auto_goto != 0)
    CenterCityMapOnEvent(city, center_x, center_y);
  return changed;
}

// Capture the selection before demolition. Facility damage uses the original kind;
// other tiles use their live zone after removal and optional fire marking.
void DemolishCityFootprintForDisaster(City *city, TileIndex tile_index, int animate, int mark_fire) {
  assert(IsMapTileValid(city, tile_index));
  int tile = city->tile[tile_index].kind;
  if (IsTileKindTunnel(tile))
    return;

  CityTileArea area = ResolveBulldozeFootprint(city, tile_index);
  BulldozeCityTile(city, tile_index, animate != 0);

  if (mark_fire) {
    for (int dy = 0; dy < area.height; ++dy)
      for (int dx = area.width - 1; dx >= 0; --dx) {
        TileIndex cell = GetRelativeTileOrInvalid(city, area.start, dx, dy);
        if (cell == INVALID_TILE) continue;
        if (HasTileWater(&city->tile[cell])) continue;
        WriteOverlay(city, cell, CITY_OVERLAY_FIRE);
        InvalidateTile(city, cell);
      }
  }

  int category;
  if (IsTileKindFacility(tile))
    category = tile == TK_HYDRO_POWER ? 10 : (tile - 189u);
  else
    category = ReadCityZoneKind(&city->tile[tile_index]);

  if (city->disasters.event_data == CITY_DISASTER_EVENT_DATA_NONE ||
      kDisasterDamagePriority[city->disasters.event_data] < kDisasterDamagePriority[category])
    city->disasters.event_data = category;
}

// Disaster markers clear traffic in the valid tile's containing 2x2 record. The caller owns damage publication;
// this plain field write adds no callbacks, RNG or neighbor updates.
static SC2K_FORCE_INLINE void ClearTrafficAtTile(City *city, TileIndex tile) {
  CityPoint point = MapTilePt(city, tile);
  city->half[(point.y >> 1) * (city->map_width >> 1) + (point.x >> 1)].traffic_density = 0;
}

// Spread flooding at or below the source's land level. Invalid candidate neighbours are ignored;
// admitted cells update their markers, traffic and disaster state in place.
void SpreadCityFloodToCell(City *city, TileIndex tile_index, int flood_level) {
  if (tile_index == INVALID_TILE) return;
  InvalidateTile(city, tile_index);
  const TileInfo *tp = &city->tile[tile_index];
  if (ReadCityLandHeight(tp) > flood_level)
    return;

  // Ordinary open water needs no flood marker; rivers, banks and hydro spillways remain eligible.
  if (tp->kind != TK_HYDRO_POWER && ReadCityWaterGroundKind(tp) == TK_WATER)
    return;

  int overlay = ReadOverlay(city, tile_index);
  if (overlay == CITY_OVERLAY_NONE) {
    WriteOverlay(city, tile_index, CITY_OVERLAY_FLOOD);
    ClearTrafficAtTile(city, tile_index);
    return;
  }
  if (IsCitySignOverlay(overlay)) {
    if (!city->individual_facilities) ClearOverlayAssociation(city, overlay);
    WriteOverlay(city, tile_index, CITY_OVERLAY_FLOOD);
    ClearTrafficAtTile(city, tile_index);
    return;
  }
  if (IsCityFacilityOverlay(overlay)) {
    DemolishCityFootprintForDisaster(city, tile_index, 1, 0);
    WriteOverlay(city, tile_index, CITY_OVERLAY_FLOOD);
    ClearTrafficAtTile(city, tile_index);
    return;
  }
  if (overlay >= CITY_OVERLAY_EVENT_END && overlay < CITY_OVERLAY_BORDER_CONNECTION)
    ReplaceCityTileVariant(city, tile_index, TK_RUBBLE, NextCityMapRandomModulo(city, 4));
}

// Replace an absent overlay or a sign with a transient marker. Invalid indices are ignored;
// other overlay associations remain intact.
void SetCityMapOverlayIfReplaceable(City *city, TileIndex tile_index, u8 overlay) {
  if (tile_index == INVALID_TILE) return;
  InvalidateTile(city, tile_index);
  int previous = ReadOverlay(city, tile_index);
  if (previous == CITY_OVERLAY_NONE || IsCitySignOverlay(previous))
    WriteOverlay(city, tile_index, overlay);
}

// Remove only the transient 0xFD and 0xFE overlays. Invalid indices are ignored; persistent labels/events and
// unrelated markers are not cleared by this helper.
void ClearTransientCityMapOverlay(City *city, TileIndex tile_index) {
  if (tile_index == INVALID_TILE) return;
  InvalidateTile(city, tile_index);

  int overlay = ReadOverlay(city, tile_index);
  if (overlay == CITY_OVERLAY_RIOTS_SOUTH_EAST || overlay == CITY_OVERLAY_RIOTS_NORTH_WEST)
    WriteOverlay(city, tile_index, CITY_OVERLAY_NONE);
}

// Consume a fire marker and apply the tile-specific destruction rule. Requires a valid affected tile; may remove a
// footprint, change ground recovery and update statistics using deterministic choices.
void ResolveDestroyedCityMapCell(City *city, TileIndex tile_index) {
  if (tile_index == INVALID_TILE) return;
  if (ReadOverlay(city, tile_index) != CITY_OVERLAY_FIRE)
    return;

  WriteOverlay(city, tile_index, CITY_OVERLAY_NONE);
  int tile = city->tile[tile_index].kind;
  if (IsTileKindTunnel(tile))
    return;

  if (IsTileKindMultiHighway(tile) || tile == TK_SUBWAY_TO_RAIL || IsTileKindBuilding(tile)) {
    if (ReadFootprintCorners(city, tile_index) == CITY_FOOTPRINT_CORNERS_ALL)
      BulldozeCityTile(city, tile_index, false);
    return;
  }

  BulldozeCityTile(city, tile_index, false);
  ReplaceCityTileVariant(city, tile_index, TK_RUBBLE, NextCityMapRandomModulo(city, 4));
}

// Apply the original powered-cell, overlay, and terrain disaster rules. INVALID_TILE is an ordinary rejected target;
// other inputs must be valid, and success may change terrain, overlays and statistics.
bool ApplyDisasterCellEffect(City *city, TileIndex tile_index, int allow_low_tiles) {
  if (tile_index == INVALID_TILE) return false;
  InvalidateTile(city, tile_index);
  if (HasTileWater(&city->tile[tile_index]))
    return false;

  int tile = city->tile[tile_index].kind;
  if (IsTileKindBareGround(tile) && allow_low_tiles == 0)
    return false;

  int overlay = ReadOverlay(city, tile_index);
  if (overlay == CITY_OVERLAY_NONE) {
    WriteOverlay(city, tile_index, CITY_OVERLAY_FIRE);
    ClearTrafficAtTile(city, tile_index);
    return true;
  }
  if (IsCitySignOverlay(overlay)) {
    if (!city->individual_facilities) ClearOverlayAssociation(city, overlay);
    WriteOverlay(city, tile_index, CITY_OVERLAY_FIRE);
    ClearTrafficAtTile(city, tile_index);
    return true;
  }
  if (IsCityFacilityOverlay(overlay)) {
    DemolishCityFootprintForDisaster(city, tile_index, 1, 1);
    WriteOverlay(city, tile_index, CITY_OVERLAY_FIRE);
    ClearTrafficAtTile(city, tile_index);
    return true;
  }
  if (IsCityEventOverlay(overlay))
    return false;
  if (overlay >= CITY_OVERLAY_BORDER_CONNECTION) {
    if (overlay != CITY_OVERLAY_BORDER_CONNECTION)
      return false;
    WriteOverlay(city, tile_index, CITY_OVERLAY_NONE);
    if (IsTileKindRoadRoute(tile))
      --city->infrastructure.service_bonus_b;
    else
      --city->infrastructure.service_bonus_a;
    WriteOverlay(city, tile_index, CITY_OVERLAY_FIRE);
    ClearTrafficAtTile(city, tile_index);
    return true;
  }

  ReplaceCityTileVariant(city, tile_index, TK_RUBBLE, NextCityMapRandomModulo(city, 4));
  return false;
}

// Find a nuclear plant, demolish its footprint and scatter damage and radioactive waste around it.
// City disaster work retains the plant origin and strike iteration; returns false when no plant exists.
i64 ApplyCityMeltdownDisaster(City *city) {
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    int center_y = city->disasters.location.y;
    int center_x = city->disasters.location.x;
    TileIndex center = MapTileXYOrInvalid(city, center_x, center_y);
    if (center == INVALID_TILE || city->tile[center].kind != TK_NUCLEAR_POWER_PLANT) {
      center = FindDisasterPlant(city, TK_NUCLEAR_POWER_PLANT);
      if (center == INVALID_TILE) return false;
      CityPoint point = MapTilePt(city, center);
      center_y = point.y;
      center_x = point.x;
    }
    if (city->settings.auto_goto != 0)
      CenterCityMapOnEvent(city, center_x, center_y);
    CityTileArea footprint = ResolveBulldozeFootprint(city, MapTileXY(city, center_x, center_y));
    CityPoint start = footprint.start == INVALID_TILE ? (CityPoint){0} : MapTilePt(city, footprint.start);
    center_y = start.y + 1;
    center_x = start.x + footprint.width - 2;
    *work = (CityDisasterWork){.type = CITY_DISASTER_MELTDOWN, .origin_x = center_x,
        .origin_y = center_y, .phase = 1};
  }
  DemolishCityFootprintForDisaster(city, MapTileXY(city, work->origin_x, work->origin_y), 1, 1);
  while (work->iteration < 65 * 65) {
    int y = work->origin_y + work->iteration / 65 - 32;
    int x = work->origin_x + work->iteration % 65 - 32;
    if ((Sc2kCrtRand(city) & 0x1F) != 0) { ++work->iteration; continue; }
    TileIndex cell = MapTileXYOrInvalid(city, x, y);
    if (cell == INVALID_TILE) { ++work->iteration; continue; }
    bool cell_effect = (Sc2kCrtRand(city) & 3) == 0;
    if (cell_effect) ApplyDisasterCellEffect(city, cell, 1);
    else DemolishCityFootprintForDisaster(city, cell, 0, 0);
    if (!cell_effect && (Sc2kCrtRand(city) & 1)) {
      if (HasTileWater(&city->tile[cell])) WriteOverlay(city, cell, CITY_OVERLAY_POLLUTION);
      else ReplaceCityTileAndStatistics(city, cell, TK_RADIOACTIVE_WASTE);
    }
    ++work->iteration;
  }
  int center_x = work->origin_x, center_y = work->origin_y;
  *work = (CityDisasterWork){0};
  for (int offset_y = -1; offset_y <= 2; ++offset_y)
    for (int offset_x = -2; offset_x <= 1; ++offset_x)
      if ((Sc2kCrtRand(city) & 1) != 0) {
        TileIndex tile = MapTileXYOrInvalid(city, center_x + offset_x, center_y + offset_y);
        if (tile != INVALID_TILE) ReplaceCityTileAndStatistics(city, tile, TK_RADIOACTIVE_WASTE);
      }

  RefreshCityMapTiles(city);
  return true;
}

// Convert directional along/across coordinates into a disaster-scan tile. Coordinates must describe an in-map point;
// returned TileIndex uses the supplied City's geometry and allocates nothing.
static TileIndex CityDisasterEdgeTile(const City *city, bool vertical, int along, int across) {
  return MapTileXY(city, vertical ? across : along, vertical ? along : across);
}

// Sweep inland from the selected map edge, demolishing coastal structures and leaving flood overlays.
// City disaster work retains the edge direction and scan progress until the hurricane passes finish.
i64 ApplyCityHurricaneDisaster(City *city) {
  enum CityHurricaneWorkPhase {
    CITY_HURRICANE_SEARCH = 0, CITY_HURRICANE_DEMOLISH = 1,
  };
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_EARTHQUAKE);
    city->disasters.event_decay_ticks = 60;
    city->disasters.event_cleanup_ticks = 50;
    *work = (CityDisasterWork){.type = CITY_DISASTER_HURRICANE,
        .direction = (city->geography.rotation + 1) & 3};
  }
  int rotation = work->direction;
  bool vertical = rotation & 1, reverse = rotation < CITY_MAP_ROTATION_180;
  while (work->attempts < 20) {
    if (work->phase == CITY_HURRICANE_SEARCH) {
      int across = NextCityMapRandomModulo(city, vertical ? city->map_width : city->map_height);
      int last = (vertical ? city->map_height : city->map_width) - 1;
      int along = reverse ? last : 0;
      bool check_tile = true;
      while (reverse ? along >= 0 : along <= last) {
        if ((reverse ? along > 0 : check_tile) &&
            !IsTileKindUnbuilt(city->tile[CityDisasterEdgeTile(city, vertical, along, across)].kind)) break;
        int step = NextCityMapRandomModulo(city, 20);
        along += reverse ? -step : step;
        check_tile = along < last;
      }
      if (reverse ? along > 0 : along < last) {
        if (rotation == CITY_MAP_ROTATION_180) ++work->attempts;
        work->x = vertical ? across : along;
        work->y = vertical ? along : across;
        work->phase = CITY_HURRICANE_DEMOLISH;
      }
    }
    if (work->phase != CITY_HURRICANE_SEARCH) {
      DemolishCityFootprintForDisaster(city, MapTileXY(city, work->x, work->y),
          rotation == CITY_MAP_ROTATION_0 || rotation == CITY_MAP_ROTATION_270, 0);
      work->phase = CITY_HURRICANE_SEARCH;
    }
    ++work->attempts;
  }
  *work = (CityDisasterWork){0};
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_EARTHQUAKE);
  int attempts = rotation == CITY_MAP_ROTATION_0 || rotation == CITY_MAP_ROTATION_270 ? 50 : 100;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    int across = NextCityMapRandomModulo(city, vertical ? city->map_width : city->map_height);
    int last = (vertical ? city->map_height : city->map_width) - 1;
    int along = reverse ? last : 0;
    // Only the southward pass inspects the final row before rejecting it.
    while ((reverse ? along >= 0 : along < last + (rotation == CITY_MAP_ROTATION_270)) &&
        IsTileKindBareGround(city->tile[CityDisasterEdgeTile(city, vertical, along, across)].kind))
      along += reverse ? -1 : 1;
    if (reverse ? along > 0 : along < last) {
      TileIndex tile = CityDisasterEdgeTile(city, vertical, along, across);
      WriteOverlay(city, tile, CITY_OVERLAY_FLOOD);
      InvalidateTile(city, tile);
    }
  }
  PlayCitySound(city, INVALID_TILE, CITY_SOUND_EARTHQUAKE);
  return true;
}

// Raise terrain and scatter fire and pollution around the scheduled volcano origin. Terrain-water permission
// is explicit; retained work restores temporary labels and cash on completion or cancellation.
i64 ApplyCityVolcanoDisaster(City *city, bool terrain_water_allowed) {
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    *work = (CityDisasterWork){.type = CITY_DISASTER_VOLCANO,
        .origin_y = city->disasters.location.y, .origin_x = city->disasters.location.x,
        .cash_restore = city->indicators.cash, .terrain_water_allowed = terrain_water_allowed};
    if (city->settings.auto_goto) {
      work->label_restore = BeginCityVolcanoPresentation(city->game,
          MapTileXY(city, work->origin_x, work->origin_y));
    }
    city->indicators.cash = 25000;
    PlayCitySoundWithDelay(city->game, CITY_SOUND_FIRE, -1);
  }
  if (work->phase == CITY_DISASTER_WORK_WAIT) {
    if (!StepGameDelay(city->game, &city->game->animation_delay)) return CITY_OPERATION_PENDING;
    work->phase = CITY_DISASTER_WORK_READY;
    if ((Sc2kCrtRand(city) & 7) != 0) PlayCitySound(city, work->strike, CITY_SOUND_EXPLOSION);
  }
  while (city->indicators.cash > 0) {
    int strike_y = work->origin_y + Sc2kCrtRandRange(city, 5) - 2;
    int strike_x = work->origin_x + Sc2kCrtRandRange(city, 5) - 2;
    TileIndex strike = MapTileXYOrInvalid(city, strike_x, strike_y);
    if (strike == INVALID_TILE) continue;
    work->strike = strike;
    WriteOverlay(city, strike, (Sc2kCrtRand(city) & 1) == 0 ? CITY_OVERLAY_POLLUTION : CITY_OVERLAY_FIRE);
    InvalidateTile(city, strike);
    if (MarkTerrainRaisePath(city, strike, work->terrain_water_allowed, strike) > 0) {
      RaiseMarkedTerrain(city, work->strike);
    } else city->indicators.cash = AddInt64Clamped(city->indicators.cash, -1000);
    int secondary_y = (Sc2kCrtRand(city) & 0x1F) + work->origin_y - 16;
    int secondary_x = (Sc2kCrtRand(city) & 0x1F) + work->origin_x - 16;
    TileIndex secondary = MapTileXYOrInvalid(city, secondary_x, secondary_y);
    if (secondary != INVALID_TILE) {
      int overlay = HasTileWater(&city->tile[secondary]) ?
          CITY_OVERLAY_POLLUTION : CITY_OVERLAY_FIRE;
      WriteOverlay(city, secondary, overlay);
      InvalidateTile(city, secondary);
    }
    if ((++work->redraw_counter & 3) == 0) {
      work->phase = CITY_DISASTER_WORK_WAIT;
      BeginGameDelay(city->game, &city->game->animation_delay, 20, true);
      return CITY_OPERATION_PENDING;
    }
    if ((Sc2kCrtRand(city) & 7) != 0) PlayCitySound(city, work->strike, CITY_SOUND_EXPLOSION);
  }
  city->indicators.cash = work->cash_restore;
  if (city->settings.auto_goto) RestoreCityVolcanoPresentation(city, work->label_restore);
  *work = (CityDisasterWork){0};
  StopAllCitySound(city->game);
  StopCityMusicSequence(city->game);
  return true;
}

// Shake the viewport, then strike a 65-by-65 region around the origin. Retains its shake/strike iteration across
// pending delays; may mutate map cells and consume RNG before completion.
i64 ApplyCityEarthquakeDisaster(City *city) {
  CityDisasterWork *work = &city->disaster_work;
  if (!work->type) {
    *work = (CityDisasterWork){.type = CITY_DISASTER_EARTHQUAKE,
        .origin_y = city->disasters.location.y, .origin_x = city->disasters.location.x};
    if (city->settings.auto_goto)
      CenterCityMapOnEvent(city, work->origin_x, work->origin_y);
    StartCityEarthquakePresentation(city);
  }
  if (work->phase == CITY_DISASTER_WORK_WAIT) {
    if (!StepGameDelay(city->game, &city->game->animation_delay)) return CITY_OPERATION_PENDING;
    ++work->iteration;
    work->phase = CITY_DISASTER_WORK_READY;
  }
  if (work->phase < CITY_DISASTER_WORK_DAMAGE && work->iteration < 24) {
    BeginGameDelay(city->game, &city->game->animation_delay, 5, false);

    PlayCitySound(city, MapTileXY(city, work->origin_x, work->origin_y), CITY_SOUND_EXPLOSION);
    work->phase = CITY_DISASTER_WORK_WAIT;
    return CITY_OPERATION_PENDING;
  }
  if (work->phase < CITY_DISASTER_WORK_DAMAGE) {

    work->phase = CITY_DISASTER_WORK_DAMAGE;
    work->iteration = 0;
  }
  while (work->iteration < 65 * 65) {
    int offset_y = work->iteration / 65 - 32;
    int offset_x = work->iteration % 65 - 32;
    if ((Sc2kCrtRand(city) & 0x3F) != 0) { ++work->iteration; continue; }
    TileIndex tile = MapTileXYOrInvalid(city, work->origin_x + offset_x, work->origin_y + offset_y);
    if (tile == INVALID_TILE || IsTileKindLandscape(city->tile[tile].kind)) {
      ++work->iteration;
      continue;
    }
    if ((Sc2kCrtRand(city) & 3) == 0) ApplyDisasterCellEffect(city, tile, 0);
    else DemolishCityFootprintForDisaster(city, tile, 0, 0);
    ++work->iteration;
  }
  *work = (CityDisasterWork){0};
  return true;
}
