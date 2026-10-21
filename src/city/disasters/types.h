#ifndef SC2K_CITY_DISASTERS_TYPES_H
#define SC2K_CITY_DISASTERS_TYPES_H

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

#endif
