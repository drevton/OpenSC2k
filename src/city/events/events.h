#ifndef SC2K_DOMAIN_SRC_CITY_EVENTS_EVENTS_H
#define SC2K_DOMAIN_SRC_CITY_EVENTS_EVENTS_H

#include "city/maps/tile_storage.h"
#include "city/events/types.h"

// Event positions and indexed pool operations. Shared record definitions live in types.h.

CityPoint ReadCityMapEventPosition(const City *city, const CityMapEvent *event);
CityPoint ReadCityMapEventTarget(const City *city, const CityMapEvent *event);
// Train targets are map coordinates; all other targets are normalized native sub-tile offsets.
void SetCityMapEventTarget(City *city, CityMapEvent *event, int x, int y);
void ReindexCityMapEvents(City *city, int width, int height);

// Virtual overlay IDs keep the stock marker range free without widening map tiles.
static inline int CityEventOverlay(int slot) {
  return slot < CITY_STOCK_EVENT_SLOT_COUNT ? CITY_OVERLAY_EVENT_FIRST + slot :
      256 + slot - CITY_STOCK_EVENT_SLOT_COUNT;
}
static inline bool IsCityEventOverlay(int overlay) {
  return (overlay >= CITY_OVERLAY_EVENT_FIRST && overlay < CITY_OVERLAY_EVENT_END) ||
      (overlay >= 256 && overlay < 256 + CITY_EVENT_MAX_SLOTS - CITY_STOCK_EVENT_SLOT_COUNT);
}
static inline int CityEventOverlaySlot(int overlay) {
  return overlay < 256 ? overlay - CITY_OVERLAY_EVENT_FIRST : overlay - 256 + CITY_STOCK_EVENT_SLOT_COUNT;
}
static inline bool IsCityMarkerOverlay(int overlay) {
  return overlay >= CITY_OVERLAY_EVENT_END && overlay < 256;
}
static inline bool IsCityEventOrMarkerOverlay(int overlay) {
  return IsCityEventOverlay(overlay) || IsCityMarkerOverlay(overlay);
}
// Maxis Man's goal reserves 251..255 for markers; event targets are virtual overlay IDs.
int CityEventSlotCapacity(int width, int height);
int CityEventAllocationLimit(const City *city);
bool CanSpawnCityVehicle(const City *city, int type);
void UnlinkCityMapEvent(City *city, int slot);
void LinkCityMapEvent(City *city, int slot);
void MoveCityMapEvent(City *city, int slot, TileIndex tile);
int FirstCityMapEventOnTile(const City *city, TileIndex tile);
int NextCityMapEventOnTile(const City *city, int slot);
// Discard all event records and hash membership in existing storage. Does not clear tile overlays or spawn counters.
void ClearCityMapEvents(City *city);
void RebuildCityEventHash(City *city);
int ReadOverlay(const City *city, TileIndex tile);
u16 ReadCityEventUnderlay(const City *city, TileIndex tile);

// Slot allocation and release.

int FindFreeCityEventSlot(City *city);
void ClearCityMapEventSlot(City *city, int slot);

// Tables

// Native immutable values; no executable mapping is needed.
extern const i8 kCityMapEventSubtileYStep[8];
extern const i8 kCityMapEventSubtileXStep[8];

// Shared movement maintains tile/hash membership and byte fractions. Slots must contain live records.
// speed_type selects the event-kind speed; direction is an explicit eight-way heading.
void AdvanceCityMapEventPosition(City *city, int speed_type, int slot, int direction);
// Ship boundary failure unlinks but retains its event; the caller decides its subsequent state/removal.
bool AdvanceCityShipPosition(City *city, int slot);
// Sailboat boundary failure releases its event; the final map row/column are excluded.
bool AdvanceCitySailboatPosition(City *city, int slot);
// Always return an adjacent heading, even when already aligned. SteerCityEventTowardPoint preserves alignment.
int TurnCityEventHeadingToward(int direction, int target);
// World coordinates, including off-map waypoints. Half-axis thresholds select headings; coincident points select SE.
int CityEventHeadingTowardPoint(int x, int y, int target_x, int target_y);
int SteerCityEventTowardPoint(int direction, int x, int y, int target_x, int target_y);
int RandomlyPerturbCityEventHeading(City *city, int direction, int chance);

void CreateCityMapEventAtTile(City *city, TileIndex tile, int type, int direction);
void UpdateCityMapEvents(City *city);
void UpdateCityMapEventTimer(City *city);

// Aircraft (aircraft.c). Spawning and updates own City state/RNG; waypoint decoding is read-only for file codecs.
// During approach the destination is the runway reference point; derive its rotated offset only when needed.
void ReadCityAirplaneApproachOffset(const CityMapEvent *event, int *x, int *y);
// Target effects use native tile indices; invalid/non-helicopter targets leave City unchanged.
void CrashCityHelicopterAtTile(City *city, TileIndex tile);
// Free shared action: TEST is read-only and execution acknowledges no-op targets as before.
i64 CmdCrashCityHelicopterAtTile(City *city, u32 flags, TileIndex tile);
void TrySpawnCityHelicopter(City *city, TileIndex tile);
void TrySpawnCityAirplane(City *city, TileIndex tile, int axis);
void UpdateCityMapAirplane(City *city, int slot);
void UpdateCityMapHelicopter(City *city, int slot);

// Ships and sailboats (boats.c). Admission, arrival and stage changes stay inside the family.
void SpawnBorderWaterEvent(City *city, TileIndex source);
void TrySpawnCityMarinaSailboats(City *city, TileIndex tile);
void UpdateCityMapShip(City *city, int slot);
void UpdateCityMapSailboat(City *city, int slot);

// Trains and subways (trains.c). An engine owns its two linked coaches; reversal preserves their pool slots.
void SwapCityTrainEnds(City *city, int first_slot, int second_slot);
void TrySpawnCityStationTrains(City *city, TileIndex tile);
void UpdateCityMapTrain(City *city, int slot);

// Disaster actors (disaster_actors.c). Creation/update applies City effects and delivers presentation requests.
void CreateCityMapMaxisMan(City *city, int disaster_type);
int CreateCityMapEffectEvent(City *city, TileIndex tile, int variant, int state, int linked);
void UpdateCityMapExplosion(City *city, int slot);
void UpdateCityMapMaxisMan(City *city, int slot);
void UpdateCityMapTornado(City *city, int slot);
void UpdateCityMapMonster(City *city, int slot);

#endif
