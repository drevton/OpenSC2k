#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "drawing/maps/map_view.h"
#include "game/application_internal.h"
#include "drawing/drawing_internal.h"
#include "drawing/bitmaps.h"

#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "ui/map.h"
#include "drawing/drawing.h"

// Map Data
// Drawing subsystem: map data.

enum { MAP_CATEGORY_SIZE = 20, MAP_CONTROL_GROUP_GAP = 10,
  MAP_MAX_OPTIONS = CITY_MAP_BITMAP_OPTIONS, MAP_RESIZE_WIDTH = 12 };

static const char *const kMapOptionNames[CITY_DATA_GROUP_COUNT][MAP_MAX_OPTIONS] = {
  {"Structures", "Zones"}, {"Roads", "Rail", "Traffic"}, {"Power"}, {"Water Supply"},
  {"Density", "Rate of Growth"}, {"Crime Rate", "Police Power", "Police Depts"},
  {"Pollution"}, {"Land Value"}, {"Fire Power", "Fire Depts"}, {"Schools", "Colleges"}
};

static const u8 kMapCategoryIcons[CITY_DATA_GROUP_COUNT] = {
  CITY_MAP_ICON_STRUCTURES, CITY_MAP_ICON_ROADS, CITY_MAP_ICON_POWER, CITY_MAP_ICON_WATER,
  CITY_MAP_ICON_POPULATION, CITY_MAP_ICON_SAFETY, CITY_MAP_ICON_POLLUTION, CITY_MAP_ICON_LAND_VALUE,
  CITY_MAP_ICON_FIRE_COVERAGE, CITY_MAP_ICON_SCHOOL
};

static const u8 kMapOptionIcons[CITY_DATA_GROUP_COUNT][MAP_MAX_OPTIONS] = {
  {CITY_MAP_ICON_STRUCTURES, CITY_MAP_ICON_ZONES},
  {CITY_MAP_ICON_ROADS, CITY_MAP_ICON_RAIL, CITY_MAP_ICON_TRAFFIC},
  {CITY_MAP_ICON_POWER}, {CITY_MAP_ICON_WATER},
  {CITY_MAP_ICON_DENSITY, CITY_MAP_ICON_GROWTH},
  {CITY_MAP_ICON_CRIME, CITY_MAP_ICON_POLICE_COVERAGE, CITY_MAP_ICON_POLICE_STATION},
  {CITY_MAP_ICON_POLLUTION}, {CITY_MAP_ICON_LAND_VALUE},
  {CITY_MAP_ICON_FIRE_COVERAGE, CITY_MAP_ICON_FIRE_STATION}, {CITY_MAP_ICON_SCHOOL, CITY_MAP_ICON_COLLEGE}
};

typedef enum MapButtonKind {
  MAP_BUTTON_CATEGORY,
  MAP_BUTTON_OPTION,
  MAP_BUTTON_OVERLAY,
  MAP_BUTTON_PROJECTION,
} MapButtonKind;
_Static_assert(CITY_MAP_BITMAP_CATEGORIES == CITY_DATA_GROUP_COUNT, "Map cache covers every category");

static int MapButtonSize(const Window *window) {
  // Reserve room for the largest option group, so changing categories never overlaps buttons.
  int count = CITY_DATA_GROUP_COUNT + MAP_MAX_OPTIONS + 2;
  return IntMin(MAP_CATEGORY_SIZE, (window->bounds.right - window->bounds.left - MAP_CONTROL_GROUP_GAP - MAP_RESIZE_WIDTH) / count);
}

CityRect CityMapResizeGripBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - MAP_RESIZE_WIDTH, b.bottom - MAP_RESIZE_WIDTH, b.right, b.bottom};
}

// One geometry definition serves drawing, clicks and hover help. The secondary
// controls stay together at the right edge as the window grows.
static CityRect MapButtonBounds(const Window *window, MapButtonKind kind, int index) {
  CityRect b = window->bounds;
  int size = MapButtonSize(window), left;
  if (kind == MAP_BUTTON_CATEGORY) left = b.left + index * size;
  else if (kind == MAP_BUTTON_OPTION) {
    int group = window->data_map_selection / CITY_DATA_SELECTION_STRIDE;
    int count = CityMapDataOptionCount(group);
    if (count == 1) return (CityRect){0};
    left = b.right - MAP_RESIZE_WIDTH - (2 + count) * size - MAP_CONTROL_GROUP_GAP + index * size;
  } else left = b.right - MAP_RESIZE_WIDTH - size - (kind == MAP_BUTTON_OVERLAY ? size : 0);
  return (CityRect){left, b.bottom - size, left + size, b.bottom};
}

const char *CityMapTooltipAt(const Window *window, int x, int y) {
  if (window->shaded) return NULL;
  if (CanvasPointInRect(CityMapResizeGripBounds(window), x, y)) return "Drag this corner to resize the window.";
  static const char *const kCategories[] = {
    "Show buildings and zones.", "Show roads, railways and traffic.", "Show electricity supply.",
    "Show water supply.", "Show population density and growth.", "Show crime and police coverage.",
    "Show pollution.", "Show land values.", "Show fire protection.", "Show schools and colleges."
  };
  for (int tab = 0; tab < CITY_DATA_GROUP_COUNT; ++tab)
    if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_CATEGORY, tab), x, y)) return kCategories[tab];
  int group = window->data_map_selection / CITY_DATA_SELECTION_STRIDE;
  for (int option = 0; option < CityMapDataOptionCount(group); ++option)
    if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_OPTION, option), x, y))
      return kMapOptionNames[group][option];
  if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_OVERLAY, 0), x, y))
    return "Show on map: display the selected data overlay in the city view.";
  if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_PROJECTION, 0), x, y))
    return "Isometric: switch between isometric and top-down map views.";
  if (y < window->bounds.bottom - MapButtonSize(window) - 4)
    return "Click a position on this map to center the city view there.";
  return NULL;
}

static bool IsIsometricMap(const Window *window) {
  return window->city && window->city->game->ui->map_data.isometric;
}

CityRect Sc2kCanvasMapDataPreviewRect(const Window *window) {
  if (window->shaded) return (CityRect){0};
  CityRect area = {window->bounds.left + 4, window->bounds.top + 18,
      window->bounds.right - 4, window->bounds.bottom - MapButtonSize(window) - 4};
  int map_width = IsIsometricMap(window) ? 2 : window->city ? window->city->map_height : 128;
  int map_height = IsIsometricMap(window) ? 1 : window->city ? window->city->map_width : 128;
  int width = IntMin(area.right - area.left, (area.bottom - area.top) * map_width / map_height);
  int height = width * map_height / map_width;
  int left = (area.left + area.right - width) / 2, top = (area.top + area.bottom - height) / 2;
  return (CityRect){left, top, left + width, top + height};
}

static TileIndex MapTileAt(const Window *window, CityRect rect, int x, int y) {
  const City *city = window->city;
  int width = rect.right - rect.left, height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0 || !CanvasPointInRect(rect, x, y))
    return INVALID_TILE;
  int u, v;
  if (IsIsometricMap(window)) {
    int sum = city->map_width + city->map_height;
    int horizontal = (i64)(2 * (x - rect.left) + 1) * sum * 128 / width - city->map_width * 256;
    int vertical = (i64)(2 * (y - rect.top) + 1) * sum * 128 / height;
    u = (vertical + horizontal) >> 9;
    v = (vertical - horizontal) >> 9;
  } else {
    u = (x - rect.left) * city->map_height / width;
    v = (y - rect.top) * city->map_width / height;
  }
  // The preview's horizontal axis is world y; its vertical axis is world x.
  return MapTileXYOrInvalid(city, v, u);
}

static u32 BuildMapPreview(Canvas *pixels, const void *context) {
  const Window *window = context;
  CityRect bounds = {0, 0, pixels->width, pixels->height};
  for (int y = 0; y < pixels->height; ++y)
    for (int x = 0; x < pixels->width; ++x) {
      TileIndex tile = MapTileAt(window, bounds, x, y);
      if (!IsMapTileValid(window->city, tile)) continue;
      pixels->pixels[(size_t)y * pixels->stride + x] = 0xFF000000 |
          window->map_preview[(MapTileX(window->city, tile) * 128 / window->city->map_width) * 128 +
              MapTileY(window->city, tile) * 128 / window->city->map_height];
    }
  return 0;
}

static void DrawMapPreview(Canvas *canvas, const Window *window, CityBitmapCache *cache) {
  if (!window->map_preview) return;
  CityBitmapPaint paint = {.bounds = Sc2kCanvasMapDataPreviewRect(window),
      .build = BuildMapPreview, .context = window};
  if (!DrawCityCachedBitmap(canvas, cache, &paint))
    window->city->game->execution_failed = true;
}

static bool DrawMapButton(Canvas *canvas, CitySpriteImage *cache, const CityInterfaceIcons *icons, CityRect rect,
    int icon, bool selected) {
  selected |= DrawCanvasButtonBox(canvas, rect.left, rect.top, rect.right, rect.bottom, canvas_title, !selected);
  return !icons || DrawCityInterfaceIcon(canvas, cache,
      icons, icon, (CityRect){rect.left + 2 + selected, rect.top + 2 + selected,
          rect.right - 2 + selected, rect.bottom - 2 + selected});
}

void DrawCityMapDataWindow(Drawing *drawing, Canvas *canvas, const Window *window) {
  CityRect bounds = window->bounds;
  int group = window->data_map_selection / CITY_DATA_SELECTION_STRIDE;
  int option = window->data_map_selection % CITY_DATA_SELECTION_STRIDE;
  char title[64];
  snprintf(title, sizeof(title), "Map - %s", kMapOptionNames[group][option]);
  if (!window->shaded)
    CanvasBeveledBox(canvas, bounds.left, bounds.top + 14, bounds.right, bounds.bottom, canvas_title, true);
  DrawCityWindowCaption(canvas, bounds, title, canvas_title, 0xFFD3BB93, 0xFF674F2B, 33);
  for (int control = CITY_VIEWPORT_SHADE; control <= CITY_VIEWPORT_STICKY; ++control) {
    CityRect rect = CityViewportControlBounds(window, control);
    bool pressed = (control == CITY_VIEWPORT_SHADE && window->shaded) ||
        (control == CITY_VIEWPORT_STICKY && window->sticky);
    DrawCanvasButtonFrame(canvas, rect, canvas_title, 0xFFD3BB93, 0xFF674F2B, !pressed);
    DrawCityWindowControlIcon(canvas, rect, control == CITY_VIEWPORT_SHADE ? CITY_WINDOW_ICON_SHADE :
        control == CITY_VIEWPORT_DEFAULT_SIZE ? CITY_WINDOW_ICON_DEFAULT_SIZE : CITY_WINDOW_ICON_STICKY, pressed);
  }
  if (window->shaded) return;
  int buttons_top = bounds.bottom - MapButtonSize(window);
  DrawCanvasFrame(canvas, (CityRect){bounds.left + 2, bounds.top + 16, bounds.right - 2, buttons_top - 2},
      0xFF101010, 0xFFD3BB93, 0xFF674F2B, false);
  // Continue the inset map's light/face/dark separator directly into the button row.
  CanvasFill(canvas, bounds.left, buttons_top - 1, bounds.right, buttons_top, 0xFF674F2B);
  CityMapDataWindowDrawing *pixels = window->city->game->ui->map_data.drawing;
  DrawMapPreview(canvas, window, &pixels->preview);
  bool icons_ready = true;
  const CityInterfaceIcons *icons = window->city ? &window->city->game->resources->map_category_icons : NULL;
  for (int tab = 0; tab < CITY_DATA_GROUP_COUNT; ++tab)
    icons_ready &= DrawMapButton(canvas, &pixels->categories[tab], icons,
        MapButtonBounds(window, MAP_BUTTON_CATEGORY, tab), kMapCategoryIcons[tab], tab == group);
  int count = CityMapDataOptionCount(group);
  if (count > 1)
    for (int index = 0; index < count; ++index)
      icons_ready &= DrawMapButton(canvas, &pixels->options[index], icons,
          MapButtonBounds(window, MAP_BUTTON_OPTION, index), kMapOptionIcons[group][index], index == option);
  icons_ready &= DrawMapButton(canvas, &pixels->overlay, icons,
      MapButtonBounds(window, MAP_BUTTON_OVERLAY, 0), CITY_MAP_ICON_OVERLAY, drawing->map_state.data_overlay_enabled);
  icons_ready &= DrawMapButton(canvas, &pixels->projection, icons,
      MapButtonBounds(window, MAP_BUTTON_PROJECTION, 0), CITY_MAP_ICON_ISOMETRIC, IsIsometricMap(window));
  if (!icons_ready) window->city->game->execution_failed = true;
  CityRect grip = CityMapResizeGripBounds(window);
  CanvasBeveledBox(canvas, grip.left, buttons_top, grip.right, grip.top, canvas_title, true);
  CanvasBeveledBox(canvas, grip.left, grip.top, grip.right, grip.bottom, canvas_title, true);
  for (int line = 0; line < 3; ++line)
    for (int pixel = 0; pixel < 3 + line * 2; ++pixel)
      CanvasFill(canvas, bounds.right - 3 - pixel, bounds.bottom - 3 - line * 2 + pixel,
          bounds.right - 2 - pixel, bounds.bottom - 2 - line * 2 + pixel, 0xFF674F2B);
}

void Sc2kCanvasDrawLandUse(Canvas *canvas, const Window *window) {
  static const char *const labels[11] = {
    "Transportation", "Power", "Water", "Residential", "Commercial", "Industrial",
    "Ports/Airports", "Education", "Health/Safety", "Recreation", "Arcologies"
  };
  CityRect bounds = window->bounds;
  CanvasPanelFrame(canvas, bounds, "Land Use");
  CanvasTextAt(canvas, bounds.left + 12, bounds.top + 29, "Category", canvas_text);
  CanvasTextRight(canvas, bounds.left + 251, bounds.top + 29, "Acres", canvas_text);
  CanvasTextRight(canvas, bounds.right - 11, bounds.top + 29, "% of City", canvas_text);
  for (i32 index = 0; index < CITY_LAND_USE_COUNT; ++index) {
    i32 top = bounds.top + 42 + index * 21;
    CanvasTextAt(canvas, bounds.left + 12, top + 6, labels[index], canvas_text);
    CanvasBeveledBox(canvas, bounds.left + 176, top, bounds.left + 258, top + 19,
        canvas_light, 0);
    CanvasBeveledBox(canvas, bounds.left + 264, top, bounds.right - 9, top + 19,
        canvas_light, 0);
    char acres[16];
    char percent[8];
    CanvasBudgetAmount(acres, sizeof(acres), window->land_use_acres[index]);
    snprintf(percent, sizeof(percent), "%u%%", window->land_use_percent[index]);
    CanvasTextRight(canvas, bounds.left + 253, top + 6, acres, canvas_text);
    CanvasTextRight(canvas, bounds.right - 14, top + 6, percent, canvas_text);
  }
}

// Map Data Host

static void ReleaseMapDataDrawing(City *city, void *context) {
  CityMapDataWindowDrawing *pixels = context;
  if (city->game->ui->map_data.drawing == pixels) city->game->ui->map_data.drawing = NULL;
  ReleaseCityWindowDrawing(&pixels->base);
}

void SetCityMapDataPresentation(City *city, CityCanvasDesktop *desktop) {
  city->game->ui->map_data.desktop = desktop;
  city->game->ui->map_data.isometric = true;
}
bool IsCityMapDataVisible(City *city) { return city->game->ui->map_data.visible; }
bool CityMapDataServiceAvailable(City *city) {
  return city->game->ui->map_data.desktop != NULL;
}
int ReadCityMapDataSelection(City *city) {
  CityMapDataState *state = &city->game->ui->map_data;
  return state->group * CITY_DATA_SELECTION_STRIDE + state->option;
}
void SetCityMapDataSelection(City *city, int selection) {
  CityMapDataState *state = &city->game->ui->map_data;
  if (ReadCityMapDataSelection(city) != selection && state->drawing)
    InvalidateCityWindowBitmaps(&state->drawing->base);
  state->group = selection / CITY_DATA_SELECTION_STRIDE;
  state->option = selection % CITY_DATA_SELECTION_STRIDE;
  InvalidateCityInformation(city->game, CITY_UPDATE_MAP);
  if (state->desktop == NULL) return;
  for (int i = 0; i < state->desktop->count; ++i) {
    Window *window = &state->desktop->windows[i];
    if (window->window_id != state->window_id) continue;
    window->data_map_selection = selection;
    Sc2kDirtyMarkRect(&state->desktop->dirty, window->bounds);
  }
}
void HideCityMapDataDialog(City *city) {
  CityMapDataState *state = &city->game->ui->map_data;
  if (!state->visible) return;
  if (state->desktop != NULL)
    Sc2kCanvasDesktopDismissWindow(state->desktop, state->window_id);
  state->visible = false;
  state->refresh_enabled = false;
  state->window_id = 0;
  Drawing *drawing = city->game->drawing;
  drawing->map_state.data_overlay_enabled = false;
  RefreshCityMapViews(city->game);
}
bool ToggleCityMapDataDialog(City *city) {
  CityMapDataState *state = &city->game->ui->map_data;
  if (!CityMapDataServiceAvailable(city)) {
    return false;
  }
  if (state->visible) {
    HideCityMapDataDialog(city);
  } else {
    int index = Sc2kCanvasDesktopOpenMapData(state->desktop, (CityRect){70, 18, 370, 288},
        ReadCityMapDataSelection(city));
    if (index < 0) {
      SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
      return false;
    }
    Window *window = &state->desktop->windows[index];
    window->city = city;
    state->drawing = (CityMapDataWindowDrawing *)CreateCityWindowDrawing(city->game->drawing,
        CITY_WINDOW_DRAWING_MAP_DATA);
    if (!state->drawing || !AttachCityWindowCallback(window, ReleaseMapDataDrawing, state->drawing)) {
      if (state->drawing) ReleaseCityWindowDrawing(&state->drawing->base);
      state->drawing = NULL;
      Sc2kCanvasDesktopDismissWindow(state->desktop, window->window_id);
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
    state->window_id = window->window_id;
    state->visible = true;
    CityRect rect = Sc2kCanvasMapDataPreviewRect(window);
    state->refresh_rect = (CityRect){rect.left - window->bounds.left, rect.top - window->bounds.top,
        rect.right - window->bounds.left, rect.bottom - window->bounds.top};
  }
  state->refresh_enabled = state->visible;
  Drawing *drawing = city->game->drawing;
  RefreshCityMapViews(city->game);
  return state->visible;
}

// Map Data Window

void ToggleCityMapDataOverlay(City *city) {
  Drawing *drawing = city->game->drawing;
  drawing->map_state.data_overlay_enabled = !drawing->map_state.data_overlay_enabled;
  CityMapDataState *state = &city->game->ui->map_data;
  if (state->desktop)
    for (int index = 0; index < state->desktop->count; ++index) {
      Window *window = &state->desktop->windows[index];
      if (window->window_id == state->window_id)
        Sc2kDirtyMarkRect(&state->desktop->dirty, MapButtonBounds(window, MAP_BUTTON_OVERLAY, 0));
    }
  RefreshCityMapViews(city->game);
}
void CityMapDataDialogOnOverlaySelectionChanged(City *city, int selection) {
  Drawing *drawing = city->game->drawing;
  SetCityMapDataSelection(city, selection);
  ApplyCityMapDataModeSelection(drawing, selection);
  drawing->map_state.data_overlay_enabled = 0;
  RefreshCityMapViews(city->game);
}
void CityMapDataDialogOnListSelectionChanged(City *city, int selection) {
  SetCityMapDataSelection(city, selection);
  ApplyCityMapDataModeSelection(city->game->drawing, selection);
}
// Default entries (including selection52) leave the main-map mode unchanged.
void ApplyCityMapDataModeSelection(Drawing *drawing, int selection) {
  int previous = drawing->map_state.data_mode;
  // Zero preserves the current mode; stored modes include one for CITY_DATA_MODE_NONE.
  static const u8 kModes[CITY_DATA_GROUP_COUNT * CITY_DATA_SELECTION_STRIDE] = {
    [CITY_DATA_SELECTION_STRUCTURES] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_ZONES] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_ROADS] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_RAIL] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_FIRE_STATIONS] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_SCHOOLS] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_COLLEGES] = CITY_DATA_MODE_NONE + 1,
    [CITY_DATA_SELECTION_TRAFFIC] = CITY_DATA_MODE_TRAFFIC + 1,
    [CITY_DATA_SELECTION_POWER] = CITY_DATA_MODE_POWER + 1,
    [CITY_DATA_SELECTION_WATER] = CITY_DATA_MODE_WATER + 1,
    [CITY_DATA_SELECTION_DENSITY] = CITY_DATA_MODE_DENSITY + 1,
    [CITY_DATA_SELECTION_GROWTH] = CITY_DATA_MODE_GROWTH + 1,
    [CITY_DATA_SELECTION_CRIME] = CITY_DATA_MODE_CRIME + 1,
    [CITY_DATA_SELECTION_POLICE_COVERAGE] = CITY_DATA_MODE_POLICE_COVERAGE + 1,
    [CITY_DATA_SELECTION_POLLUTION] = CITY_DATA_MODE_POLLUTION + 1,
    [CITY_DATA_SELECTION_LAND_VALUE] = CITY_DATA_MODE_LAND_VALUE + 1,
    [CITY_DATA_SELECTION_FIRE_COVERAGE] = CITY_DATA_MODE_FIRE_COVERAGE + 1,
  };
  if (selection >= 0 && selection < sizeof(kModes) && kModes[selection])
    drawing->map_state.data_mode = kModes[selection] - 1;
  if (previous != drawing->map_state.data_mode && drawing->map_state.data_overlay_enabled)
    InvalidateCityMapViews(drawing);
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool LandUseWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  (void)game;
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    Sc2kCanvasDrawLandUse(canvas, window);
    return true;
  }
  return false;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool MapDataWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCityMapDataWindow(game->drawing, canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind != CANVAS_SDL_POINTER_DOWN || input->button != CANVAS_SDL_BUTTON_LEFT) return false;
  City *city = game->city;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  int x = event->x, y = event->y;
  if (!window->shaded) {
    int selected_group = window->data_map_selection / CITY_DATA_SELECTION_STRIDE;
    for (int group = 0; group < CITY_DATA_GROUP_COUNT; ++group)
      if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_CATEGORY, group), x, y)) {
        CityMapDataDialogOnOverlaySelectionChanged(city, group * CITY_DATA_SELECTION_STRIDE);
        ApplicationRefreshMapData(city);
        return true;
      }
    for (int option = 0; option < CityMapDataOptionCount(selected_group); ++option)
      if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_OPTION, option), x, y)) {
        CityMapDataDialogOnListSelectionChanged(city, selected_group * CITY_DATA_SELECTION_STRIDE + option);
        ApplicationRefreshMapData(city);
        return true;
      }
    if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_PROJECTION, 0), x, y)) {
      game->ui->map_data.isometric = !game->ui->map_data.isometric;
      if (game->ui->map_data.drawing) InvalidateCityWindowBitmaps(&game->ui->map_data.drawing->base);
      Sc2kDirtyMarkRect(&desktop->dirty, window->bounds);
      return true;
    }
    TileIndex tile = MapTileAt(window, Sc2kCanvasMapDataPreviewRect(window), x, y);
    if (IsMapTileValid(city, tile)) {
      CenterCityMapViewAtTile(city, NULL, MapTileX(city, tile), MapTileY(city, tile));
      return true;
    }
    if (CanvasPointInRect(MapButtonBounds(window, MAP_BUTTON_OVERLAY, 0), x, y)) {
      ToggleCityMapDataOverlay(city);
      return true;
    }
  }
  for (int control = CITY_VIEWPORT_SHADE; control <= CITY_VIEWPORT_STICKY; ++control) {
    if (!CanvasPointInRect(CityViewportControlBounds(window, control), x, y)) continue;
    Window *target = FindCityCanvasWindow(desktop, window->window_id);
    Sc2kDirtyMarkRect(&desktop->dirty, target->bounds);
    if (control == CITY_VIEWPORT_SHADE) {
      if (target->shaded) target->bounds.bottom = target->bounds.top + target->restore_height;
      else {
        target->restore_height = target->bounds.bottom - target->bounds.top;
        target->bounds.bottom = target->bounds.top + 14;
      }
      target->shaded = !target->shaded;
    } else if (control == CITY_VIEWPORT_DEFAULT_SIZE) ResetCityWindowSize(target);
    else target->sticky = !target->sticky;
    Sc2kDirtyMarkRect(&desktop->dirty, target->bounds);
    return true;
  }
  return false;
}
