// TileInfo storage for road tunnels (zero-based bits):
// Only portals use TK_TUNNEL_ENTRANCE. Their terrain[4..5] is the portal variant 0..3, corresponding to slope
// shapes 1..4; terrain[6..7] has no portal payload. terrain[0..3] retains dry ground/coverage.
// Every traversed tile, including the portals, stores depth-plus-one in altitude[10..14]: 0 no tunnel, 1 portal,
// larger values place the bore below local land height. The bore is land height - stored value + 1, in map levels.
// Interior tiles retain their surface kind/metadata; no underground TUK_ ID represents the road tunnel itself.
// altitude[0..9] and bit 15 retain their surface meanings. zone[4] selects slope versus zoning in zone[0..3];
// portal zone[5..7] has no tunnel payload. Flags, signs/effects and the independent underground layer are shared
// as described in tile_storage.h. Removing the tunnel clears its depth field, not the surface occupant above it.

#include "city/infrastructure/lines.h"
#include "city/saveload/map_format.h"
#include "city/engine/random.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/terrain.h"

#include <assert.h>

#include "assets/graphics.h"
#include "game/audio.h"
#include "city/engine/commands.h"
#include "city/infrastructure/power_lines.h"
#include "city/infrastructure/surface_lines.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_view.h"
#include "game/observation.h"
#include "game/state.h"

// Search from a valid candidate entrance for the matching tunnel exit and chargeable length. Returns a value with
// INVALID_TILE end and an error when unsupported; no terrain, treasury or RNG changes occur.
CityInfrastructureSpan FindCityRoadTunnel(const City *city, TileIndex origin_tile) {
  CityInfrastructureSpan span = {.start = origin_tile, .end = INVALID_TILE, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, origin_tile)) return span;
  CityPoint origin_point = MapTilePt(city, origin_tile);
  TileIndex tile_index = origin_tile;
  int starting_tile = ReadTile(city, tile_index);
  if (!IsTileKindLandscape(starting_tile) || starting_tile == TK_RADIOACTIVE_WASTE ||
      ReadUnderground(city, tile_index) != TUK_CLEAR)
    return span;

  int starting_terrain = ReadLegacyTerrainCode(city, tile_index);
  if (starting_terrain < CITY_TERRAIN_SHAPE_1 || starting_terrain > CITY_TERRAIN_SHAPE_4) {
    span.error = CITY_NOTICE_TUNNEL_REQUIRES_HILLSIDE;
    return span;
  }

  int starting_height = ReadCityLandHeight(&city->tile[tile_index]);
  int direction = (starting_terrain + 2) & 3;
  int span_cells = 0;
  int end_y = origin_point.y;
  int end_x = origin_point.x;
  TileIndex end_tile = tile_index;

  for (;;) {
    if (end_y < 0 || end_x < 0 || end_y >= city->map_height - 1 || end_x >= city->map_width - 1) {
      span.error = CITY_NOTICE_TUNNEL_EXCEEDS_CITY_LIMITS;
      return span;
    }

    int altitude = ReadAltitudeWord(city, end_tile);
    if ((altitude & CITY_ALTITUDE_TUNNEL_MASK) != 0) {
      span.error = CITY_NOTICE_TUNNEL_CANNOT_CROSS_TUNNELS;
      return span;
    }
    int height_delta = ReadCityLandHeight(&city->tile[end_tile]) - starting_height;
    if (height_delta > 30) {
      span.error = CITY_NOTICE_TUNNEL_TOO_DEEP;
      return span;
    }
    if (height_delta == 1) {
      int underground = ReadUnderground(city, end_tile);
      if ((underground >= TUK_SUBWAY_FIRST && underground <= TUK_SUBWAY_NS_PIPE_EW) ||
          underground == TUK_MISSILE_SILO || underground == TUK_SUBWAY_ACCESS) {
        span.error = CITY_NOTICE_TUNNEL_UNDERGROUND_OBSTRUCTION;
        return span;
      }
    }

    ++span_cells;
    end_y += kLineYStep[direction];
    end_x += kLineXStep[direction];
    end_tile = MapTileXYOrInvalid(city, end_x, end_y);
    if (end_tile == INVALID_TILE) {
      span.error = CITY_NOTICE_TUNNEL_UNSTABLE_EXIT;
      return span;
    }
    if (ReadCityLandHeight(&city->tile[end_tile]) <= starting_height) {
      if (ReadLegacyTerrainCode(city, end_tile) != ((starting_terrain + 1) & 3) + 1) {
        span.error = CITY_NOTICE_TUNNEL_UNSTABLE_EXIT;
        return span;
      }
      break;
    }
  }

  span.end = end_tile;
  span.direction = direction;
  span.length = span_cells + 1;
  return span;
}

// Build the validated tunnel's entrance/exit and update their surrounding connections. The span must belong to the
// admitted tunnel search; construction effects happen once after confirmation.
static bool FinishCityRoadTunnel(City *city, TileIndex origin_tile, CityInfrastructureSpan span,
    int starting_terrain, int starting_height) {
  int stride = kLineYStep[span.direction] * city->map_width + kLineXStep[span.direction];
  TileIndex tile_index = origin_tile;
  int span_cells = span.length - 1;
  static const i8 kNeighbors[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  DebitCityCommandCost(city, city->construction.cost);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  ReplaceCityTileVariant(city, tile_index, TK_TUNNEL_ENTRANCE,
      starting_terrain - CITY_TERRAIN_SHAPE_1);
  WriteAltitudeWord(city, tile_index, (ReadAltitudeWord(city, tile_index) & CITY_ALTITUDE_CLEAR_TUNNEL) |
      CITY_ALTITUDE_TUNNEL_ENTRANCE);
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    TileIndex adjacent = GetRelativeTileOrInvalid(city, tile_index, kNeighbors[neighbor][0], kNeighbors[neighbor][1]);
    if (adjacent != INVALID_TILE) UpdateRoadConnections(city, adjacent);
  }
  InvalidateTile(city, tile_index);

  while (span_cells > 0) {
    tile_index += stride;
    int altitude = ReadAltitudeWord(city, tile_index);
    int terrain_delta = ReadCityLandHeight(&city->tile[tile_index]) - starting_height + 1;
    int terrain_height = terrain_delta * (1 << CITY_ALTITUDE_TUNNEL_SHIFT);
    if (IsMapTileValid(city, tile_index))
      WriteAltitudeWord(city, tile_index, (altitude & CITY_ALTITUDE_CLEAR_TUNNEL) | terrain_height);
    InvalidateTile(city, tile_index);
    --span_cells;
  }

  ReplaceCityTileVariant(city, tile_index, TK_TUNNEL_ENTRANCE, (starting_terrain + 1) & 3);
  WriteAltitudeWord(city, tile_index, (ReadAltitudeWord(city, tile_index) & CITY_ALTITUDE_CLEAR_TUNNEL) |
      CITY_ALTITUDE_TUNNEL_ENTRANCE);
  for (int neighbor = 0; neighbor < 4; ++neighbor) {
    TileIndex adjacent = GetRelativeTileOrInvalid(city, tile_index, kNeighbors[neighbor][0], kNeighbors[neighbor][1]);
    if (adjacent != INVALID_TILE) UpdateRoadConnections(city, adjacent);
  }
  InvalidateTile(city, tile_index);
  return true;
}

// Native commands receive the resolved confirmation; tunnel price is simulation data.
i64 PlaceCityRoadTunnel(City *city, TileIndex tile, bool accept) {
  assert(IsMapTileValid(city, tile));
  const GameObservation *observer = city->game->observation;
  if (observer && observer->place_road_tunnel)
    return observer->place_road_tunnel(city, tile) ? 1 : CITY_COMMAND_COST_BLOCKED;
  CityInfrastructureSpan span = FindCityRoadTunnel(city, tile);
  if (span.end == INVALID_TILE) {
    return span.error ? -span.error : CITY_COMMAND_COST_BLOCKED;
  }
  city->construction.cost = (u32)span.length * CITY_ROAD_TUNNEL_COST;
  if (!accept) {
    CancelCityCommandFeedback(city);
    return CITY_COMMAND_COST_BLOCKED;
  }
  i64 affordable = AffordableCityCommandCost(city, city->construction.cost);
  if (affordable < 0) return affordable;
  return FinishCityRoadTunnel(city, tile, span, ReadLegacyTerrainCode(city, tile),
      ReadCityLandHeight(&city->tile[tile]));
}

// Place a road tunnel from its valid starting tile using the resolved confirmation bit. TEST returns the tunnel charge
// without editing terrain; execution retains the tunnel routine's negative-error or completion result.
i64 CmdPlaceRoadTerrainSpan(City *city, u32 flags, TileIndex tile, int options) {
  if (!IsMapTileValid(city, tile) || (options & ~CITY_TUNNEL_CONFIRMED)) return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) {
    CityInfrastructureSpan tunnel = FindCityRoadTunnel(city, tile);
    if (tunnel.end == INVALID_TILE) return tunnel.error ? -tunnel.error : CITY_COMMAND_COST_BLOCKED;
    return (i64)tunnel.length * CITY_ROAD_TUNNEL_COST;
  }
  return PlaceCityRoadTunnel(city, tile, (options & CITY_TUNNEL_CONFIRMED) != 0);
}

// Retained placement keeps its opening geometry across the question, then charges the live construction cost.
bool PlaceCityRoadTerrainSpan(City *city, TileIndex origin_tile) {
  assert(IsMapTileValid(city, origin_tile));
  CityInfrastructureSpan span = FindCityRoadTunnel(city, origin_tile);
  if (span.end == INVALID_TILE) {
    if (span.error) ShowCityMessage(city, span.error);
    return false;
  }
  int starting_terrain = ReadLegacyTerrainCode(city, origin_tile);
  int starting_height = ReadCityLandHeight(&city->tile[origin_tile]);
  city->construction.cost = (u32)span.length * kCityToolCosts[CITY_TOOL_GROUP_ROAD * CITY_TOOL_VARIANT_CAPACITY +
      CITY_ROAD_TUNNEL];
  if (ChooseCityConnectionQuestion(city, CITY_CONNECTION_TUNNEL, (int)city->construction.cost) != CITY_DIALOG_YES) {
    CancelCityCommandFeedback(city);
    return false;
  }
  if (AffordableCityCommandCost(city, city->construction.cost) < 0) {
    ShowCityMessage(city, CITY_NOTICE_INSUFFICIENT_OPERATING_FUNDS);
    return false;
  }

  return FinishCityRoadTunnel(city, origin_tile, span, starting_terrain, starting_height);
}

// Remove both tunnel entrances and clear the intervening depth markers. Power cleanup applies only to the selected
// entrance. The far entrance is returned for the existing post-demolition cleanup area.
TileIndex RemoveCityRoadTunnel(City *city, TileIndex tile_index, bool animate) {
  CityPoint point = MapTilePt(city, tile_index);
  static const i8 kStepY[4] = {-1, 0, 1, 0};
  static const i8 kStepX[4] = {0, -1, 0, 1};
  int direction = ReadTileVariant(&city->tile[tile_index]);
  for (int endpoint = 0; endpoint < 2; ++endpoint) {
    TileInfo *tp = &city->tile[tile_index];
    if (city->fixes & (1 << CITY_FIX_TUNNEL_RUBBLE))
      ReplaceCityTileVariant(city, tile_index, TK_RUBBLE, Sc2kCrtRand(city) & 3);
    else
      ReplaceCityTileAndStatistics(city, tile_index, ReadCityWaterGroundKind(tp));
    if (animate) ShowCityDemolitionEffect(city, tile_index, false);
    if (tp->kind != TK_RUBBLE) WriteTileZoneByte(tp, ReadTileZoneByte(tp) & CITY_ZONE_KIND_MASK);
    if (endpoint == 0) {
      // Power cleanup belongs to the selected entrance; clear tunnel depth all the way to the other entrance.
      if ((city->fixes & (1 << CITY_FIX_POWER_CLEANUP)) &&
          !HasTileWater(tp)) {
        tp->flags &= ~(TF_POWERED | TF_POWERABLE);
        RefreshCityPowerConnections(city, tile_index);
        InvalidateTile(city, tile_index);
      }
      tp->altitude &= CITY_ALTITUDE_CLEAR_TUNNEL;
      do {
        point.y += kStepY[direction];
        point.x += kStepX[direction];
        tile_index = MapTileXY(city, point.x, point.y);
        tp = &city->tile[tile_index];
        tp->altitude &= CITY_ALTITUDE_CLEAR_TUNNEL;
      } while (!IsTileKindTunnel(tp->kind));
    } else if (animate) {
      PlayCitySound(city, tile_index, CITY_SOUND_EXPLOSION);
    }
  }
  return tile_index;
}

// The map renderer supplies an already rotated/zoomed portal after its shared visibility and detail checks.
// Terrain stays in the ground pass; the entrance uses the current depth pass and ordinary sprite clipping.
void DrawCityRoadTunnelEntrance(City *city, TileIndex tile, int sprite, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[tile];
  int terrain = CityMapDryGroundShape(city, tile, geometry);
  bool native = city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE);
  int ground = -1;
  if (zoom == CITY_MAP_ZOOM_SMALL && terrain == CITY_TERRAIN_FLAT) {
    int zone = ReadCityZoneKind(tp);
    if (zone != ZONE_NONE) ground = 290 + zone;
  }
  if (zoom != CITY_MAP_ZOOM_SMALL || native)
    DrawCityMapTerrainTile(city, tile, zoom, ground, screen_x, ground_y, geometry);
  int baseline = ground_y;
  if (terrain == CITY_TERRAIN_SHAPE_13) {
    if (zoom == CITY_MAP_ZOOM_SMALL)
      DrawCityMapSpriteAtBaseline(drawing, CITY_SPRITE_CATALOG_SMALL_LAND_EDGE, screen_x, ground_y, 0);
    baseline -= CityMapHeightStep(zoom);
  }
  int flags = tp->flags;
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, baseline, 0);
  DrawCityMapPlacementTile(city, tile);
  if (((zoom == CITY_MAP_ZOOM_SMALL ? flags : tp->flags) & TF_POWER_SERVICE) == TF_POWERABLE) {
    CityPoint extent = CityMapSpriteExtent(drawing, sprite);
    int half_width = 4 << zoom;
    DrawCityMapSprite(drawing, 500 * zoom + CITY_SPRITE_CATALOG_SMALL_POWER_WARNING,
        screen_x + (extent.x >> 1) - half_width, ground_y - half_width, 0);
  }
}
