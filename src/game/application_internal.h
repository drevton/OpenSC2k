#include "city/economy/types.h"
#include "ui/tools/tools.h"
#ifndef SC2K_APPLICATION_INTERNAL_H
#define SC2K_APPLICATION_INTERNAL_H

#include "ui/input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "city/saveload/runtime.h"
#include "game/settings.h"
#include "ui/frontend.h"
#include "city/state.h"
#include "city/engine/simulation.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "audio/backend.h"
#include "base/diagnostics.h"
#include "ui/dialogs/alerts.h"
#include "assets/catalog.h"
#include "ui/map.h"
#include "ui/news/newspaper.h"
#include "ui/sdl.h"
#include "drawing/drawing.h"
#include "drawing/maps/map_render.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/cursor/cursor.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/scenario.h"
#include "ui/startup/startup.h"
#include "ui/text/text.h"

// Private interfaces shared by the standalone application modules.
// Standalone SDL host. All authoritative edits use CityCommand; drawing uses native render scratch.

enum { CITY_SCROLL_PIXELS_PER_STEP = 20, CITY_WATER_PALETTE_TICK_MS = 27 };

void OffsetCityMapPixels(City *city, CityExtraMapView *view, int dx, int dy);
void PanCityMapWithScroll(City *city, CityExtraMapView *view, const CanvasSDLEvent *event);
bool PrepareCityMapSprites(Game *game);
bool PrepareCityMapSpriteSources(Game *game);
int CityMapSpriteScales(const Drawing *drawing, int scales[2]);
bool DrawCityLoadingScreen(Game *game, CanvasSDL *display, CityIntroStage stage, int completed, int total, bool initial);
bool CreateCityFromTerrainFile(City *city, const CityCreationOptions *options, const char *path);
bool BindCityMapTarget(void *context);
MapCachePixels CityMapCachePixels(Drawing *drawing);
bool AnimateCityCanvasPalette(Game *game, double now);
bool UpdateCityWindowTimers(Game *game, double now);
int PresentCityDesktop(City *city, CanvasSDL *display);
void PaintCityMapPixels(CityRect rect, void *context);

void CityInterfacePixels(Game *game, const CanvasSDLEvent *event, i32 *x, i32 *y);
CanvasSDLEvent TransformCityInterfaceEvent(Game *game, const CanvasSDLEvent *event);
void SyncCityInterface(Game *game, CanvasSDL *display, i32 automatic_change);
void UpdateCityMapZoomLimits(Game *game);
void OpenCityGameOptions(Game *game);
void OpenCityMusicWindow(Game *game, i32 programme);
void ApplyCityMusicAction(Game *game, i32 action, Window *window);
void ApplyCityOptionsAction(Game *game, i32 action, Window *window);
bool PollCityMusic(Game *game);
bool PollCityMapViewControls(City *city);
void OpenCitySignList(City *city);
bool HandleCityMapSignInput(City *city, CityExtraMapView *view, const CanvasSDLEvent *event);
void OpenCitySignEditor(City *city, TileIndex tile);
void ResetCitySignEditor(City *city);
bool PollCitySigns(City *city);
void ApplicationRefreshMapData(City *city);
void ReleaseCityPostedCommands(Game *game);
bool StepCityAnimation(Game *game);
i64 ExecuteRecordedCityCommandWithPayload(Game *game, CityCommand packet, const CityPointerInput *input, u32 count,
    const u8 *text, u32 length);
void RunGameIdle(Game *game, CityIdleInputs inputs);
void ResetCityReplacementPresentation(City *city);
void AdvanceCityBudgetHelper(City *city, int window);
bool RequestCityConfiguration(Game *game);
bool ExecuteRecordedCityCommandWithInput(Game *game, CityCommand packet, const CityPointerInput *input, u32 count);
i64 ExecuteRecordedCityCommand(Game *game, u32 id, i32 a, i32 b, i32 c);
const char *CityCommandFailureSummary(const CityCommand *command);
const char *CityCommandFailureReason(i64 error);
bool IsCityCommandFailureResource(u32 resource);
void ApplicationSyncDialogPause(City *city);
int OpenCityBudgetWindow(City *city);
bool InitializeCityNewspaperWindow(City *city, CityNewspaperDialog *paint);
u32 OpenCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at);
void ApplicationBudgetBegin(City *city, u32 window, bool manual);
void ApplicationBudgetEnd(City *city, u32 window);
void ApplicationBudgetAdvisor(City *city, u32 window, CityBudgetCategory category);
void OpenCityAdvisor(City *city, CityBudgetCategory category);
void ApplyCityBudgetWindowRate(City *city, u32 window, int row, int rate);
void OpenCityBudgetRateEditor(City *city, u32 window, int row);
void ApplicationBudgetSync(City *city);
void ReleaseCityBudgetPresentation(CityInterfaceState *ui);
void ApplicationBudgetPoll(City *city);
void PresentCityLabelEditor(City *city, TileIndex tile, bool facility);
void AnswerCityLabelEditor(City *city, NativeLabelDialog *pending, i32 accept);
void SelectCityStadiumTeam(City *city, NativeStadiumDialog *pending, Window *window, int team);
void AnswerCityStadiumEditor(City *city, NativeStadiumDialog *pending, bool accept);
void PresentCityBondConfirmation(City *city, u32 id, int rate);
void PresentCityTunnelConfirmation(City *city, TileIndex tile);
void PresentCityNeighborConfirmation(City *city, CityCommand command, int cost);
i64 CheckCityConstructionCommand(City *city, CityCommand command);
void PresentCityCommandNotices(City *city, CityResourceNotice *notices);
void ClearCityResourceNotices(CityInterfaceState *ui);
void AnswerCityConfirmation(City *city, u32 window_id, bool accept);
void ApplicationOpenScenarioInstructions(City *city, const char *path);
void RequestCitySave(Game *game);
void PresentCityNewspaperEdition(City *city, i32 edition);

void UpdateCityToolStatus(City *city);
int CountCityCanvasWindows(const CityInterfaceState *ui);
bool PrepareCityCanvas(City *city, CanvasSDL *display);
int PresentCityDesktop(City *city, CanvasSDL *display);
bool RedrawCityCanvas(City *city, CanvasSDL *display);
void RunCitySimulationFrame(Game *game, double deadline);
bool PrepareCityCanvasDamage(City *city);
bool ApplicationPresentMap(Game *game);
void UpdateCityToolCursor(City *city);
void UpdateCityPlacementPreview(City *city);
bool IsCityMapVisibleAt(City *city, int x, int y);
void BeginCityCanvasButtonFeedback(Game *game, const CanvasSDLEvent *event);
void HandleCityCanvasPointerDown(City *city, const CanvasSDLEvent *map_event);
void HandleCityCanvasPointerMove(City *city, const CanvasSDLEvent *map_event);
void HandleCityCanvasPointerUp(City *city, const CanvasSDLEvent *event);
void ScrollCityCanvas(City *city, const CanvasSDLEvent *event);
void ExecuteCityMenuItem(Game *game, const CityMenuItem *item);
void OpenCityPanel(Game *game, int panel);
bool HandleCityMainMenuPointer(Game *game, const CanvasSDLEvent *event);
void DispatchCityCanvasEvent(City *city, CanvasSDLEvent *event);
void ReleaseCityFileNotices(City *city, i32 all);
void ApplicationStartupMenuBeep(Game *game);
bool ScreenshotCityCanvas(City *city, const char *path, int physical);
bool ApplicationNextScriptAction(Game *game);

void PublishCityMapTileDamage(City *city, TileIndex tile);

void OpenCityDeveloperConsole(Game *game);
bool HandleCityDeveloperShortcut(Game *game, const CanvasSDLEvent *event);
CityHotkey RemapCityKey(const CityInterfaceState *ui, CityHotkey key);
void CloseCityScripting(Game *game);
void ReleaseCityConsole(CityInterfaceState *ui);

#endif
