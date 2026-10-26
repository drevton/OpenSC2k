#ifndef SC2K_CITY_ECONOMY_FACILITIES_H
#define SC2K_CITY_ECONOMY_FACILITIES_H

#include "city/economy/state.h"
#include "city/types.h"

// Select information templates and, in shared-record mode, the reserved facility category.
// Unlisted tile kinds have no specialized information template.
extern const u8 kFacilityClassByTile[TK_COUNT];
// New-city facility defaults after ResetCityMapAssociations releases old instances. Native records are zeroed;
// shared records preserve unused payloads. Restore stadium names and clear the annual facility counters.
void ResetCityFacilityState(City *city);
// Reserve/aggregate a slot and initialize its record and blank/retained-label state. Some kinds consume RNG.
// Returns an overlay association, or zero when no record is needed/available.
// Full arcology allocation may reuse a slot.
// Capacity failure retains earlier reservation/free-slot updates; City owns the resulting records and labels.
int CreateFacilityInstance(City *city, TileIndex anchor, u8 kind);
// Select per-facility records or shared records. TEST validates options without conversion or allocation.
// Execution preserves any partial conversion if allocation fails and returns CITY_COMMAND_COST_BLOCKED.
i64 CmdSetIndividualFacilities(City *city, u32 flags, int enabled, int reserved1, int reserved2);

// Apply a resolved policy during command execution or loading. A no-op returns true without invalidation.
// Conversion may allocate; false retains earlier writes. A completed conversion rebuilds associations and invalidates
// the map.
bool SetCityIndividualFacilities(City *city, bool individual);

// Staged facility-label/team edits borrow City's bounded command name only for this call. TEST returns zero on
// admission; execution resolves the live facility and preserves diagnostic callbacks and their original return order.
i64 CmdEditFacilityLabel(City *city, u32 flags, TileIndex tile, int answer);
i64 CmdAssignStadiumTeam(City *city, u32 flags, TileIndex tile, int team);

// Apply an admitted team index and terminated name to the tile's live facility. Copy the team name before
// resolving that association; it may differ from the question's initial slot. No admission, dialog or damage.
void SetCityStadiumTeam(City *city, TileIndex tile, int team, const char *name);
// Deliver the captured choice to the observer or native assignment; team -1 only plays the rejection cue.
void ApplyCityStadiumTeamChoice(City *city, TileIndex tile, int team, const char *name);

// Before replacing a footprint, restore reward availability or release its live stadium team association.
// Kind and the valid northeast corner are captured by the caller. Overlay record cleanup follows during removal.
void PrepareCityFacilityRemoval(City *city, TileIndex anchor, int kind);

// Advance aging, service metrics and arcologies through their existing notices and compensation sequence.
void AdvanceAnnualFacilityState(City *city);
// Advance returns false at an aging notice; present notice_data, then resume the same work. True permits Finish.
// Finish commits totals once and returns the captured exodus count, or -1 when none begins. The simulation caller
// retains this deterministic continuation through notices.
CityAnnualFacilityWork BeginCityAnnualFacilities(City *city);
bool AdvanceCityAnnualFacilities(City *city, CityAnnualFacilityWork *work);
int FinishCityAnnualFacilities(City *city, const CityAnnualFacilityWork *work);
// Consume the captured count after the exodus notice; nested commands may have changed live cash and population.
void CompleteCityArcologyExodus(City *city, int arcology_count);

// Update resolved reward availability, or reconstruct it after a load, and notify the toolbar presentation.
void SetSpecialFacilityAvailability(City *city, int facility, i32 enabled);
void RebuildCityUnlockState(City *city);

#endif
