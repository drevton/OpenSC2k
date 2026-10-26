#ifndef SC2K_CITY_MAPS_WATER_H
#define SC2K_CITY_MAPS_WATER_H

#include "city/types.h"
#include "city/maps/terrain.h"

typedef struct MapRiverSprites MapRiverSprites;

// Exposed water stores its behavior in kind. Occupants retain that ground kind
// in terrain's low nibble so demolition restores the same spreading rule.
static SC2K_FORCE_INLINE int ReadCityWaterGroundKind(const TileInfo *tp) {
  return IsTileKindShoreTree(tp->kind) ? TK_WATER_BANK : IsTileKindExposedWater(tp->kind) ? tp->kind :
      HasTileWater(tp) ? (tp->terrain & 15) + TK_WATER : TK_CLEAR;
}

// Choose the occupied dry/wet kind while retaining supported bridge/power forms. No tile metadata is written.
int CityTileKindForWater(int kind, bool water);

// Native terrain edits preserve the supplied water level and existing salinity.
// Dry land newly covered by water borrows salinity from neighboring water.
// keep_rapids preserves existing sloped river water; landscaping passes false
// so a raised river bed at the water plane becomes dry instead.
// Updates clear/water/river/bank kinds while retaining river behavior independently of salinity.
// Their statistics counter is shared; the caller owns old/new invalidation.
void SetCityTerrainWater(City *city, TileIndex tile, int water_height, int land_shape, bool keep_rapids);

// Convert an admitted dry tile to freshwater river at its land height, preserving its slope. Existing water is
// unchanged. Updates kind/statistics, power connections, climate and neighborhood damage; no payment or RNG.
// The caller supplies a valid tile and owns admission/payment.
void CreateCityRiverTile(City *city, TileIndex tile);
// After a land edit completes, convert adjoining exposed banks on its low edges/corners to dry slopes.
// Requires a valid tile; wet input is ignored. Preserves elevations and damages the bounded neighborhood.
void ReclaimCityShorelineBanks(City *city, TileIndex land);
// A landscape visit can flood clear ground from neighboring open water. Rivers never supply spreading water.
// Requires a valid clear tile; true means its kind, water level, salinity, statistics and damage were updated.
bool FloodCityClearTile(City *city, TileIndex tile);

enum { CITY_CANAL_COST = 100 };

// Applying a level edits the whole map; the editor caller owns its final refresh. Channels require an admitted
// starting tile and nonnegative length; they preserve the original grading and CRT draw sequence.
void ApplyTerrainWaterLevel(City *city, int height);
void AdjustCityTerrainWaterLevel(City *city, int direction);
void CarveTerrainWaterChannel(City *city, TileIndex tile, int length);
enum { CITY_STREAM_DEFAULT_LENGTH = 64 };
// Editor stream placement from an admitted dry source. TEST is read-only; execution consumes City RNG and stops
// at water, obstructions, a basin or the length limit. Returns cells placed or a negative command error.
i64 CmdPlaceStream(City *city, u32 flags, TileIndex tile, int length);
// Canal admission and raw work prices are read-only; invalid tiles/endpoints are rejected.
bool CanPlaceCityCanal(const City *city, TileIndex tile);
// Check admission and return 1 after conversion, or BLOCKED without effects. No payment or RNG;
// recorded Game errors remain separate. The rectangle command charges only applied cells.
i64 ApplyWaterToolAtTile(City *city, TileIndex tile);
// Rectangle TEST validates and quotes without mutation or RNG; execution uses shared command charging policy.
i64 CmdBuildCanalsRect(City *city, u32 flags, TileIndex start, TileIndex end, int options);

// Resolved City commands: TEST is read-only; execution retains each action's original result/payment contract.
i64 CmdAdjustTerrainHeight(City *city, u32 flags, TileIndex tile, int direction);
i64 CmdCarveWaterChannel(City *city, u32 flags, TileIndex tile, int length);
i64 CmdApplyWaterTile(City *city, u32 flags, TileIndex tile, int reserved);

// Geometry is camera-relative and must describe this same wet tile. Sprite indexes include zoom.
int CityWaterSprite(const City *city, TileIndex tile, int zoom, const CityTileGeometry *geometry);
// River artwork is one water surface plus at most twelve bank pieces, all sharing the same baseline.
// UI can retain this read-only selection without consulting neighboring tiles during repaint.
// The returned value is defined in Drawing's map-sprite interface.
MapRiverSprites ReadCityRiverSprites(const City *city, TileIndex tile, int zoom,
    const CityTileGeometry *geometry);
// Paint native river/bank edges in the ground pass; false leaves the supplied terrain sprite to the caller.
bool DrawCityWaterBank(const City *city, TileIndex tile, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);

#endif
