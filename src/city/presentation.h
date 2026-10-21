#ifndef SC2K_CITY_PRESENTATION_H
#define SC2K_CITY_PRESENTATION_H

#include "city/engine/commands.h"
#include "city/types.h"

// Button sets requested by presentation callers.
enum {
  CITY_PROMPT_YES_NO_CANCEL = 3, CITY_PROMPT_YES_NO = 4,
};

// Military-offer and result notices queued by OfferDisasterUnlockBonus.
enum {
  CITY_NOTICE_MILITARY_BASE_OFFER = 240,
  CITY_NOTICE_MILITARY_BASE_ARMY = 241,
  CITY_NOTICE_MILITARY_BASE_AIR_FORCE = 242,
  CITY_NOTICE_MILITARY_BASE_NAVY = 243,
  CITY_NOTICE_MILITARY_BASE_MISSILE_SILOS = 244,
  CITY_NOTICE_MILITARY_BASE_UNSUITABLE = 411,
};

// City Notices

// Shared error and notice codes. Text is selected by UI; these values remain stable for commands and replay.
typedef enum CityNoticeId {
  CITY_NOTICE_ERROR_OPENING_FILE = 47,
  CITY_NOTICE_ERROR_READING_FILE = 48,
  CITY_NOTICE_ERROR_DECOMPRESSING_FILE = 49,
  CITY_NOTICE_UNRECOGNIZED_CITY_FORMAT = 53,
  CITY_NOTICE_INVALID_CITY_CONTENTS = 54,
  CITY_NOTICE_ERROR_WRITING_FILE = 63,
  CITY_NOTICE_PLACEMENT_AT_MAP_EDGE = 105,
  CITY_NOTICE_PLACEMENT_PROTEST = 106,
  CITY_NOTICE_MARINA_REQUIRES_SHORELINE = 107,
  CITY_NOTICE_HYDRO_REQUIRES_WATERFALL = 108,
  CITY_NOTICE_TUNNEL_CANNOT_CROSS_TUNNELS = 111,
  CITY_NOTICE_TUNNEL_EXCEEDS_CITY_LIMITS = 112,
  CITY_NOTICE_TUNNEL_TOO_DEEP = 113,
  CITY_NOTICE_TUNNEL_UNSTABLE_EXIT = 114,
  CITY_NOTICE_TUNNEL_UNDERGROUND_OBSTRUCTION = 115,
  CITY_NOTICE_TUNNEL_REQUIRES_HILLSIDE = 116,
  CITY_NOTICE_NATIONAL_GUARD_DEPLOYED = 119,
  CITY_NOTICE_NO_POLICE_TO_DISPATCH = 120,
  CITY_NOTICE_CANNOT_DISPATCH_POLICE_HERE = 121,
  CITY_NOTICE_NO_FIRETRUCKS_TO_DISPATCH = 122,
  CITY_NOTICE_CANNOT_DISPATCH_FIRETRUCKS_HERE = 123,
  CITY_NOTICE_NO_MILITARY_TO_DISPATCH = 124,
  CITY_NOTICE_CANNOT_DISPATCH_MILITARY_HERE = 125,
  CITY_NOTICE_TREE_REMOVAL_PROTEST = 236,
  CITY_NOTICE_ERROR_EXTRACTING_SCENARIO_RESOURCE = 239,
  CITY_NOTICE_SHORELINE_NEEDS_CLEARING = 255,
  CITY_NOTICE_NEEDS_OPPOSITE_SHORE = 256,
  CITY_NOTICE_CROSSING_BLOCKED = 257,
  CITY_NOTICE_INSUFFICIENT_OPERATING_FUNDS = 258,
  CITY_NOTICE_OPPOSITE_SHORE_TOO_STEEP = 259,
  CITY_NOTICE_SHORELINE_TOO_STEEP = 260,
  CITY_NOTICE_CANNOT_CROSS_OCEAN = 261,
  CITY_NOTICE_SPAN_BLOCKED_BY_CROSSING = 262,
  CITY_NOTICE_SIGN_LIMIT_REACHED = 263,
  CITY_NOTICE_CANNOT_PLACE_LABEL_HERE = 264,
  CITY_NOTICE_MAYOR_PARADE = 284,
  CITY_NOTICE_MISSION_WON = 285,
  CITY_NOTICE_MISSION_FAILED = 286,
  CITY_NOTICE_BANKRUPT = 287,
  CITY_NOTICE_BOND_LIMIT_REACHED = 288,
  CITY_NOTICE_BOND_REPAYMENT_REQUIRES_FUNDS = 290,
  CITY_NOTICE_FISCAL_CRISIS = 292,
  CITY_NOTICE_BOND_CREDIT_TOO_LOW = 293,
  CITY_NOTICE_SELECT_VALID_STADIUM_SPORT = 400,
  CITY_NOTICE_LOAD_CITY_FAILED = 412,
  CITY_NOTICE_ARCOLOGY_EXODUS_BEGUN = 529,
  CITY_NOTICE_ARCOLOGY_EXODUS_COMPENSATION = 530,
  CITY_NOTICE_ACTION_FAILED = 61705,
} CityNoticeId;

// Cursor handles use these original indices in the application object.
typedef enum CityCursorId {
  CITY_CURSOR_ARROW = 0, CITY_CURSOR_BUSY = 1,
  CITY_CURSOR_DEMOLISH = 2, CITY_CURSOR_TREE = 3,
  CITY_CURSOR_RESIDENTIAL = 4, CITY_CURSOR_COMMERCIAL = 5,
  CITY_CURSOR_INDUSTRIAL = 6, CITY_CURSOR_EDUCATION = 7, CITY_CURSOR_POWER = 8,
  CITY_CURSOR_CENTER = 11, CITY_CURSOR_RAISE_TERRAIN = 12,
  CITY_CURSOR_LOWER_TERRAIN = 13, CITY_CURSOR_STRETCH_TERRAIN = 14,
  CITY_CURSOR_LEVEL_TERRAIN = 15, CITY_CURSOR_WATER = 16,
  CITY_CURSOR_RIVER = 17, CITY_CURSOR_FOREST = 18,
  CITY_CURSOR_WATER_UTILITY = 19, CITY_CURSOR_CIVIC = 20,
  CITY_CURSOR_ROAD = 21, CITY_CURSOR_RAIL = 22, CITY_CURSOR_QUERY = 23,
  CITY_CURSOR_REWARDS = 24, CITY_CURSOR_PORT = 25,
  CITY_CURSOR_RECREATION = 26, CITY_CURSOR_SIGN = 27, CITY_CURSOR_DISPATCH = 28,
  CITY_CURSOR_HANDLE_COUNT = 30,
} CityCursorId;

// City toolbar indices diverge from persistent tool groups after Query.
enum {
  CITY_TOOLBAR_BUTTON_ROTATE_CLOCKWISE = 17,
  CITY_TOOLBAR_BUTTON_ROTATE_COUNTERCLOCKWISE = 18,
  CITY_TOOLBAR_BUTTON_ZOOM_OUT = 19, CITY_TOOLBAR_BUTTON_ZOOM_IN = 20,
  CITY_TOOLBAR_BUTTON_CENTER = 21, CITY_TOOLBAR_BUTTON_MAP = 22,
  CITY_TOOLBAR_BUTTON_POPULATION = 23, CITY_TOOLBAR_BUTTON_NEIGHBORS = 24,
  CITY_TOOLBAR_BUTTON_GRAPHS = 25, CITY_TOOLBAR_BUTTON_INDUSTRY = 26,
  CITY_TOOLBAR_BUTTON_BUDGET = 27, CITY_TOOLBAR_BUTTON_SHOW_BUILDINGS = 28,
  CITY_TOOLBAR_BUTTON_SHOW_SIGNS = 29, CITY_TOOLBAR_BUTTON_SHOW_TILE_DETAILS = 30,
  CITY_TOOLBAR_BUTTON_ZONE_GROUND = 31, CITY_TOOLBAR_BUTTON_UNDERGROUND = 32,
  CITY_TOOLBAR_BUTTON_HELP = 33, CITY_TOOLBAR_BUTTON_COUNT = 34,
};

typedef struct Game Game;

// Direct presentation hooks are implemented by UI. They must not choose shared command options.
// Semantic changes to City information, independent of window kinds and widgets.
// Windows subscribe by ORing categories into their 32-bit update_categories mask.
typedef enum CityUpdateCategory {
  CITY_UPDATE_FINANCES = 1 << 0,
  CITY_UPDATE_POPULATION = 1 << 1,
  CITY_UPDATE_INDUSTRY = 1 << 2,
  CITY_UPDATE_HISTORY = 1 << 3,
  CITY_UPDATE_NEIGHBORS = 1 << 4,
  CITY_UPDATE_MAP = 1 << 5,
  CITY_UPDATE_UTILITIES = 1 << 6,
  CITY_UPDATE_POLICIES = 1 << 7,
  CITY_UPDATE_STATUS = 1 << 8,
  CITY_UPDATE_SIGNS = 1 << 9,
  CITY_UPDATE_ALL = CITY_UPDATE_FINANCES | CITY_UPDATE_POPULATION | CITY_UPDATE_INDUSTRY |
      CITY_UPDATE_HISTORY | CITY_UPDATE_NEIGHBORS | CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES |
      CITY_UPDATE_POLICIES | CITY_UPDATE_STATUS | CITY_UPDATE_SIGNS
} CityUpdateCategory;

// Implemented by UI. Coalesces notifications without painting, callbacks or City writes.
// A Game without UI simply discards presentation notifications.
void InvalidateCityInformation(Game *game, u32 categories);

// Command feedback is collected locally; the numerical execution result remains City-owned.
void ReportCityCommandResult(City *city, i64 error);
void ReportCityResourceNotice(City *city, int bitmap, int text);
void ReportCityStadiumPlacement(City *city, TileIndex tile);
void ReportCityTileInspection(City *city, TileIndex tile);
// The command supplies both the reason and the location; UI only presents them.
void ReportCityCommandFailure(City *city, i64 error, TileIndex error_tile);
void ReportCityCommandBlocker(City *city, TileIndex blocker);
void CancelCityCommandFeedback(City *city);

// Tool and window presentation. Resolved command options never come from these notifications.
void RebuildCityToolbar(City *city);
void DisableCityToolbarItem(City *city, int index);
void SelectCityBulldozerFromToolbar(City *city, int group);
void SetCityToolsVisible(Game *game, bool visible);
void RefreshCityRewardToolbar(City *city);
void RestoreCityCursorAfterDialog(City *city);

// Retained pointer callers collect their local input here; shared commands supply explicit endpoints.
TileIndex TrackCityZoneDrag(Game *game, TileIndex start, int unit_cost);
TileIndex TrackInfrastructureLinePlacement(Game *game, TileIndex start, int unit_cost);
TileIndex TrackCityMapZonePlacementDrag(Game *game, TileIndex start, int unit_cost);
u32 BeginCityVolcanoPresentation(Game *game, TileIndex location);
i64 ExecuteEndpointLineCommand(Game *game, CityCommandId id, TileIndex tile, TileIndex end);

// Nonmodal presentation retains each caller until its own completion boundary.
void PresentCityCheatNotice(City *city, const char *text);
bool BeginCityDialogPause(City *city, int token);
bool EndCityDialogPause(City *city, int token);
int ChooseCityConnectionQuestion(City *city, int kind, int cost);
int SelectInfrastructureSpan(City *city, int distance, int enabled_styles);
void ShowCityResourceAlertDialog(City *city, i32 bitmap_id, i32 text_id);
void ShowRankedCityEventModal(City *city, u32 event_code, u32 event_data);
void PresentCityDisasterUnlockOffer(City *city);
void ShowCityMissionOutcome(City *city, int outcome);
void ShowCityLoadWarnings(City *city);
const char *CityNoticeText(int notice);
char *FormatCityErrorReason(const char *reason);
void RefreshCitySpeedStatus(City *city, i32 mode);
void ShowCityMessage(City *city, u32 resource_id);
i32 QueryCityOverlayNameDialog(City *city, int slot, CityNameCommandInput *state);
void ShowCityTileInformationDialog(City *city, TileIndex tile);
int RunPausedCityAction(City *city, int (*action)(City *city, void *context), void *context);
i32 RunPausedCityModalDialog(City *city, void *dialog);
void RefreshCityDrawing(City *city);
void RefreshCityMapTiles(City *city);
void RefreshCityMapViews(Game *game);
u32 SetCityCursor(City *city, int cursor_id, int restore);
void HideCityChartPanel(City *city, int code);
bool OpenInitializedCityNewspaper(City *city, int edition, bool ended);
void FinishCityNewspaperInitialization(City *city, int edition);
void ApplyCityNewspaperPageEffects(City *city, int edition);

// Local presentation state never supplies shared command choices.
void ResetCityNewTools(City *city);
void SelectCityDispatchTool(City *city, int tool);
TileIndex TrackCitySelectedInfrastructureLine(Game *game, TileIndex start, int group);
void ShowCityMapControls(City *city, bool visible);
void RestoreCityMapControls(City *city);
void UpdateCityPolicyControls(City *city, int policy, int cost);
void EnableCityDeveloperTools(City *city);
bool SetCityDemolitionSuppressed(City *city, bool suppressed);
bool IsCityOutcomeNoticeActive(const City *city);
bool IsCityPresentationMinimized(const City *city);
void SetCityProblemNotice(City *city, const char *text);
void SetCityTileScanPresentation(City *city, int direction_mask);
void RestoreCityVolcanoPresentation(City *city, u32 labels);
void StartCityEarthquakePresentation(City *city);

// Host view state is read or applied at the codec's existing boundaries.
void ReadCitySavedView(City *city, int *position, int *mode);
void ApplyCitySavedViewPosition(City *city, int position);
void ApplyCitySavedViewMode(City *city, int mode);
void SetCityLoadedViewCenter(City *city, int x, int y);
void RestoreCityLoadedView(City *city);
// Borrow the magnified view only for this save caller; no allocation or ownership transfer.
void *BeginCitySaveView(City *city);
void EndCitySaveView(City *city, void *saved_view);
void RefreshCityLoadedMusicPreference(City *city);
void SetCitySavePromptActive(City *city, bool active);

typedef enum CityNewspaperInitialization {
  CITY_NEWSPAPER_INITIALIZATION_SKIPPED,
  CITY_NEWSPAPER_INITIALIZATION_READY,
  CITY_NEWSPAPER_INITIALIZATION_NOTICE,
} CityNewspaperInitialization;
CityNewspaperInitialization BeginCityNewspaperOpeningPresentation(City *city, int edition);

i32 CenterCityMapViewAndResetOverlays(City *city, int x, int y, int zoom);
// Publish drawing-only damage without marking deterministic map state as changed.
void PublishCityMapTileDamage(City *city, TileIndex tile);

void ResetCityReplacementPresentation(City *city);

void CenterCityMapOnEvent(City *city, int x, int y);
void InvalidateCityMapEvent(City *city, int slot);
void ShowCityDemolitionEffect(City *city, TileIndex tile, bool large);
void HideCitySimulationPanels(City *city);
enum {
  CITY_CONNECTION_TUNNEL = 3,
  CITY_CONNECTION_ISSUE_BOND = 4,
  CITY_CONNECTION_REDEEM_BOND = 5,
};

#endif
