#ifndef SC2K_CITY_INFRASTRUCTURE_TYPES_H
#define SC2K_CITY_INFRASTRUCTURE_TYPES_H

#include "city/types.h"

// City-owned network totals and retained infrastructure counters.
typedef struct {
  u32 power_total;
  u32 power_percent;
  u32 water_total;
  u32 water_percent;
  union {
    u32 capacity_word;
    struct {
      u16 capacity;
      u16 capacity_upper;
    };
  };
  u32 counter_a;
  u32 counter_b;
  u32 counter_c;
  u32 subway_tiles;
  u32 facility_total;
  i16 facility_ratio;
  i16 mayor_rating;
  u32 service_bonus_a;
  u32 service_bonus_b;
  u16 stadium_team_mask;
} CityInfrastructureState;

// Connected-component traversal is retained City work, never stored in tile payloads or presentation owners.
typedef enum CityUtilityStage {
  CITY_UTILITY_COMPLETE, CITY_UTILITY_MEASURE, CITY_UTILITY_DISTRIBUTE,
} CityUtilityStage;

typedef struct CityUtilityTraversal {
  u32 available, demand, transit;
  int source_x, source_y;
  u8 stage; // CityUtilityStage
} CityUtilityTraversal;

// Shared span-query result. Dry approaches are included in native water crossings.
typedef struct CityInfrastructureSpan {
  TileIndex start, end; // Includes dry approaches for native water crossings.
  TileIndex error_tile; // Actual blocker, or INVALID_TILE for a failure without a specific cell.
  int direction, length, error, styles;
} CityInfrastructureSpan;

typedef enum CityInfrastructureKind {
  CITY_INFRASTRUCTURE_ROAD, CITY_INFRASTRUCTURE_RAIL, CITY_INFRASTRUCTURE_POWER,
  CITY_INFRASTRUCTURE_PIPE, CITY_INFRASTRUCTURE_SUBWAY, CITY_INFRASTRUCTURE_HIGHWAY,
  CITY_INFRASTRUCTURE_COUNT,
} CityInfrastructureKind;

// Infrastructure line directions and step tables use west/south/east/north order.
typedef enum CityInfrastructureDirection {
  CITY_LINE_WEST = 0, CITY_LINE_SOUTH = 1,
  CITY_LINE_EAST = 2, CITY_LINE_NORTH = 3,
  CITY_LINE_DIRECTION_COUNT = 4,
  CITY_LINE_DIRECTION_MASK = 3,
} CityInfrastructureDirection;

// Shared line-tracer placement stages.
// Value 6 bypasses military-zone rejection; no stock writer has been found.
typedef enum CityPlacementStage {
  CITY_PLACEMENT_ROAD = 0,
  CITY_PLACEMENT_RAIL = 1,
  CITY_PLACEMENT_POWER = 2,
  CITY_PLACEMENT_SUBWAY = 3,
  CITY_PLACEMENT_PIPE = 4,
  CITY_PLACEMENT_ZONE = 5,
  CITY_PLACEMENT_ALLOW_MILITARY = 6
} CityPlacementStage;

// Transport footprint selectors. Values 4–7 choose inclines, 8–11 bends
// and 12 the junction. Placement maps each selector to a kind and orientation.
typedef enum CityTransportFootprint {
  CITY_TRANSPORT_FOOTPRINT_NONE = -1,
  CITY_TRANSPORT_FOOTPRINT_NS = 0, CITY_TRANSPORT_FOOTPRINT_EW = 1,
  CITY_TRANSPORT_FOOTPRINT_FORCE_NS = 2, CITY_TRANSPORT_FOOTPRINT_FORCE_EW = 3,
  CITY_TRANSPORT_FOOTPRINT_GRADED_4 = 4, CITY_TRANSPORT_FOOTPRINT_GRADED_5 = 5,
  CITY_TRANSPORT_FOOTPRINT_GRADED_6 = 6, CITY_TRANSPORT_FOOTPRINT_GRADED_7 = 7,
  CITY_TRANSPORT_FOOTPRINT_MULTI_8 = 8, CITY_TRANSPORT_FOOTPRINT_MULTI_9 = 9,
  CITY_TRANSPORT_FOOTPRINT_MULTI_10 = 10, CITY_TRANSPORT_FOOTPRINT_MULTI_11 = 11,
  CITY_TRANSPORT_FOOTPRINT_MULTI_12 = 12,
  CITY_TRANSPORT_FOOTPRINT_BRIDGE_13 = 13, CITY_TRANSPORT_FOOTPRINT_BRIDGE_14 = 14,
  CITY_TRANSPORT_FOOTPRINT_ROTATION_0 = 15, CITY_TRANSPORT_FOOTPRINT_ROTATION_1 = 16,
} CityTransportFootprint;

// Transport compatibility groups terrain shapes independently of XTER IDs.
typedef enum CityTransportTerrainClass {
  CITY_TRANSPORT_TERRAIN_FLAT = 0,
  CITY_TRANSPORT_TERRAIN_CLASS_1 = 1, CITY_TRANSPORT_TERRAIN_CLASS_2 = 2,
  CITY_TRANSPORT_TERRAIN_CLASS_3 = 3, CITY_TRANSPORT_TERRAIN_CLASS_4 = 4,
  CITY_TRANSPORT_TERRAIN_CLASS_COUNT = 5,
} CityTransportTerrainClass;

// Span chooser/command values. BridgeStyle and HighwayBridgeStyle define the separate saved head fields.
typedef enum CitySpanStyle {
  CITY_SPAN_CANCEL = -1,
  CITY_SPAN_POWER = 0, CITY_SPAN_RAIL = 1, CITY_SPAN_CAUSEWAY = 2,
  CITY_SPAN_RAISING_BRIDGE = 3, CITY_SPAN_SUSPENSION = 4,
  CITY_SPAN_HIGHWAY = 5, CITY_SPAN_REINFORCED_HIGHWAY = 6,
  CITY_SPAN_WOOD = 7, CITY_SPAN_CONCRETE, CITY_SPAN_ARCH_STEEL,
  CITY_SPAN_SUSPENSION_CONCRETE, CITY_SPAN_SUSPENSION_STEEL, CITY_SPAN_SUSPENSION_YELLOW,
  CITY_SPAN_CANTILEVER_STEEL, CITY_SPAN_CANTILEVER_BROWN, CITY_SPAN_CANTILEVER_RED,
  CITY_SPAN_GIRDER_STEEL, CITY_SPAN_TUBULAR_STEEL, CITY_SPAN_TUBULAR_YELLOW, CITY_SPAN_TUBULAR_SILICON,
  CITY_SPAN_STYLE_COUNT,
  CITY_ROAD_BRIDGE_STYLE_FIRST = CITY_SPAN_WOOD,
  CITY_ROAD_BRIDGE_STYLE_COUNT = CITY_SPAN_STYLE_COUNT - CITY_ROAD_BRIDGE_STYLE_FIRST,
  CITY_ROAD_BRIDGE_PIECES = 6,
  CITY_ROAD_BRIDGE_COMMAND_FLAG = 128,
  CITY_BRIDGE_COMMAND_STYLE_MASK = 31, CITY_BRIDGE_COMMAND_RAIL = 256,
  CITY_BRIDGE_CHOICE_ICON_COUNT = CITY_SPAN_STYLE_COUNT + CITY_ROAD_BRIDGE_STYLE_COUNT,
} CitySpanStyle;

typedef enum CitySpanStyleMask {
  CITY_SPAN_STYLE_POWER_MASK = 1 << CITY_SPAN_POWER,
  CITY_SPAN_STYLE_RAIL_MASK = 1 << CITY_SPAN_RAIL,
  CITY_SPAN_STYLE_CAUSEWAY_MASK = 1 << CITY_SPAN_CAUSEWAY,
  CITY_SPAN_STYLE_RAISING_BRIDGE_MASK = 1 << CITY_SPAN_RAISING_BRIDGE,
  CITY_SPAN_STYLE_SUSPENSION_MASK = 1 << CITY_SPAN_SUSPENSION,
  CITY_SPAN_STYLE_NATIVE_ROAD_MASK = ((1 << CITY_SPAN_STYLE_COUNT) - 1) &
      ~((1 << CITY_ROAD_BRIDGE_STYLE_FIRST) - 1),
  CITY_SPAN_STYLE_ROAD_MASK = CITY_SPAN_STYLE_NATIVE_ROAD_MASK | CITY_SPAN_STYLE_CAUSEWAY_MASK |
      CITY_SPAN_STYLE_RAISING_BRIDGE_MASK | CITY_SPAN_STYLE_SUSPENSION_MASK,
  CITY_SPAN_STYLE_SHORT_MASK = CITY_SPAN_STYLE_POWER_MASK | CITY_SPAN_STYLE_RAIL_MASK |
      CITY_SPAN_STYLE_CAUSEWAY_MASK,
  CITY_SPAN_STYLE_WITH_RAISING_MASK = CITY_SPAN_STYLE_SHORT_MASK | CITY_SPAN_STYLE_RAISING_BRIDGE_MASK,
  CITY_SPAN_STYLES_ALL = (1 << CITY_SPAN_STYLE_COUNT) - 1,
} CitySpanStyleMask;

// Recorded road-span selector byte: styles0..2 map to original span2..4.
// The cancel value7 is distinct from the original dialog's return-1.
typedef enum CityRoadSpanSelection {
  CITY_ROAD_SPAN_BASIC = 0, CITY_ROAD_SPAN_ARCH = 1,
  CITY_ROAD_SPAN_DECORATED = 2, CITY_ROAD_SPAN_STYLE_COUNT = 3,
  CITY_ROAD_SPAN_CANCEL = 7,
} CityRoadSpanSelection;

typedef enum CityBorderConnectionKind {
  CITY_BORDER_CONNECTION_ROAD = 0,
  CITY_BORDER_CONNECTION_RAIL = 1,
  CITY_BORDER_CONNECTION_HIGHWAY_OR_TUNNEL = 2,
} CityBorderConnectionKind;

#endif
