// Airplanes and helicopters own their spawn, approach, collision and flight stages here.
// Destination/stage data stays in the City event record; presentation receives damage and sound requests directly.
// TileInfo supplies kind, surface height and traffic density through map queries; aircraft add no tile metadata.

#include "city/events/events.h"

#include <assert.h>
#include <string.h>

#include "game/audio.h"
#include "city/disasters/types.h"
#include "city/engine/random.h"
#include "city/engine/commands.h"
#include "city/maps/layers.h"
#include "city/presentation.h"
#include "city/state.h"

// Stock collision thresholds: SMALLMED.DAT kinds 113–250, original height / 3.
// These are simulation constants; replacing or resizing artwork must not change collisions.
static const u8 kCityAirplaneCollisionHeights[TK_PLYMOUTH_ARCOLOGY - TK_RESIDENTIAL_FIRST - 1] = {
  [TK_LOWER_CLASS_HOMES_113 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LOWER_CLASS_HOMES_114 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LOWER_CLASS_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_117 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_118 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_MIDDLE_CLASS_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_121 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_122 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_LUXURY_HOMES_LAST - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_GAS_STATION_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_BED_BREAKFAST_INN - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CONVENIENCE_STORE - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_GAS_STATION_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_SMALL_OFFICE_BUILDING - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_OFFICE_BUILDING_1X1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_WAREHOUSE_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CASSIDY_S_TOY_STORE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WAREHOUSE_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CHEMICAL_STORAGE - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_WAREHOUSE_1X1_3 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_INDUSTRIAL_SUBSTATION - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CONSTRUCTION_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CONSTRUCTION_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_ABANDONED_BUILDING_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_ABANDONED_BUILDING_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_CHEAP_APARTMENTS - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_APARTMENTS_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_APARTMENTS_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_NICE_APARTMENTS_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_NICE_APARTMENTS_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONDOMINIUM_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONDOMINIUM_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_CONDOMINIUM_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_SHOPPING_CENTER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_GROCERY_STORE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_OFFICE_BUILDING_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_RESORT_HOTEL - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_BUILDING_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_RETAIL - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_OFFICE_BUILDING_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_OFFICE_BUILDING_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_BUILDING_2X2_5 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_OFFICE_BUILDING_2X2_6 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_WAREHOUSE_2X2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CHEMICAL_PROCESSING_2X2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_FACTORY_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_FACTORY_2X2_5 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_FACTORY_2X2_6 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONSTRUCTION_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CONSTRUCTION_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_ABANDONED_BUILDING_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_2X2_3 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_ABANDONED_BUILDING_2X2_4 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_LARGE_APARTMENT_BUILDING_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_LARGE_APARTMENT_BUILDING_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_CONDOMINIUM_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONDOMINIUM_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_OFFICE_PARK - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_TOWER_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_MINI_MALL - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_THEATRE_SQUARE - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_DRIVE_IN_THEATER - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_OFFICE_TOWER_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 10,
  [TK_OFFICE_TOWER_3X3_3 - TK_RESIDENTIAL_FIRST - 1] = 11,
  [TK_PARKING_LOT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_HISTORIC_OFFICE_BUILDING - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_CORPORATE_HEADQUARTERS - TK_RESIDENTIAL_FIRST - 1] = 12,
  [TK_CHEMICAL_PROCESSING_3X3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_LARGE_FACTORY - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_INDUSTRIAL_THINGAMAJIG - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_FACTORY_3X3 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_LARGE_WAREHOUSE - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_WAREHOUSE_3X3 - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CONSTRUCTION_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_CONSTRUCTION_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_ABANDONED_BUILDING_3X3_1 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_ABANDONED_BUILDING_3X3_2 - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_HYDRO_POWER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WIND_POWER - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_GAS_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_OIL_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_NUCLEAR_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_SOLAR_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_MICROWAVE_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 8,
  [TK_FUSION_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_COAL_POWER_PLANT - TK_RESIDENTIAL_FIRST - 1] = 9,
  [TK_CITY_HALL - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_HOSPITAL - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_POLICE_STATION - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_FIRE_STATION - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_MUSEUM - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_LARGE_PARK - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_SCHOOL - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_STADIUM - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_PRISON - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_COLLEGE - TK_RESIDENTIAL_FIRST - 1] = 7,
  [TK_ZOO - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_STATUE - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_WATER_PUMP - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_RUNWAY_FIRST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_RUNWAY_LAST - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_PIER - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_CRANE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CONTROL_TOWER_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_CONTROL_TOWER_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 6,
  [TK_WAREHOUSE_1X1_4 - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_BUILDING_1X1_1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_BUILDING_1X1_2 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_TARMAC - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_F_15B - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_HANGAR_1X1 - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_SUBWAY_STATION - TK_RESIDENTIAL_FIRST - 1] = 2,
  [TK_RADAR - TK_RESIDENTIAL_FIRST - 1] = 1,
  [TK_WATER_TOWER - TK_RESIDENTIAL_FIRST - 1] = 5,
  [TK_BUS_STATION - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_RAIL_STATION - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_PARKING_LOT_2X2_1 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_PARKING_LOT_2X2_2 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_LOADING_BAY - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_TOP_SECRET - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_CARGO_YARD - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_MAYORS_HOUSE - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_WATER_TREATMENT_PLANT - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_LIBRARY - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_HANGAR_3X3 - TK_RESIDENTIAL_FIRST - 1] = 3,
  [TK_CHURCH - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_MARINA - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_MISSILE_SILO - TK_RESIDENTIAL_FIRST - 1] = 4,
  [TK_DESALINATION_PLANT - TK_RESIDENTIAL_FIRST - 1] = 5,
};

static const u8 kCityEventDirectionSearchOrder[7] = {
  1, 7, 2, 6, 3, 5, 4
};

static const i8 kCityEventDestinationYOffset[8] = {
  0, 3, 3, 3, 0, -3, -3, -3
};

static const i8 kCityEventDestinationXOffset[8] = {
  -3, -3, 0, 3, 3, 3, 0, -3
};

// Spawn a helicopter from a valid airport tile when overlay and pool admission allow it.
// Destination selection consumes City RNG; the caller supplies the developed airport location.
void TrySpawnCityHelicopter(City *city, TileIndex tile_index) {
  assert(IsMapTileValid(city, tile_index));
  int overlay = ReadOverlay(city, tile_index);
  if (IsCityEventOrMarkerOverlay(overlay) || city->disasters.monster_count != 0 ||
      !CanSpawnCityVehicle(city, CITY_MAP_EVENT_HELICOPTER))
    return;
  CityMapEvents *events = &city->events;
  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;
  CityMapEvent *event = &events->event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_HELICOPTER;
  event->direction = CITY_HEADING_SOUTH;
  event->next_slot = CITY_HELICOPTER_TAKEOFF;
  event->tile = tile_index;
  event->height = 0;
  SetCityMapEventTarget(city, event, 128, 128);
  event->helicopter.destination_y = Sc2kCrtRandRange(city, city->map_height);
  event->helicopter.destination_x = Sc2kCrtRandRange(city, city->map_width);
  event->previous_overlay = overlay;
  ++city->disasters.type2_count;
  ResetCityHelicopterSoundTiming(city);
  LinkCityMapEvent(city, slot);
  InvalidateTile(city, tile_index);
  InvalidateCityMapEvent(city, slot);
}

// Decode the approach-waypoint offset for an airplane record's stored direction. Event and x/y outputs must be valid;
// writes only the two integer outputs without changing motion or drawing random values.
void ReadCityAirplaneApproachOffset(const CityMapEvent *event, int *x, int *y) {
  static const i8 kOffsetX[4] = {16, 0, -16, 0};
  static const i8 kOffsetY[4] = {0, -16, 0, 16};
  int axis = (event->next_slot >> 5) & 3;
  *x = kOffsetX[axis];
  *y = kOffsetY[axis];
}

// Spawn an airplane on a runway or at the map border with that runway as its target.
// The caller supplies a valid runway tile and heading; spawn selection and border coordinates consume City RNG.
void TrySpawnCityAirplane(City *city, TileIndex tile_index, int axis) {
  assert(IsMapTileValid(city, tile_index));
  CityPoint point = MapTilePt(city, tile_index);
  int overlay = ReadOverlay(city, tile_index);
  if (IsCityEventOrMarkerOverlay(overlay) || city->disasters.monster_count != 0 ||
      !CanSpawnCityVehicle(city, CITY_MAP_EVENT_AIRPLANE))
    return;
  CityMapEvents *events = &city->events;
  int slot = FindFreeCityEventSlot(city);
  if (slot == 0)
    return;
  CityMapEvent *event = &events->event[slot];
  memset(event, 0, sizeof(*event));
  event->type = CITY_MAP_EVENT_AIRPLANE;
  SetCityMapEventTarget(city, event, 128, 128);
  if (Sc2kCrtRandRange(city, 10) >= 5) {
    event->tile = tile_index;
    event->airplane.destination_y = 20;
    event->airplane.destination_x = 20;
    event->direction = axis;
    event->height = 0;
    event->next_slot = CITY_AIRPLANE_TAKEOFF;
    event->previous_overlay = overlay;
    LinkCityMapEvent(city, slot);
    InvalidateTile(city, tile_index);
  } else {
    // Preserve the stock 10..109 range at 128, extending it along each map axis.
    enum { ENTRY_LOW = 10, ENTRY_HIGH = 18 };
    int width_span = city->map_width - ENTRY_LOW - ENTRY_HIGH;
    int height_span = city->map_height - ENTRY_LOW - ENTRY_HIGH;
    switch (Sc2kCrtRand(city) & 3) {
    case CITY_AIRPLANE_SPAWN_NORTH:
      event->tile = MapTileXY(city, Sc2kCrtRandRange(city, width_span) + ENTRY_LOW, 0);
      event->direction = CITY_HEADING_SOUTH_EAST;
      break;
    case CITY_AIRPLANE_SPAWN_WEST:
      event->tile = MapTileXY(city, 0, Sc2kCrtRandRange(city, height_span) + ENTRY_LOW);
      event->direction = CITY_HEADING_NORTH_EAST;
      break;
    case CITY_AIRPLANE_SPAWN_SOUTH:
      event->tile = MapTileXY(city, Sc2kCrtRandRange(city, width_span) + ENTRY_LOW, city->map_height - 1);
      event->direction = CITY_HEADING_NORTH_WEST;
      break;
    default:
      event->tile = MapTileXY(city, city->map_width - 1, Sc2kCrtRandRange(city, height_span) + ENTRY_LOW);
      event->direction = CITY_HEADING_SOUTH_WEST;
      break;
    }
    event->height = 16;
    event->next_slot = 16 * axis + 3;
    event->airplane.destination_y = point.y;
    event->airplane.destination_x = point.x;
    TileIndex event_tile = event->tile;
    event->previous_overlay = ReadCityEventUnderlay(city, event_tile);
    LinkCityMapEvent(city, slot);
    InvalidateTile(city, event_tile);
  }
  ++city->disasters.type1_count;
  InvalidateCityMapEvent(city, slot);
}

// Materialize the waypoint when leaving the approach stage. Event must be an approaching airplane; transforms its
// stored runway reference once when leaving that stage.
static void FinishCityAirplaneApproach(CityMapEvent *event) {
  if (event->type != CITY_MAP_EVENT_AIRPLANE ||
      (event->state & CITY_AIRPLANE_STAGE_MASK) != CITY_AIRPLANE_APPROACH) return;
  int x, y;
  ReadCityAirplaneApproachOffset(event, &x, &y);
  event->airplane.destination_y += y;
  event->airplane.destination_x += x;
}

// Set a helicopter on this tile to its crash state and play its stage cue. Invalid tiles and other event types
// are no-ops, matching the command's existing admission behavior. The target is not projected through a map view.
void CrashCityHelicopterAtTile(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  int overlay = ReadOverlay(city, tile);
  if (!IsCityEventOverlay(overlay))
    return;
  CityMapEvent *event = &city->events.event[CityEventOverlaySlot(overlay)];
  if (event->type == CITY_MAP_EVENT_HELICOPTER) {
    event->state = CITY_HELICOPTER_CRASH;
    PlayCitySound(city, tile, CITY_SOUND_HELICOPTER_STAGE);
  }
}

// The shared action is free and admits no-op targets. TEST leaves events and audio untouched;
// execution returns completion even when the tile contains no helicopter.
i64 CmdCrashCityHelicopterAtTile(City *city, u32 flags, TileIndex tile) {
  if (flags & CITY_COMMAND_TEST) return 0;
  CrashCityHelicopterAtTile(city, tile);
  return 1;
}

// Probe the next directional event position for an arcology-or-later special building. Requires a valid event slot and
// heading; off-map targets return false and this does not move the event.
static bool IsCityEventDirectionBlockedBySpecialTile(City *city, int slot, int direction) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  int y = position.y + kCityEventDestinationYOffset[direction];
  int x = position.x + kCityEventDestinationXOffset[direction];
  TileIndex target = MapTileXYOrInvalid(city, x, y);
  return target != INVALID_TILE && IsTileKindArcology(ReadTile(city, target));
}

// Search the fixed heading order when the current event direction encounters a special-building obstruction. Requires
// a valid event slot and direction; stores the first unblocked heading without drawing random values.
static void FindNextValidCityEventDirection(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  if (!IsCityEventDirectionBlockedBySpecialTile(city, slot, event->direction))
    return;
  int direction = event->direction;
  for (int index = 0; index < 7; ++index) {
    direction = (kCityEventDirectionSearchOrder[index] + event->direction) & 7;
    if (!IsCityEventDirectionBlockedBySpecialTile(city, slot, direction))
      break;
  }
  event->direction = (u8)direction;
}

// Approach uses coordinate signs: both nonzero axes select a diagonal, regardless of their relative lengths.
// Differences must fit int. Coincident points select east, preserving the approach state's existing choice.
static int CityAirplaneApproachHeading(int x, int y, int target_x, int target_y) {
  int dy = target_y - y;
  int dx = target_x - x;
  if (dy == 0)
    return dx < 0 ? CITY_HEADING_WEST : CITY_HEADING_EAST;
  if (dx == 0)
    return dy > 0 ? CITY_HEADING_SOUTH : CITY_HEADING_NORTH;
  if (dy > 0)
    return dx < 0 ? CITY_HEADING_SOUTH_WEST : CITY_HEADING_SOUTH_EAST;
  return dx < 0 ? CITY_HEADING_NORTH_WEST : CITY_HEADING_NORTH_EAST;
}

// Return the Manhattan distance between two coordinate pairs. Differences and their absolute sum must fit int; this
// calculation neither requires map admission nor changes any event state.
static int CityAircraftManhattanDistance(int x, int y, int target_x, int target_y) {
  int dy = target_y - y;
  int dx = target_x - x;
  return (dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx);
}

// Advance one airplane through its recorded approach, flight and departure states. Requires a live airplane slot and
// consistent target data; may move, convert or clear the event and adjust active counters.
void UpdateCityMapAirplane(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.type1_count;

  TileIndex event_tile = event->tile;
  int tile = ReadTile(city, event_tile);
  // Stock excludes the first house kind from building collision handling.
  if (IsTileKindBuilding(tile) && tile != TK_LOWER_CLASS_HOMES_FIRST &&
      ReadCityZoneKind(&city->tile[event_tile]) != ZONE_AIRPORT) {
    if (IsTileKindArcology(tile)) {
      FinishCityAirplaneApproach(event);
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = NextCityMapRandomModulo(city, 16) == 0;
      event->direction = CITY_EXPLOSION_START;
      return;
    }
    if (kCityAirplaneCollisionHeights[tile - TK_RESIDENTIAL_FIRST - 1] > event->height) {
      FinishCityAirplaneApproach(event);
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
      return;
    }
  }

  switch (event->next_slot & CITY_AIRPLANE_STAGE_MASK) {
  case CITY_AIRPLANE_TAKEOFF:
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    if (event->height == 0)
      PlayCityAirplaneSound(city, slot, CITY_SOUND_AIRPLANE_TAKEOFF);
    if (event->height >= 14)
      event->next_slot = CITY_AIRPLANE_CRUISE;
    else
      ++event->height;
    break;
  case CITY_AIRPLANE_LANDING:
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    --event->height;
    if (event->height == 0) {
      TileIndex landing_tile = event->tile;
      PlayCityAirplaneSound(city, slot, CITY_SOUND_AIRPLANE_LANDING);
      if (ReadTile(city, landing_tile) == TK_RUNWAY_FIRST) {
        ClearCityMapEventSlot(city, slot);
      } else {
        event->type = CITY_MAP_EVENT_EXPLOSION;
        event->next_slot = CITY_DISASTER_CRASH;
        event->explosion.trigger_disaster = 1;
        event->direction = CITY_EXPLOSION_START;
      }
    }
    break;
  case CITY_AIRPLANE_CRUISE:
    event->direction = (u8)RandomlyPerturbCityEventHeading(city, event->direction, 5);
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    break;
  case CITY_AIRPLANE_APPROACH: {
    int offset_x, offset_y;
    ReadCityAirplaneApproachOffset(event, &offset_x, &offset_y);
    int target_x = event->airplane.destination_x + offset_x;
    int target_y = event->airplane.destination_y + offset_y;
    int direction = CityAirplaneApproachHeading(position.x, position.y, target_x, target_y);
    event->direction = (u8)direction;
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    // An off-map waypoint cannot be a runway-alignment arrival.
    if (CityAircraftManhattanDistance(position.x, position.y, target_x, target_y) < 2 &&
        MapTileXYOrInvalid(city, target_x, target_y) != INVALID_TILE) {
      FinishCityAirplaneApproach(event);
      int orientation = event->next_slot >> 4;
      event->direction = (u8)TurnCityEventHeadingToward(direction, orientation);
      event->next_slot = 16 * orientation + CITY_AIRPLANE_ALIGN_RUNWAY;
      switch (orientation) {
      case CITY_HEADING_WEST:
        event->airplane.destination_x -= 6;
        break;
      case CITY_HEADING_SOUTH:
        event->airplane.destination_y += 6;
        break;
      case CITY_HEADING_EAST:
        event->airplane.destination_x += 6;
        break;
      case CITY_HEADING_NORTH:
        event->airplane.destination_y -= 6;
        break;
      default:
        break;
      }
    }
    break;
  }
  case CITY_AIRPLANE_ALIGN_RUNWAY: {
    int direction =
        SteerCityEventTowardPoint(event->direction, position.x, position.y,
            event->airplane.destination_x, event->airplane.destination_y);
    event->direction = (u8)direction;
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);
    if (CityAircraftManhattanDistance(position.x, position.y,
            event->airplane.destination_x, event->airplane.destination_y) < 2) {
      int orientation = event->next_slot >> 4;
      event->direction = (u8)orientation;
      event->next_slot = CITY_AIRPLANE_LANDING;
      if (orientation == CITY_HEADING_WEST || orientation == CITY_HEADING_EAST)
        MoveCityMapEvent(city, slot, MapTileXY(city, position.x, event->airplane.destination_y));
      else if (orientation == CITY_HEADING_SOUTH || orientation == CITY_HEADING_NORTH)
        MoveCityMapEvent(city, slot, MapTileXY(city, event->airplane.destination_x, position.y));
    }
    break;
  }
  case CITY_AIRPLANE_CRASH:
    if (event->height != 0) {
      --event->height;
      if (event->height == 8)
        PlayCityAirplaneSound(city, slot, CITY_SOUND_AIRCRAFT_CRASH);
      int direction = event->direction;
      event->direction = (u8)((direction + 1) & 7);
      AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_AIRPLANE, slot, direction);
      if (event->type == CITY_MAP_EVENT_UNUSED) return;
      position = ReadCityMapEventPosition(city, event);
    } else {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
    }
    break;
  default:
    break;
  }
}

// Advance one helicopter. Slot must contain a live helicopter; may update its target/motion or clear it after its
// final state.
void UpdateCityMapHelicopter(City *city, int slot) {
  CityMapEvent *event = &city->events.event[slot];
  CityPoint position = ReadCityMapEventPosition(city, event);
  ++city->disasters.type2_count;

  if (IsTileKindArcology(ReadTile(city, event->tile))) {
    event->type = CITY_MAP_EVENT_EXPLOSION;
    event->next_slot = CITY_DISASTER_CRASH;
    event->explosion.trigger_disaster = 0;
    event->direction = CITY_EXPLOSION_START;
    return;
  }

  switch (event->next_slot) {
  case CITY_HELICOPTER_TAKEOFF:
    event->direction = (u8)((event->direction + 1) & 7);
    if (event->height >= 10)
      event->next_slot = CITY_HELICOPTER_FLY_TO_TARGET;
    else
      ++event->height;
    break;
  case CITY_HELICOPTER_FLY_TO_TARGET: {
    event->direction =
        SteerCityEventTowardPoint(event->direction, position.x, position.y,
            event->helicopter.destination_x, event->helicopter.destination_y);
    FindNextValidCityEventDirection(city, slot);
    AdvanceCityMapEventPosition(city, CITY_MAP_EVENT_HELICOPTER, slot, event->direction);
    if (event->type == CITY_MAP_EVENT_UNUSED) return;
    position = ReadCityMapEventPosition(city, event);

    if (ReadTrafficDensity(city, MapTileXY(city, (position.x >> 1) * 2, (position.y >> 1) * 2)) >= 0xAA)
      PlayCityAmbientSound(city, event->tile, CITY_SOUND_HELICOPTER_FLIGHT);

    if (CityAircraftManhattanDistance(position.x, position.y,
            event->helicopter.destination_x, event->helicopter.destination_y) < 2) {
      int y = (Sc2kCrtRand(city) & 0x3F) + city->geography.center_y - 32;
      int x = (Sc2kCrtRand(city) & 0x3F) + city->geography.center_x - 32;
      if (y < 0 || y >= city->map_height)
        y = (Sc2kCrtRand(city) & 0x3F) + 32;
      if (x < 0 || x >= city->map_width)
        x = (Sc2kCrtRand(city) & 0x3F) + 32;
      event->helicopter.destination_y = y;
      event->helicopter.destination_x = x;

      if ((Sc2kCrtRand(city) & 1) != 0) {
        TileIndex event_tile = event->tile;
        if (ReadTile(city, event_tile) == TK_CLEAR &&
            ReadTerrainCode(&city->tile[event_tile]) == CITY_TERRAIN_FLAT)
          event->next_slot = CITY_HELICOPTER_LANDING;
      }
    }
    break;
  }
  case CITY_HELICOPTER_LANDING:
    event->direction = (u8)((event->direction + 1) & 7);
    if (event->height <= 2)
      event->next_slot = CITY_HELICOPTER_ON_GROUND;
    else
      --event->height;
    break;
  case CITY_HELICOPTER_ON_GROUND:
    if (Sc2kCrtRandRange(city, 20) == 0)
      event->next_slot = CITY_HELICOPTER_TAKEOFF;
    break;
  case CITY_HELICOPTER_CRASH:
    event->direction = (u8)((event->direction + 2) & 7);
    if (event->height == 4)
      PlayCityAmbientSound(city, event->tile, CITY_SOUND_AIRCRAFT_CRASH);
    if (event->height <= 2) {
      event->type = CITY_MAP_EVENT_EXPLOSION;
      event->next_slot = CITY_DISASTER_HELICOPTER_CRASH;
      event->explosion.trigger_disaster = 1;
      event->direction = CITY_EXPLOSION_START;
    } else {
      --event->height;
    }
    break;
  default:
    break;
  }
}
