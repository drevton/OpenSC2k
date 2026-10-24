#ifndef SC2K_CITY_MAPS_LABELS_H
#define SC2K_CITY_MAPS_LABELS_H

#include "city/types.h"
#include <stddef.h>

// Native UTF-8 slots and stock XLAB records have independent sizes.
enum {
  CITY_OVERLAY_NAME_COUNT = 256,
  CITY_OVERLAY_NAME_BYTES = 32,
  CITY_OVERLAY_LEGACY_NAME_BYTES = 25,
  CITY_SIGN_COLOR_COUNT = 32,
  CITY_OVERLAY_NAME_MAX_LENGTH = CITY_OVERLAY_NAME_BYTES - 1,
  CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH = CITY_OVERLAY_LEGACY_NAME_BYTES - 2,
};

typedef struct CityOverlayName {
  u8 character[CITY_OVERLAY_NAME_BYTES];
} CityOverlayName;

// Sparse associations use indices as stable command tokens; hash links are rebuilt on load.
typedef struct {
  TileIndex tile;
  u32 next;
  CityOverlayName name;
} CitySignRecord;

typedef struct {
  CitySignRecord *record;
  u32 *buckets;
  int capacity, used, free_slot;
  u32 revision;
} CitySigns;

// Native UTF-8 label/team/mayor slots. Stock XLAB records are converted by the file codec.

typedef struct {
  CityOverlayName entry[CITY_OVERLAY_NAME_COUNT];
} CityOverlayNames;
_Static_assert(sizeof(CityOverlayName) == 32 && offsetof(CityOverlayName, character) == 0,
    "native UTF-8 name record");
_Static_assert(sizeof(CityOverlayNames) == 8192, "native XLAB name table");

typedef struct CityAnnualFacilityRecord CityAnnualFacilityRecord;

// Sparse signs and facility associations are City-owned. Tiles/slots must be valid where no admission result is given.
// Borrowed record/name pointers must not survive reservation, reset, release or callbacks that may change storage.
// Base overlay reads project stationary effects before signs, then eligible facilities. Stock reads omit extended IDs.
// Native writes select a sign or stationary effect; zero clears only the effect. Callers manage damage.
// Moving a sign also requires the caller to clear its old tile's presence flag.
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
// Ensure room for the requested slot count, including reserved slots. Existing capacity needs no allocation.
// Failure leaves the live arrays and counters unchanged. Successful growth preserves records/names, invalidates
// sparse borrows and rebuilds that association's hash chains with a power-of-two capacity.
bool ReserveCityFacilities(City *city, int requested);
bool ReserveCitySigns(City *city, int requested);
// Rebuild both hashes from live records without allocation; advance the sign revision even for empty tables.
void RebuildCityAssociationHashes(City *city);
// Link a dynamic record with an allocated detail and valid tile. Reserved slots are ignored.
// The caller must ensure the record is not already linked.
void LinkCityFacility(City *city, int slot);
// Release sparse sign/facility allocations and clear fixed facility records. Leaves tile overlays and names alone.
void ReleaseCityAssociations(City *city);
// Decode stock byte overlays after geometry/events are loaded. Failure can retain the first successful reservation;
// success consumes the bytes into native associations and rebuilds both hashes.
bool ImportStockCityAssociations(City *city);

// New-city reset: release sparse associations, clear tile overlays, empty fixed names and restore the default mayor.
// Requires bound map storage. Preserves fixed facility records; ResetCityFacilityState installs their defaults next.
void ResetCityMapAssociations(City *city);
void ClearCityOverlayNames(City *city);
// Remove a sign/dynamic facility and its name, or clear an event's retained underlay and stationary effect.
// Reserved shared facilities are untouched. The caller owns damage around these changes.
void ClearOverlayAssociation(City *city, int overlay);
// Return an available sign overlay identifier, or zero at the policy's limit; does not reserve the slot.
int FindFreeOverlayNameSlot(const City *city);
bool CanEditCityMapSign(const City *city, TileIndex tile);
bool CityTileHasFacilityLabel(const City *city, TileIndex tile);
// A leading byte 1..31 selects a native sign color; plain names retain default green.
int CitySignTextColor(const char *text);
const char *CitySignTextName(const char *text);
// Sign commands return a zero TEST price on admission and a negative error on rejection.
i64 CmdRenameCitySign(City *city, u32 flags, TileIndex tile, int slot, const u8 *text, int length);
i64 CmdMoveCitySign(City *city, u32 flags, TileIndex tile, TileIndex destination, int slot);
void EditCityOverlayNameAtTile(City *city, TileIndex tile);
void EditCityOverlayLabelAtCell(City *city, TileIndex tile);
void ApplyCityStadiumTeamChoice(City *city, TileIndex tile, int team, const char *name);
void AcceptCityStadiumTeamDialog(City *city, TileIndex tile, int team, const char *name);

// Staged map/facility/team edits borrow City's command name only for this call. TEST returns zero on admission;
// execution returns the edit's signed result and preserves diagnostic callbacks and their original return order.
i64 CmdEditMapLabel(City *city, u32 flags, TileIndex tile, int answer);
i64 CmdEditFacilityLabel(City *city, u32 flags, TileIndex tile, int answer);
i64 CmdAssignStadiumTeam(City *city, u32 flags, TileIndex tile, int team);

#endif
