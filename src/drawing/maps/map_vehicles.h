#ifndef SC2K_DRAWING_MAPS_MAP_VEHICLES_H
#define SC2K_DRAWING_MAPS_MAP_VEHICLES_H

#include "city/events/types.h"
#include "city/types.h"

typedef struct Drawing Drawing;

// Positions are large-zoom pixels with eight fractional bits. This history is
// presentation-only: XTHG records keep their original deterministic cadence.
typedef struct { int x, y, z; } VehiclePosition3D;

typedef struct {
  VehiclePosition3D from, target, offset;
  double started;
  u8 type, train_heading, train_path, train_previous_path; // The cached path uses existing record padding.
  u8 train_reversed; // Retain corrected path history until copied City targets catch up; uses existing padding.
} VehicleMotion;
_Static_assert(sizeof(VehicleMotion) <= 56, "vehicle history remains within its existing 64-bit record size");

typedef struct {
  VehicleMotion *slots;
  int capacity;
  double time;
  int rotation;
  u8 helicopter_frame;
  bool enabled, initialized, animating;
} DrawingVehicles;

// These event kinds participate in vehicle visibility and interpolation, including subway events without artwork.
static inline bool IsCityMapVehicle(int type) {
  return type == CITY_MAP_EVENT_AIRPLANE || type == CITY_MAP_EVENT_HELICOPTER ||
      type == CITY_MAP_EVENT_SHIP || type == CITY_MAP_EVENT_BULLDOZER || type == CITY_MAP_EVENT_SAILBOAT ||
      IsCityMapEventTrain(type);
}

// Clear event-vehicle interpolation and decorative road cars while retaining allocated storage.
void ClearCityVehicleVisualization(Drawing *drawing);
// Sample canonical events using GAME_TIME_UNPAUSED; City, routes and RNG remain unchanged.
// Does nothing when visualization is disabled. Reuses event drawing storage; failed growth retains partial allocations.
// Returns whether a displayed position, train heading or active rotor changed.
bool UpdateCityVehicleAnimation(const City *city, double now);
// Depth for this tile's overlay group in the bound camera, including interpolated vehicle positions.
// Tile must be valid; nonvehicle overlays keep the tile's ground depth.
i32 CityMapOverlayDepth(const City *city, TileIndex tile);

// Event renderer operations. The camera, sprite cache and target must already match zoom.
// Train artwork uses the sampled path/heading or the canonical tile pose when interpolation is disabled.
// tile is the dispatcher's valid world tile, and slot identifies a live train event.
void DrawCityMapTrain(City *city, TileIndex tile, int zoom, int slot);

#endif
