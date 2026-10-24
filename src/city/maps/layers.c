// TileInfo category conversion and remains storage (zero-based bits):
// Shared fields are defined in tile_storage.h; each family's opening comment describes its terrain/zone payload.
// TK_RUBBLE is one kind with appearance 0..3 in terrain[4..5]; terrain[6..7] has no rubble payload.
// TK_RADIOACTIVE_WASTE has no additional appearance field. Both are dry, retain ground in terrain[0..3] and
// zone-or-slope in zone[0..4], and have no family-specific zone[5..7] payload. Hazard lifetimes belong to City.
// The replacement boundary clears incompatible species/connections/facing/links in terrain[4..7] and growth/timing
// in zone[5..7], while retaining compatible metadata. Dry-building stages and bridge-head style share altitude's
// water-height field and must be cleared when their old interpretation stops applying. Callers install new payloads.
// Kind classes and module bindings live in kinds.c, never in TileInfo bits.

#include "city/maps/buildings.h"
#include "city/maps/ground.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/maps/trees.h"
#include "city/military.h"
#include "city/maps/tile_storage.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/maps/map_types.h"
#include "city/maps/map.h"
#include "city/growth/growth.h"
#include "city/saveload/map_format.h"

// Shared replacement and footprint operations; field access stays in layers.h.

// Test the requested corner for a valid building or highway footprint tile. Uses the family's native metadata and
// supported corner enum; does not reconstruct geometry from artwork IDs.
bool IsCityFootprintCorner(const City *city, TileIndex tile, int corner) {
  int kind = ReadTile(city, tile);
  if (IsTileKindBuilding(kind)) return IsCityBuildingCorner(city, tile, corner);
  if (IsTileKindHighwayBridge(kind) || IsTileKindMultiHighway(kind)) {
    int zone = IsTileKindHighwayBridge(kind) ? CityBridgeHighwayZone(city, tile) : ReadZoneByte(city, tile);
    int links = zone >> 6;
    return (((links & 1) << 1) | ((links ^ (links >> 1)) & 1)) == corner;
  }
  if (IsTileKindStraightHighwayFamily(kind)) {
    CityRect bounds = CityHighwayFootprintBounds(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
    bool bottom = corner == CITY_BUILDING_CORNER_BOTTOM_LEFT || corner == CITY_BUILDING_CORNER_BOTTOM_RIGHT;
    CityPoint point = MapTilePt(city, tile);
    return point.x == (corner & 2 ? bounds.right - 1 : bounds.left) &&
        point.y == (bottom ? bounds.bottom - 1 : bounds.top);
  }
  return (ReadFootprintCorners(city, tile) & (CITY_ZONE_CORNER_0 << ((city->geography.rotation + corner) & 3))) != 0;
}

// Decode footprint corner bits for a valid native tile across building and highway families. Returns a value derived
// from stored links/rotation; callers must not treat the zone upper nibble as a generic corner mask.
int ReadFootprintCorners(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  u32 classes = ReadCityTileClasses(kind);
  if ((classes & TCLASS_HIGHWAY) || IsTileKindHighwayBridge(kind)) {
    int zone = ReadTileZoneByte(&city->tile[tile]);
    if (IsTileKindHighwayBridge(kind))
      zone = CityBridgeHighwayZone(city, tile);
    else if (IsTileKindStraightHighwayFamily(kind)) {
      CityRect bounds = CityHighwayFootprintBounds(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
      CityPoint point = MapTilePt(city, tile);
      int mask = 0;
      int rotation = city->geography.rotation;
      if (point.x == bounds.left && point.y == bounds.top) mask |= CITY_ZONE_CORNER_0 << rotation;
      if (point.x == bounds.left && point.y == bounds.bottom - 1) mask |= CITY_ZONE_CORNER_0 << ((rotation + 1) & 3);
      if (point.x == bounds.right - 1 &&
          point.y == bounds.bottom - 1) mask |= CITY_ZONE_CORNER_0 << ((rotation + 2) & 3);
      if (point.x == bounds.right - 1 && point.y == bounds.top) mask |= CITY_ZONE_CORNER_0 << ((rotation + 3) & 3);
      return mask;
    }
    return CityHighwayCornerMask(city, zone);
  }
  if (classes & TCLASS_SINGLE_FOOTPRINT) return CITY_ZONE_CORNER_MASK;
  if (kind == TK_CLEAR || IsTileKindExposedWater(kind) || (classes & (TCLASS_TREE | TCLASS_VARIANT |
      TCLASS_POWER | TCLASS_PLAIN_TRANSPORT | TCLASS_GROUND_CROSSING |
          TCLASS_SURFACE_BRIDGE)))
    return 0;
  if (IsTileKindBuilding(kind))
    return CityBuildingCornerMask(city, tile, &city->tile[0].terrain, sizeof(TileInfo));
  return ReadTileZoneByte(&city->tile[tile]) & CITY_ZONE_CORNER_MASK;
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
  TileInfo replacement = *tp;
  replacement.kind = kind;
  if (kind_changed && (CityTileCanStoreBuildingStage(tp) || CityTileCanStoreBuildingStage(&replacement)))
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
  int previous_statistic = CityTileStatisticAt(city, tile);

  // Remove the old occupant and any underpass from the appropriate statistics table.
  if (!(classes & TCLASS_SURFACE_BRIDGE)) {
    int under = ReadCityBridgeUnderpassKind(tp);
    if (under) --city->statistics.count[CityTileStatisticsKind(city, under)];
  }
  if (ReadCityZoneKind(tp) == ZONE_MILITARY) {
    UpdateCityMilitaryTileCounts(city, tp->kind, kind);
  } else {
    --city->statistics.count[previous_statistic];
    ++city->statistics.count[CityTileStatisticsKind(city, kind)];
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
