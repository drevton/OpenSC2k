#ifndef SC2K_SIMULATION_COMMAND_INPUT_H
#define SC2K_SIMULATION_COMMAND_INPUT_H

#include <stddef.h>

#include "city/maps/map_types.h"
#include "city/types.h"

// Shared construction working state. Nested commands retain original overwrites;
// these fields affect costs and map mutations, not just preview presentation.
typedef struct {
  u32 terrain_surcharge_count;
  u16 zone_code;
  i16 direction;
  u32 min_x, min_y;
  u32 max_x, max_y;
  u32 cost;
  i16 endpoint_x, endpoint_y;
  u32 cell_count;
  u16 stage;
} CityConstructionState;

// Immutable command text, independent of the canvas editor that collects it.
typedef struct CityNameCommandInput {
  CityOverlayName name;
  int length;
} CityNameCommandInput;

enum {
  CITY_DRAG_INPUT_RELEASE = 1,
  CITY_DRAG_INPUT_SHIFT_CANCEL = 2,
  CITY_DRAG_INPUT_MASK = 3,
};
typedef struct {
  CityPoint point;
  u32 flags; // CITY_DRAG_INPUT_*
} CityZoneDragInput;
_Static_assert(sizeof(CityZoneDragInput) == 12, "zone drag input payload size");
_Static_assert(offsetof(CityZoneDragInput, flags) == 8, "zone drag input flags offset");
typedef struct CityPointerInput {
  CityPoint point;
  u32 flags;
} CityPointerInput;
_Static_assert(sizeof(CityPointerInput) == 12, "pointer input payload size");
_Static_assert(offsetof(CityPointerInput, flags) == 8, "pointer input flags offset");
// Explicit construction choices are command input, independent of an active window.
typedef struct {
  int span;
  bool border, supplied, endpoint_supplied;
  TileIndex end;
  int unit_cost;
  int terrain_options;
} CityConstructionChoices;

// Native input cursor; each nested command saves and restores its parent's cursor.
typedef struct {
  const CityPoint *points; // Borrowed only by comparison captures.
  const CityPointerInput *samples;
  u32 count;
  u32 index;
  u32 release_flags;
} CityPointerCommandState;

#endif
