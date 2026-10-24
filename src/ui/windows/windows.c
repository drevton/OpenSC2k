#include "ui/updates.h"
#include "ui/windows/windows.h"

#include "assets/state.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/file_browser.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/spans.h"
#include "ui/map_views.h"
#include "ui/signs.h"
#include "ui/state.h"
#include "ui/windows/window_internal.h"

// Window policy is shared by opening, hit testing, resizing and tooltips.
static const CityWindowTraits kWindowTraits[CITY_CANVAS_WINDOW_KIND_COUNT] = {
  [0] = {.procedure = DefaultWindowProcedure},
  [CITY_CANVAS_NEWSPAPER] = {.procedure = NewspaperWindowProcedure, .picture_images = 1,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_SIGN_LIST] = {.procedure = SignWindowProcedure, .update_categories = CITY_UPDATE_SIGNS,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE,
      .minimum_width = CITY_SIGN_LIST_WIDTH, .minimum_height = 90, .default_width = CITY_SIGN_LIST_WIDTH,
      .default_height = CITY_SIGN_LIST_HEIGHT},
  [CITY_CANVAS_SIGN_EDIT] = {.procedure = SignWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_POLICIES] = {.procedure = PoliciesWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_POLICIES | CITY_UPDATE_FINANCES | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_BONDS] = {.procedure = BondsWindowProcedure, .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE,
      .update_categories = CITY_UPDATE_FINANCES,
      .minimum_width = CITY_BOND_WINDOW_WIDTH, .minimum_height = CITY_BOND_WINDOW_MIN_HEIGHT,
      .default_width = CITY_BOND_WINDOW_WIDTH, .default_height = CITY_BOND_WINDOW_HEIGHT},
  [CITY_CANVAS_MAP_DATA] = {.procedure = MapDataWindowProcedure,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_POPULATION,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE,
      .minimum_width = CITY_MAP_DATA_MIN_WIDTH, .minimum_height = CITY_MAP_DATA_MIN_HEIGHT,
      .default_width = CITY_MAP_DATA_DEFAULT_WIDTH, .default_height = CITY_MAP_DATA_DEFAULT_HEIGHT},
  [CITY_CANVAS_LAND_USE] = {.procedure = LandUseWindowProcedure,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_POPULATION,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_JUKEBOX] = {.procedure = MusicWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .picture_images = CITY_MUSIC_SPRITE_COUNT},
  [CITY_CANVAS_MUSIC_PROGRAMME] = {.procedure = MusicWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_GAME_OPTIONS] = {.procedure = OptionsWindowProcedure,
      .flags = CITY_WINDOW_SINGLETON | CITY_WINDOW_RESIZABLE, .minimum_width = GAME_OPTIONS_MIN_WIDTH,
      .minimum_height = GAME_OPTIONS_HEIGHT, .default_width = GAME_OPTIONS_WIDTH,
      .default_height = GAME_OPTIONS_HEIGHT},
  [CITY_CANVAS_HISTORY] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_HISTORY},
  [CITY_CANVAS_DEMOGRAPHICS] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_POPULATION},
  [CITY_CANVAS_NEIGHBORS] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .picture_images = CITY_CANVAS_NEIGHBOR_ICON_COUNT,
      .update_categories = CITY_UPDATE_NEIGHBORS | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_INDUSTRY] = {.procedure = ChartsWindowProcedure, .flags = CITY_WINDOW_SINGLETON,
      .update_categories = CITY_UPDATE_INDUSTRY | CITY_UPDATE_FINANCES},
  [CITY_CANVAS_SCENARIO_LIST] = {.procedure = ScenarioWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_ABANDON] = {.procedure = LeaveWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_EXIT] = {.procedure = LeaveWindowProcedure, .flags = CITY_WINDOW_SINGLETON},
  [CITY_CANVAS_FACILITY_INFORMATION] = {.procedure = InformationWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON,
      .update_categories = CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_FINANCES | CITY_UPDATE_POPULATION},
  [CITY_CANVAS_CONFIRM] = {.procedure = MessageWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_SAVE_QUESTION] = {.procedure = FileQuestionWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_FILE_QUESTION] = {.procedure = FileQuestionWindowProcedure, .flags = CITY_WINDOW_DONE_BUTTON},
  [CITY_CANVAS_FILE_SAVE] = {.procedure = FileWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_FILE_BROWSER_WIDTH, .minimum_height = 180, .default_width = CITY_FILE_BROWSER_WIDTH,
      .default_height = CITY_FILE_BROWSER_HEIGHT},
  [CITY_CANVAS_FILE_OPEN] = {.procedure = FileWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_FILE_BROWSER_WIDTH, .minimum_height = 180, .default_width = CITY_FILE_BROWSER_WIDTH,
      .default_height = CITY_FILE_BROWSER_HEIGHT},
  [CITY_CANVAS_SPAN_STYLE] = {.procedure = SpanWindowProcedure, .flags = CITY_WINDOW_RESIZABLE,
      .minimum_width = CITY_SPAN_CHOICE_WIDTH,
      .minimum_height = CITY_SPAN_LIST_TOP + CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT,
      .default_width = CITY_SPAN_CHOICE_WIDTH,
      .default_height = CITY_SPAN_LIST_TOP + CITY_SPAN_ROW_HEIGHT + CITY_SPAN_FOOTER_HEIGHT},
  [CITY_CANVAS_MAP_VIEWPORT] = {.procedure = MapViewportWindowProcedure,
      .flags = CITY_WINDOW_RESIZABLE | CITY_WINDOW_SHADEABLE, .minimum_width = CITY_VIEWPORT_WIDTH,
      .minimum_height = 100, .default_width = CITY_VIEWPORT_WIDTH, .default_height = CITY_VIEWPORT_HEIGHT},
  [CITY_CANVAS_TILE_INFORMATION] = {.procedure = InformationWindowProcedure},
  [CITY_CANVAS_BUDGET] = {.procedure = BudgetWindowProcedure,
      .update_categories = CITY_UPDATE_FINANCES | CITY_UPDATE_POLICIES},
  [CITY_CANVAS_BUDGET_RATE] = {.procedure = BudgetRateWindowProcedure},
  [CITY_CANVAS_NOTICE] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_ADVISOR] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_COMMAND_ERROR] = {.procedure = MessageWindowProcedure},
  [CITY_CANVAS_LABEL_EDIT] = {.procedure = NameEditorWindowProcedure},
  [CITY_CANVAS_STADIUM_TEAM] = {.procedure = NameEditorWindowProcedure},
  [CITY_CANVAS_TOOL_STATUS] = {.procedure = ToolStatusWindowProcedure, .flags = CITY_WINDOW_NO_KEYBOARD},
  [CITY_CANVAS_STARTUP_MENU] = {.procedure = StartupWindowProcedure},
  [CITY_CANVAS_CONSOLE] = {.procedure = ConsoleWindowProcedure},
  [CITY_CANVAS_CONSTRUCTION] = {.procedure = ConstructionWindowProcedure},
};

const CityWindowTraits *ReadCityWindowTraits(WindowKind kind) {
  return &kWindowTraits[(unsigned)kind < CITY_CANVAS_WINDOW_KIND_COUNT ? kind : 0];
}

void ResetCityWindowSize(Window *window) {
  const CityWindowTraits *traits = ReadCityWindowTraits(window->kind);
  window->shaded = false;
  window->bounds.right = window->bounds.left + traits->default_width;
  window->bounds.bottom = window->bounds.top + traits->default_height;
}

bool CityWindowCloseAt(const Window *window, int x, int y) {
  if (CanvasCloseAt(window->bounds, x, y)) return true;
  if (window->message_buttons || !(ReadCityWindowTraits(window->kind)->flags & CITY_WINDOW_DONE_BUTTON)) return false;
  CityRect b = window->bounds;
  return CanvasPointInRect((CityRect){b.right - 51, b.bottom - 24, b.right - 10, b.bottom - 9}, x, y);
}

CityRect CityWindowResizeGrip(const Window *window) {
  const CityWindowTraits *traits = ReadCityWindowTraits(window->kind);
  if (!(traits->flags & CITY_WINDOW_RESIZABLE) ||
      ((traits->flags & CITY_WINDOW_SHADEABLE) && window->shaded) ||
      (window->kind == CITY_CANVAS_SPAN_STYLE && !window->span_styles)) return (CityRect){0};
  CityRect b = window->bounds;
  return (CityRect){b.right - 12, b.bottom - 12, b.right, b.bottom};
}

// The three right-aligned caption controls use logical window coordinates.
CityRect CityCaptionControlBounds(const Window *window, CityCaptionControl control) {
  CityRect b = window->bounds;
  int left = b.right - (CITY_CAPTION_STICKY - control + 1) * 11;
  return (CityRect){left, b.top, left + 11, b.top + 14};
}

CityCaptionControl CityCaptionControlAt(const Window *window, int x, int y) {
  for (int control = CITY_CAPTION_SHADE; control <= CITY_CAPTION_STICKY; ++control)
    if (CanvasPointInRect(CityCaptionControlBounds(window, control), x, y)) return control;
  return CITY_CAPTION_NONE;
}

static void DrawWindowHighlight(Canvas *canvas, const Window *window) {
  if (!window->highlighted && !window->highlight_pending) return;
  CityRect b = window->bounds;
  CanvasFill(canvas, b.left, b.top, b.right, b.top + 1, 0xFFFFFFFF);
  CanvasFill(canvas, b.left, b.bottom - 1, b.right, b.bottom, 0xFFFFFFFF);
  CanvasFill(canvas, b.left, b.top + 1, b.left + 1, b.bottom - 1, 0xFFFFFFFF);
  CanvasFill(canvas, b.right - 1, b.top + 1, b.right, b.bottom - 1, 0xFFFFFFFF);
}

// Enter every paint procedure with a window-local origin, including unscaled
// drawing and input-time button probes. Raster storage stays in the shared framebuffer.
void DrawCityCanvasWindow(Game *game, Canvas *canvas, const Window *window) {
  if (!window->visible) return;
  CityRect bounds = window->bounds;
  Window local_window = *window;
  local_window.bounds = (CityRect){0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
  Canvas local = *canvas;
  local.raster = CanvasRasterView(canvas);
  local.raster.origin_x += bounds.left;
  local.raster.origin_y += bounds.top;
  local.width = local_window.bounds.right;
  local.height = local_window.bounds.bottom;
  local.clip = (CityRect){canvas->clip.left - bounds.left, canvas->clip.top - bounds.top,
      canvas->clip.right - bounds.left, canvas->clip.bottom - bounds.top};
  local.interface_font = game->resources ? &game->resources->interface_font : NULL;
  const CityButtonFeedback *feedback = &game->ui->button_feedback;
  CityRect pressed_button = feedback->bounds;
  local.pressed_button = feedback->active && feedback->window_id == window->window_id ? &pressed_button : NULL;
  CityCanvasButtonProbe probe;
  if (canvas->button_probe) {
    probe = *canvas->button_probe;
    probe.x -= bounds.left; probe.y -= bounds.top;
    local.button_probe = &probe;
  }
  SendWindowEvent(game, &local_window, &(WindowEvent){.kind = WINDOW_PAINT, .canvas = &local});
  DrawWindowHighlight(&local, &local_window);
  if (canvas->button_probe && probe.found) {
    probe.bounds.left += bounds.left; probe.bounds.right += bounds.left;
    probe.bounds.top += bounds.top; probe.bounds.bottom += bounds.top;
    *canvas->button_probe = probe;
  }
}

void KeepCityWindowCaptionVisible(const CityCanvasDesktop *desktop, CityRect *b) {
  int w = b->right - b->left, h = b->bottom - b->top;
  int x = b->left, y = b->top;
  int min_x = w <= desktop->canvas_width ? 0 : 32 - w;
  int max_x = w <= desktop->canvas_width ? desktop->canvas_width - w : desktop->canvas_width - 32;
  int max_y = h <= desktop->canvas_height ? desktop->canvas_height - h : desktop->canvas_height - 14;
  x = IntClamp(x, min_x, max_x);
  y = IntClamp(y, CityMenuBounds(desktop, 0).bottom, max_y);
  *b = (CityRect){x, y, x + w, y + h};
}

int CountCityCanvasWindows(const CityInterfaceState *ui) {
  i32 count = 0;
  for (i32 index = 0; index < ui->desktop.count; ++index)
    count += ui->desktop.windows[index].kind != CITY_CANVAS_TOOL_STATUS &&
        ui->desktop.windows[index].kind != CITY_CANVAS_MAP_VIEWPORT;
  return count;
}
