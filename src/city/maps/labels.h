#ifndef SC2K_CITY_MAPS_LABELS_H
#define SC2K_CITY_MAPS_LABELS_H

#include "city/types.h"

typedef struct CityOverlayName CityOverlayName;
typedef struct CityAnnualFacilityRecord CityAnnualFacilityRecord;
typedef struct CityNameCommandInput CityNameCommandInput;

// Sparse signs and facility associations are City-owned. Tiles/slots must be valid where no admission result is given.
// Borrowed record/name pointers must not survive reservation, reset, release or callbacks that may change storage.
// Base overlay reads project stationary effects before signs, then eligible facilities. Stock reads omit extended IDs.
// Writing zero clears only the stationary effect; removing a sign/facility uses ClearOverlayAssociation explicitly.
void WriteOverlay(City *city, TileIndex tile, int value);
int ReadCityBaseOverlay(const City *city, TileIndex tile);
u8 ReadCityStockBaseOverlay(const City *city, TileIndex tile);
int ReadCitySignOverlay(const City *city, TileIndex tile);
int ReadCityFacilityOverlay(const City *city, TileIndex tile);
CityOverlayName *CityOverlayNameAt(City *city, int overlay);
const CityOverlayName *ReadCityOverlayName(const City *city, int overlay);
CityAnnualFacilityRecord *CityFacilityRecordAt(City *city, int slot);
const CityAnnualFacilityRecord *ReadCityFacilityRecord(const City *city, int slot);
int CityFacilityRecordLimit(const City *city);
bool CityHasExtendedAssociations(const City *city);
bool ReserveCityFacilities(City *city, int capacity);
bool ReserveCitySigns(City *city, int capacity);
void RebuildCityAssociationHashes(City *city);
void LinkCityFacility(City *city, int slot);
// Release sparse sign/facility allocations and clear fixed facility records. Leaves tile overlays and names alone.
void ReleaseCityAssociations(City *city);
bool ImportStockCityAssociations(City *city);

// New-city reset: release sparse associations, clear tile overlays, empty fixed names and restore the default mayor.
// Requires bound map storage. Preserves fixed facility records; ResetCityFacilityState installs their defaults next.
void ResetCityMapAssociations(City *city);
void ClearCityOverlayNames(City *city);
void ClearOverlayAssociation(City *city, int overlay);
int FindFreeOverlayNameSlot(City *city);
bool CanEditCityMapSign(const City *city, TileIndex tile);
bool CityTileHasFacilityLabel(const City *city, TileIndex tile);
// A leading byte 1..31 selects a native sign color; plain names retain default green.
int CitySignTextColor(const char *text);
const char *CitySignTextName(const char *text);
// Sign commands return a zero TEST price on admission and a negative error on rejection.
i64 CmdRenameCitySign(City *city, u32 flags, TileIndex tile, int slot, const u8 *text, int length);
i64 CmdMoveCitySign(City *city, u32 flags, TileIndex tile, TileIndex destination, int slot);
void EditCityOverlayNameAtTile(City *city, TileIndex tile);
i64 ApplyCityMapLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer);
void ApplyCityFacilityLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer);
void EditCityOverlayLabelAtCell(City *city, TileIndex tile);
void ApplyCityStadiumTeamChoice(City *city, TileIndex tile, int team, const char *name);
void AcceptCityStadiumTeamDialog(City *city, TileIndex tile, int team, const char *name);

// Staged map/facility/team edits borrow City's command name only for this call. TEST returns zero on admission;
// execution returns the edit's signed result and preserves diagnostic callbacks and their original return order.
i64 CmdEditMapLabel(City *city, u32 flags, TileIndex tile, int answer);
i64 CmdEditFacilityLabel(City *city, u32 flags, TileIndex tile, int answer);
i64 CmdAssignStadiumTeam(City *city, u32 flags, TileIndex tile, int team);

#endif
