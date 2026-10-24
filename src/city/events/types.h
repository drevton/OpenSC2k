#ifndef SC2K_CITY_EVENTS_TYPES_H
#define SC2K_CITY_EVENTS_TYPES_H

#include <stddef.h>

#include "city/types.h"

// Shared event records and retained cadence/dispatch state. Operations live in events.h and emergency.h.
// Stable event kinds used by saved records and the event dispatcher. The type field stores one byte.
// Bulldozer events have artwork but no movement handler.
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

// Random rail-station probes: paired vertical then horizontal choices.
typedef enum CityRailProbeDirection {
  CITY_RAIL_PROBE_NORTH = 0, CITY_RAIL_PROBE_SOUTH = 1,
  CITY_RAIL_PROBE_WEST = 2, CITY_RAIL_PROBE_EAST = 3,
} CityRailProbeDirection;

typedef enum CityAirplaneSpawnSide {
  CITY_AIRPLANE_SPAWN_NORTH = 0, CITY_AIRPLANE_SPAWN_WEST = 1,
  CITY_AIRPLANE_SPAWN_SOUTH = 2, CITY_AIRPLANE_SPAWN_EAST = 3,
} CityAirplaneSpawnSide;

// Sailboat state byte: sailing until a rare incident stops movement before removal.
typedef enum CitySailboatState { CITY_SAILBOAT_SAILING = 0, CITY_SAILBOAT_INCIDENT = 1 } CitySailboatState;

// Explosions reuse direction as its explosion animation stage, not a heading.
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
typedef struct CityMapEvent {
  u8 type;
  // Aircraft/ships use eight-way headings; trains/sailboats use four-way directions; explosions use stages.
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

enum { CITY_NATIVE_EVENT_BYTES = 22 };

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

typedef struct {
  CityMapEvent *event;
  u16 *buckets; // Slot + 1 links; zero is empty, including when slot zero is indexed.
  u16 capacity;
} CityMapEvents;

// Shared resolved service IDs; these are independent of the locally selected dispatch tool.
typedef enum CityDispatchService {
  CITY_DISPATCH_POLICE = 0, CITY_DISPATCH_FIRE = 1, CITY_DISPATCH_MILITARY = 2,
} CityDispatchService;

// Unit markers use slots 1..count, including the final one-based slot.
enum { CITY_CIVIC_DISPATCH_UNITS = 30, CITY_MILITARY_DISPATCH_UNITS = 5 };

typedef struct CityEmergencyDispatchState {
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
} CityEmergencyDispatchState;

// Logical event cadence and animation phase; the host clock and UI scheduling belong to Game.
typedef struct CityEventRuntime {
  u32 timer_deadline;
  i16 tick_sample;
  i16 tick_average;
  u16 animation_phase;
  CityEmergencyDispatchState dispatch;
} CityEventRuntime;

#endif
