#ifndef SC2K_DOMAIN_SRC_UI_TOOLS_TOOLS_H
#define SC2K_DOMAIN_SRC_UI_TOOLS_TOOLS_H

#include "city/engine/input.h"
#include "city/tools/types.h"
#include "ui/tools/types.h"

typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityMapViewConfig CityMapViewConfig;
typedef struct CityResourceState CityResourceState;

// Frame Tools

// Local map control visibility and initial map-drawing readiness.
void SetCityMapControlsVisible(CityInterfaceState *ui, bool visible);
void ShowCityMapToolWindows(City *city);

// Publish the pending selection's tile damage, then clear its redraw request. Does not advance City or RNG.
void FlushCityMapSelectionRedraw(City *city);

// Format the selected tool's borrowed asset name/cost or terrain help into the caller's bounded buffer.
void FormatCityToolStatusText(const CityResourceState *assets, char *text, u32 capacity, int tool);

// Tool Presentation

// Tool presentation uses the supplied City's UI; mutations stay on the command path.
void EnableCityFacilityToolbarItem(City *city, int index);

void SetCityToolToolbarStyle(City *city, int button, int style);
void ShowCityToolHelp(City *city, int topic);

// Toolbar State

void InitializeCityToolbars(CityInterfaceState *ui);
int GetCityToolbarStyle(const CityInterfaceState *ui, int index);
void SetCityToolbarStyle(CityInterfaceState *ui, int index, int style);

// Toolbar

// Read local tool selection; variants without a defined sound retain previous_sound. No mutation or RNG.
int GetCityToolPlacementSoundId(const CityInterfaceState *ui, bool terrain_editor, int previous_sound);
// Resolve the current UI selection and ask Audio to preload it; failed preloads retain the previous recording.
bool PreloadSelectedCityToolSound(Game *game);

void SwitchCityMapOverlayFlagBank(City *city, int refresh_button);
void SelectCityToolGroupAndMode(Game *game, u32 group, u32 variant);
void ResetCityToolboxForTerrainEditing(City *city);

void RefreshCityToolbarButtonStyles(City *city, bool reset_display_flags);
// Save one toolbar style set and disable nonseparators; restore only items that are still nonseparators.
// The dialog lifetime module calls these for the first pause and final dismissal, never once per nested dialog.
// No allocation, City mutation, palette work or input pumping occurs.
void SuspendCityToolbarForDialog(CityInterfaceState *ui);
void RestoreCityToolbarAfterDialog(CityInterfaceState *ui);

typedef enum CityPlacementDragKind { CITY_DRAG_ZONE, CITY_DRAG_LINE, CITY_DRAG_TRANSPORT } CityPlacementDragKind;

extern const u8 kCityToolVariantCounts[CITY_TOOL_GROUP_COUNT];

// Native map input and shared endpoint placement.
void UpdateCityMapToolPreview(Game *game);

// Consume the staged drag through release or cancellation, updating preview and construction counters.
// A supplied command endpoint bypasses polling. NULL view uses the main map; a supplied view is borrowed.
// Returns the endpoint, or INVALID_TILE for rejection/cancellation. Nested commands retain their live overwrites.
TileIndex TrackCityPlacementDrag(Game *game, CityMapViewConfig *view, TileIndex start,
    int unit_cost, CityPlacementDragKind kind);

void BeginCityMapPointerInput(Game *game, CityPoint point);

// Project a valid endpoint into main-map input coordinates and confirm that picking returns the same tile.
// Return a release sample on success, or a cancellation sample if the bounded search finds no matching point.
CityZoneDragInput Sc2kZoneDragEndpointInput(Game *game, TileIndex tile);

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

// Translate a canvas tool ID into group bits 0..7, variant bits 8..15 and demolition variant bits 16..23.
// Return -1 for an unsupported tool. road_variant applies only to CITY_CANVAS_TOOL_ROAD and accepts
// CITY_ROAD_ROAD through CITY_ROAD_BUS_DEPOT; explicit bridge tools have their own IDs.
i32 Sc2kCanvasMapPointerTools(i32 tool, i32 road_variant);
// Pure UI tool-to-kind translation for building/facility artwork. Other tools return TK_CLEAR, never sprite IDs.
TileKind CityToolBuildingKind(int tool);
u32 Sc2kCurrentCityMapPointerTools(CityInterfaceState *ui);

// The enclosing command keeps staged samples/points alive; index must be below its count.
// Reading the point does not advance the cursor. Modifier application changes local UI key state only.
CityPoint ReadCityCommandPointer(City *city, u32 index);
void ApplyCityCommandPointerModifiers(City *city, u32 index);

bool RunCityPointerGesture(Game *game, const CityPointerInput *input, u32 count, int options, int tools);

bool SelectCityToolWithModifiers(Game *game, int group, int variant, u32 modifiers);

void UpdateCityToolStatus(City *city);

TileIndex TrackCityZoneDrag(Game *game, TileIndex start, int unit_cost);

#endif
