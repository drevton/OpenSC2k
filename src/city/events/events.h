#ifndef SC2K_DOMAIN_SRC_CITY_EVENTS_EVENTS_H
#define SC2K_DOMAIN_SRC_CITY_EVENTS_EVENTS_H

#include "city/maps/tile_storage.h"
#include <stddef.h>

#include "city/types.h"

// Event

// Event-kind values index the sprite table and event dispatcher.
// The large-zoom atlas confirms the vehicle/effect
// names. Kind 4 has a bulldozer sprite but no update handler. Kind 16 is Maxis
// Man: sprite495, disaster targeting, and OpenSC2K's TYPE_MAXIS_MAN agree.
// CityMapEvent.type remains u8: these constants do not change the guest ABI.
typedef enum CityMapEventKind {
  CITY_MAP_EVENT_UNUSED = 0,
  CITY_MAP_EVENT_AIRPLANE = 1,
  CITY_MAP_EVENT_HELICOPTER = 2,
  CITY_MAP_EVENT_SHIP = 3,
  CITY_MAP_EVENT_BULLDOZER = 4,
  CITY_MAP_EVENT_MONSTER = 5,
  CITY_MAP_EVENT_EXPLOSION = 6,
  CITY_MAP_EVENT_POLICE = 7,
  CITY_MAP_EVENT_FIRE = 8,
  CITY_MAP_EVENT_SAILBOAT = 9,
  CITY_MAP_EVENT_TRAIN_ENGINE = 10,
  CITY_MAP_EVENT_TRAIN_CAR = 11,
  CITY_MAP_EVENT_SUBWAY_ENGINE = 12,
  CITY_MAP_EVENT_SUBWAY_CAR = 13,
  CITY_MAP_EVENT_MILITARY = 14,
  CITY_MAP_EVENT_TORNADO = 15,
  CITY_MAP_EVENT_MAXIS_MAN = 16,
  CITY_MAP_EVENT_KIND_COUNT = 17
} CityMapEventKind;

// Eight-way map headings: x increases east, y increases south. These are not
// the four-way route indices or the camera's rotated screen directions.
typedef enum CityMapHeading {
  CITY_HEADING_WEST = 0, CITY_HEADING_SOUTH_WEST = 1, CITY_HEADING_SOUTH = 2,
  CITY_HEADING_SOUTH_EAST = 3, CITY_HEADING_EAST = 4, CITY_HEADING_NORTH_EAST = 5,
  CITY_HEADING_NORTH = 6, CITY_HEADING_NORTH_WEST = 7,
  CITY_HEADING_COUNT = 8, CITY_HEADING_MASK = 7,
} CityMapHeading;

// Low nibble of an airplane's next_slot; its high nibble stores runway heading.
// Helicopters reuse the byte with their own stage values.
typedef enum CityAirplaneStage {
  CITY_AIRPLANE_TAKEOFF = 0, CITY_AIRPLANE_LANDING = 1, CITY_AIRPLANE_CRUISE = 2,
  CITY_AIRPLANE_APPROACH = 3, CITY_AIRPLANE_ALIGN_RUNWAY = 4, CITY_AIRPLANE_CRASH = 7,
  CITY_AIRPLANE_STAGE_MASK = 15,
} CityAirplaneStage;

typedef enum CityHelicopterStage {
  CITY_HELICOPTER_TAKEOFF = 0, CITY_HELICOPTER_FLY_TO_TARGET = 2,
  CITY_HELICOPTER_LANDING = 3, CITY_HELICOPTER_ON_GROUND = 4, CITY_HELICOPTER_CRASH = 5,
} CityHelicopterStage;

// Border-water ship stages used by UpdateCityMapShip.
typedef enum CityShipStage {
  CITY_SHIP_TRAVEL = 0, CITY_SHIP_TURN_TO_TARGET = 1, CITY_SHIP_FIND_PASSAGE = 2,
  CITY_SHIP_AT_PIER = 3, CITY_SHIP_DEPART = 4,
} CityShipStage;

// Original monster state and tile-effect byte, dispatched by UpdateCityMapMonster.
typedef enum CityMonsterState {
  CITY_MONSTER_SEEK_CENTER = 0, CITY_MONSTER_APPLY_TILE_EFFECT = 1,
  CITY_MONSTER_RANDOM_MOVEMENT = 2, CITY_MONSTER_LEAVE = 3,
} CityMonsterState;

typedef enum CityMonsterAction {
  CITY_MONSTER_DESTROY_TILE = 0, CITY_MONSTER_PLANT_TREES = 1,
  CITY_MONSTER_REGRADE_TERRAIN = 2, CITY_MONSTER_PLACE_WIND_POWER = 3,
} CityMonsterAction;

// Maxis Man next_slot is a movement state, not a linked event-slot index.
typedef enum CityMaxisManState {
  CITY_MAXIS_MAN_TRACK_TARGET = 0,
  CITY_MAXIS_MAN_RANDOM_STEP = 1,
  CITY_MAXIS_MAN_DEPART = 2,
} CityMaxisManState;

// Train states use byte arrays. Bus boarding switches
// from states 0..3 to 4..7; stations 8/10/11 tally separate facility traffic.
// Alighting at 9 returns to ordinary road travel. Rail 12 and subway 13 are
// distinct graph modes, not surface or underground tile IDs.
typedef enum CityTransportRouteState {
  CITY_ROUTE_ROAD = 0,
  CITY_ROUTE_HIGHWAY = 1,
  CITY_ROUTE_TUNNEL = 2,
  CITY_ROUTE_CONNECTOR = 3,
  CITY_ROUTE_BUS_ROAD = 4,
  CITY_ROUTE_BUS_HIGHWAY = 5,
  CITY_ROUTE_BUS_TUNNEL = 6,
  CITY_ROUTE_BUS_CONNECTOR = 7,
  CITY_ROUTE_BUS_STATION = 8,
  CITY_ROUTE_ALIGHTING_STATION = 9,
  CITY_ROUTE_RAIL_STATION = 10,
  CITY_ROUTE_SUBWAY_STATION = 11,
  CITY_ROUTE_RAIL = 12,
  CITY_ROUTE_SUBWAY = 13
} CityTransportRouteState;

// Random rail-station probes: paired vertical then horizontal choices.
typedef enum CityRailProbeDirection {
  CITY_RAIL_PROBE_NORTH = 0, CITY_RAIL_PROBE_SOUTH = 1,
  CITY_RAIL_PROBE_WEST = 2, CITY_RAIL_PROBE_EAST = 3,
} CityRailProbeDirection;

typedef enum CityAirplaneSpawnSide {
  CITY_AIRPLANE_SPAWN_NORTH = 0, CITY_AIRPLANE_SPAWN_WEST = 1,
  CITY_AIRPLANE_SPAWN_SOUTH = 2, CITY_AIRPLANE_SPAWN_EAST = 3,
} CityAirplaneSpawnSide;

// Type9 state byte: sailing until a rare incident stops movement before removal.
typedef enum CitySailboatState { CITY_SAILBOAT_SAILING = 0, CITY_SAILBOAT_INCIDENT = 1 } CitySailboatState;

// Type6 reuses direction as its explosion animation stage, not a heading.
typedef enum CityExplosionStage {
  CITY_EXPLOSION_START = 0, CITY_EXPLOSION_SECOND_FRAME = 1, CITY_EXPLOSION_RESOLVE = 2,
} CityExplosionStage;

// Event slot0 is reserved for the interactive tool preview.
enum { CITY_EVENT_SLOT_TOOL_PREVIEW = 0, CITY_EVENT_SLOT_FIRST = 1, CITY_STOCK_EVENT_SLOT_COUNT = 40,
    CITY_EVENT_MIN_SLOTS = 128, CITY_EVENT_MAX_SLOTS = 2048, CITY_EVENT_HASH_BUCKETS = 4096 };

// Border ship probes use N/S/W/E, unlike the airplane spawn order.
typedef enum CityShipSpawnSide {
  CITY_SHIP_SPAWN_NORTH = 0, CITY_SHIP_SPAWN_SOUTH = 1,
  CITY_SHIP_SPAWN_WEST = 2, CITY_SHIP_SPAWN_EAST = 3,
} CityShipSpawnSide;

// State

// Current positions use TileIndex. Sub-tile offsets and potentially off-map
// destinations remain coordinate pairs. The target storage bit survives type
// changes. Released records are entirely zeroed.

enum { CITY_EVENT_TARGET_TILE = 1, CITY_EVENT_HASH_LINKED = 2 };

// One legacy movement unit is 16 native units. Padding is explicit because
// trains use the same four bytes for their next-tile index.
enum { CITY_EVENT_OFFSET_SCALE = 16, CITY_EVENT_TILE_FRACTION = 256, CITY_EVENT_SHIP_FRACTION = 192 };
typedef struct { u8 offset_y, reserved_y, offset_x, reserved_x; } CityMapEventMotion;

static inline bool IsCityMapEventTrain(int type) {
  return type >= CITY_MAP_EVENT_TRAIN_ENGINE && type <= CITY_MAP_EVENT_SUBWAY_CAR;
}
typedef struct {
  u8 type;
  u8 direction;
  union {
    u16 next_slot;
    u16 state;
  };
  // OpenSC2K Z: altitude, or an age/counter for non-aircraft events.
  union {
    u8 age;
    u8 height;
  };
  union { u8 coordinate_flags; u8 reserved_byte; };
  // Stock import/export payload; native map tiles retain the underlying association.
  u16 previous_overlay;
  TileIndex tile;
  // Named object payloads share storage, not meanings. The raw view is for
  // codecs and retained fields that an object's behavior does not consume.
  union {
    struct { CityMapEventMotion motion; u16 destination_y, destination_x, crash; } airplane;
    struct { CityMapEventMotion motion; u16 destination_y, destination_x, crash; } helicopter;
    struct { TileIndex home_tile; u8 offset_y, offset_x; u16 destination_y, destination_x; } ship;
    CityMapEventMotion bulldozer;
    struct { CityMapEventMotion motion; u16 upper_pose, lower_pose, action; } monster;
    struct { CityMapEventMotion motion; u16 reserved[2], trigger_disaster; } explosion;
    CityMapEventMotion police;
    CityMapEventMotion fire;
    CityMapEventMotion sailboat;
    struct { TileIndex next_tile; u16 animation_frame; } train;
    CityMapEventMotion military;
    CityMapEventMotion tornado;
    struct { CityMapEventMotion motion; u16 destination_y, destination_x, goal; } maxis_man;
    CityMapEventMotion motion;
    struct { u16 target_y, target_x, destination_y, destination_x, goal, reserved_word; } raw;
    struct { u8 object_data[10]; u16 hash_next; };
  };
} CityMapEvent;

enum { CITY_NATIVE_EVENT_BYTES = 22, CITY_STOCK_EVENT_BYTES = 12 };

_Static_assert(offsetof(CityMapEvent, type) == 0, "native event type layout");
_Static_assert(offsetof(CityMapEvent, direction) == 1, "native event direction layout");
_Static_assert(offsetof(CityMapEvent, state) == 2, "native event state layout");
_Static_assert(offsetof(CityMapEvent, age) == 4, "native event age layout");
_Static_assert(offsetof(CityMapEvent, previous_overlay) == 6, "native event overlay layout");
_Static_assert(offsetof(CityMapEvent, motion.offset_y) == 12, "native event Y fraction layout");
_Static_assert(offsetof(CityMapEvent, motion.reserved_y) == 13, "native event Y reserve layout");
_Static_assert(offsetof(CityMapEvent, motion.offset_x) == 14, "native event X fraction layout");
_Static_assert(offsetof(CityMapEvent, motion.reserved_x) == 15, "native event X reserve layout");
_Static_assert(offsetof(CityMapEvent, raw.destination_y) == 16, "native event destination Y layout");
_Static_assert(offsetof(CityMapEvent, raw.destination_x) == 18, "native event destination X layout");
_Static_assert(offsetof(CityMapEvent, raw.goal) == 20, "native event goal layout");
_Static_assert(offsetof(CityMapEvent, ship.home_tile) == 12, "ship home tile layout");
_Static_assert(offsetof(CityMapEvent, ship.offset_y) == 16, "ship Y fraction layout");
_Static_assert(offsetof(CityMapEvent, ship.offset_x) == 17, "ship X fraction layout");
_Static_assert(offsetof(CityMapEvent, ship.destination_y) == 18, "ship destination Y layout");
_Static_assert(offsetof(CityMapEvent, ship.destination_x) == 20, "ship destination X layout");
_Static_assert(offsetof(CityMapEvent, reserved_byte) == 5, "reserved byte fills age/height padding");
_Static_assert(offsetof(CityMapEvent, hash_next) == CITY_NATIVE_EVENT_BYTES,
    "native saves contain only the persistent event prefix");
_Static_assert(offsetof(CityMapEvent, tile) == 8, "event tile alignment");
_Static_assert(sizeof(CityMapEvent) == 24, "native event record size");

typedef struct City City;
CityPoint ReadCityMapEventPosition(const City *city, const CityMapEvent *event);
CityPoint ReadCityMapEventTarget(const City *city, const CityMapEvent *event);
// Train targets are map coordinates; all other targets are native scaled offsets.
void SetCityMapEventTarget(City *city, CityMapEvent *event, int x, int y);
void ReindexCityMapEvents(City *city, int width, int height);
void DecodeCityMapEventCoordinates(City *city, CityMapEvent *event, int x, int y, int target_x, int target_y);
void EncodeStockCityMapEvent(const City *city, u8 bytes[CITY_STOCK_EVENT_BYTES], const CityMapEvent *event);
void DecodeStockCityMapEvent(City *city, CityMapEvent *event, const u8 bytes[CITY_STOCK_EVENT_BYTES]);

// During approach, destination is the runway reference point, not the offset waypoint.
// The orientation rotates with the map; derive the signed offset only when needed.
void ReadCityAirplaneApproachOffset(const CityMapEvent *event, int *x, int *y);

typedef struct {
  CityMapEvent *event;
  u16 *buckets; // Slot + 1 links; zero is empty, including when slot zero is indexed.
  u16 capacity;
} CityMapEvents;

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
void RebuildCityEventHash(City *city);
void ImportStockCityEventLinks(City *city);
int ReadOverlay(const City *city, TileIndex tile);
u16 ReadCityEventUnderlay(const City *city, TileIndex tile);

// Runtime

typedef struct City City;

int FindFreeCityEventSlot(City *city);
void FinishCityAirplaneApproach(CityMapEvent *event);
void ClearCityMapEventSlot(City *city, int slot);

// Dispatch uses slots 1..count, including the final one-based slot.
enum { CITY_CIVIC_DISPATCH_UNITS = 30, CITY_MILITARY_DISPATCH_UNITS = 5 };

typedef struct {
  u32 timer_deadline;
  i16 tick_sample;
  i16 tick_average;
  u16 animation_phase;
  i16 police_unit_count;
  i16 fire_unit_count;
  i16 military_unit_count;
  i16 police_index;
  i16 fire_index;
  i16 military_index;
  u32 police_blocked_warning;
  u32 fire_blocked_warning;
  u32 military_blocked_warning;
  u16 police_previous_x[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 police_previous_y[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 fire_previous_x[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 fire_previous_y[CITY_CIVIC_DISPATCH_UNITS + 1];
  u16 military_previous_x[CITY_MILITARY_DISPATCH_UNITS + 1];
  u16 military_previous_y[CITY_MILITARY_DISPATCH_UNITS + 1];
} CityEventRuntime;

// Tables

// Native immutable values; no executable mapping is needed.
extern const i8 kCityMapEventSubtileYStep[8];
extern const i8 kCityMapEventSubtileXStep[8];
extern const i8 kCityMapEvent12StepY[8];
extern const i8 kCityMapEvent12StepX[8];
extern const i8 kCityMapEvent16StepY[8];
extern const i8 kCityMapEvent16StepX[8];
extern const i8 kCityEventAdjacentYOffset[8];
extern const i8 kCityEventAdjacentXOffset[8];
extern const u8 kCityMapEventSubtileSpeed[17];
extern const u8 kCityEventDirectionSearchOrder[7];
extern const u8 kCityEventTile237DirectionOrderA[4];
extern const u8 kCityEventTile237DirectionOrderB[4];
extern const i8 kCityEventTile237DirectionYOffset[4];
extern const i8 kCityEventTile237DirectionXOffset[4];
extern const i8 kCityEventOverlayBlockYOffset[8];
extern const i8 kCityEventOverlayBlockXOffset[8];
extern const i8 kCityEventTurnClockwiseOrder[9];
extern const i8 kCityEventTurnCounterclockwiseOrder[9];
extern const i8 kBorderWaterTileYOffsets[4];
extern const i8 kBorderWaterTileXOffsets[4];
extern const i8 kCityEventDestinationYOffset[8];
extern const i8 kCityEventDestinationXOffset[8];
extern const u8 kCityMapEventInitialAge[24];
extern const i8 kCityTrainSpawnYStep[4];
extern const i8 kCityTrainSpawnXStep[4];
extern const i8 kCityTrainUpdateYStep[16];
extern const i16 kCityTrainUpdateXStep[16];
extern const u8 kCityTrainUpdateAnimation[64];

// The original low-nibble train direction can also select neighboring table bytes.
// The full reachable values are explicit arrays, including those non-cardinal reads.
static inline int CityTrainUpdateYStep(int direction) {
  return kCityTrainUpdateYStep[direction];
}

static inline int CityTrainUpdateXStep(int direction) {
  return kCityTrainUpdateXStep[direction];
}

static inline int CityTrainUpdateAnimation(int direction, int next_direction) {
  return kCityTrainUpdateAnimation[4 * direction + next_direction];
}

// Actors

typedef struct City City;

void UpdateCityMapExplosion(City *city, int slot);
void UpdateCityMapShip(City *city, int slot);
bool IsCityEventTileUnoccupiedAndBuildable(City *city, TileIndex tile_index);
void SpawnBorderWaterEvent(City *city, TileIndex source);
void CreateCityMapMaxisMan(City *city, int disaster_type);
int CreateCityMapEffectEvent(City *city, TileIndex tile, int variant, int state, int linked);
void UpdateCityMapMaxisMan(City *city, int slot);
void UpdateCityMapTrain(City *city, int slot);
void UpdateCityMapTornado(City *city, int slot);

// Movement

typedef struct City City;

void AdvanceCityMapEventPosition(City *city, int mode, int slot, int direction);
bool AdvanceCityMapEventPosition12(City *city, int slot);
bool AdvanceCityMapEventPosition16(City *city, int slot);
bool CanCityEventAdvanceInDirection(City *city, int slot, int direction);
int TurnCityEventDirectionToward(int direction, int target);
int CityEventDirection(int destination_x, int destination_y, int source_x, int source_y);
int SteerCityEventTowardTile(int direction, int x, int y, int target_x, int target_y);

void SetCityMapEventStageAtTileOffset(City *city, int x, int y);
bool CanDispatchCityService(const City *city, int service);
bool HasCityDispatchedUnits(const City *city);
void InitializeCityEmergencyDispatchState(City *city);
void DispatchCityService(City *city, TileIndex tile, int service);
void CreateCityMapEventAtTile(City *city, TileIndex tile, int type, int direction);
void UpdateCityMapEvents(City *city);
void UpdateCityMapEventTimer(City *city);
bool CheckCityEventAdjacentTile(City *city, int slot, int direction);
void UpdateCityMapSailboat(City *city, int slot);
int RandomlyPerturbCityEventDirection(City *city, int direction, int chance);
int ComputeCityEventDirection(int x, int y, int target_x, int target_y);
int CityEventManhattanDistance(int x, int y, int target_x, int target_y);
bool IsCityEventDirectionBlockedBySpecialTile(City *city, int slot, int direction);
bool IsCityBoatPassageTile(const City *city, TileIndex tile);
void FindNextValidCityEventDirection(City *city, int slot);
void UpdateCityMapAirplane(City *city, int slot);
void UpdateCityMapHelicopter(City *city, int slot);
void UpdateCityMapMonster(City *city, int slot);

// Vehicles

typedef struct City City;

bool IsCityEventTargetTile(City *city, TileIndex tile_index, int event_type);
void CopyCityMapLinkedEventRecord(City *city, int source_slot, int destination_slot);
void SwapCityMapEventPositions(City *city, int first_slot, int second_slot);
i32 FindTile237EventDirection(City *city, TileIndex tile_index, int direction, int event_type);
void TrySpawnCityEventType2(City *city, TileIndex tile_index);
void TrySpawnCityEventType1(City *city, TileIndex tile_index, int axis);
bool TryCreateTile237Event(City *city, TileIndex tile_index);
void TrySpawnTile237Events(City *city, TileIndex tile_index);
void TryCreateTile248Event(City *city, TileIndex tile_index);
void TrySpawnTile248Events(City *city, TileIndex tile_index);

#endif
