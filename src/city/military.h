#ifndef SC2K_CITY_MILITARY_H
#define SC2K_CITY_MILITARY_H

#include "city/maps/map_types.h"

// Update counter buckets for an existing military-zone occupant's replacement, removing previous before adding kind.
// This changes only counts. The caller owns kind/zone writes, metadata conversion and invalidation.
void UpdateCityMilitaryTileCounts(City *city, u8 previous, u8 kind);

// Stage the legacy reward offer's answer (UNANSWERED/YES/NO) without opening or dismissing presentation.
// Reserved fields must be zero; TEST leaves the pending choice unchanged. The command ignores map location.
i64 CmdSetMilitaryRewardAnswer(City *city, u32 flags, int answer, int reserved1, int reserved2);
CityMilitaryPlacement BeginCityMilitaryPlacement(City *city, bool accept);
void FinishCityMilitaryPlacement(City *city, CityMilitaryPlacement placement);
void OfferDisasterUnlockBonus(City *city);

void GrowCityMilitaryBaseTile(City *city, TileIndex tile);
bool GrowSpecialZoneSilos(City *city, TileIndex origin, int zone_kind);

#endif
