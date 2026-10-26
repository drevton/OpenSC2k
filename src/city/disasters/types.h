#ifndef SC2K_CITY_DISASTERS_TYPES_H
#define SC2K_CITY_DISASTERS_TYPES_H

#include "city/types.h"

// Disaster

// Pending and processed disaster IDs use u16 storage. Status names cover IDs 0..16.
// ProcessPendingCityDisaster maps 17 to the helicopter crash article; 18 creates
// a crashing airplane. IDs 5 and 17
// record crashes already triggered by map events, without starting a new one.
// The status description table ends at HURRICANE, excluding both 17 and 18.
typedef enum CityDisasterKind {
  CITY_DISASTER_NONE = 0,
  CITY_DISASTER_FIRE = 1,
  CITY_DISASTER_FLOOD = 2,
  CITY_DISASTER_RIOTS = 3,
  CITY_DISASTER_POLLUTION = 4,
  CITY_DISASTER_CRASH = 5,
  CITY_DISASTER_EARTHQUAKE = 6,
  CITY_DISASTER_TORNADO = 7,
  CITY_DISASTER_MONSTER = 8,
  CITY_DISASTER_MELTDOWN = 9,
  CITY_DISASTER_MICROWAVE = 10,
  CITY_DISASTER_VOLCANO = 11,
  CITY_DISASTER_FIRE_STORM = 12,
  CITY_DISASTER_MASS_RIOTS = 13,
  CITY_DISASTER_MAJOR_FLOOD = 14,
  CITY_DISASTER_CHEMICAL_SPILL = 15,
  CITY_DISASTER_HURRICANE = 16,
  CITY_DISASTER_HELICOPTER_CRASH = 17,
  CITY_DISASTER_SCHEDULED_PLANE_CRASH = 18,
  CITY_DISASTER_COUNT = 19,
} CityDisasterKind;

// No affected-tile category has been selected for the pending disaster.
enum { CITY_DISASTER_EVENT_DATA_NONE = -1 };

// Original menu-origin distributions, distinct from the simulation's random scheduling.
enum {
  CITY_FIRE_ORIGIN_SPAN = 40,
  CITY_FIRE_ORIGIN_RADIUS = 20,
  CITY_CENTERED_DISASTER_ORIGIN_MASK = 31,
  CITY_CENTERED_DISASTER_ORIGIN_RADIUS = 15
};

// Chooses an offset around the disaster; these are not route headings.
typedef enum CityDisasterSpawnSide {
  CITY_DISASTER_SPAWN_SOUTH = 0, CITY_DISASTER_SPAWN_EAST = 1,
  CITY_DISASTER_SPAWN_NORTH = 2, CITY_DISASTER_SPAWN_WEST = 3,
} CityDisasterSpawnSide;

// State

typedef struct {
  i32 y;
  i32 x;
} CityDisasterLocation;

typedef struct {
  i16 months[4];
} CityDisasterPeriods;

typedef struct {
  CityDisasterLocation location;
  CityDisasterPeriods periods;
  u32 map_update_pending;
  u16 pending_type;
  u16 last_type;
  u16 last_event_slot;
  i16 event_data;
  // Working event counts, rebuilt during updates and adjusted during spawning/removal.
  i16 train_count; // Surface and subway engines; car records are not counted.
  i16 sailboat_count;
  i16 tornado_count;
  i16 monster_count;
  i16 airplane_count;
  i16 helicopter_count;
  i16 maxis_man_count;
  i16 ship_count;
  u16 border_water_x;
  u16 border_water_y;
  i16 event_decay_ticks;
  u16 event_cleanup_ticks;
} CityDisasterState;

enum { CITY_OPERATION_PENDING = -2 };

// Explicit operation locals retained across animation boundaries. No C stack
// or tile snapshot is retained; resumption uses the live City.
// The active disaster type selects the meaning of phase and the shared working fields;
// phase codes belong to that effect, rather than one sequence shared by every disaster.
typedef struct {
  u8 type, phase;
  i16 iteration, attempts;
  int origin_x, origin_y;
  int x, y, direction, run_length, run_position, remaining;
  u32 update_gate;
  bool applied, terrain_water_allowed;
  TileIndex strike;
  i64 cash_restore;
  u32 label_restore, redraw_counter;
} CityDisasterWork;

#endif
