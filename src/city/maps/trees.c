// TileInfo storage for trees (zero-based bits):
// kind selects count/legacy density: 6..8 one tree, 9..10 two, 11 three, 12 four; species is not encoded in kind.
// terrain[4..7] is the climate-local species (eight arctic species, twelve otherwise); terrain[0..3] is dry ground
// cover, including snow/desert/recovery, as documented in ground.c. flags[1..2] retains the permanent region.
// zone[5..7] is growth: 0..2 growing, 3 grown, 4..5 dying, 6 dead; 7 has no defined growth stage.
// zone[4] selects slope versus zoning in zone[0..3]. Flat trees can carry zoning until a building replaces them.
// Shore-tree kinds retain bank water in altitude[5..9]. Other tree kinds are dry.
// altitude[0..4] is land height and [10..14] can describe an independent road tunnel below;
// altitude[5..9] is not tree age or construction progress. Shared flags, overlay and underground follow tile_storage.h.

#include "drawing/maps/map_sprites.h"
#include "city/maps/trees.h"
#include "city/maps/terrain.h"
#include "city/maps/tables.h"
#include "city/maps/ground.h"
#include "city/maps/module.h"

#include <assert.h>

#include "game/audio.h"
#include "city/engine/commands.h"
#include "city/engine/random.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/terrain/terrain.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_view.h"
#include "drawing/sprites/sprites.h"

// Select natural tree aging and reseeding. Invalid or reserved values fail before TEST or mutation.
i64 CmdSetTreeLifecycle(City *city, u32 flags, int enabled, int reserved1, int reserved2) {
  if ((enabled != 0 && enabled != 1) || reserved1 || reserved2)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  city->tree_lifecycle = enabled != 0;
  return 1;
}

// Return the number of local tree species for a ground climate. Arctic climates have eight species and other climates
// twelve; snow appearance is reduced to its permanent base climate first.
int CityTreeSpeciesCount(int climate) {
  return TerrainBaseClimate(climate) == CLIMATE_ARCTIC ? 8 : 12;
}

// Convert a climate-local tree species to its artwork species index. Species must already be within
// CityTreeSpeciesCount; this mapping is read-only and performs no random selection.
int CityTreeGlobalSpecies(int climate, int species) {
  climate = TerrainBaseClimate(climate);
  int base = climate == CLIMATE_ARCTIC ? 12 :
      climate >= CLIMATE_TROPICAL && climate <= CLIMATE_DESERT_100 ? 20 : 0;
  return base + species;
}

// Select a climate-appropriate local species from an already supplied random value. Does not advance any generator;
// the result follows the biome's restricted species set.
static int TreeSpeciesFromSeed(int climate, u32 random) {
  climate = TerrainBaseClimate(climate);
  if (climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100) return 7;
  if (climate == CLIMATE_RAINFOREST) return random % 7;
  if (climate == CLIMATE_TROPICAL) return 8 + random % 4;
  return random % CityTreeSpeciesCount(climate);
}

// Draw one landscape random value and map it to a local species for the supplied climate. This changes only the
// landscape RNG stream, so previews must use a supplied seed instead.
int RandomCityTreeSpecies(City *city, int climate) {
  return TreeSpeciesFromSeed(climate, NextLandscapeRandom(&city->landscape_random));
}

// Prepare a tree replacement while tp->kind still names its previous occupant. Shared replacement has already
// cleared incompatible occupant metadata and owns the kind write, statistics and invalidation. Entering trees consumes
// one landscape draw; adding a tree restarts growth, and rocks give way to grass only on entry.
void InitializeCityTreeMetadata(City *city, TileInfo *tp, int kind) {
  bool new_tree = !IsTileKindTree(tp->kind);
  if (new_tree) WriteTreeSpecies(tp, RandomCityTreeSpecies(city, ReadCityTileBaseClimate(city, tp)));
  if (new_tree || CityTreeCount(kind) > CityTreeCount(tp->kind)) WriteTreeGrowth(tp, CITY_TREE_GROWING_1);
  if (new_tree && ReadGroundKind(tp) == CITY_GROUND_ROCKS) WriteGroundKind(tp, CITY_GROUND_GRASS);
}

// Advance or remove the terrain tree at one map cell.
i64 ApplyCityTreeToolAtTile(City *city, TileIndex tile_index, bool remove) {
  assert(IsMapTileValid(city, tile_index));
  const TileInfo *tp = &city->tile[tile_index];
  int kind = tp->kind;
  if (HasTileWater(tp) || kind == TK_RADIOACTIVE_WASTE) return CITY_COMMAND_COST_BLOCKED;
  int replacement;
  if (remove) {
    replacement = TK_CLEAR;
  } else if (IsTileKindBareGround(kind)) {
    replacement = (Sc2kCrtRand(city) & 1) + TK_TREE_FIRST;
  } else if (IsTileKindSparseTree(kind)) {
    replacement = kind + 1;
  } else if (IsTileKindUnbuilt(kind)) {
    replacement = (Sc2kCrtRand(city) & 1) + TK_TREE_DENSE_FIRST;
  } else {
    return CITY_COMMAND_COST_BLOCKED;
  }
  ReplaceCityTileAndStatistics(city, tile_index, replacement);
  InvalidateTile(city, tile_index);
  return true;
}

// Apply twenty randomized tree-tool attempts around a map cell. Coordinates are the admitted brush origin; consumes
// deterministic attempts even on rejected cells and returns whether any attempt changed a tile.
i64 ApplyCityTreeToolBrush(City *city, int x, int y, bool remove) {
  bool changed = false;
  for (int attempt = 0; attempt < 20; ++attempt) {
    int candidate_y = y + (int)(Sc2kCrtRand(city) & 7);
    candidate_y -= (int)(Sc2kCrtRand(city) & 7);
    int candidate_x = x + (int)(Sc2kCrtRand(city) & 7);
    candidate_x -= (int)(Sc2kCrtRand(city) & 7);
    TileIndex tile_index = MapTileXYOrInvalid(city, candidate_x, candidate_y);
    if (tile_index == INVALID_TILE) continue;
    const TileInfo *tp = &city->tile[tile_index];
    if (HasTileWater(tp)) continue;

    int kind = tp->kind;
    int replacement;
    if (remove) {
      if (IsTileKindBareGround(kind))
        continue;
      if (kind == TK_TREE_FIRST)
        replacement = TK_CLEAR;
      else if (IsTileKindUnbuilt(kind))
        replacement = kind - 1;
      else {
        InvalidateTile(city, tile_index);
        continue;
      }
    } else if (IsTileKindBareGround(kind)) {
      replacement = (Sc2kCrtRand(city) & 1) + TK_TREE_FIRST;
    } else if (IsTileKindSparseTree(kind)) {
      replacement = kind + 1;
    } else if (IsTileKindUnbuilt(kind)) {
      replacement = (Sc2kCrtRand(city) & 1) + TK_TREE_DENSE_FIRST;
    } else {
      InvalidateTile(city, tile_index);
      continue;
    }
    ReplaceCityTileAndStatistics(city, tile_index, replacement);
    InvalidateTile(city, tile_index);
    changed = true;
  }
  return changed ? 1 : CITY_COMMAND_COST_BLOCKED;
}

// A shoreline tree occupies the raised land without replacing its water surface.
static bool IsCityTreeShore(const City *city, TileIndex tile) {
  return IsTileKindShoreBank(city->tile[tile].kind) &&
      ReadCityTileGeometry(city, tile).water_surface == CITY_WATER_SURFACE_SHORE;
}

static int TreeKindAtCount(const TileInfo *tp, int count) {
  return IsTileKindShoreBank(tp->kind) ? TK_SHORE_TREE_FIRST + count - 1 : CityTreeTile(count);
}

// Check species, climate, existing trees and terrain for one tree action. Requires a valid tile and decoded options;
// returns zero on admission or a negative gameplay error without mutation or RNG.
static int CityTreePlacementError(const City *city, TileIndex tile, int options) {
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  if (options & CITY_TREE_REMOVE) return IsTileKindTree(kind) ? 0 : CITY_COMMAND_COST_NOTHING_TO_REMOVE;
  int species = options & CITY_TREE_SPECIES_MASK;
  int climate = (options & CITY_TREE_CLIMATE_MASK) >> CITY_TREE_CLIMATE_SHIFT;
  int tile_climate = ReadCityTileBaseClimate(city, tp);
  if (CityTreeGlobalSpecies(climate, 0) != CityTreeGlobalSpecies(tile_climate, 0) ||
      species > CityTreeSpeciesCount(tile_climate)) return CITY_COMMAND_COST_WRONG_TREE_CLIMATE;
  if (IsTileKindTree(kind) && CityTreeCount(kind) >= 4) return CITY_COMMAND_COST_TREE_LIMIT;
  return ReadGroundKind(tp) != CITY_GROUND_ROCKS &&
      (!HasTileWater(tp) || IsCityTreeShore(city, tile)) &&
      IsTileKindUnbuilt(kind) && kind != TK_RADIOACTIVE_WASTE ?
      CITY_COMMAND_COST_OK : CITY_COMMAND_COST_BLOCKED;
}

// Test the same single-tile tree rules used by rectangle pricing and execution. Tile and options must already be
// valid; the predicate neither changes the map nor consumes RNG.
static bool CanPlaceCityTree(const City *city, TileIndex tile, int options) {
  return CityTreePlacementError(city, tile, options) == CITY_COMMAND_COST_OK;
}

// Quote eligible tree edits in the inclusive rectangle using explicit species and removal options. Invalid
// endpoints/options return an admission error; the query changes neither RNG, treasury nor map state.
static CityCommandCost GetCityTreeRectangleCost(const City *city, TileIndex start, TileIndex end, int options) {
  CityCommandCost quote = {.error = CITY_COMMAND_COST_INVALID, .error_tile = INVALID_TILE};
  if (!IsMapTileValid(city, start) || !IsMapTileValid(city, end) ||
      (options & ~(CITY_TREE_SPECIES_MASK | CITY_TREE_REMOVE | CITY_TREE_CLIMATE_MASK)) ||
      ((options & CITY_TREE_CLIMATE_MASK) >> CITY_TREE_CLIMATE_SHIFT) >= CLIMATE_COUNT ||
      (options & CITY_TREE_SPECIES_MASK) > 12)
    return quote;
  CityRect bounds = MapTileRectangle(city, start, end);
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x)
      if (CanPlaceCityTree(city, MapTileXY(city, x, y), options)) quote.cost += CITY_TREE_PLANT_COST;
  quote.error = !quote.cost ? CityTreePlacementError(city, start, options) : CITY_COMMAND_COST_OK;
  return quote;
}

// Apply explicit tree species, climate and removal choices across an inclusive rectangle.
// TEST quotes without RNG; DoCityCommand pays separately from this operation's result.
i64 CmdPlantTreesRect(City *city, u32 flags, TileIndex location, TileIndex end, int options) {
  CityCommandCost quote = GetCityTreeRectangleCost(city, location, end, options);
  if (flags & CITY_COMMAND_TEST) return CityCommandPriceResult(quote);
  if (!(city->fixes & (1ull << CITY_FIX_NATIVE_LANDSCAPE))) return CITY_COMMAND_COST_INVALID;
  if (quote.error) return quote.error;
  if (!quote.cost) return 1;
  CityRect bounds = MapTileRectangle(city, location, end);
  int species = options & CITY_TREE_SPECIES_MASK;
  for (int y = bounds.top; y < bounds.bottom; ++y)
    for (int x = bounds.left; x < bounds.right; ++x) {
      TileIndex tile = MapTileXY(city, x, y);
      if (!CanPlaceCityTree(city, tile, options)) continue;
      int kind = ReadTile(city, tile);
      int replacement = options & CITY_TREE_REMOVE ? ReadCityWaterGroundKind(&city->tile[tile]) :
          TreeKindAtCount(&city->tile[tile], IsTileKindTree(kind) ? CityTreeCount(kind) + 1 : 1);
      ReplaceCityTileAndStatistics(city, tile, replacement);
      if (!(options & CITY_TREE_REMOVE)) {
        int climate = (options & CITY_TREE_CLIMATE_MASK) >> CITY_TREE_CLIMATE_SHIFT;
        int chosen = species ? species - 1 : RandomCityTreeSpecies(city, climate);
        WriteTreeSpecies(&city->tile[tile], chosen);
      }
      InvalidateTile(city, tile);
    }
  PlayCitySound(city, location, CITY_SOUND_TREE_OR_ZONE_PLACEMENT);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  return true;
}

// OpenTTD's PlaceTree selects count, growth and ground from separate bits of one random word. Seven is clamped to dead,
// giving that final stage twice the probability of each other stage. Keep the terrain generator's forest footprint.
void GenerateCityTree(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  u32 random = NextLandscapeRandom(&city->landscape_random);
  int count = 1 + ((random >> 22) & 3);
  int growth = IntMin((random >> 16) & 7, CITY_TREE_DEAD);
  TileInfo *tp = &city->tile[tile];
  int climate = ReadCityTileBaseClimate(city, tp);
  ReplaceCityTileAndStatistics(city, tile, CityTreeTile(count));
  WriteTreeSpecies(tp, TreeSpeciesFromSeed(climate, random >> 24));
  WriteTreeGrowth(tp, growth);
  if (!CityClimateHasGroundCover(climate))
    WriteGroundKind(tp, random & (1 << 28) ? CITY_GROUND_ROUGH : CITY_GROUND_GRASS);
}

// Check whether a valid tile's climate permits natural tree spreading. The predicate reads permanent climate only; it
// does not select or plant a tree.
static bool CityTreesCanSpread(const City *city, const TileInfo *tp) {
  int climate = ReadCityTileBaseClimate(city, tp);
  return climate < CLIMATE_DESERT_50 || climate > CLIMATE_DESERT_100;
}

// Check whether the supplied tile is clear, unzoned, dry and recovered enough for landscape growth.
static bool CanPlantCityTree(const City *city, const TileInfo *tp) {
  return tp->kind == TK_CLEAR && !ReadCityZoneKind(tp) &&
      ReadGroundKind(tp) <= CITY_GROUND_ROUGH && CityTreesCanSpread(city, tp);
}

// Attempt one deterministic landscape tree planting, optionally restricted to rainforest. Consumes landscape RNG even
// when the chosen tile is rejected; updates tile statistics and damage only on an eligible site.
static void PlantRandomCityTree(City *city, bool rainforest) {
  u32 random = NextLandscapeRandom(&city->landscape_random);
  TileIndex tile = random & (city->map_cell_count - 1);
  TileInfo *tp = &city->tile[tile];
  if ((rainforest && ReadCityTileBaseClimate(city, tp) != CLIMATE_RAINFOREST) ||
      !CanPlantCityTree(city, tp))
    return;
  ReplaceCityTileAndStatistics(city, tile, TK_TREE_FIRST);
  WriteTreeSpecies(tp, TreeSpeciesFromSeed(ReadCityTileBaseClimate(city, tp),
      random >> 24));
}

// OpenTTD's global tree tick seeds empty land independently of existing trees. The byte countdown and small-map tick
// skipping keep density proportional to map area. Climate is per tile here, so rainforest attempts reject other tiles.
// Requires initialized native landscape state; applies deterministic species/age initialization and updates its
// retained tick counter.
void SeedCityTrees(City *city) {
  if (!city->tree_lifecycle) return;
  int cells = city->map_cell_count;
  int skip = IntMax(1, cells >> 12);
  if (skip < 16 && (city->landscape_tick & (16 / skip - 1))) return;
  int scale = IntMax(1, cells >> 16);
  for (int attempt = 0; attempt < scale; ++attempt) PlantRandomCityTree(city, true);
  int previous = city->tree_tick_counter;
  city->tree_tick_counter -= scale;
  if (previous <= city->tree_tick_counter) PlantRandomCityTree(city, false);
}

// Advance a tree tile after ground recovery on its scheduled growth visit. The caller owns traversal and cadence;
// saplings mature with lifecycle disabled, while spontaneous growth and death use only the landscape RNG.
static void GrowCityTrees(City *city, TileIndex tile) {
  TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  int growth = ReadTreeGrowth(tp);
  // Planted saplings mature even when spontaneous birth and death are disabled.
  if (!city->tree_lifecycle && growth >= CITY_TREE_GROWN) return;
  if (growth == CITY_TREE_GROWN) {
    u32 random = NextLandscapeRandom(&city->landscape_random);
    int action = random & 7;
    bool can_spread = CityTreesCanSpread(city, tp);
    if (action == 0 || (!can_spread && ReadTreeSpecies(tp) != 7)) {
      WriteTreeGrowth(tp, CITY_TREE_DYING_1);
    } else if (action == 1 && can_spread && CityTreeCount(kind) < 4) {
      ReplaceCityTileAndStatistics(city, tile, TreeKindAtCount(tp, CityTreeCount(kind) + 1));
      WriteTreeGrowth(tp, CITY_TREE_GROWING_1);
    } else if (action == 1 || action == 2) {
      // OpenTTD spreads to one of eight neighbours, only onto recovered grass
      // or rough ground. Deserts keep cacti isolated.
      if (!can_spread) return;
      static const i8 kTreeNeighborX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
      static const i8 kTreeNeighborY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
      int direction = NextLandscapeRandom(&city->landscape_random) & 7;
      TileIndex neighbor = GetRelativeTileOrInvalid(city, tile, kTreeNeighborX[direction], kTreeNeighborY[direction]);
      if (neighbor == INVALID_TILE) return;
      if (CanPlantCityTree(city, &city->tile[neighbor]) &&
          ReadCityTileBaseClimate(city, &city->tile[neighbor]) == ReadCityTileBaseClimate(city, tp)) {
        ReplaceCityTileAndStatistics(city, neighbor, TK_TREE_FIRST);
        WriteTreeSpecies(&city->tile[neighbor], ReadTreeSpecies(tp));
      }
      return;
    } else return;
  } else if (growth == CITY_TREE_DEAD) {
    if (!CityTreesCanSpread(city, tp)) {
      // Isolated desert trees regenerate rather than disappearing forever.
      WriteTreeGrowth(tp, CITY_TREE_GROWING_1);
    } else if (CityTreeCount(kind) > 1) {
      ReplaceCityTileAndStatistics(city, tile, TreeKindAtCount(tp, CityTreeCount(kind) - 1));
      WriteTreeGrowth(tp, CITY_TREE_GROWN);
    } else {
      int ground = ReadGroundKind(tp);
      ReplaceCityTileAndStatistics(city, tile, ReadCityWaterGroundKind(tp));
      // Natural death retains its ground; demolition deliberately clears it.
      WriteGroundKind(tp, ground);
    }
  } else {
    WriteTreeGrowth(tp, growth + 1);
  }
  InvalidateTile(city, tile);
}

// Visit the tile selected by the economy phase after its radioactive-waste decay attempt. Both remaining waste
// and the clear ground it can become take the non-tree admission branch. Keep CRT draws independent of native growth;
// at map edges a rejected direction still probes the original tile, as in the stock algorithm.
void GrowCityStockTrees(City *city, TileIndex tile_index) {
  const TileInfo *tp = &city->tile[tile_index];
  int old_tile = tp->kind;
  CityPoint point = MapTilePt(city, tile_index);
  int x = point.x, y = point.y;
  if (city->tree_lifecycle || HasTileWater(tp))
    return;
  if (!IsTileKindTreeOrPark(old_tile)) {
    u32 draw = Sc2kCrtRand(city);
    if (draw & 0xF)
      return;
  }
  if (IsTileKindTree(old_tile) && old_tile != TK_TREE_LAST) {
    ReplaceCityTileAndStatistics(city, tile_index, old_tile + 1);
  }
  u32 direction = Sc2kCrtRand(city) & 3;
  if (direction == CITY_TREE_PROBE_SOUTH && y < city->map_height - 1)
    ++y;
  else if (direction == CITY_TREE_PROBE_NORTH && y > 0)
    --y;
  else if (direction == CITY_TREE_PROBE_EAST && x < city->map_width - 1)
    ++x;
  else if (direction == CITY_TREE_PROBE_WEST && x > 0)
    --x;
  TileIndex neighbor_index = MapTileXY(city, x, y);
  u32 neighbor_tile = ReadTile(city, neighbor_index);
  if (HasTileWater(&city->tile[neighbor_index]) || !IsTileKindUnbuilt(neighbor_tile) ||
      neighbor_tile == TK_TREE_LAST || neighbor_tile == TK_RADIOACTIVE_WASTE)
    return;
  ReplaceCityTileAndStatistics(city, neighbor_index,
      IsTileKindTree(neighbor_tile) ? neighbor_tile + 1 : TK_TREE_FIRST);
}

// OpenTTD tree_land.h, GPL-2.0, revision d48d693d7ff09b643846e23a5f033cf3582ab27f.
// Art indices refer to the seven-frame banks in native landscape.dat.
#define CITY_TREE_MIXED_ART_BANK(base) \
  {base, base, base, base}, \
  {base, base, base + 3, base + 5}, \
  {base, base + 6, base, base}, \
  {base, base + 5, base + 4, base}, \
  {base + 5, base + 5, base + 5, base}, \
  {base + 5, base, base + 6, base + 4}, \
  {base + 5, base + 6, base + 5, base + 3}, \
  {base + 5, base + 5, base + 5, base}, \
  {base + 6, base + 6, base + 6, base + 6}, \
  {base + 6, base + 6, base, base}, \
  {base + 6, base + 5, base + 6, base}, \
  {base + 6, base + 6, base + 5, base}, \
  {base + 3, base + 5, base + 4, base + 3}, \
  {base + 3, base + 4, base + 3, base}, \
  {base + 3, base + 3, base + 3, base}, \
  {base + 3, base + 3, base + 3, base + 4}, \
  {base + 4, base + 5, base + 1, base + 3}, \
  {base + 4, base + 2, base + 7, base + 6}, \
  {base + 4, base + 3, base + 2, base + 1}, \
  {base + 4, base + 2, base + 3, base + 7}, \
  {base + 1, base + 1, base + 7, base + 4}, \
  {base + 1, base + 2, base + 2, base}, \
  {base + 1, base + 7, base + 2, base + 1}, \
  {base + 1, base, base + 3, base + 7}, \
  {base + 2, base + 5, base + 7, base + 3}, \
  {base + 2, base + 1, base + 2, base + 6}, \
  {base + 2, base + 7, base + 2, base + 1}, \
  {base + 2, base + 4, base + 3, base + 7}, \
  {base + 7, base + 6, base + 7, base + 3}, \
  {base + 7, base + 2, base + 7, base + 5}, \
  {base + 7, base + 7, base + 2, base + 1}, \
  {base + 7, base + 4, base + 3, base + 7}
#define CITY_TREE_SINGLE_ART_BANK(art) \
  {art, art, art, art}, {art, art, art, art}, {art, art, art, art}, {art, art, art, art}

const u8 kCityTreeArtLayouts[196][4] = {
  {6, 7, 8, 9},
  {6, 9, 10, 11},
  {6, 10, 7, 11},
  {6, 6, 8, 10},
  {8, 9, 7, 6},
  {8, 11, 8, 8},
  {8, 6, 6, 10},
  {8, 11, 9, 7},
  {11, 8, 11, 11},
  {11, 7, 6, 6},
  {11, 10, 6, 6},
  {11, 9, 7, 9},
  {0, 6, 8, 1},
  {0, 2, 11, 4},
  {0, 6, 3, 10},
  {0, 9, 4, 6},
  {4, 7, 8, 0},
  {4, 5, 7, 2},
  {4, 11, 6, 3},
  {4, 3, 10, 6},
  {2, 2, 0, 2},
  {2, 3, 2, 2},
  {2, 5, 2, 2},
  {2, 2, 2, 2},
  {5, 0, 1, 2},
  {5, 3, 4, 2},
  {5, 2, 3, 0},
  {5, 5, 2, 3},
  {1, 4, 4, 2},
  {1, 1, 2, 0},
  {1, 5, 2, 2},
  {1, 2, 1, 2},
  {12, 11, 13, 12},
  {12, 17, 12, 7},
  {12, 12, 12, 18},
  {12, 15, 10, 14},
  {14, 14, 16, 14},
  {14, 16, 13, 14},
  {14, 12, 15, 14},
  {14, 13, 18, 17},
  {16, 14, 16, 6},
  {16, 16, 8, 9},
  {16, 12, 18, 16},
  {16, 16, 16, 15},
  {18, 18, 12, 8},
  {18, 17, 18, 6},
  {18, 12, 18, 15},
  {18, 15, 17, 18},
  CITY_TREE_MIXED_ART_BANK(19),
  {37, 38, 37, 39},
  {37, 41, 43, 37},
  {37, 37, 46, 50},
  {37, 42, 37, 37},
  {38, 38, 37, 39},
  {38, 41, 38, 38},
  {38, 38, 43, 52},
  {38, 42, 38, 51},
  {41, 38, 41, 40},
  {41, 41, 38, 46},
  {41, 37, 43, 41},
  {41, 50, 38, 41},
  {42, 42, 37, 52},
  {42, 43, 38, 42},
  {42, 37, 50, 41},
  {42, 42, 38, 52},
  {46, 46, 42, 42},
  {46, 52, 38, 46},
  {46, 38, 50, 46},
  {46, 50, 38, 51},
  {51, 51, 42, 52},
  {51, 38, 39, 41},
  {51, 38, 50, 46},
  {51, 50, 51, 52},
  {50, 50, 40, 38},
  {50, 50, 37, 38},
  {50, 38, 50, 50},
  {50, 50, 51, 52},
  {48, 48, 49, 48},
  {48, 49, 48, 49},
  {48, 49, 49, 48},
  {48, 48, 48, 49},
  {44, 35, 44, 36},
  {44, 37, 44, 45},
  {44, 44, 47, 35},
  {44, 47, 44, 44},
  {47, 47, 44, 35},
  {47, 41, 44, 47},
  {47, 44, 47, 36},
  {47, 47, 44, 45},
  {35, 35, 47, 36},
  {35, 42, 45, 35},
  {35, 36, 52, 35},
  {35, 35, 44, 51},
  {52, 36, 44, 52},
  {52, 52, 44, 35},
  {52, 36, 52, 35},
  {52, 52, 47, 51},
  CITY_TREE_SINGLE_ART_BANK(53),
  CITY_TREE_SINGLE_ART_BANK(54),
  CITY_TREE_SINGLE_ART_BANK(55),
  CITY_TREE_SINGLE_ART_BANK(56),
  CITY_TREE_SINGLE_ART_BANK(57),
  CITY_TREE_SINGLE_ART_BANK(58),
  CITY_TREE_SINGLE_ART_BANK(59),
  CITY_TREE_SINGLE_ART_BANK(60),
  CITY_TREE_SINGLE_ART_BANK(61),
  CITY_TREE_MIXED_ART_BANK(27),
};
#undef CITY_TREE_MIXED_ART_BANK
#undef CITY_TREE_SINGLE_ART_BANK

static const u8 kCityTreePositions[4][4][2] = {
  {{9, 3}, {1, 8}, {0, 0}, {8, 9}},
  {{4, 4}, {9, 1}, {6, 9}, {0, 9}},
  {{9, 1}, {0, 9}, {6, 6}, {3, 0}},
  {{3, 9}, {8, 2}, {9, 9}, {1, 5}},
};

int CityTreeGroundOffset(int terrain, int zoom) {
  int corners = kTerrainRaisedCorners[terrain & 15];
  return (__builtin_popcount(corners) * CityMapHeightStep(zoom) + 2) >> 2;
}

// Draw the tree silhouettes over the already-painted ground. The caller applies visibility policy and supplies
// camera-relative slope/screen coordinates. Drawing decides whether each sprite is collected or rasterized now.
static void DrawCityTrees(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y, int land_shape) {
  Drawing *drawing = city->drawing;
  const TileInfo *tp = &city->tile[tile];
  int kind = tp->kind;
  int rotation = CityMapCameraRotation(drawing);
  int scale = 1 << zoom, count = CityTreeCount(kind);
  int climate = ReadCityTileBaseClimate(city, tp);
  int species = CityTreeGlobalSpecies(climate, ReadTreeSpecies(tp));
  int hash = __builtin_popcount(tile + MapTileX(city, tile) * 16 + MapTileY(city, tile) * 16);
  int layout = species * 4 + (hash & 3);
  if (species >= 12 && species < 20 && ReadTileSnowCoverage(tp) >= 3)
    layout += 164 - 12 * 4;
  const u8 *art = kCityTreeArtLayouts[layout];
  const u8 (*layout_positions)[2] = kCityTreePositions[(hash >> 2) & 3];
  CityPoint positions[4];
  bool shore = IsTileKindShoreTree(kind);
  int corners = kTerrainRaisedCorners[land_shape & 15];
  if (shore && !corners) return;
  for (int tree = 0; tree < count; ++tree) {
    if (shore) {
      // The supplied slope is camera-relative. Keep every trunk close to a raised land corner.
      static const u8 kCornerX[] = {0, 0, 16, 16}, kCornerY[] = {0, 16, 16, 0};
      int choice = (hash + tree) % __builtin_popcount(corners), corner = 0;
      for (; corner < 4; ++corner) if ((corners & (1 << corner)) && !choice--) break;
      int inset_x = 1 + (layout_positions[tree][0] & 3);
      int inset_y = 1 + (layout_positions[tree][1] & 3);
      positions[tree] = (CityPoint){kCornerX[corner] ? 16 - inset_x : inset_x,
          kCornerY[corner] ? 16 - inset_y : inset_y};
    } else {
      CityPoint offset = CityMapCameraVector(rotation,
          layout_positions[tree][0] - 8, layout_positions[tree][1] - 8);
      positions[tree] = (CityPoint){offset.x + 8, offset.y + 8};
    }
  }
  int tree_baseline = ground_y - 4 * scale -
      (shore ? 0 : CityTreeGroundOffset(land_shape, zoom));
  bool drawn[4] = {false};
  for (int tree = 0; tree < count; ++tree) {
    int next = -1;
    for (int i = 0; i < count; ++i)
      if (!drawn[i] && (next < 0 || positions[i].x + positions[i].y < positions[next].x + positions[next].y))
        next = i;
    drawn[next] = true;
    int x = positions[next].x, y = positions[next].y;
    // As in OpenTTD, only the newest tree uses the tile's changing age.
    int growth = next == count - 1 ? ReadTreeGrowth(tp) : CITY_TREE_GROWN;
    int sprite = MAP_LANDSCAPE_SPRITE_FIRST + zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        CITY_LANDSCAPE_TREE_FIRST - CITY_LANDSCAPE_GROUND_FIRST + art[next] * 7 + growth;
    int height = 0;
    if (shore) {
      int north = (corners & 1 ? 16 - x : 0) + (corners & 8 ? x : 0);
      int south = (corners & 2 ? 16 - x : 0) + (corners & 4 ? x : 0);
      height = (CityMapHeightStep(zoom) * (north * (16 - y) + south * y) + 128) >> 8;
    }
    DrawCityMapTreeAtBaseline(drawing, sprite, screen_x + (y - x) * scale / 4,
        tree_baseline + ((x + y) * scale >> 3) - height, positions[next]);
  }
}

// Trees share natural terrain painting, then add their own silhouettes only when that layer is visible.
static bool DrawCityTreeTile(City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry) {
  if (!DrawCityGroundTile(city, tile, zoom, screen_x, ground_y, geometry)) return false;
  Drawing *drawing = city->drawing;
  if (CityMapTileDetailsVisible(drawing) && CityMapLayerVisible(drawing, CITY_MAP_LAYER_TREES))
    DrawCityTrees(city, tile, zoom, screen_x, ground_y, geometry->land_shape);
  return true;
}

static void TickTreeTile(City *city, TileIndex tile) {
  RefreshCityTileGroundCover(city, tile, false);
  CityPoint point = MapTilePt(city, tile);
  u32 cycle = 11 * point.x + 9 * point.y + (city->landscape_tick >> 8);
  AdvanceCityGroundRecovery(city, tile);
  if ((cycle & 15) == 15) GrowCityTrees(city, tile);
}

const TileModule kCityTreeModule = {
  .tick = TickTreeTile, .draw = DrawCityTreeTile, .draw_ground = DrawCityGroundSurface,
};
