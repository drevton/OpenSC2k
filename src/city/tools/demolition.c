#include "city/tools/tools.h"

#include "city/engine/commands.h"
#include "city/events/events.h"
#include "city/infrastructure/networks.h"
#include "city/maps/ground.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/maps/module.h"
#include "city/presentation.h"
#include "city/state.h"
#include "game/audio.h"
#include "game/errors.h"

// Underground clearing has its own admission; bare surface recovery needs no edit or payment.
static bool IsCityBulldozeGroundNoop(const TileInfo *tp, bool underground) {
  return !underground && IsCityGroundClearingNoop(tp);
}

// Quote the demolition charge for a valid tile in the selected surface/underground layer. Admission is checked
// separately; no structure, cash, RNG or damage state changes.
i64 GetCityBulldozeTileCost(const City *city, TileIndex tile, bool underground) {
  const TileInfo *tp = &city->tile[tile];
  if (IsCityBulldozeGroundNoop(tp, underground)) return 0;
  if (!underground && IsClearCityGrassTile(tp) && !(city->fixes & (1ull << CITY_FIX_GRASS_CLEARING_COST)))
    return 0;
  return kCityToolCosts[CITY_DEMOLISH_CLEAR];
}

// Check obstruction and protected-category rules for the resolved layer/alt choices. Invalid tiles report nothing
// to remove; returns zero or a negative error without editing City.
int GetCityBulldozeTileError(City *city, TileIndex tile_index, bool underground, bool alt) {
  if (!IsMapTileValid(city, tile_index)) return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  const TileInfo *tp = &city->tile[tile_index];
  if (ReadOverlay(city, tile_index) == CITY_OVERLAY_FIRE && !alt)
    return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  if (ReadCityZoneKind(tp) == ZONE_MILITARY)
    return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  if (IsCityBulldozeGroundNoop(tp, underground)) return CITY_COMMAND_COST_OK;
  if (underground) {
    int tunnel_depth = (tp->altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT;
    if ((tp->underground & CITY_UNDERGROUND_KIND_MASK) == TUK_CLEAR && tunnel_depth != CITY_TUNNEL_DEPTH_ENTRANCE &&
        !(tp->flags & TF_PIPED))
      return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  } else {
    const TileModule *module = TileModuleForClasses(ReadCityTileClasses(tp->kind));
    if (module->clear) return module->clear(city, CITY_COMMAND_TEST, tile_index);
  }
  return CITY_COMMAND_COST_OK;
}

// Shared demolition admission for execution and previews. Invalid tiles return false; layer/alt are resolved choices.
// Does not change map, RNG or treasury.
bool CanBulldozeCityTile(City *city, TileIndex tile, bool underground, bool alt) {
  return GetCityBulldozeTileError(city, tile, underground, alt) == CITY_COMMAND_COST_OK;
}

// The caller has checked admission against the current tile. Pay and apply the edit without probing it again;
// footprint removal and late failures retain their partial effects. The caller owns feedback suppression.
static i64 ApplyAdmittedBulldozerTile(City *city, TileIndex tile_index, int options, int cost_index) {
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  TileInfo *tp = &city->tile[tile_index];
  if (IsCityBulldozeGroundNoop(tp, underground)) {
    if (!(options & CITY_BULLDOZER_NO_EFFECT)) {
      PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
      ShowCityDemolitionEffect(city, tile_index, false);
    }
    return true;
  }
  int kind = tp->kind;
  int tunnel_depth = underground ?
      (tp->altitude & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT :
      CITY_TUNNEL_DEPTH_NONE;
  i64 cost = cost_index == CITY_DEMOLISH_CLEAR ? GetCityBulldozeTileCost(city, tile_index, underground) :
      kCityToolCosts[cost_index];
  i64 payment = PayCityCommandCost(city, cost);
  if (payment < 0) return payment;
  bool surface_demolition = underground && (tunnel_depth == CITY_TUNNEL_DEPTH_ENTRANCE ||
      (tp->underground & CITY_UNDERGROUND_KIND_MASK) == TUK_SUBWAY_ACCESS);
  // Structural demolition plays its explosion in BulldozeCityTile. Ground
  // clearing and hidden networks need the same feedback after their edit.
  bool play_sound = underground ? !surface_demolition :
      IsTileKindBridgeMiddle(kind) || IsTileKindExposedWater(kind) || IsTileKindUnbuilt(kind);
  InvalidateTile(city, tile_index);
  if (underground) {
    if (!IsTileKindBuilding(kind))
      tp->flags &= ~TF_PIPED;
    if (surface_demolition) BulldozeCityTile(city, tile_index, true);
    SetUndergroundTile(city, tile_index, TUK_CLEAR);
    if (!surface_demolition) ShowCityDemolitionEffect(city, tile_index, false);
  } else {
    const TileModule *module = TileModuleForClasses(ReadCityTileClasses(tp->kind));
    if (module->clear) {
      i64 result = module->clear(city, 0, tile_index);
      if (result < 0) return result;
    } else {
      BulldozeCityTile(city, tile_index, true);
    }
  }
  RefreshAdjacentSurfaceConnections(city, tile_index);
  RefreshAdjacentUndergroundConnections(city, tile_index);
  if (play_sound) {
    PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
    ShowCityDemolitionEffect(city, tile_index, false);
  }
  return true;
}

// Check the resolved layer and modifier before paying for the selected tool. Rejection has no effects;
// a failure during the admitted edit keeps its charge and any earlier writes.
i64 ApplyBulldozerTile(City *city, TileIndex tile, int options, int cost_index) {
  int error = GetCityBulldozeTileError(city, tile, (options & CITY_BULLDOZER_UNDERGROUND) != 0,
      (options & CITY_BULLDOZER_ALT) != 0);
  if (error) return error;
  return ApplyAdmittedBulldozerTile(city, tile, options, cost_index);
}

// Quote the currently admitted cells in an inclusive rectangle, in row order. This sums their per-cell prices;
// execution can change later cells by removing a footprint. Quoting does not remove tiles, charge cash or consume RNG.
static CityCommandCost GetCityBulldozeRectangleCost(City *city, TileIndex start, TileIndex end, int options) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      (options & ~CITY_BULLDOZER_FLAGS_MASK)) return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  bool alt = (options & CITY_BULLDOZER_ALT) != 0;
  bool eligible = false;
  int last_error = CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  for (int y = bounds.top; y < bounds.bottom; ++y) {
    TileIndex tile = MapTileXY(city, bounds.left, y);
    for (int x = bounds.left; x < bounds.right; ++x, ++tile) {
      int error = GetCityBulldozeTileError(city, tile, underground, alt);
      if (!error) {
        eligible = true;
        quote.cost += GetCityBulldozeTileCost(city, tile, underground);
      } else if (error != CITY_COMMAND_COST_NOTHING_TO_REMOVE) last_error = error;
    }
  }
  quote.error = !eligible ? last_error : CITY_COMMAND_COST_OK;
  return quote;
}

// Demolish the structure at a valid resolved tile, optionally queuing its visual animation. Animation must be zero or
// one; TEST only checks admission, and execution reports Game error status while Drawing owns the queued pixels.
i64 CmdBulldoze(City *city, u32 flags, TileIndex location, int animate) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || (animate != 0 && animate != 1))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ClearGameError(city->game);
  BulldozeCityTile(city, location, animate != 0);
  return GetGameError(city->game) == 0;
}

// Execute the retained single-cell demolition action with resolved layer/options. Validates tile and allowed bits;
// TEST uses read-only error/cost probes rather than performing effects or consuming RNG.
i64 CmdApplyBulldozerTool(City *city, u32 flags, TileIndex location, int options) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!IsMapTileValid(city, location) || (options & ~CITY_BULLDOZER_FLAGS_MASK) != 0)
    return invalid;
  if (flags & CITY_COMMAND_TEST) {
    bool underground = options & CITY_BULLDOZER_UNDERGROUND;
    int error = GetCityBulldozeTileError(city, location, underground, options & CITY_BULLDOZER_ALT);
    if (error) return error == CITY_COMMAND_COST_CLEAR_DEEP_WATER ? error : CITY_COMMAND_COST_BLOCKED;
    return GetCityBulldozeTileCost(city, location, underground);
  }
  ClearGameError(city->game);
  bool previous_suppression = SetCityDemolitionSuppressed(city, (options & CITY_BULLDOZER_NO_EFFECT) != 0);
  i64 applied = ApplyBulldozerTile(city, location, options, CITY_DEMOLISH_CLEAR);
  SetCityDemolitionSuppressed(city, previous_suppression);
  return applied;
}

// Demolish admitted cells in a resolved inclusive rectangle, in row order. Each edit checks and pays its live
// charge; footprint edits can change later cells. TEST quotes the current rectangle without performing those edits.
i64 CmdBulldozeRect(City *city, u32 flags, TileIndex location, TileIndex end, int options) {
  CityCommandCost quote = GetCityBulldozeRectangleCost(city, location, end, options);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (quote.error) return quote.error;
  i64 affordable = AffordableCityCommandCost(city, quote.cost);
  if (affordable < 0) return affordable;
  CityRect bounds = MapTileRectangle(city, location, end);
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  bool alt = (options & CITY_BULLDOZER_ALT) != 0;
  TileIndex endpoints[2] = {location, end};
  int endpoint_count = location == end ? 1 : 2;
  bool large[2];
  for (int i = 0; i < endpoint_count; ++i) {
    CityTileArea footprint = ResolveBulldozeFootprint(city, endpoints[i]);
    large[i] = footprint.width > 1 || footprint.height > 1;
  }
  bool demolished = false;
  i64 failure = 0;
  ClearGameError(city->game);
  PlayCitySoundWithDelay(city->game, CITY_SOUND_BULLDOZER_WORK, -1);
  for (int y = bounds.top; y < bounds.bottom && !GetGameError(city->game); ++y)
    for (int x = bounds.left; x < bounds.right && !GetGameError(city->game); ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (IsCityBulldozeGroundNoop(&city->tile[tile], underground)) continue;
      if (CanBulldozeCityTile(city, tile, underground, alt)) {
        bool previous_suppression = SetCityDemolitionSuppressed(city, true);
        i64 result = ApplyAdmittedBulldozerTile(city, tile, options | CITY_BULLDOZER_NO_EFFECT, CITY_DEMOLISH_CLEAR);
        SetCityDemolitionSuppressed(city, previous_suppression);
        if (result < 0) failure = result;
        else demolished |= result > 0;
      }
    }
  StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
  // Bare ground is admitted without changing tiles. Its loop otherwise starts
  // and stops within this call, so acknowledge the successful tool action once.
  if (!demolished && !failure && !GetGameError(city->game) && !(options & CITY_BULLDOZER_NO_EFFECT))
    PlayCitySound(city, location, CITY_SOUND_EXPLOSION);
  if ((demolished || (!failure && !GetGameError(city->game))) && !(options & CITY_BULLDOZER_NO_EFFECT))
    for (int i = 0; i < endpoint_count; ++i)
      ShowCityDemolitionEffect(city, endpoints[i], large[i]);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return failure ? failure : 1;
}
