#ifndef SC2K_CITY_MILITARY_H
#define SC2K_CITY_MILITARY_H

#include "city/types.h"

// Byte-valued military base/unlock mode. Unavailable covers a declined offer
// and failure to find suitable land. Layouts follow the original zone7 scan.
typedef enum CityMilitaryBaseKind {
  CITY_MILITARY_BASE_NONE = 0,
  CITY_MILITARY_BASE_UNAVAILABLE = 1,
  CITY_MILITARY_BASE_ARMY = 2,
  CITY_MILITARY_BASE_AIR_FORCE = 3,
  CITY_MILITARY_BASE_NAVY = 4,
  CITY_MILITARY_BASE_MISSILE_SILOS = 5,
  CITY_MILITARY_BASE_COUNT = 6
} CityMilitaryBaseKind;

// Military-zone counters have their own sixteen-entry ordering (ReplaceCityTileAndStatistics).
// OTHER is incremented/decremented for all unlisted surface IDs.
typedef enum CitySpecialTileStatistic {
  CITY_SPECIAL_TILE_OTHER = 0, CITY_SPECIAL_TILE_RUNWAY_FIRST = 1,
  CITY_SPECIAL_TILE_RUNWAY_LAST = 2, CITY_SPECIAL_TILE_PARKING_LOT = 3,
  CITY_SPECIAL_TILE_CARGO_YARD = 4, CITY_SPECIAL_TILE_RADAR = 5,
  CITY_SPECIAL_TILE_WAREHOUSE = 6, CITY_SPECIAL_TILE_BUILDING_FIRST = 7,
  CITY_SPECIAL_TILE_BUILDING_SECOND = 8, CITY_SPECIAL_TILE_TOP_SECRET = 9,
  CITY_SPECIAL_TILE_CRANE = 10, CITY_SPECIAL_TILE_CONTROL_TOWER = 11,
  CITY_SPECIAL_TILE_F_15B = 12, CITY_SPECIAL_TILE_HANGAR_SMALL = 13,
  CITY_SPECIAL_TILE_HANGAR_LARGE = 14, CITY_SPECIAL_TILE_MISSILE_SILO = 15,
  CITY_SPECIAL_TILE_COUNT = 16
} CitySpecialTileStatistic;

typedef struct {
  u32 count[CITY_SPECIAL_TILE_COUNT];
} CitySpecialTileCounts;

// Stage the legacy reward offer's answer (UNANSWERED/YES/NO) without opening or dismissing presentation.
// Reserved fields must be zero; TEST leaves the pending choice unchanged. The command ignores map location.
i64 CmdSetMilitaryRewardAnswer(City *city, u32 flags, int answer, int reserved1, int reserved2);
enum { CITY_MILITARY_FINISH_DONE, CITY_MILITARY_FINISH_ZONE, CITY_MILITARY_FINISH_UNAVAILABLE };
typedef struct {
  TileIndex site;
  u16 notice;
  u8 after_notice;
} CityMilitaryPlacement;
CityMilitaryPlacement BeginCityMilitaryPlacement(City *city, bool accept);
void FinishCityMilitaryPlacement(City *city, CityMilitaryPlacement placement);
void OfferDisasterUnlockBonus(City *city);

void GrowCityMilitaryBaseTile(City *city, TileIndex tile);
bool GrowSpecialZoneSilos(City *city, TileIndex origin, int zone_kind);

#endif
