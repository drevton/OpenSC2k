// TileInfo storage for clear ground and ground cover beneath trees/transport (zero-based bits):
// kind is TK_CLEAR for empty dry land; the other families retain their own kind and upper terrain payload.
// TK_CLEAR has no payload in terrain[4..7].
// terrain[0..3]: 0 grass, 1..4 snow at 25/50/75/100%, 5 rough, 6 rocks, 7..10 recovery, 11/12 desert half/full,
// 13 shore ground. Value 5 means paving on ground transport instead of rough. Ground and snow are not separate bits.
// zone[0..3] is zoning when zone[4] is clear, otherwise the slope; clear-ground zone[5..7] is a modulo-eight
// recovery counter. Trees use those three bits for growth instead; their recovery timing comes from the scan cycle.
// flags[1..2] retains the permanent climate region; displayed snow/desert coverage comes from terrain[0..3].
// altitude keeps land/tunnel heights. Dry clear ground has no use for altitude[5..9]; it is not a water-presence test.
// Water kinds use the low terrain nibble differently and are handled by water.c. Other fields follow tile_storage.h.

#include "city/maps/buildings.h"
#include "city/maps/ground.h"
#include "city/maps/module.h"

#include <assert.h>

#include "city/engine/random.h"
#include "city/growth/growth.h"
#include "city/infrastructure/infrastructure.h"
#include "city/maps/layers.h"
#include "city/maps/trees.h"
#include "city/maps/water.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/terrain/terrain.h"
#include "drawing/maps/map_terrain.h"
#include "drawing/maps/map_view.h"
#include "game/state.h"

// Ground and coverage occupy the low nibble, including exposed bank recovery.
// Other wet tiles use it for their underlying water kind instead.
int ReadGroundKind(const TileInfo *tp) {
  return HasTileWater(tp) && !IsTileKindShoreBank(tp->kind) ? CITY_GROUND_GRASS : tp->terrain & 15;
}

void WriteGroundKind(TileInfo *tp, int kind) {
  assert(kind >= 0 && kind <= CITY_GROUND_SHORE);
  tp->terrain = (tp->terrain & 240) | kind;
}

void BeginCityGroundRecovery(City *city, TileInfo *tp) {
  WriteGroundKind(tp, city->settings.timer_mode == CITY_TIMER_STOPPED ? CITY_GROUND_GRASS :
      CITY_GROUND_RECOVERY_GRASS_FIRST);
  WriteGroundRecoveryCounter(tp, 0);
}

// Explicit clearing and newly exposed beds stay bare even in the terrain editor.
void BeginCityBareGroundRecovery(City *city, TileInfo *tp) {
  WriteGroundKind(tp, CityClimateHasGroundCover(ReadTileClimate(city, tp)) ?
      CITY_GROUND_RECOVERY_FIRST : CITY_GROUND_RECOVERY_GRASS_FIRST);
  WriteGroundRecoveryCounter(tp, 0);
}

// OpenTTD 15.3's rock walk consumes its budget on revisits and rejected moves, retaining the last valid tile rather
// than jumping across unsuitable land.
static void GenerateCityRocks(City *city, TileIndex tile, int length) {
  assert(IsMapTileValid(city, tile));
  TileInfo *tp = &city->tile[tile];
  int climate = ReadTileClimate(city, tp);
  if (tp->kind != TK_CLEAR ||
      (climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100)) return;
  for (;;) {
    WriteGroundKind(tp, CITY_GROUND_ROCKS);
    TileIndex next;
    for (;;) {
      if (--length == 0) return;
      int direction = NextLandscapeRandom(&city->landscape_random) & 3;
      next = GetRelativeTileOrInvalid(city, tile, (direction == 0) - (direction == 2),
          (direction == 1) - (direction == 3));
      if (next == INVALID_TILE) continue;
      const TileInfo *np = &city->tile[next];
      if (np->kind != TK_CLEAR) continue;
      int next_climate = ReadTileClimate(city, np);
      if (next_climate >= CLIMATE_DESERT_50 && next_climate <= CLIMATE_DESERT_100) continue;
      break;
    }
    tile = next;
    tp = &city->tile[tile];
  }
}

// Add rough tiles and rock clusters after climate and generated trees are initialized. The caller invalidates
// the complete map first; all random choices use the shared landscape stream in their original order.
void GenerateCityGround(City *city) {
  TileIndex cells = city->map_cell_count;
  // OpenTTD 15.3 scatters 1024..2047 individual rough tiles and seeds 128..255
  // rock clusters, scaled to a 256-square map.
  int rough = (int)((i64)(1024 + (NextLandscapeRandom(&city->landscape_random) & 1023)) * cells >> 16);
  int rocks = (int)((i64)(128 + (NextLandscapeRandom(&city->landscape_random) & 127)) * cells >> 16);
  for (int attempt = 0; attempt < rough; ++attempt) {
    TileIndex tile = NextLandscapeRandom(&city->landscape_random) % cells;
    TileInfo *tp = &city->tile[tile];
    int climate = ReadTileClimate(city, tp);
    if (tp->kind == TK_CLEAR &&
        (climate < CLIMATE_DESERT_50 || climate > CLIMATE_DESERT_100))
      WriteGroundKind(tp, CITY_GROUND_ROUGH);
  }
  for (int patch = 0; patch < rocks; ++patch) {
    u32 random = NextLandscapeRandom(&city->landscape_random);
    GenerateCityRocks(city, random % cells, 5 + ((random >> 16) & 15));
  }
}

// Test whether a valid neighboring building is developed enough to encourage paved transport ground. Uses kind,
// construction stage and land value only; does not start construction or edit ground.
static bool CityBuildingEncouragesPaving(const City *city, TileIndex index) {
  const TileInfo *tp = &city->tile[index];
  if (ReadCityBuildingConstructionStage(tp)) return false;
  int kind = tp->kind;
  enum { PAVING_LAND_VALUE_MINIMUM = 128 };
  if (IsTileKindZoneBuilding(kind) &&
      kTileZoneCategory[kind - TK_RESIDENTIAL_FIRST] == CITY_BUILDING_DEVELOPED &&
      ReadLandValue(city, index) >= PAVING_LAND_VALUE_MINIMUM) return true;
  // Quality is a property of the building kind, not its footprint size.
  return (kind >= TK_LUXURY_HOMES_FIRST && kind <= TK_LUXURY_HOMES_LAST) ||
      kind == TK_SMALL_OFFICE_BUILDING || kind == TK_OFFICE_BUILDING_1X1 ||
      (kind >= TK_NICE_APARTMENTS_2X2_1 && kind <= TK_CONDOMINIUM_2X2_3) ||
      (kind >= TK_OFFICE_BUILDING_2X2_1 && kind <= TK_OFFICE_BUILDING_2X2_6) ||
      (kind >= TK_FACTORY_2X2_3 && kind <= TK_FACTORY_2X2_6) ||
      (kind >= TK_LARGE_APARTMENT_BUILDING_3X3_1 && kind <= TK_THEATRE_SQUARE) ||
      (kind >= TK_OFFICE_TOWER_3X3_2 && kind <= TK_OFFICE_TOWER_3X3_3) ||
      kind == TK_HISTORIC_OFFICE_BUILDING || kind == TK_CORPORATE_HEADQUARTERS ||
      kind == TK_LARGE_FACTORY || kind == TK_INDUSTRIAL_THINGAMAJIG || kind == TK_FACTORY_3X3;
}

// Road edges occupy the low nibble; rail edges the high nibble. Crossings must inherit along the actual route, rather
// than from a different network. Tile must be a valid supported transport/crossing; returns stored directional paving
// connectivity without changing ground.
static int CityPavingConnections(const TileInfo *tp) {
  int kind = tp->kind;
  if (kind == TK_ROAD) return ReadTransportConnections(tp);
  if (kind == TK_RAIL) return ReadTransportConnections(tp) << 4;
  if (IsTileKindGroundCrossing(kind)) {
    int mask = ReadCityCrossingConnections(tp);
    if (kind == TK_ROAD_POWER_CROSSING) return mask;
    if (kind == TK_RAIL_POWER_CROSSING) return mask << 4;
    return mask | ((CONN_ALL ^ mask) << 4);
  }
  assert(kind == TK_HIGHWAY_ROAD || kind == TK_HIGHWAY_RAIL);
  int mask = ReadCityHighwayAxis(tp) ? CONN_EW : CONN_NS;
  return kind == TK_HIGHWAY_RAIL ? mask << 4 : mask;
}

// Determine a transport tile's paving from its stored connections and adjacent development. Requires a valid tile; the
// calculation is read-only and map boundaries are handled by neighbor probes.
static bool CityTransportShouldBePaved(const City *city, TileIndex tile) {
  static const i8 kNeighborX[] = {0, 1, 0, -1}, kNeighborY[] = {-1, 0, 1, 0};
  int connections = CityPavingConnections(&city->tile[tile]), paved = 0;
  for (int side = 0; side < 4; ++side) {
    TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kNeighborX[side], kNeighborY[side]);
    if (neighbor == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[neighbor];
    int direction = CONN_NORTH >> side;
    if (!(connections & (direction | (direction << 4)))) {
      if (CityBuildingEncouragesPaving(city, neighbor)) return true;
      continue;
    }
    int opposite = CONN_NORTH >> ((side + 2) & 3);
    if (!IsTileKindGroundTransport(tp->kind) ||
        ReadGroundKind(tp) != CITY_TRANSPORT_GROUND_PAVED) continue;
    int neighbor_connections = CityPavingConnections(tp);
    if (((connections & direction) && (neighbor_connections & opposite)) ||
        ((connections & (direction << 4)) && (neighbor_connections & (opposite << 4)))) {
      if (++paved == 2) return true;
    }
  }
  return false;
}

// Settle transport-ground inheritance after import when native landscape and paving fixes are enabled. Repeats until
// stable and invalidates changed drawing; this initialization scan is not a routine redraw operation.
void InitializeCityTransportPaving(City *city) {
  u32 required = (1 << CITY_FIX_NATIVE_LANDSCAPE) | (1 << CITY_FIX_TRANSPORT_PAVING);
  if ((city->fixes & required) != required) return;
  bool changed, any_changed = false;
  // Stock imports settle inheritance immediately, independent of scan order.
  // This only adds paving, so each successful pass reduces the unpaved count.
  do {
    changed = false;
    for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
      TileInfo *tp = &city->tile[tile];
      if (IsTileKindGroundTransport(tp->kind) &&
          ReadGroundKind(tp) != CITY_TRANSPORT_GROUND_PAVED && CityTransportShouldBePaved(city, tile)) {
        WriteGroundKind(tp, CITY_TRANSPORT_GROUND_PAVED);
        changed = true;
      }
    }
    any_changed |= changed;
  } while (changed);
  if (any_changed) InvalidateMap(city);
}

// Occasionally update one valid transport tile's paving using landscape RNG. Requires native landscape traversal; the
// paving option gates the effect and changed ground is explicitly invalidated.
void GrowCityTransportGround(City *city, TileIndex tile) {
  if (!(city->fixes & (1 << CITY_FIX_TRANSPORT_PAVING)) ||
      (NextLandscapeRandom(&city->landscape_random) & 3)) return;
  bool pave = CityTransportShouldBePaved(city, tile);
  TileInfo *tp = &city->tile[tile];
  int ground = ReadGroundKind(tp), next = ground;
  if (pave) next = CITY_TRANSPORT_GROUND_PAVED;
  else if (ground == CITY_TRANSPORT_GROUND_PAVED || ground == CITY_GROUND_RECOVERY_LAST) next = CITY_GROUND_GRASS;
  else if (ground >= CITY_GROUND_RECOVERY_FIRST && ground < CITY_GROUND_RECOVERY_LAST) next = ground + 1;
  if (ground == next) return;
  WriteGroundKind(tp, next);
  InvalidateTile(city, tile);
}

// Advance recovery timing and ground appearance for one landscape visit. Requires a valid tile and the caller's
// cycle/tree classification; updates metadata in place and invalidates visible transitions.
void GrowCityGround(City *city, TileIndex tile, u32 cycle, bool tree) {
  TileInfo *tp = &city->tile[tile];
  int ground = ReadGroundKind(tp);
  if (ground < CITY_GROUND_RECOVERY_FIRST || ground > CITY_GROUND_RECOVERY_LAST) return;
  int kind = tp->kind;
  bool bridge = IsTileKindBridge(kind);
  if (tree || bridge) {
    if ((cycle & 7) != 7) return;
  } else {
    int counter = ReadGroundRecoveryCounter(tp);
    WriteGroundRecoveryCounter(tp, (counter + 1) & 7);
    if (counter != 7) return;
  }
  WriteGroundKind(tp, ground == CITY_GROUND_RECOVERY_LAST ? CITY_GROUND_GRASS :
      ground + 1);
  InvalidateTile(city, tile);
}

static int SnowGrassFamily(int climate) {
  if (climate == CLIMATE_SNOW_TEMPERATE_ARCTIC_50) return 0;
  return climate == CLIMATE_SNOW_ARCTIC_TROPICAL_50 ? 1 : -1;
}

// Ground categories and tree age/species are independent of the legacy XBLD
// cover IDs. This path borrows preloaded, prescaled OpenGFX2 sprites.
int CityGroundSpriteForShape(const City *city, TileIndex tile, int zoom, int shape) {
  const TileInfo *tp = &city->tile[tile];
  int climate = ReadCityTileClimate(city, tile), ground = ReadGroundKind(tp);
  int base = TerrainBaseClimate(climate);
  int blend = base >= CLIMATE_TEMPERATE_ARCTIC && base <= CLIMATE_ARCTIC_TROPICAL ?
      base - CLIMATE_TEMPERATE_ARCTIC : -1;
  int biome = base == CLIMATE_ARCTIC ? 1 :
      base >= CLIMATE_TROPICAL && base <= CLIMATE_DESERT_100 ? 2 :
      base == CLIMATE_SIMCITY ? 3 : 0;
  if (shape > 13) shape = 0;
  int first = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM;
  if (ground == CITY_TRANSPORT_GROUND_PAVED && IsTileKindGroundTransport(tp->kind))
    return first + CITY_LANDSCAPE_ROAD_PAVING_FIRST - CITY_LANDSCAPE_GROUND_FIRST + shape;
  int variant = 0;
  if (CityClimateHasGroundCover(climate)) {
    int snow = CityClimateSnowCoverage(climate);
    int target = snow ? ReadTileSnowCoverage(tp) : (climate - CLIMATE_DESERT_50 + 1) << 1;
    int amount = ground >= CITY_GROUND_RECOVERY_FIRST && ground <= CITY_GROUND_RECOVERY_LAST ?
        ground - CITY_GROUND_RECOVERY_FIRST : 4;
    amount = IntMin(target, amount);
    if (!snow) amount = (amount + 1) & ~1; // Upstream desert art is 0%, 50% or 100%.
    if (amount) {
      if (snow && amount < 4) {
        int family = SnowGrassFamily(climate);
        if (family >= 0) return first + CITY_LANDSCAPE_SNOW_GRASS_FIRST - CITY_LANDSCAPE_GROUND_FIRST +
            (family * 2 + amount - 1) * 14 + shape;
      }
      return first + ((snow ? 28 : 32) + amount - 1) * 14 + shape;
    }
    variant = 1;
  } else if (ground >= CITY_GROUND_RECOVERY_FIRST && ground <= CITY_GROUND_RECOVERY_LAST) {
    variant = 1 + IntMax(0, ground - CITY_GROUND_RECOVERY_GRASS_FIRST);
  } else if (ground == CITY_GROUND_ROUGH) {
    variant = 4;
    if (!shape && blend < 0) {
      static const u8 kRoughVariants[] = {0, 1, 2, 3, 4, 0, 1, 2};
      int choice = kRoughVariants[(MapTileX(city, tile) * 7 + MapTileY(city, tile) * 13) & 7];
      if (choice) return first + 36 * 14 + biome * 4 + choice - 1;
    }
  } else if (ground == CITY_GROUND_ROCKS) {
    // OpenGFX2 maps rocky land to rocks_alt, the dense rock layout. The
    // sparse rocks sheet is not an alternate density for generated rocks.
    variant = 6;
  }
  if (blend >= 0) return first + CITY_LANDSCAPE_GRASS_BLEND_FIRST - CITY_LANDSCAPE_GROUND_FIRST +
      (blend * 7 + variant) * 14 + shape;
  return first + (biome * 7 + variant) * 14 + shape;
}

// Draw natural clear/tree ground and its zone wash using prepared camera geometry, without a selection marker.
// Tree silhouettes remain in the tree module; Drawing owns visibility, clipping, collection and final pixels.
bool DrawCityGroundSurface(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *tile_geometry) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  CityTileGeometry geometry = *tile_geometry;
  if (!(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) ||
      (kind != TK_CLEAR && !IsTileKindTree(kind))) return false;
  if (IsTileKindShoreTree(kind)) {
    DrawCityMapTerrainTile(city, tile, zoom, -1, screen_x, ground_y, &geometry);
    return true;
  }
  int zone = ReadCityZoneKind(tp);
  int ground = CityGroundSpriteForShape(city, tile, zoom, geometry.land_shape);
  DrawCityMapTerrainSkirt(city, tile, zoom, screen_x, ground_y,
      geometry.land_height);
  int shape = geometry.land_shape;
  if (shape > CITY_TERRAIN_FLAT && shape <= CITY_TERRAIN_SHAPE_13) {
    int face = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM - CITY_LANDSCAPE_GROUND_FIRST +
        CITY_LANDSCAPE_TERRAIN_FACE_FIRST + shape;
    DrawCityMapTerrainFace(city, tile, zoom, face, screen_x, ground_y,
        CityMapSlopeFaceSides(city, tile, shape, geometry.land_height));
  }
  bool paint_ground = IsCityMapGroundPass(drawing);
  if (paint_ground) DrawCityMapSpriteAtBaseline(drawing, ground, screen_x, ground_y, 0);
  if (paint_ground && !geometry.land_shape && zone)
    DrawCityMapSpriteAtBaseline(drawing, 290 + zone + 500 * zoom, screen_x, ground_y, 0);
  return true;
}

// Whole-tile drawing adds selection once. The depth ground pass borrows only the surface above, so its zone wash
// is painted before tree silhouettes without blending a second selection marker over the same tile.
bool DrawCityGroundTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  if (!DrawCityGroundSurface(city, tile, zoom, screen_x, ground_y, geometry)) return false;
  DrawCityMapPlacementTile(city, tile);
  return true;
}

// Fallback ground artwork keeps park selection around the silhouette, while clear land and hidden details use
// only the placement marker. Geometry and source layout are already projected into the active camera.
void DrawCityGroundTileFallback(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->game->drawing;
  const TileInfo *tp = &city->tile[tile];
  int terrain = CityMapViewTerrainCode(city, tile), zone = ReadCityZoneKind(tp);
  int ground = terrain == CITY_TERRAIN_FLAT && zone != ZONE_NONE ? MapSpriteZoomIndex(290 + zone, zoom) : -1;
  DrawCityMapTerrainTile(city, tile, zoom, ground, screen_x, ground_y, geometry);
  bool details = CityMapTileDetailsVisible(drawing), park = layout == CITY_MAP_TILE_SPRITE_PARK;
  if (!park || !details) DrawCityMapPlacementTile(city, tile);
  else DrawCityBuildingSelection(city, tile, CITY_BUILDING_SELECTION_BEFORE, CityMapDrawOptions(drawing));
  if (layout == CITY_MAP_TILE_SPRITE_CLEAR || !details) return;
  int baseline = ground_y - (terrain == CITY_TERRAIN_SHAPE_13 ? CityMapHeightStep(zoom) : 0);
  if ((city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) &&
      layout >= CITY_MAP_TILE_SPRITE_TREE_FIRST && layout <= CITY_MAP_TILE_SPRITE_TREE_LAST)
    baseline = ground_y - CityTreeGroundOffset(geometry->land_shape, zoom);
  int sprite = MapSpriteZoomIndex(layout, zoom);
  DrawCityMapSpriteAtBaseline(drawing, sprite, screen_x, baseline, 0);
  if (park) DrawCityBuildingSelection(city, tile, CITY_BUILDING_SELECTION_AFTER, CityMapDrawOptions(drawing));
  if ((tp->flags & TF_POWER_SERVICE) == TF_POWERABLE) {
    CityPoint extent = CityMapSpriteExtent(drawing, sprite);
    int half_width = 4 << zoom;
    DrawCityMapSprite(drawing, MapSpriteZoomIndex(CITY_SPRITE_CATALOG_SMALL_POWER_WARNING, zoom),
        screen_x + (extent.x >> 1) - half_width, ground_y - half_width, 0);
  }
}

static bool DrawRemainsTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  Drawing *drawing = city->game->drawing;
  if (city->tile[tile].kind != TK_RADIOACTIVE_WASTE || !(city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE)) ||
      !CityMapTileDetailsVisible(drawing) || geometry->water != CITY_WATER_NONE) return false;
  int sprite = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM -
      CITY_LANDSCAPE_GROUND_FIRST + CITY_LANDSCAPE_RADIOACTIVE_FIRST + geometry->land_shape;
  DrawCityMapTerrainTile(city, tile, zoom, sprite, screen_x, ground_y, geometry);
  DrawCityMapPlacementTile(city, tile);
  return true;
}

static void TickGroundTile(City *city, TileIndex tile) {
  if (FloodCityClearTile(city, tile)) return;
  RefreshCityTileGroundCover(city, tile, false);
  CityPoint point = MapTilePt(city, tile);
  u32 cycle = 11 * point.x + 9 * point.y + (city->landscape_tick >> 8);
  GrowCityGround(city, tile, cycle, false);
}

const TileModule kCityGroundModule = {.tick = TickGroundTile, .draw = DrawCityGroundTile};
const TileModule kCityRemainsModule = {.draw = DrawRemainsTile};
