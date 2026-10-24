#include "city/events/events.h"
#include "city/maps/labels.h"
#include "game/errors.h"
#include "city/infrastructure/networks.h"
#include "city/maps/terrain.h"
#include "city/maps/ground.h"
#include "city/saveload/map_format.h"
#include "city/engine/random.h"
#include "city/economy/facilities.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/buildings.h"
#include "city/maps/water.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/power_lines.h"
#include "city/engine/commands.h"
#include "assets/resource_types.h"
#include "audio/types.h"
#include "city/events/news.h"
#include "city/engine/city_types.h"
#include "city/infrastructure/types.h"
#include "game/audio.h"
#include "city/presentation.h"

#include "city/state.h"
#include "city/economy/economy.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/tools/tools.h"

// Resolve a valid tile to the complete structure dimensions selected for demolition. Returns a value describing the
// area; this neither removes the structure nor changes damage state.
CityTileArea ResolveBulldozeFootprint(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (IsTileKindStraightHighwayFamily(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
    if (bounds.right == bounds.left || bounds.bottom == bounds.top)
      return (CityTileArea){.start = INVALID_TILE};
    return (CityTileArea){MapTileXY(city, bounds.left, bounds.top),
        bounds.right - bounds.left, bounds.bottom - bounds.top};
  }
  bool highway = IsTileKindMultiHighway(kind);
  if (!IsTileKindBuilding(kind) && !highway) return (CityTileArea){tile, 1, 1};
  TileIndex anchor = highway ? CityHighwayAnchor(city, tile) : GetBuildingStartTile(city, tile);
  if (anchor == INVALID_TILE) return (CityTileArea){.start = INVALID_TILE};
  CityPoint dimensions = highway ? (CityPoint){2, 2} :
      GetBuildingFootprintDimensions(kind, ReadBuildingOrientation(city, anchor));
  return (CityTileArea){anchor, dimensions.x, dimensions.y};
}

// Release facility bookkeeping before replacing its footprint. Visit rows north-to-south and cells east-to-west to
// preserve rubble RNG and overlay cleanup order. The caller invalidates the completed edit's extents.
static void DemolishCityFootprintCells(City *city, CityTileArea footprint, int kind) {
  CityPoint start = MapTilePt(city, footprint.start);
  int right = start.x + footprint.width - 1;
  int facility_overlay = ReadCityFacilityOverlay(city, footprint.start);
  TileIndex anchor_index = footprint.start + footprint.width - 1;
  PrepareCityFacilityRemoval(city, anchor_index, kind);
  for (int row = 0; row < footprint.height; ++row)
    for (int column = 0; column < footprint.width; ++column) {
      int cell_y = start.y + row;
      int cell_x = right - column;
      const TileIndex cell_index = MapTileXYOrInvalid(city, cell_x, cell_y);
      if (cell_index == INVALID_TILE) continue;
      TileInfo *tp = &city->tile[cell_index];
      if (ReadLegacyTerrainCode(city, cell_index) == CITY_TERRAIN_FLAT)
        ReplaceCityTileVariant(city, cell_index, TK_RUBBLE, Sc2kCrtRand(city) & 3);
      else
        ReplaceCityTileAndStatistics(city, cell_index, ReadCityWaterGroundKind(tp));
      tp->flags &= ~(TF_POWERED | TF_POWERABLE);
      RefreshCityPowerConnections(city, cell_index);
      if (tp->kind != TK_RUBBLE)
        WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
      int overlay = ReadOverlay(city, cell_index);
      if (overlay != CITY_OVERLAY_NONE &&
          (overlay < CITY_OVERLAY_EVENT_END || IsCityEventOverlay(overlay) || IsCitySignOverlay(overlay) ||
              overlay == CITY_OVERLAY_BORDER_CONNECTION)) {
        if (overlay < CITY_OVERLAY_EVENT_FIRST || overlay == CITY_OVERLAY_BORDER_CONNECTION)
          tp->overlay = CITY_OVERLAY_NONE;
        ClearOverlayAssociation(city, overlay);
        if (overlay == CITY_OVERLAY_BORDER_CONNECTION) {
          if (IsTileKindRoadRoute(kind))
            --city->infrastructure.service_bonus_b;
          else
            --city->infrastructure.service_bonus_a;
        }
      }
    }
  if (facility_overlay) ClearOverlayAssociation(city, facility_overlay);
}

// Remove an admitted structure synchronously; Drawing advances queued effects. This is the shared mutation body for
// tools, terrain and disasters: it does not quote admission, charge money or choose gameplay options.
void BulldozeCityTile(City *city, TileIndex tile_index, bool animate) {
  int kind = ReadTile(city, tile_index);
  if (RemoveCityWaterPowerSpan(city, tile_index, animate) ||
      RemoveCityNativeBridge(city, tile_index, animate)) return;
  if (IsTileKindBareGround(kind)) return;
  CityTileArea footprint = ResolveBulldozeFootprint(city, tile_index);
  int width = footprint.width, height = footprint.height;
  if (!width || !height) return;
  CityPoint start_point = MapTilePt(city, footprint.start);
  int anchor_x = start_point.x + width - 1, anchor_y = start_point.y;
  int size = IntMax(width, height);
  TileIndex anchor_index = footprint.start + width - 1;
  InvalidateCityMapEvent(city, city->disasters.last_event_slot);
  bool linked_span = width == 2 && height == 2 &&
      ClassifyTransportSpanTile(city, GetRelativeTileOrInvalid(city, anchor_index, -1, 0)) >=
          CITY_TRANSPORT_FOOTPRINT_BRIDGE_13;
  if (animate && !IsTileKindTunnel(kind) && !linked_span && !IsTileKindRunwayOrPier(kind))
    PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);

  if (IsTileKindTunnel(kind)) {
    TileIndex far_entrance = RemoveCityRoadTunnel(city, tile_index, animate);
    start_point = MapTilePt(city, far_entrance);
  } else if (linked_span) {
    --anchor_x;
    RemoveCityLinkedHighwaySpan(city, tile_index, MapTileXY(city, anchor_x, anchor_y), kind, animate);
  } else {
    if (IsTileKindRunwayOrPier(kind)) {
      RemoveCityConnectedRunwayOrPier(city, footprint.start, kind, animate);
      if (animate) PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
    } else {
      DemolishCityFootprintCells(city, footprint, kind);
    }
    if (IsTileKindHighwayFootprint(kind)) RecalculateCityHighwayFootprintTerrain(city, footprint);
    if (size == 1 && !IsTileKindBuilding(kind) &&
        ReadLegacyTerrainCode(city, anchor_index) != CITY_TERRAIN_FLAT)
      RecalculateTerrainCell(city, anchor_index);
  }

  if (animate)
    ShowCityDemolitionEffect(city, MapTileXY(city, anchor_x - (width >> 1), anchor_y + (height >> 1)), size > 1);
  if (city->fixes & (1ull << CITY_FIX_POWER_CLEANUP)) {
    for (int row = start_point.y; row < start_point.y + height; ++row)
      for (int column = start_point.x; column < start_point.x + width; ++column) {
        TileIndex demolished = MapTileXYOrInvalid(city, column, row);
        if (demolished == INVALID_TILE) continue;
        TileInfo *tp = &city->tile[demolished];
        if (IsTileKindUnbuilt(tp->kind) &&
            !HasTileWater(tp)) {
          tp->flags &= ~(TF_POWERED | TF_POWERABLE);
          RefreshCityPowerConnections(city, demolished);
          InvalidateTile(city, demolished);
        }
      }
  }
  for (int row = start_point.y; row < start_point.y + height; ++row)
    for (int offset = 1; offset <= 4; ++offset) {
      TileIndex tile = MapTileXYOrInvalid(city, start_point.x - offset, row - offset);
      InvalidateTile(city, tile);
    }
  for (int column = start_point.x; column < start_point.x + width; ++column)
    for (int offset = 1; offset <= 4; ++offset) {
      TileIndex tile = MapTileXYOrInvalid(city, column - offset, start_point.y - offset);
      InvalidateTile(city, tile);
    }
  int left = IntMax(0, start_point.x), top = IntMax(0, start_point.y);
  InvalidateTileRange(city, MapTileXYOrInvalid(city, left, top), start_point.x + width - left,
      start_point.y + height - top);
}

// Recognize clear ground, including an empty dry bridge underpass. Reads the supplied tile only;
// it does not start recovery or remove ground cover.
static bool IsClearCityGroundTile(const TileInfo *tp) {
  if (tp->kind == TK_CLEAR) return true;
  return IsTileKindBridgeMiddle(tp->kind) && !HasTileWater(tp) &&
      ReadCityBridgeUnderpass(tp) == CITY_BRIDGE_UNDERPASS_NONE;
}

// Check whether demolition would leave a valid ground tile unchanged. Underground selects the layer being edited; the
// predicate does not charge or invalidate anything.
static bool IsCityBulldozeGroundNoop(const TileInfo *tp, bool underground) {
  if (underground || !IsClearCityGroundTile(tp)) return false;
  int ground = ReadGroundKind(tp);
  return ground >= CITY_GROUND_RECOVERY_FIRST && ground <= CITY_GROUND_RECOVERY_GRASS_FIRST;
}

// Recognize clear grass, including partially recovered grass that can be cleared again.
// Bare recovery ground is handled separately as a no-op.
static bool IsClearCityGrassTile(const TileInfo *tp) {
  int ground = ReadGroundKind(tp);
  return IsClearCityGroundTile(tp) && (ground == CITY_GROUND_GRASS ||
      (ground >= CITY_GROUND_SNOW_FIRST && ground <= CITY_GROUND_SNOW_LAST) ||
      ground == CITY_GROUND_DESERT_HALF || ground == CITY_GROUND_DESERT_FULL ||
      (ground > CITY_GROUND_RECOVERY_GRASS_FIRST && ground <= CITY_GROUND_RECOVERY_LAST));
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
int GetCityBulldozeTileError(const City *city, TileIndex tile_index, bool underground, bool alt) {
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
    int kind = tp->kind;
    if (kind == TK_RADIOACTIVE_WASTE) return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
    if (IsTileKindBridgeMiddle(kind)) {
      int ground = ReadGroundKind(tp);
      return ReadCityBridgeUnderpass(tp) != CITY_BRIDGE_UNDERPASS_NONE ||
          ground == CITY_GROUND_ROUGH || ground == CITY_GROUND_ROCKS || IsClearCityGrassTile(tp) ?
          CITY_COMMAND_COST_OK : CITY_COMMAND_COST_NOTHING_TO_REMOVE;
    }
    if (IsTileKindExposedWater(kind)) {
      if (ReadCityWaterHeight(tp) > ReadCityLandHeight(tp))
        return CITY_COMMAND_COST_CLEAR_DEEP_WATER;
      return CITY_COMMAND_COST_OK;
    }
    if (kind == TK_CLEAR) {
      int ground = ReadGroundKind(tp);
      if (ground != CITY_GROUND_ROUGH && ground != CITY_GROUND_ROCKS && !IsClearCityGrassTile(tp))
        return CITY_COMMAND_COST_NOTHING_TO_REMOVE;
    }
  }
  return CITY_COMMAND_COST_OK;
}

// Shared demolition admission for execution and previews. Invalid tiles return false; layer/alt are resolved choices.
// Does not change map, RNG or treasury.
bool CanBulldozeCityTile(const City *city, TileIndex tile, bool underground, bool alt) {
  return GetCityBulldozeTileError(city, tile, underground, alt) == CITY_COMMAND_COST_OK;
}

// Apply one admitted demolition edit using explicit options and the selected cost-table entry. Requires a valid tile
// and matching cost index; execution may remove a whole footprint and returns the original signed result, preserving
// partial effects.
i64 ApplyBulldozerTile(City *city, TileIndex tile_index, int options,
    int cost_index) {
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  int error = GetCityBulldozeTileError(city, tile_index, underground, (options & CITY_BULLDOZER_ALT) != 0);
  if (error) {
    return error;
  }
  if (IsCityBulldozeGroundNoop(&city->tile[tile_index], underground)) {
    if (!(options & CITY_BULLDOZER_NO_EFFECT)) {
      PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
      ShowCityDemolitionEffect(city, tile_index, false);
    }
    return true;
  }
  int kind = ReadTile(city, tile_index);
  int tunnel_depth = underground ?
      (ReadAltitudeWord(city, tile_index) & CITY_ALTITUDE_TUNNEL_MASK) >> CITY_ALTITUDE_TUNNEL_SHIFT :
      CITY_TUNNEL_DEPTH_NONE;
  i64 cost = cost_index == CITY_DEMOLISH_CLEAR ? GetCityBulldozeTileCost(city, tile_index, underground) :
      kCityToolCosts[cost_index];
  i64 payment = PayCityCommandCost(city, cost);
  if (payment < 0) return payment;
  bool surface_demolition = underground && (tunnel_depth == CITY_TUNNEL_DEPTH_ENTRANCE ||
      ReadUnderground(city, tile_index) == TUK_SUBWAY_ACCESS);
  // Structural demolition plays its explosion in BulldozeCityTile. Ground
  // clearing and hidden networks need the same feedback after their edit.
  bool play_sound = underground ? !surface_demolition :
      IsTileKindBridgeMiddle(kind) || IsTileKindExposedWater(kind) || IsTileKindUnbuilt(kind);
  InvalidateTile(city, tile_index);
  if (underground) {
    if (!IsTileKindBuilding(kind))
      city->tile[tile_index].flags &= ~TF_PIPED;
    if (surface_demolition) BulldozeCityTile(city, tile_index, true);
    SetUndergroundTile(city, tile_index, TUK_CLEAR);
    if (!surface_demolition) ShowCityDemolitionEffect(city, tile_index, false);
  } else if (IsTileKindBridgeMiddle(city->tile[tile_index].kind)) {
    ClearCityBridgeUnderpass(city, tile_index);
  } else if (IsTileKindExposedWater(kind)) {
    ClearCityExposedWater(city, tile_index);
  } else {
    if (kind == TK_SUBWAY_STATION ||
        IsTileKindTrainTransition(kind))
      SetUndergroundTile(city, tile_index, TUK_CLEAR);
    if (IsTileKindUnbuilt(kind)) {
      if (IsTileKindTree(kind) && Sc2kCrtRandRange(city, 20) == 0) {
        StopCitySound(city->game, CITY_SOUND_BULLDOZER_WORK);
        PlayCitySound(city, tile_index, CITY_SOUND_DEMOLITION);
        ShowCityResourceAlertDialog(city, CITY_BITMAP_NOTICE_403, CITY_NOTICE_TREE_REMOVAL_PROTEST);
        InsertRankedCityEvent(city, CITY_NEWS_TREE_REMOVAL_PROTEST, 0);
        return CITY_COMMAND_COST_BLOCKED;
      }
      ReplaceCityTileAndStatistics(city, tile_index,
          ReadCityWaterGroundKind(&city->tile[tile_index]));
      if (IsTileKindTree(kind)) ShowCityDemolitionEffect(city, tile_index, false);
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

// Command execution uses explicit options, including inside a rectangle command. Requires a valid tile and supported
// explicit options; returns the original signed edit/error result and charges through the shared execution path.
i64 ApplyCityBulldozerAtTile(City *city, TileIndex tile, int options) {
  bool previous_suppression = SetCityDemolitionSuppressed(city, (options & CITY_BULLDOZER_NO_EFFECT) != 0);
  i64 applied = ApplyBulldozerTile(city, tile, options, CITY_DEMOLISH_CLEAR);
  SetCityDemolitionSuppressed(city, previous_suppression);
  return applied;
}

// Quote the currently admitted cells in an inclusive rectangle, in row order. This sums their per-cell prices;
// execution can change later cells by removing a footprint. Quoting does not remove tiles, charge cash or consume RNG.
static CityCommandCost GetCityBulldozeRectangleCost(const City *city, TileIndex start, TileIndex end, int options) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      (options & ~CITY_BULLDOZER_FLAGS_MASK)) return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  bool underground = (options & CITY_BULLDOZER_UNDERGROUND) != 0;
  bool alt = (options & CITY_BULLDOZER_ALT) != 0;
  bool eligible = false;
  int last_error = CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      int error = GetCityBulldozeTileError(city, tile, underground, alt);
      if (!error) {
        eligible = true;
        quote.cost += GetCityBulldozeTileCost(city, tile, underground);
      } else if (error != CITY_COMMAND_COST_NOTHING_TO_REMOVE) last_error = error;
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
i64 CmdApplyBulldozerTool(City *city, u32 flags, TileIndex location,
    int options) {
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
  return ApplyCityBulldozerAtTile(city, location, options);
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
        i64 result = ApplyCityBulldozerAtTile(city, tile, options | CITY_BULLDOZER_NO_EFFECT);
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
