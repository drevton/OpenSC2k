#ifndef SC2K_CITY_MAP_FORMAT_H
#define SC2K_CITY_MAP_FORMAT_H

#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/saveload/terrain_format.h"

// Native XGND climate header precedes the existing snow/moisture lattice bytes.
enum { CITY_SAVE_CLIMATE_HEADER_SIZE = 8 };

// Stock XZON uses one-hot footprint corners. Native footprint links are
// converted to this convention only for codecs and retained stock algorithms.
enum {
  CITY_ZONE_CORNER_MASK = 0xF0,
  CITY_ZONE_CORNER_0 = 0x10, CITY_ZONE_CORNER_1 = 0x20,
  CITY_ZONE_CORNER_2 = 0x40, CITY_ZONE_CORNER_3 = 0x80,
};

// Original SC2 file and statistic IDs, independent of TileKind.
// Compact native infrastructure IDs are independent of the stock artwork catalog.
// Translate complete tile records at import/export; never store these IDs as native kinds.
typedef enum CityLegacySurfaceTile {
  TKL_RUBBLE_LAST = 4,
  TKL_PARK = 13,
  TKL_POWER_LINE_EAST_WEST = 14,
  TKL_POWER_LINE_NORTH_SOUTH = 15,
  TKL_POWER_LINE_WEST_SOUTH = 20,
  TKL_POWER_LINE_SOUTH_EAST = 21,
  TKL_POWER_LINE_EAST_NORTH = 22,
  TKL_POWER_LINE_NORTH_WEST = 23,
  TKL_POWER_LINE_WEST_SOUTH_NORTH = 24,
  TKL_POWER_LINE_WEST_SOUTH_EAST = 25,
  TKL_POWER_LINE_SOUTH_EAST_NORTH = 26,
  TKL_POWER_LINE_WEST_EAST_NORTH = 27,
  TKL_POWER_LINE_JUNCTION = 28,
  TKL_POWER_LINE_LAST = 28,
  TKL_ROAD_EAST_WEST = 29,
  TKL_ROAD_NORTH_SOUTH = 30,
  TKL_ROAD_WEST_SOUTH = 35,
  TKL_ROAD_SOUTH_EAST = 36,
  TKL_ROAD_EAST_NORTH = 37,
  TKL_ROAD_NORTH_WEST = 38,
  TKL_ROAD_WEST_SOUTH_NORTH = 39,
  TKL_ROAD_WEST_SOUTH_EAST = 40,
  TKL_ROAD_SOUTH_EAST_NORTH = 41,
  TKL_ROAD_WEST_EAST_NORTH = 42,
  TKL_ROAD_JUNCTION = 43,
  TKL_ROAD_LAST = 43,
  TKL_RAIL_FIRST = 44,
  TKL_RAIL_EAST_WEST = 44,
  TKL_RAIL_NORTH_SOUTH = 45,
  TKL_RAIL_SLOPE_46 = 46,
  TKL_RAIL_SLOPE_49 = 49,
  TKL_RAIL_WEST_SOUTH = 50,
  TKL_RAIL_SOUTH_EAST = 51,
  TKL_RAIL_EAST_NORTH = 52,
  TKL_RAIL_NORTH_WEST = 53,
  TKL_RAIL_WEST_SOUTH_NORTH = 54,
  TKL_RAIL_WEST_SOUTH_EAST = 55,
  TKL_RAIL_SOUTH_EAST_NORTH = 56,
  TKL_RAIL_WEST_EAST_NORTH = 57,
  TKL_RAIL_JUNCTION = 58,
  TKL_RAIL_LAST = 58,
  TKL_RAIL_TRANSITION_59 = 59,
  TKL_RAIL_TRANSITION_60 = 60,
  TKL_RAIL_TRANSITION_61 = 61,
  TKL_RAIL_TRANSITION_62 = 62,
  TKL_TUNNEL_ENTRANCE_FIRST = 63,
  TKL_TUNNEL_ENTRANCE_LAST = 66,
  TKL_ROAD_POWER_CROSSING_FIRST = 67,
  TKL_ROAD_POWER_CROSSING_LAST = 68,
  TKL_ROAD_RAIL_CROSSING_FIRST = 69,
  TKL_ROAD_RAIL_CROSSING_LAST = 70,
  TKL_RAIL_POWER_CROSSING_FIRST = 71,
  TKL_RAIL_POWER_CROSSING_LAST = 72,
  TKL_HIGHWAY_FIRST = 73,
  TKL_HIGHWAY_NS = 74,
  TKL_HIGHWAY_ROAD_CROSSING_FIRST = 75,
  TKL_HIGHWAY_ROAD_CROSSING_LAST = 76,
  TKL_HIGHWAY_RAIL_CROSSING_FIRST = 77,
  TKL_HIGHWAY_RAIL_CROSSING_LAST = 78,
  TKL_HIGHWAY_POWER_CROSSING_FIRST = 79,
  TKL_HIGHWAY_POWER_CROSSING_LAST = 80,
  TKL_HIGHWAY_LAST = 80,
  TKL_SUSPENSION_BRIDGE_FIRST = 81,
  TKL_SUSPENSION_BRIDGE_82 = 82,
  TKL_SUSPENSION_BRIDGE_83 = 83,
  TKL_SUSPENSION_BRIDGE_84 = 84,
  TKL_SUSPENSION_BRIDGE_LAST = 85,
  TKL_BRIDGE_FIRST = 86,
  TKL_ROAD_BRIDGE_DECK = 87,
  TKL_RAISING_BRIDGE_CLOSED = 88,
  TKL_RAISING_BRIDGE_OPEN = 89,
  TKL_RAIL_BRIDGE_FIRST = 90,
  TKL_BRIDGE_LAST = 91,
  TKL_RAIL_BRIDGE_LAST = 91,
  TKL_POWER_LINE_BRIDGE = 92,
  TKL_HIGHWAY_ENTRANCE_93 = 93,
  TKL_HIGHWAY_ENTRANCE_94 = 94,
  TKL_HIGHWAY_ENTRANCE_LAST = 96,
  TKL_HIGHWAY_ENTRANCE_96 = 96,
  TKL_HIGHWAY_INCLINE_FIRST = 97,
  TKL_HIGHWAY_INCLINE_LAST = 100,
  TKL_HIGHWAY_BEND_FIRST = 101,
  TKL_HIGHWAY_BEND_LAST = 104,
  TKL_HIGHWAY_JUNCTION = 105,
  TKL_HIGHWAY_BRIDGE_FIRST = 106,
  TKL_HIGHWAY_BRIDGE_LAST = 107,
  TKL_SUBWAY_TO_RAIL_FIRST = 108,
  TKL_SUBWAY_TO_RAIL_LAST = 111,
  TKL_HYDRO_POWER_LAST = 199,
} CityLegacySurfaceTile;

// Stock XBIT orientation/water bits. Native orientation uses zone metadata; kinds encode water presence.
enum { CITY_LEGACY_MAP_FLAG_ROTATED = 0x02, CITY_LEGACY_MAP_FLAG_WATER = 0x04 };

// Read-only stock projection of the full record, shared by SC2 and comparison exports.
TileInfo TranslateCityTileToLegacy(const City *city, TileIndex tile);
// Preserve live totals while splitting consolidated kinds into stock directional counters.
void ReadCityLegacyTileCounts(const City *city, u32 counts[TK_COUNT]);
void DecodeCityLegacyTileCounts(const u8 *bytes, CityTileCounts *counts);
// Source map must remain unchanged for the entire import pass; result must not alias it.
bool TranslateCityTileFromLegacy(const City *city, TileIndex tile, TileInfo *result);
void WriteLegacyCityTerrainCode(City *city, TileIndex tile, int value);
// Read-only stock terrain projection for retained algorithms. Tile must be valid in the supplied City.
int ReadLegacyTerrainCode(const City *city, TileIndex tile);
// Project a supplied native shape or raw stock code using the tile's occupant/water metadata.
int ProjectCityTerrainToLegacy(const City *city, TileIndex tile, int terrain);

typedef struct CityFileChunkSpec CityFileChunkSpec;
void ExportCityMapChunk(City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size);
// Codec lifecycle: new cities start native; raw stock decoding is explicitly scoped.
void InitializeCityMapFormat(City *city);
void BeginLegacyCityMapImport(City *city);
// Publish complete native tiles and counters before simulation rebuilding.
bool ConvertCityMapToNative(City *city);
// Finish landscape/facility initialization after the load's simulation rebuilding.
bool ImportCityMapFormat(City *city);

u32 CityGroundExtensionSize(const City *city);
bool EncodeCityGroundExtension(const City *city, u8 *bytes, u32 size);
bool ApplyCityGroundExtension(City *city, const u8 *bytes, u32 size);
// Native save admission reads the records directly; no stock conversion or map copy.
bool ValidateCityTileRecords(const City *city);

// Stock sprite mirroring derived from native facing/axis, never native flags.
bool LegacyCityTileFlip(const City *city, TileIndex tile);
// Stock rubble classification retained by the original placement algorithm.
bool IsLegacyRubbleKind(int kind);
// Compatibility counter keys used by simulation bookkeeping; neither query changes counters or tile records.
int CityTileStatisticsKind(const City *city, int kind);
int CityTileStatisticAt(const City *city, TileIndex tile);

#endif
