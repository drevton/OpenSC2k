#ifndef SC2K_CITY_INFRASTRUCTURE_HIGHWAYS_H
#define SC2K_CITY_INFRASTRUCTURE_HIGHWAYS_H

#include "city/engine/input.h"
#include "city/infrastructure/types.h"
#include "city/maps/map.h"

// The planning variants write only caller-owned working state, never City or its maps.
typedef struct {
  TileIndex start;
  CityRect land, water;
  i64 cost, land_cost;
  int axis, direction, shape;
  CityInfrastructureSpan span;
} CityCompactHighwayPlan;

// Highway links identify the anchor of a rectangular footprint.
TileIndex CityHighwayAnchor(const City *city, TileIndex tile);
// Compact placement uses the picked tile; existing footprints supply their own anchor.
TileIndex CityHighwayPlacementAnchor(const City *city, TileIndex tile);
// Bounded neighbour reads also work on the dense zone plane during import.
CityRect CityHighwayFootprintBounds(const City *city, TileIndex tile, const u8 *zones, int stride);
SC2K_PRESERVE_MOST int CityHighwayCornerMask(const City *city, int zone);
void InitializeCityHighwayFootprint(City *city, TileIndex top_left);
void UpdateCityHighwayTile(City *city, TileIndex tile_index, int shape);
// Land/curve/junction selectors 0..12; bridges are built as complete native spans.
void UpdateCityHighwayFootprint(City *city, TileIndex tile, int footprint_shape);
void ClearZoneStateAndUpdateTransportSpanNeighbors(City *city, TileIndex tile);
int CityHighwayGradeEdgeHeight(CityTileGeometry geometry, int direction);
bool CityHighwayEdgesAlign(const City *city, CityRect bounds);
void RefreshCityHighwayConnections(City *city, TileIndex tile);
SC2K_PRESERVE_MOST bool IsCityCompactHighwayLine(const City *city, TileIndex start, TileIndex end);
bool PlanCityCompactHighway(const City *city, TileIndex start, TileIndex end, int orientation,
    CityCompactHighwayPlan *plan);
// Retained command preview: endpoints are valid and the caller has selected an existing compact plan or admitted
// a legacy two-by-two footprint (compact == NULL). Update live construction counts/direction and publish damage;
// leave terrain, treasury and RNG unchanged. The plan is borrowed only for this call.
void UpdateCityHighwayPreview(City *city, TileIndex start, TileIndex end, const CityCompactHighwayPlan *compact);
// Price an existing plan with a resolved span style; does not check the treasury.
i64 CityCompactHighwayCost(const CityCompactHighwayPlan *plan, int span_style);
i64 CmdBuildCityCompactHighway(City *city, u32 flags, TileIndex start, TileIndex end, int orientation, int span_style);
// Geometry is read-only; traversal writes only the caller-supplied plan/cursor, which may be City.construction.
bool AdvanceCityHighwayLine(const City *city, CityConstructionState *state, int *x, int *y);
void PrepareCityHighwayLine(const City *city, CityConstructionState *state, int x, int y, int end_x, int end_y);
bool CityHighwayCellAlreadyBuilt(const City *city, TileIndex tile);
CityInfrastructureSpan FindCityHighwaySpan(const City *city, TileIndex origin, int direction, bool choose_direction);
CityInfrastructureSpan FindCityNativeHighwaySpan(const City *city, TileIndex origin, int direction, bool choose_direction);
i64 PlaceCityBridgeOrTunnel(City *city, TileIndex origin_tile, int ui_mode);
i64 PlaceCityNativeTransportLine(City *city, TileIndex origin_tile);
i64 PlaceCityLegacyTransportLine(City *city, TileIndex origin_tile);
i64 PlaceCityTransportLine(City *city, TileIndex origin_tile);
// Span queries take tile indexes; optional queries accept INVALID_TILE.
// Compatibility and height use the northwest tile; height requires a valid 2x2 footprint.
int ClassifyTransportSpanTile(const City *city, TileIndex tile);
int GetMaximumTerrainHeightIn2x2(const City *city, TileIndex tile);
int GetTransportSpanCompatibilityMask(const City *city, TileIndex tile);
int ResolveTransportSpanDirection(const City *city, TileIndex tile);
enum { CITY_ONRAMP_COST = 25 };
// Read-only admission for a dry, flat site adjoining compatible road and highway cells. Invalid tiles return false.
bool CanPlaceCityOnramp(const City *city, TileIndex tile);
// Requires a valid tile. Places and reconnects an admitted entrance; returns success or a negative gameplay error.
i64 PlaceCityHighwayOnramp(City *city, TileIndex tile);

// Remove an admitted linked two-by-two span without charging. Origin selects the sound; anchor starts the span scan.
// Kind is captured before any edits. Shore rebuilding, cell order, water restoration and effects retain their order.
void RemoveCityLinkedHighwaySpan(City *city, TileIndex origin, TileIndex anchor, int kind, bool animate);
// Recalculate terrain after removing a complete nonempty highway footprint. The supplied area must be valid.
void RecalculateCityHighwayFootprintTerrain(City *city, CityTileArea footprint);

bool CityHighwayFootprintHasWater(const City *city, TileIndex anchor);

// View rules do not mutate City. Rotation is a camera turn relative to the stored map.
// Ramp view returns a two-bit artwork variant and a mirror bit at bit 2.
int CityHighwayRampView(const City *city, TileIndex tile, int rotation);
// Drawing height is in map levels; supplied geometry must describe this same tile.
int CityHighwayDrawHeight(const City *city, TileIndex tile, const CityTileGeometry *geometry);
// Highway artwork only: compact carriageways, entrances, shared shapes and highway bridges.
int CityHighwaySpriteFlip(const City *city, TileIndex tile, int layout);
// Compact highway artwork keeps the perpendicular surface in the ground pass and the deck in the object pass.
void DrawCityHighwayTransportArtwork(City *city, TileIndex tile, int layout, int zoom, int screen_x, int baseline,
    const CityTileGeometry *geometry);
// Shared catalog footprints paint their four ground tiles and native road/rail underpasses in the ground pass,
// then submit the shared deck. Underpasses never come from the terrain service.
void DrawCityHighwaySharedFootprint(City *city, TileIndex tile, int layout, int zoom, int screen_x, int ground_y,
    const CityTileGeometry *geometry);
// Paint the road/rail underneath an unmodified native crossing during depth-ground collection.
void DrawCityHighwayCrossingGround(City *city, int layout, int zoom, int screen_x, int ground_y);

#endif
