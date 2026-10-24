#ifndef SC2K_DOMAIN_SRC_CITY_DISASTERS_DISASTERS_H
#define SC2K_DOMAIN_SRC_CITY_DISASTERS_DISASTERS_H

#include "city/types.h"
#include "city/disasters/types.h"

void CancelCityDisasterWork(City *city);

void SetDisasterLocation(CityDisasterLocation *location, int x, int y);

// Tables

extern const CityDisasterPeriods kCityDefaultDisasterPeriods;

// Dispatch

void FailRadio(City *city, i32 outcome);
void FinishCityMissionOutcome(City *city, int outcome);
void ScheduleRandomDisaster(City *city, int kind);
void ScheduleCenteredDisaster(City *city, int kind, int radius);
void SchedulePollutionDisaster(City *city);
void EvaluateCityProblemsAndRandomDisaster(City *city);
i64 AdvanceCityDisasterEffects(City *city, bool terrain_water_allowed);

// Effects apply the queued location immediately or resume City-owned disaster work. An i64 result may be
// CITY_OPERATION_PENDING; resume that same effect after its delay instead of scheduling it again.
// Successful effects return nonzero, and false/zero means no suitable target or event slot was found.

i64 ApplyCityFireDisaster(City *city, bool fire_storm);
bool ApplyCityMobileDisaster(City *city, bool monster);
i64 ApplyCityRandomDisasterStrikes(City *city, bool delayed);
bool ApplyDisasterCellEffect(City *city, TileIndex tile_index, int allow_low_tiles);
void DemolishCityFootprintForDisaster(City *city, TileIndex tile, int animate,
    int clear_overlays);
void ResolveDestroyedCityMapCell(City *city, TileIndex tile_index);
void SpreadCityMapFireToCell(City *city, TileIndex tile_index, int maximum_power_level);
void SetCityMapOverlayIfReplaceable(City *city, TileIndex tile_index, u8 overlay);
void ClearTransientCityMapOverlay(City *city, TileIndex tile_index);
bool ApplyCityRiotDisaster(City *city);
i64 ApplyCityMicrowaveDisaster(City *city);
bool ApplyCityFloodDisaster(City *city);
bool ApplyCityPollutionDisaster(City *city);
int ApplyCityChemicalSpillDisaster(City *city);
i64 ApplyCityMeltdownDisaster(City *city);
i64 ApplyCityHurricaneDisaster(City *city);
i64 ApplyCityVolcanoDisaster(City *city, bool terrain_water_allowed);
i64 ApplyCityEarthquakeDisaster(City *city);
bool ApplyCityAirCrashDisaster(City *city);

// Toggle random-disaster suppression; this does not cancel an active or explicitly requested disaster.
// The command ignores map location and requires zero reserved fields. TEST leaves the rule unchanged.
i64 CmdToggleNoDisasters(City *city, u32 flags, int reserved1, int reserved2, int reserved3);

// Shared command admission and execution. Reserved parameters must be zero. The fifteen explicit selectors below
// ignore location; crash, helicopter-crash and chemical-spill requests require INVALID_TILE and a valid city center.
// TEST returns zero on admission without changing City, RNG or presentation; execution returns one on success.
// Invalid payloads or unavailable crash-event slots return CITY_COMMAND_COST_INVALID.
i64 CmdStartDisaster(City *city, u32 flags, TileIndex location,
    int kind, int parameter2, int parameter3);

// Queue a supported explicit disaster request. These selectors consume their documented location draws;
// callers needing validation or a read-only query must use CmdStartDisaster through RunCityCommand.

void SelectCityFireDisaster(City *city);
void SelectCityFloodDisaster(City *city);
void SelectCityAirCrashDisaster(City *city);
void SelectCityTornadoDisaster(City *city);
void SelectCityEarthquakeDisaster(City *city);
void SelectCityMonsterDisaster(City *city);
void SelectCityRiotsDisaster(City *city);
void SelectCityHurricaneDisaster(City *city);
void SelectCityMicrowaveDisaster(City *city);
void SelectCityPollutionDisaster(City *city);
void SelectCityVolcanoDisaster(City *city);
void SelectCityFireStormDisaster(City *city);
void SelectCityMajorFloodDisaster(City *city);
void SelectCityMassRiotsDisaster(City *city);
void SelectCityMeltdownDisaster(City *city);

#endif
