// TileInfo category conversion and remains storage (zero-based bits):
// Shared fields are defined in tile_storage.h; each family's opening comment describes its terrain/zone payload.
// TK_RUBBLE is one kind with appearance 0..3 in terrain[4..5]; terrain[6..7] has no rubble payload.
// TK_RADIOACTIVE_WASTE has no additional appearance field. Both are dry, retain ground in terrain[0..3] and
// zone-or-slope in zone[0..4], and have no family-specific zone[5..7] payload. Hazard lifetimes belong to City.
// The replacement boundary clears incompatible species/connections/facing/links in terrain[4..7] and growth/timing
// in zone[5..7], while retaining compatible metadata. Dry-building stages and bridge-head style share altitude's
// water-height field and must be cleared when their old interpretation stops applying. Callers install new payloads.
// Kind classes and module bindings live in kinds.c, never in TileInfo bits.

#include "city/maps/layers.h"
#include "city/maps/terrain.h"
#include "city/maps/water.h"

#include "city/economy/facilities.h"
#include "city/engine/random.h"
#include "city/events/events.h"
#include "city/growth/growth.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/networks.h"
#include "city/infrastructure/power_lines.h"
#include "city/infrastructure/tunnels.h"
#include "city/maps/buildings.h"
#include "city/maps/ground.h"
#include "city/maps/labels.h"
#include "city/maps/map.h"
#include "city/maps/map_types.h"
#include "city/maps/tables.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/military.h"
#include "city/presentation.h"
#include "audio/types.h"

// Shared replacement and footprint operations; field access stays in layers.h.

// Decode footprint corner bits for a valid native tile across building and highway families. Returns a value derived
// from stored links/rotation, using CityFootprintCornerMask bits rather than packed zone-byte bits.
int ReadFootprintCorners(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  u32 classes = ReadCityTileClasses(kind);
  if ((classes & TCLASS_HIGHWAY) || IsTileKindHighwayBridge(kind)) return CityHighwayCornerMask(city, tile);
  if (classes & TCLASS_SINGLE_FOOTPRINT) return CITY_FOOTPRINT_CORNERS_ALL;
  if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || (classes & (TCLASS_TREE | TCLASS_VARIANT |
      TCLASS_POWER | TCLASS_PLAIN_TRANSPORT | TCLASS_GROUND_CROSSING |
          TCLASS_SURFACE_BRIDGE)))
    return 0;
  if (IsTileKindBuilding(kind))
    return CityBuildingCornerMask(city, tile);
  return (city->tile[tile].terrain >> CITY_ZONE_METADATA_SHIFT) & CITY_FOOTPRINT_CORNERS_ALL;
}

// Map category replacement

// Convert the old occupant's packed metadata before installing its replacement kind. Ground accessors must still
// see the old kind here. Flags, overlay associations and footprint placement are handled by the caller's operation.
static void ConvertCityTileMetadata(City *city, TileInfo *tp, u8 kind) {
  int previous = tp->kind;
  bool kind_changed = previous != kind;
  u32 old_classes = ReadCityTileClasses(previous), classes = ReadCityTileClasses(kind);
  u32 changed = old_classes ^ classes, shared = old_classes & classes;
  int water_ground = ReadCityWaterGroundKind(tp);
  int shore_ground = IsTileKindShoreBank(previous) ? ReadGroundKind(tp) : CITY_GROUND_GRASS;
  int zone = ReadTileZoneByte(tp);
  int highway_links = zone & (CITY_BUILDING_CONTINUATION_MASK | CITY_HIGHWAY_ORIENTATION_MASK);

  // Footprint/variant bits stop belonging to the old occupant before its ground is decoded.
  if (kind_changed && ((changed & TCLASS_HIGHWAY) || (classes & TCLASS_SINGLE_FOOTPRINT) ||
      ((old_classes | classes) & TCLASS_VARIANT) || IsTileKindBuilding(previous) || IsTileKindBuilding(kind)))
    WriteTileZoneByte(tp, zone & CITY_ZONE_KIND_MASK);

  // Construction progress shares the water-height bits; clear it when either dry building category changes.
  if (kind_changed && (IsTileKindDryBuilding(previous) || IsTileKindDryBuilding(kind)))
    tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;

  if (kind_changed && IsCityBridgeHead(tp)) tp->altitude &= CITY_ALTITUDE_CLEAR_WATER;
  bool was_line = IsTileKindGroundTransport(previous), is_line = IsTileKindGroundTransport(kind);
  if (was_line && !is_line && ReadGroundKind(tp) == CITY_TRANSPORT_GROUND_PAVED)
    WriteGroundKind(tp, CITY_GROUND_GRASS);

  // Species, links and connections share the upper ground nibble. Timing and growth share the upper zone bits.
  bool was_ground = previous == TK_CLEAR || IsTileKindExposedWater(previous) || (old_classes & TCLASS_TREE);
  bool is_ground = kind == TK_CLEAR || IsTileKindExposedWater(kind) || (classes & TCLASS_TREE);
  const u32 metadata_classes = TCLASS_TREE | TCLASS_SURFACE_BRIDGE | TCLASS_POWER |
      TCLASS_PLAIN_TRANSPORT | TCLASS_GROUND_CROSSING;
  if (was_ground != is_ground || (changed & metadata_classes) ||
      (kind_changed && (classes & (TCLASS_PLAIN_TRANSPORT | TCLASS_GROUND_CROSSING))) ||
      IsTileKindPlainPowerLine(previous) != IsTileKindPlainPowerLine(kind))
    tp->terrain &= 15;
  if (shared & TCLASS_HIGHWAY) tp->terrain = (tp->terrain & 15) | highway_links;
  if (kind_changed && !(shared & TCLASS_TREE)) tp->zone &= ~CITY_TILE_GROWTH_MASK;
  if (classes & TCLASS_TREE) InitializeCityTreeMetadata(city, tp, kind);

  if (is_line && !(classes & TCLASS_WATER)) {
    if (!was_line) {
      u64 paving = (1ull << CITY_FIX_TRANSPORT_PAVING) | (1ull << CITY_FIX_NATIVE_LANDSCAPE);
      WriteGroundKind(tp, (city->fixes & paving) == paving ?
          CITY_GROUND_RECOVERY_GRASS_FIRST : CITY_GROUND_GRASS);
    }
  } else if (kind == TK_CLEAR && !(old_classes & TCLASS_SURFACE_BRIDGE)) {
    if (city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE)) BeginCityGroundRecovery(city, tp);
    else WriteGroundKind(tp, CITY_GROUND_GRASS);
  } else if (!is_ground && !(classes & (TCLASS_SURFACE_BRIDGE | TCLASS_WATER))) {
    int ground = ReadGroundKind(tp);
    if (ground >= CITY_GROUND_ROUGH && ground <= CITY_GROUND_RECOVERY_LAST)
      WriteGroundKind(tp, CITY_GROUND_GRASS);
  }
  if (classes & TCLASS_WATER)
    tp->terrain = (tp->terrain & 240) | (IsTileKindShoreBank(kind) ? shore_ground :
        IsTileKindExposedWater(kind) ? 0 :
        (water_ground ? water_ground : TK_WATER) - TK_WATER);
  else if (old_classes & TCLASS_WATER) WriteGroundKind(tp, CITY_GROUND_GRASS);
}

// Keep statistics and metadata conversion together at the kind-write boundary. Same-kind writes deliberately run
// conversion too: clear-ground recovery and some transport metadata resets are part of the established behavior.
void ReplaceCityTileAndStatistics(City *city, TileIndex tile, u8 kind) {
  assert(IsMapTileValid(city, tile));
  InvalidateTile(city, tile);
  TileInfo *tp = &city->tile[tile];
  u32 classes = ReadCityTileClasses(kind);
  int previous_statistic = ReadCityTileStatisticsKey(city, tp);

  // Remove the old occupant and any underpass from the appropriate statistics table.
  if (!(classes & TCLASS_SURFACE_BRIDGE)) {
    int under = ReadCityBridgeUnderpassKind(tp);
    if (under) --city->statistics.count[CityTileStatisticsKey(city, under)];
  }
  if (ReadCityZoneKind(tp) == ZONE_MILITARY) {
    UpdateCityMilitaryTileCounts(city, tp->kind, kind);
  } else {
    --city->statistics.count[previous_statistic];
    ++city->statistics.count[CityTileStatisticsKey(city, kind)];
  }

  ConvertCityTileMetadata(city, tp, kind);
  tp->kind = kind;
}

// Replace the category through the shared operation, then install the caller-selected variant.
// Tile and variant must be valid; the caller invalidates final extents after the complete edit.
void ReplaceCityTileVariant(City *city, TileIndex tile, int kind, int variant) {
  ReplaceCityTileAndStatistics(city, tile, kind);
  TileInfo *tp = &city->tile[tile];
  WriteTileZoneByte(tp, ReadCityZoneKind(tp) |
      ((variant << CITY_TILE_VARIANT_SHIFT) & CITY_TILE_VARIANT_MASK));
}

// Read the selection footprint for removal without changing City or damage. Requires a valid tile; an unresolved
// highway/building anchor returns INVALID_TILE and zero dimensions. Connected span removal can reach beyond this area.
CityTileArea ResolveBulldozeFootprint(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  if (IsTileKindStraightHighwayFamily(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile);
    if (bounds.right == bounds.left || bounds.bottom == bounds.top)
      return (CityTileArea){.start = INVALID_TILE};
    return (CityTileArea){MapTileXY(city, bounds.left, bounds.top),
        bounds.right - bounds.left, bounds.bottom - bounds.top};
  }
  bool highway = IsTileKindMultiHighway(kind);
  if (!IsTileKindBuilding(kind) && !highway) return (CityTileArea){tile, 1, 1};
  TileIndex anchor = highway ? CityHighwayAnchor(city, tile) : GetBuildingStartTile(city, tile);
  if (anchor == INVALID_TILE) return (CityTileArea){.start = INVALID_TILE};
  int size = highway ? 2 : GetBuildingFootprintSize(kind);
  return (CityTileArea){anchor, size, size};
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
      if (IsFlatDryTile(tp))
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
  int kind = city->tile[tile_index].kind;
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
        !IsFlatDryTile(&city->tile[anchor_index]))
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
