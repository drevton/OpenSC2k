#ifndef SC2K_CITY_MAPS_LABELS_H
#define SC2K_CITY_MAPS_LABELS_H

#include "city/types.h"

// Name records live in map_types.h; these operations borrow their City-owned storage.
typedef struct CityOverlayName CityOverlayName;
typedef struct CityAnnualFacilityRecord CityAnnualFacilityRecord;
typedef struct CityNameCommandInput CityNameCommandInput;

// Sparse signs and facility associations are City-owned. Tiles/slots must be valid where no admission result is given.
// Borrowed record/name pointers must not survive reservation, reset, release or callbacks that may change storage.
// Base overlay reads project stationary effects before signs, then eligible facilities. Stock reads omit extended IDs.
// Native writes select a sign or stationary effect; zero clears only the effect. Callers manage damage.
// Moving a sign also requires the caller to clear its old tile's presence flag.
void WriteOverlay(City *city, TileIndex tile, int value);
int ReadCityBaseOverlay(const City *city, TileIndex tile);
u8 ReadCityStockBaseOverlay(const City *city, TileIndex tile);
int ReadCitySignOverlay(const City *city, TileIndex tile);
// Resolve a valid tile's shared facility category or hashed building anchor. Shared categories bypass anchor lookup;
// broken building links prevent a hashed association. Zero means no association.
int ReadCityFacilityOverlay(const City *city, TileIndex tile);
CityOverlayName *CityOverlayNameAt(City *city, int overlay);
const CityOverlayName *ReadCityOverlayName(const City *city, int overlay);
CityAnnualFacilityRecord *CityFacilityRecordAt(City *city, int slot);
const CityAnnualFacilityRecord *ReadCityFacilityRecord(const City *city, int slot);
// Exclusive scan limit: shared policy exposes the fixed records; individual policy also exposes extra used slots.
// Holes remain in that range. Re-read after callbacks or policy changes rather than retaining a scan-wide snapshot.
int CityFacilityRecordLimit(const City *city);
bool CityHasExtendedAssociations(const City *city);
// Ensure room for the unsigned slot count, including reserved slots. Existing capacity succeeds without allocation
// or further validation. Growth rejects counts above map area plus the sign sentinel or reserved facility prefix.
// Failure leaves the live arrays and counters unchanged. Successful growth preserves records/names, invalidates
// sparse borrows and rebuilds that association's hash chains with a power-of-two capacity.
bool ReserveCityFacilities(City *city, u32 requested);
bool ReserveCitySigns(City *city, u32 requested);
// Rebuild both hashes from live records without allocation; advance the sign revision even for empty tables.
// Rebuilt chains traverse slots in ascending order; reserved shared facilities stay outside the hash.
void RebuildCityAssociationHashes(City *city);
// Link a dynamic record with an allocated detail and valid tile. Reserved slots are ignored.
// The caller must ensure the record is not already linked.
void LinkCityFacility(City *city, int slot);
// Release sparse sign/facility allocations and clear fixed facility records. Leaves tile overlays and names alone.
void ReleaseCityAssociations(City *city);
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

// Prepare a valid tile's existing or available editable sign slot. Returns its overlay ID or a negative notice.
// May reserve sign storage; does not attach the slot, collect text, report errors or invalidate the tile.
int PrepareCityMapLabel(City *city, TileIndex tile);
// Commit a captured slot without repeating admission or reservation. Cancellation removes the live sign;
// every other answer uses the supplied bounded text. The caller validates its length and owns damage.
void FinishCityMapLabelChoice(City *city, TileIndex tile, int slot, const CityNameCommandInput *state, int answer);

// Staged map-label edits borrow City's command name only for this call. TEST returns zero on admission;
// execution returns the edit's signed result and preserves diagnostic callbacks and their original return order.
i64 CmdEditMapLabel(City *city, u32 flags, TileIndex tile, int answer);

#endif
