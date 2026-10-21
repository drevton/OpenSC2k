#ifndef SC2K_UI_FRONTEND_H
#define SC2K_UI_FRONTEND_H

#include "city/presentation.h"

#include "ui/input.h"
#include "city/engine/commands.h"
#include "ui/sdl.h"
#include "drawing/canvas.h"
#include "drawing/maps/map_render.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/state.h"
#include "city/types.h"
#include "ui/dialogs/dialog_state.h"

// Frontend

typedef struct {
  City *city;
  Canvas canvas;
  CityMapViewConfig *view;
  CityRect destination;
  CityRect source;
} NativeScreenShakePaint;

// Private scoped capture data used by the direct canvas presentation leaves.
bool NativeCaptureNewspaper(City *city, CityNewspaperDialog *paint, NativeNewspaperDialog *dialog);
void NativeReleaseNewspaperPage(NativeNewspaperDialog *dialog);

bool ComposeCityNewspaperPreview(const City *city, int edition, NativeNewspaperDialog *dialog);

// Application Exit
// File choices collect all options before executing the load command.
void RequestCityFileOpen(Game *game);
void RequestCityFilePath(Game *game, const char *path);
void RequestNewCity(Game *game);
void CompleteCityTerrainEditing(Game *game);
void RequestCityTerrainEditor(Game *game);
void RequestCityScenarioOpen(Game *game);
void OpenCityScenarioPath(Game *game, const char *path, bool from_path);
typedef struct City City;
typedef struct Game Game;

void HandleCityExitCommand(City *city);
void OpenCityAbandonDialog(Game *game);
void AnswerCityLeaveDialog(Game *game, bool accept);
void CompleteCityAbandon(Game *game);
void RequestCityApplicationExit(Game *game);

// Deliver pending simulation notices/decisions to this UI after deterministic work returns.
void DeliverCitySimulationEvents(Game *game, int token, i64 result, bool cancelled);

// Map Views

// Borrowed only during synchronous painting/damage traversal.
typedef struct {
  City *city;
  CityExtraMapView *view;
} CityMapPaint;

void ShowCityConstructionCost(City *city, TileIndex tile, i64 cost);
bool UpdateCityCostTexts(Drawing *drawing, double now);
void PrepareCityCostTextDamage(void *context);
void DrawCityCostTexts(void *context);
void PrepareCityDemolitionDamage(void *context);
void DrawCityDemolitionEffects(void *context);
void OpenCityMapView(City *city);
void PublishCityMapChanges(City *city);
void PublishCityEventDamage(MapSpriteFootprints *sprites, void *context);
void PublishCityMainEventDamage(City *city);
void PublishCityRoadCarDamage(City *city, const RoadCar *previous, const RoadCar *current);
void PrepareCityMapAnimations(CityMapAnimationFootprints *previous, void *context);
bool RenderCityMapViews(City *city, bool preserved_source);
void MarkCityMapRect(CityRect rect, void *context);
void PaintCityMapFragment(CityRect rect, void *context);
void RepaintCityMapDamage(CityRect screen, void *context);
bool HandleCityMainMapPan(City *city, const CanvasSDLEvent *event);

i64 RequestCityCheat(Game *game, CityCheat cheat);

typedef struct CityMenuItem CityMenuItem;
void ExecuteCityMenuItem(Game *game, const CityMenuItem *item);

CityRect ClipCityMapViewportPixels(CityRect rect, CityMapPaint *paint);
CityRect CityMapScreenRect(CityRect rect, void *context);
int CityViewportMapZoom(const CityExtraMapView *view);
int CityViewportMagnification(const CityExtraMapView *view);
void SyncCityMapViewSize(const City *city, CityExtraMapView *view, Window *window);

#endif
