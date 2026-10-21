#ifndef SC2K_DOMAIN_SRC_UI_TOOLS_TOOLS_H
#define SC2K_DOMAIN_SRC_UI_TOOLS_TOOLS_H

#include "city/tools/types.h"
#include "city/presentation.h"

#include "game/state.h"
#include "city/engine/commands.h"
#include "city/engine/input.h"
#include "drawing/drawing.h"
#include "city/types.h"
#include "city/infrastructure/infrastructure.h"

// Frame Tools

void MarkSelectedCityMenuCommand(Game *game, int command);

// Tool Presentation

// Tool presentation uses the supplied City's UI; mutations stay on the command path.
void EnableCityFacilityToolbarItem(City *city, int index);

void SetCityToolToolbarStyle(City *city, int button, int style);
void ShowCityToolHelp(City *city, int topic);

// Toolbar State

typedef struct CityInterfaceState CityInterfaceState;

void InitializeCityToolbars(CityInterfaceState *ui);
int GetCityToolbarStyle(const CityInterfaceState *ui, int index);
void SetCityToolbarStyle(CityInterfaceState *ui, int index, int style);

// Toolbar
typedef struct Game Game;

void SwitchCityMapOverlayFlagBank(City *city, int refresh_button);
void SelectCityToolGroupAndMode(Game *game, u32 group, u32 variant);
void ResetCityToolboxForTerrainEditing(City *city);

void DisableCityToolbarButton(City *city, int index);

void RefreshCityToolbarButtonStyles(City *city, bool reset_display_flags);
void RefreshCityToolbarAndClearOverlayMenuChecks(City *city);
bool EnterCityPaletteModalState(City *city);
bool LeaveCityPaletteModalState(City *city);

typedef enum CityPlacementDragKind { CITY_DRAG_ZONE, CITY_DRAG_LINE, CITY_DRAG_TRANSPORT } CityPlacementDragKind;

i32 Sc2kCanvasFacilityVariant(i32 tool);
extern const u8 kCityToolVariantCounts[CITY_TOOL_GROUP_COUNT];

// Native map input and shared endpoint placement.
CityZoneDragInput Sc2kNextCityMapZoneInput(Game *game);
u32 UpdateCityMapCursor(Game *game);
void UpdateCityMapToolPreview(Game *game);
TileIndex TrackCityPlacementDrag(Game *game, CityMapViewConfig *view, TileIndex start,
    int unit_cost, CityPlacementDragKind kind);

void BeginCityMapPointerInput(Game *game, CityPoint point);

// Recorded construction input and camera controls enter at the UI boundary.

CityZoneDragInput Sc2kZoneDragEndpointInput(Game *game, TileIndex tile);
void InitializeCityMapView(Game *game, TileIndex location, int zoom);
void RecalculateCityMapClientOrigins(Game *game);
void ReleaseCityMapMouseCapture(Game *game);
void EndCityMapPan(Game *game);

// Resolved local tool intent; no City mutations or cached admission results.
typedef struct {
  TileIndex start, end;
  u32 options;
  int group, variant, size, bulldoze_options, tree_options, terrain_options, highway_facing, facility_facing;
  bool clear, dezone, trees, canal, bridge, highway, line, surface_span, span_tool, endpoint_drag, point;
} CityPlacementIntent;
CityPlacementIntent ResolveCityPlacementIntent(Game *game, int tools, TileIndex start, TileIndex end, u32 options);
bool RunCityMapToolAction(Game *game, const CityPlacementIntent *intent);
bool RunCityLevelTerrain(Game *game, TileIndex start, TileIndex end, int height, int options);

enum {
  CITY_POINTER_MODIFIER_ALT = 1u << 8,
  CITY_POINTER_MODIFIER_SHIFT = 1u << 9,
  CITY_POINTER_MODIFIER_CONTROL = 1u << 10,
  CITY_POINTER_MODIFIER_MASK = CITY_POINTER_MODIFIER_ALT | CITY_POINTER_MODIFIER_SHIFT |
      CITY_POINTER_MODIFIER_CONTROL,
  CITY_POINTER_SAMPLE_RELEASE = 1,
  CITY_POINTER_SAMPLE_MASK = CITY_POINTER_MODIFIER_MASK | CITY_POINTER_SAMPLE_RELEASE,
  CITY_POINTER_EXPLICIT_ANSWERS = 1u << 11,
  CITY_POINTER_SPAN_CHOICE_SHIFT = 12,
  CITY_POINTER_SPAN_CHOICE_MASK = 7u << CITY_POINTER_SPAN_CHOICE_SHIFT,
  CITY_POINTER_ACCEPT_BORDER = 1u << 16,
  CITY_POINTER_UNDERGROUND = 1 << 17
};

i32 Sc2kCanvasMapPointerTools(i32 tool, i32 road_variant);
u32 Sc2kCurrentCityMapPointerTools(CityInterfaceState *ui);
CityPoint ReadCityCommandPointer(City *city, u32 index);
void ApplyCityCommandPointerModifiers(City *city, u32 index);
CityZoneDragInput Sc2kNextZoneDragInput(City *city);

bool RunCityPointerGesture(Game *game, const CityPointerInput *input, u32 count, int options, int tools);

bool SelectCityToolWithModifiers(Game *game, int group, int variant, u32 modifiers);

#endif
