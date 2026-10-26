#include "game/update.h"
#include <string.h>
#include "base/text.h"
#include "ui/windows/window_internal.h"
#include "ui/map.h"
#include "ui/menus/menu.h"
#include "ui/menus/catalog.h"
#include "ui/text/text.h"

#include <stdlib.h>
#include <stdio.h>

#include "assets/fonts.h"
#include "assets/state.h"
#include "audio/state.h"
#include "base/time.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/engine/clock.h"
#include "city/events/emergency.h"
#include "city/maps/buildings.h"
#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/sprites/preparation.h"
#include "drawing/bitmaps.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_camera.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"
#include "game/timing.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/messages.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/startup/startup.h"
#include "ui/state.h"
#include "ui/text/status.h"
#include "ui/tools/placement.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/tooltips.h"
#include "ui/windows/windows.h"

// Icon toolbar, floating construction windows, dropdowns and status bar. All coordinates are UI units.

static bool MoveCityMenu(CityCanvasDesktop *desktop, int x, int y);

int CityToolbarIconColumns(int icon) {
  return icon == CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_ROAD_BRIDGE ||
      icon == CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_RAIL_BRIDGE ? 2 : 1;
}

bool PrepareCityToolbarIcons(Game *game) {
  Drawing *drawing = game->drawing;
  const CityInterfaceIcons *source = &game->resources->main_toolbar_icons;
  int scale = drawing->options.scale;
  if (drawing->toolbar_icon_scale == scale && drawing->toolbar_icon_data == source->data.data) return true;
  ReleaseCityToolbarIcons(drawing);
  if (!source->data.data) return true;
  drawing->toolbar_icon_source = source;
  drawing->toolbar_icon_scale = scale;
  // Position-independent image dimensions need one prepared size per UI scale.
  int size = (20 * scale + 99) / 100;
  for (int icon = 0; icon < CITY_TOOLBAR_ICON_COUNT; ++icon) {
    int width = (20 * CityToolbarIconColumns(icon) * scale + 99) / 100;
    if (!PrepareCityToolbarIcon(drawing, icon, width, size, true)) return false;
  }
  // Publish readiness only after every button succeeds. A failed attempt is rebuilt on the next call.
  drawing->toolbar_icon_data = source->data.data;
  return true;
}

bool PrepareCityToolbarIconSize(Game *game, int icon, int size) {
  Drawing *drawing = game->drawing;
  if (!drawing->toolbar_icon_source || size <= 0) return true;
  int pixels = (size * drawing->options.scale + 99) / 100;
  int width = (size * CityToolbarIconColumns(icon) * drawing->options.scale + 99) / 100;
  return PrepareCityToolbarIcon(drawing, icon, width, pixels, false) != NULL;
}

void DrawCityToolbarIcon(Game *game, Canvas *canvas, CityRect bounds, int icon, bool disabled, bool flipped) {
  Drawing *drawing = game->drawing;
  if (!drawing->toolbar_icon_source) return;
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip,
      (CityRect){bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1});
  int columns = CityToolbarIconColumns(icon);
  int size = IntMin((bounds.right - bounds.left - 2 * columns) / columns, bounds.bottom - bounds.top - 2);
  int pressed = !disabled && CityCanvasButtonPressed(canvas, bounds);
  int left = bounds.left + ((bounds.right - bounds.left - size * columns) >> 1) + pressed;
  int top = bounds.top + ((bounds.bottom - bounds.top - size) >> 1) + pressed;
  bounds = (CityRect){left, top, left + size * columns, top + size};
  CityRect box = CanvasRasterImageBounds(canvas, bounds);
  int width = box.right - box.left, height = box.bottom - box.top;
  CityRect area = IntersectCityRects(box, CanvasRasterRect(canvas, canvas->clip));
  if (area.left < area.right && area.top < area.bottom) {
    const RunSprite *sprite = FindCityToolbarIcon(drawing, icon, width, height);
    if (!sprite) game->execution_failed = true;
    else if (!DrawCanvasRunSprite(canvas, sprite, bounds,
        (flipped ? SPRITE_FLIPPED : 0) | (disabled ? SPRITE_DISABLED : 0))) game->execution_failed = true;
  }
  canvas->clip = clip;
}

static const u32 kConstructionFace = 0xFF687C4C;
static const u32 kConstructionLight = 0xFF99B06C;
static const u32 kConstructionDark = 0xFF3C5830;
static const CityDropdownColors kTreeDropdownColors = {
  .face = 0xFF838583, .light = 0xFF99B06C, .dark = 0xFF3C5830, .pressed_face = 0xFFA8A8A8,
  .text = 0xFF101010,
  .disabled_face = 0xFF838583, .disabled_light = 0xFF99B06C, .disabled_dark = 0xFF3C5830,
  .disabled_text = 0xFF636562, .disabled_item_text = 0xFF636562,
  .selected_face = 0xFF101010, .selected_text = 0xFFFFFFFF
};

// OpenTTD's full toolbar has 30 buttons and six quarter-button spacers.
enum { kButtonWidth = 22, kDemandWidth = 2 * kButtonWidth,
    kDefaultStatusWidth = 30 * kButtonWidth + ((6 * kButtonWidth) >> 2),
    kStatusHeight = 12, kDemandBarCount = 3, kDemandBarWidth = 6, kDemandBarPitch = 12,
    kToolbarSwitch = 127, kStatusDate = 128, kToolbarDemand, kStatusMoney };
enum { kTreeClimateDropdown = 1 };
enum { kConstructionPreviewWidth = CITY_BUILDING_PREVIEW_WIDTH + 6 };

static bool IsToolSubmenu(int menu) {
  return menu >= kResidentialDensityMenu && menu <= kRecreationMenu;
}

static void MenuShortcutToString(const CityMenuItem *item, char *text, size_t capacity) {
  if (item->action == CITY_MENU_ACTION_SUBMENU && item->parameter >= kResidentialDensityMenu &&
      item->parameter <= kIndustrialDensityMenu) {
    const CityMenuItem *choices = kToolSubmenus[item->parameter - kResidentialDensityMenu].items;
    char first[48], second[48];
    CityHotkeyToString(choices[0].shortcut, first, sizeof(first));
    CityHotkeyToString(choices[1].shortcut, second, sizeof(second));
    snprintf(text, capacity, "%s / %s", first, second);
  } else CityHotkeyToString(item->shortcut, text, capacity);
}

static const CityMenuItem *RememberedSubmenuTool(const CityCanvasDesktop *desktop, int menu) {
  int index = menu - kResidentialDensityMenu;
  const CityToolSubmenu *submenu = &kToolSubmenus[index];
  int remembered = desktop->menu.remembered_submenu[index];
  for (int i = 0; i < submenu->count; ++i)
    if (submenu->items[i].parameter + 1 == remembered) return &submenu->items[i];
  return &submenu->items[0];
}

static int ConstructionGroupIndex(int category) {
  for (int i = 0; i < CITY_CONSTRUCTION_GROUP_COUNT; ++i)
    if (kConstructionGroupCategories[i] == category) return i;
  return -1;
}

static const CityToolbarDefinition *ConstructionCategoryDefinition(int category, bool group) {
  return group ? &kConstructionGroups[ConstructionGroupIndex(category - 1)] : &kCityToolbarDefinitions[category - 1];
}

static const CityToolbarDefinition *ConstructionWindowDefinition(const Window *window) {
  return ConstructionCategoryDefinition(window->toolbar_category, window->toolbar_group);
}

static bool ToolbarIsZoneCategory(int index) {
  return (index >= CITY_TOPBAR_RESIDENTIAL && index <= CITY_TOPBAR_INDUSTRIAL) || index == CITY_TOPBAR_PORTS;
}

static bool ToolbarUsesDropdown(int index) {
  return !kCityToolbarDefinitions[index].category && kCityToolbarDefinitions[index].count;
}

// Art IDs stay stable; consolidated categories each have one visible button.
static const u8 kToolbarOrder[] = {
  CITY_TOPBAR_PAUSE, CITY_TOPBAR_SPEED, CITY_TOPBAR_SETTINGS, CITY_TOPBAR_FILE,
  CITY_TOPBAR_BUDGET, CITY_TOPBAR_GRAPHS, CITY_TOPBAR_NEWSPAPER,
  CITY_TOPBAR_MAP, CITY_TOPBAR_ZOOM_IN, CITY_TOPBAR_ZOOM_OUT, CITY_TOPBAR_ROTATE,
  CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE, CITY_TOPBAR_LAYERS, CITY_TOPBAR_UNDERWORLD,
  CITY_TOPBAR_LANDSCAPE, CITY_TOPBAR_RESIDENTIAL, CITY_TOPBAR_ROADS,
  CITY_TOPBAR_POWER, CITY_TOPBAR_EDUCATION,
  CITY_TOPBAR_EMERGENCY, CITY_TOPBAR_HELP
};
static const u8 kTerrainToolbarOrder[] = {
  CITY_TOPBAR_PAUSE, CITY_TOPBAR_FILE, CITY_TOPBAR_SETTINGS,
  CITY_TOPBAR_MAP, CITY_TOPBAR_ZOOM_IN, CITY_TOPBAR_ZOOM_OUT,
  CITY_TOPBAR_ROTATE, CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE, CITY_TOPBAR_TERRAIN_CENTER,
  CITY_TOPBAR_TERRAIN_RAISE, CITY_TOPBAR_TERRAIN_LOWER,
  CITY_TOPBAR_TERRAIN_STRETCH, CITY_TOPBAR_TERRAIN_LEVEL, CITY_TOPBAR_TREES,
  CITY_TOPBAR_TERRAIN_WATER, CITY_TOPBAR_TERRAIN_STREAM, CITY_TOPBAR_TERRAIN_CLEAR, CITY_TOPBAR_HELP
};

static bool TerrainToolbar(const CityCanvasDesktop *desktop) {
  return desktop->game && desktop->game->city->settings.timer_mode == CITY_TIMER_STOPPED;
}

static const u8 *ToolbarOrder(const CityCanvasDesktop *desktop, int *count) {
  if (TerrainToolbar(desktop)) {
    *count = sizeof(kTerrainToolbarOrder) / sizeof(*kTerrainToolbarOrder);
    return kTerrainToolbarOrder;
  }
  *count = sizeof(kToolbarOrder) / sizeof(*kToolbarOrder);
  return kToolbarOrder;
}

static int ToolbarButtonWidth(const CityCanvasDesktop *desktop, int index) {
  return TerrainToolbar(desktop) && index == CITY_TOPBAR_PAUSE ?
      IntMin(72, IntMax(kButtonWidth, desktop->canvas_width / 3)) : kButtonWidth;
}

static int ToolbarButtonIcon(int index) {
  if (index >= CITY_TOPBAR_TERRAIN_RAISE) {
    int tool = kCityToolbarDefinitions[index].direct.parameter;
    if (tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN) return CITY_TOOLBAR_STRETCH_TERRAIN_ICON;
    if (tool == CITY_CANVAS_TOOL_STREAM) tool = CITY_CANVAS_TOOL_WATER;
    return CITY_TOPBAR_ICON_COUNT + tool;
  }
  return index == CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE ? CITY_TOPBAR_ROTATE :
      index == CITY_TOPBAR_TREES ? CITY_TOPBAR_ICON_COUNT + CITY_CANVAS_TOOL_TREES :
      index == CITY_TOPBAR_UNDERWORLD ? CITY_TOOLBAR_UNDERWORLD_ICON : index;
}

static void DrawTerrainStreamIcon(Canvas *canvas, CityRect bounds, bool pressed) {
  static const CityPoint kCourse[] = {{9, 0}, {7, 4}, {11, 7}, {10, 10}, {5, 13}, {7, 18}};
  int size = IntMin(bounds.right - bounds.left, bounds.bottom - bounds.top) - 8;
  int left = bounds.left + (bounds.right - bounds.left - size) / 2 + pressed;
  int top = bounds.top + (bounds.bottom - bounds.top - size) / 2 + pressed;
  for (int pass = 0; pass < 2; ++pass)
    for (size_t i = 1; i < sizeof(kCourse) / sizeof(*kCourse); ++i)
      DrawCanvasLine(canvas, left + kCourse[i - 1].x * size / 18, top + kCourse[i - 1].y * size / 18,
          left + kCourse[i].x * size / 18, top + kCourse[i].y * size / 18,
          pass ? 0xFF70D0EC : 0xFF245878, IntMax(1, size * (pass ? 2 : 5) / 18));
}

static int PrimaryToolbarCategory(int category) {
  if (category == CITY_TOPBAR_TREES) return CITY_TOPBAR_LANDSCAPE;
  if (ToolbarIsZoneCategory(category) || category == CITY_TOPBAR_BULLDOZE) return CITY_TOPBAR_RESIDENTIAL;
  if (category == CITY_TOPBAR_RAIL) return CITY_TOPBAR_ROADS;
  if (category == CITY_TOPBAR_WATER) return CITY_TOPBAR_POWER;
  if (category >= CITY_TOPBAR_EDUCATION && category <= CITY_TOPBAR_REWARDS) return CITY_TOPBAR_EDUCATION;
  return category;
}

typedef struct {
  CityRect bounds, status_bounds, buttons[CITY_TOPBAR_COUNT], switch_button, demand_bounds;
  int pages;
} CityToolbarLayout;

static CityToolbarLayout ToolbarLayout(const CityCanvasDesktop *desktop) {
  CityToolbarLayout layout = {.pages = 1};
  int count;
  const u8 *order = ToolbarOrder(desktop, &count);
  int demand_width = TerrainToolbar(desktop) ? 0 :
      IntMin(kDemandWidth, IntMax(0, desktop->canvas_width - 2 * kButtonWidth));
  int extra_width = ToolbarButtonWidth(desktop, CITY_TOPBAR_PAUSE) - kButtonWidth;
  int full_width = count * kButtonWidth + demand_width + extra_width;
  for (int i = 1; i < count; ++i) full_width += kCityToolbarDefinitions[order[i]].separator ? 3 : 0;
  int bar_width = IntMin(desktop->canvas_width, IntMax(kDefaultStatusWidth, full_width));
  int bar_left = (desktop->canvas_width - bar_width) >> 1;
  layout.status_bounds = (CityRect){bar_left, 0, bar_left + bar_width, CITY_MENU_BAR_HEIGHT};
  if (desktop->canvas_width >= full_width) {
    int left = (desktop->canvas_width - full_width) >> 1;
    layout.bounds = (CityRect){left, 0, left + full_width, CITY_MENU_BAR_HEIGHT};
    layout.demand_bounds = (CityRect){layout.bounds.right - demand_width, 0,
        layout.bounds.right, CITY_MENU_BAR_HEIGHT};
    for (int position = 0; position < count; ++position) {
      int i = order[position];
      if (position && kCityToolbarDefinitions[i].separator) left += 3;
      int width = ToolbarButtonWidth(desktop, i);
      layout.buttons[i] = (CityRect){left, 0, left + width, CITY_MENU_BAR_HEIGHT};
      left += width;
    }
    return layout;
  }
  // Like OpenTTD's split toolbar: one row, a switch button and shared essentials.
  // Extremely narrow canvases use more pages rather than hiding any command.
  static const u8 kCommon[] = {CITY_TOPBAR_PAUSE, CITY_TOPBAR_MAP, CITY_TOPBAR_ZOOM_IN,
      CITY_TOPBAR_ZOOM_OUT, CITY_TOPBAR_SETTINGS, CITY_TOPBAR_HELP};
  int slots = IntMax(2, (desktop->canvas_width - demand_width - extra_width) / kButtonWidth);
  int common = IntMin(sizeof(kCommon) / sizeof(*kCommon), IntMax(0, 2 * (slots - 1) - count));
  int capacity = slots - common - 1;
  int remaining = count - common;
  layout.pages = (remaining + capacity - 1) / capacity;
  capacity = (remaining + layout.pages - 1) / layout.pages;
  slots = common + capacity + 1;
  int width = IntMin(desktop->canvas_width, slots * kButtonWidth + demand_width + extra_width);
  int origin = (desktop->canvas_width - width) >> 1;
  layout.bounds = (CityRect){origin, 0, origin + width, CITY_MENU_BAR_HEIGHT};
  int page = desktop->menu.toolbar_page % layout.pages;
  int ordinal = 0, left = origin;
  for (int position = 0; position < count; ++position) {
    int i = order[position];
    bool shared = false;
    for (int j = 0; j < common; ++j) if (kCommon[j] == i) shared = true;
    if (!shared && ordinal++ / capacity != page) continue;
    int right = left + ToolbarButtonWidth(desktop, i);
    layout.buttons[i] = (CityRect){left, 0, right, CITY_MENU_BAR_HEIGHT};
    left = right;
  }
  layout.switch_button = (CityRect){origin + (slots - 1) * kButtonWidth + extra_width, 0,
      origin + slots * kButtonWidth + extra_width, CITY_MENU_BAR_HEIGHT};
  layout.demand_bounds = (CityRect){layout.switch_button.right, 0, origin + width, CITY_MENU_BAR_HEIGHT};
  return layout;
}

static CityRect ToolbarButtonBounds(const CityCanvasDesktop *desktop, int index) {
  CityToolbarLayout layout = ToolbarLayout(desktop);
  return layout.buttons[TerrainToolbar(desktop) && index == CITY_TOPBAR_TREES ? index :
      PrimaryToolbarCategory(index)];
}

static void RevealToolbarButton(CityCanvasDesktop *desktop, int index) {
  if (!TerrainToolbar(desktop) || index != CITY_TOPBAR_TREES) index = PrimaryToolbarCategory(index);
  CityToolbarLayout layout = ToolbarLayout(desktop);
  if (layout.buttons[index].right) return;
  for (int page = 0; page < layout.pages; ++page) {
    desktop->menu.toolbar_page = page;
    CityToolbarLayout candidate = ToolbarLayout(desktop);
    if (candidate.buttons[index].right) return;
  }
}

static const Window *ConstructionWindow(const CityCanvasDesktop *desktop, u32 id) {
  for (int i = 0; i < desktop->count; ++i)
    if (desktop->windows[i].kind == CITY_CANVAS_CONSTRUCTION && desktop->windows[i].window_id == id)
      return &desktop->windows[i];
  return NULL;
}

enum { kConstructionRowHeight = 28, kConstructionScrollbarWidth = 12 };

static bool ConstructionUsesTreePicker(int category, bool group) {
  return !group && category == CITY_TOPBAR_TREES + 1;
}

static int TreePickerCount(int climate) {
  return climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100 ? 1 :
      CityTreeSpeciesCount(climate);
}

static int TreePickerSpecies(int climate, int item) {
  return climate >= CLIMATE_DESERT_50 && climate <= CLIMATE_DESERT_100 ? 7 : item;
}

static CityRect TreeClimateBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 1, b.top + 15, b.right - 1, b.top + 29};
}

static bool ConstructionUsesChooser(int category, bool group) {
  return !group && (category == CITY_TOPBAR_POWER + 1 || category == CITY_TOPBAR_WATER + 1 ||
      category == CITY_TOPBAR_REWARDS + 1);
}

static bool ConstructionUsesList(int category, bool group) {
  if (group) return false;
  switch (category - 1) {
    case CITY_TOPBAR_POWER: case CITY_TOPBAR_WATER: case CITY_TOPBAR_REWARDS:
      return true;
    default: return false;
  }
}

static CityRect ConstructionRowsBounds(const Window *window) {
  CityRect b = window->bounds;
  if (ConstructionUsesChooser(window->toolbar_category, window->toolbar_group)) {
    int footer = IntClamp(b.bottom - b.top - 75, 60, 120);
    return (CityRect){b.left + kConstructionPreviewWidth + 16, b.top + 33,
        b.right - 8, IntMax(b.top + 51, b.bottom - footer)};
  }
  return (CityRect){b.left, b.top + 14, b.right, b.bottom};
}

static CityRect ConstructionConfirmBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - 78, b.bottom - 23, b.right - 8, b.bottom - 5};
}

static int ConstructionRowHeight(const Window *window) {
  return ConstructionUsesChooser(window->toolbar_category, window->toolbar_group) ? 16 : kConstructionRowHeight;
}

static int ConstructionVisibleRows(const Window *window) {
  CityRect rows = ConstructionRowsBounds(window);
  return IntMax(1, (rows.bottom - rows.top - 2) / ConstructionRowHeight(window));
}

static CityScrollbar ConstructionScrollbar(const Window *window) {
  CityRect track = ConstructionRowsBounds(window);
  track.left = track.right - kConstructionScrollbarWidth;
  track.top += 12;
  track.bottom -= 12;
  return ReadCityScrollbar(track, ConstructionWindowDefinition(window)->count, ConstructionVisibleRows(window),
      window->toolbar_scroll, 4);
}

static bool ConstructionHasScrollbar(const Window *window) {
  return ConstructionUsesList(window->toolbar_category, window->toolbar_group) &&
      ConstructionWindowDefinition(window)->count > ConstructionVisibleRows(window);
}

static int ConstructionButtonWidth(const CityMenuItem *item) {
  int columns = item->action == CITY_MENU_ACTION_TOOL ?
      CityToolbarIconColumns(CITY_TOPBAR_ICON_COUNT + item->parameter) : 1;
  return columns * kButtonWidth;
}

static CityRect ConstructionGridButtonBounds(const CityToolbarDefinition *category, CityRect bounds, int item) {
  int left = bounds.left, top = bounds.top + 14;
  for (int i = 0; i <= item; ++i) {
    int width = ConstructionButtonWidth(&category->items[i]);
    if (left > bounds.left && left + width > bounds.right) {
      left = bounds.left;
      top += CITY_MENU_BAR_HEIGHT;
    }
    if (i == item) return (CityRect){left, top, left + width, top + CITY_MENU_BAR_HEIGHT};
    left += width;
  }
  return (CityRect){0};
}

static CityRect ContextButtonBounds(const Window *window, int item) {
  if (ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group)) {
    CityRect b = window->bounds;
    if (item == 12) return (CityRect){b.left + 1, b.bottom - 15, b.right - 1, b.bottom - 1};
    int count = TreePickerCount(window->toolbar_selection), columns = IntMin(4, count);
    int width = b.right - b.left - 2, height = (b.bottom - b.top - 44) / ((count + columns - 1) / columns);
    int column = item % columns, top = TreeClimateBounds(window).bottom + (item / columns) * height;
    return (CityRect){b.left + 1 + column * width / columns, top,
        b.left + 1 + (column + 1) * width / columns, top + height};
  }
  if (ConstructionUsesList(window->toolbar_category, window->toolbar_group)) {
    CityRect rows = ConstructionRowsBounds(window);
    int height = ConstructionRowHeight(window);
    int top = rows.top + 1 + (item - window->toolbar_scroll) * height;
    return (CityRect){rows.left + 1, top,
        rows.right - (ConstructionHasScrollbar(window) ? kConstructionScrollbarWidth : 1), top + height};
  }
  return ConstructionGridButtonBounds(ConstructionWindowDefinition(window), window->bounds, item);
}

// Try eight edge/corner positions around visible windows. Work in physical
// pixels so adjoining borders meet even after dragging or fractional scaling.
static CityPoint PlaceConstructionWindow(const CityCanvasDesktop *desktop, const Window *opening) {
  enum { kWindowCount = sizeof(desktop->windows) / sizeof(*desktop->windows) };
  CityRect occupied[kWindowCount];
  CityPoint positions[5 + 8 * kWindowCount];
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  int width = CanvasScaleCeil(opening->bounds.right - opening->bounds.left, scale, 100);
  int height = CanvasScaleCeil(opening->bounds.bottom - opening->bounds.top, scale, 100);
  CityPoint preferred = {CanvasScaleCeil(opening->bounds.left, scale, 100),
      CanvasScaleCeil(opening->bounds.top, scale, 100)};
  if (!opening->toolbar_group)
    for (int i = desktop->count - 1; i >= 0; --i) {
      const Window *parent = &desktop->windows[i];
      if (!parent->visible || parent->kind != CITY_CANVAS_CONSTRUCTION || !parent->toolbar_group ||
          parent->toolbar_category - 1 != PrimaryToolbarCategory(opening->toolbar_category - 1)) continue;
      CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, parent->window_id);
      preferred = (CityPoint){pixels.left, pixels.bottom};
      break;
    }
  int left = 0, top = CityMenuPixelBounds(desktop, 0).bottom;
  int screen_width = desktop->pixel_state.scale ? desktop->pixel_state.width : desktop->canvas_width;
  int right = IntMax(left, screen_width - width);
  int bottom = IntMax(top, CityMenuPixelBounds(desktop, 2).top - height);
  int count = 0, candidates = 5;
  positions[0] = preferred;
  positions[1] = (CityPoint){left, top};
  positions[2] = (CityPoint){right, top};
  positions[3] = (CityPoint){left, bottom};
  positions[4] = (CityPoint){right, bottom};
  for (int i = desktop->count - 1; i >= 0; --i) {
    const Window *window = &desktop->windows[i];
    if (!window->visible) continue;
    CityRect b = Sc2kCanvasDesktopPixelBounds(desktop, window->window_id);
    occupied[count++] = b;
    positions[candidates++] = (CityPoint){b.right, b.top};
    positions[candidates++] = (CityPoint){b.left - width, b.top};
    positions[candidates++] = (CityPoint){b.left, b.bottom};
    positions[candidates++] = (CityPoint){b.left, b.top - height};
    positions[candidates++] = (CityPoint){b.right, b.bottom - height};
    positions[candidates++] = (CityPoint){b.left - width, b.bottom - height};
    positions[candidates++] = (CityPoint){b.right - width, b.bottom};
    positions[candidates++] = (CityPoint){b.right - width, b.top - height};
  }
  CityPoint best = preferred;
  i64 best_overlap = INT64_MAX, best_distance = INT64_MAX;
  for (int i = 0; i < candidates; ++i) {
    int x = IntClamp(positions[i].x, left, right);
    int y = IntClamp(positions[i].y, top, bottom);
    CityRect candidate = {x, y, x + width, y + height};
    i64 overlap = 0;
    for (int j = 0; j < count; ++j) {
      CityRect b = occupied[j];
      int w = IntMax(0, IntMin(candidate.right, b.right) - IntMax(candidate.left, b.left));
      int h = IntMax(0, IntMin(candidate.bottom, b.bottom) - IntMax(candidate.top, b.top));
      overlap += (i64)w * h;
    }
    i64 distance = abs(x - preferred.x) + abs(y - preferred.y);
    if (overlap < best_overlap || (overlap == best_overlap && distance < best_distance)) {
      best = (CityPoint){x, y};
      best_overlap = overlap;
      best_distance = distance;
    }
  }
  return best;
}

static CityRect ConstructionDefaultBounds(const CityCanvasDesktop *desktop, int category, bool group) {
  const CityToolbarDefinition *button = ConstructionCategoryDefinition(category, group);
  const char *title = group ? button->label : kConstructionTitles[category - 1];
  int width = 0;
  for (int i = 0; i < button->count; ++i) width += ConstructionButtonWidth(&button->items[i]);
  width = IntMax(width, CanvasFontTextWidth(title, kCanvasHeadingFont) + 24);
  width = IntMin(width, IntMax(kButtonWidth, desktop->canvas_width));
  int height = ConstructionGridButtonBounds(button, (CityRect){0, 0, width, 0}, button->count - 1).bottom;
  if (ConstructionUsesList(category, group)) {
    width = IntMin(250, desktop->canvas_width);
    int available = desktop->canvas_height - CITY_MENU_BAR_HEIGHT - kStatusHeight - 14;
    int rows = IntMin(button->count, IntMax(1, available / kConstructionRowHeight));
    height = 14 + rows * kConstructionRowHeight;
  }
  if (ConstructionUsesChooser(category, group)) {
    width = IntMin(390, desktop->canvas_width);
    height = IntMin(300, desktop->canvas_height - CITY_MENU_BAR_HEIGHT - kStatusHeight);
  }
  if (ConstructionUsesTreePicker(category, group)) {
    width = IntMin(170, desktop->canvas_width);
    int count = TreePickerCount(desktop->menu.tree_climate);
    height = IntMin(44 + ((count + 3) >> 2) * 58,
        desktop->canvas_height - CITY_MENU_BAR_HEIGHT - kStatusHeight);
  }
  int left = IntClamp(ToolbarButtonBounds(desktop, category - 1).left, 0, desktop->canvas_width - width);
  int top = CITY_MENU_BAR_HEIGHT;
  return (CityRect){left, top, left + width, top + height};
}

static void OpenConstructionWindow(CityCanvasDesktop *desktop, int category, bool group) {
  int primary = PrimaryToolbarCategory(category - 1);
  bool landscape = primary == CITY_TOPBAR_LANDSCAPE;
  u32 existing = 0;
  WindowIds windows = SnapshotCityWindowIds(desktop);
  for (int i = windows.count - 1; i >= 0; --i) {
    Window *window = FindCityCanvasWindow(desktop, windows.ids[i]);
    if (!window || window->kind != CITY_CANVAS_CONSTRUCTION) continue;
    int other = PrimaryToolbarCategory(window->toolbar_category - 1);
    if (window->toolbar_category == category && window->toolbar_group == group) existing = window->window_id;
    // Landscaping and its tree picker stay open alongside construction toolbars.
    else if (!window->sticky && landscape == (other == CITY_TOPBAR_LANDSCAPE) &&
        (group || !window->toolbar_group || other != primary))
      Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
  }
  if (!ConstructionUsesChooser(category, group)) desktop->menu.category = category;
  Window *current = FindCityCanvasWindow(desktop, existing);
  if (current && current->visible) {
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(current));
    if (current->shaded) current->bounds.bottom = current->bounds.top + current->restore_height;
    current->shaded = false;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(current));
    FocusCanvasWindow(desktop, existing, false);
    return;
  }
  Window window = {0};
  window.kind = CITY_CANVAS_CONSTRUCTION;
  window.bounds = ConstructionDefaultBounds(desktop, category, group);
  window.toolbar_category = category;
  window.toolbar_group = group;
  if (ConstructionUsesTreePicker(category, group)) window.toolbar_selection = desktop->menu.tree_climate;
  window.title = group ? ConstructionCategoryDefinition(category, true)->label : kConstructionTitles[category - 1];
  if (ConstructionUsesList(category, group)) {
    int selected = desktop->menu.remembered[category - 1];
    for (int i = 0; i < kCityToolbarDefinitions[category - 1].count; ++i)
      if (kCityToolbarDefinitions[category - 1].items[i].parameter + 1 == selected) {
        window.toolbar_selection = i;
        window.toolbar_scroll = IntMax(0, i - ConstructionVisibleRows(&window) + 1);
      }
  }
  CityPoint position = PlaceConstructionWindow(desktop, &window);
  int index = Sc2kCanvasDesktopOpen(desktop, window);
  if (index >= 0) PositionCityWindowPixels(desktop, desktop->windows[index].window_id, position);
}

static const CityMenuItem *DropdownItems(const CityCanvasDesktop *desktop, int *count) {
  int menu = desktop->menu.selected;
  *count = 0;
  if (menu == CITY_TOPBAR_FILE + 1 && desktop->game &&
      desktop->game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
    *count = sizeof(kTerrainFileItems) / sizeof(*kTerrainFileItems);
    return kTerrainFileItems;
  }
  if (menu == kDisasterMenu) {
    *count = sizeof(kDisasterItems) / sizeof(*kDisasterItems);
    return kDisasterItems;
  }
  if (IsToolSubmenu(menu)) {
    const CityToolSubmenu *submenu = &kToolSubmenus[menu - kResidentialDensityMenu];
    *count = submenu->count;
    return submenu->items;
  }
  if (menu && menu <= CITY_TOPBAR_COUNT && ToolbarUsesDropdown(menu - 1)) {
    *count = kCityToolbarDefinitions[menu - 1].count;
    return kCityToolbarDefinitions[menu - 1].items;
  }
  return NULL;
}

static int DropdownRows(const CityCanvasDesktop *desktop, int count) {
  int top = CityMenuBounds(desktop, 0).bottom;
  return IntClamp((desktop->canvas_height - top - kStatusHeight - 4) / CITY_MENU_ROW_HEIGHT, 1, count);
}

static bool MenuItemIsToggle(const CityMenuItem *item) {
  switch (item->action) {
  case CITY_MENU_ACTION_MUSIC: case CITY_MENU_ACTION_AUTO_GOTO: case CITY_MENU_ACTION_SUBSCRIPTION:
  case CITY_MENU_ACTION_EXTRA_NEWSPAPER: case CITY_MENU_ACTION_MAP_LAYER: return true;
  case CITY_MENU_ACTION_COMMAND:
    return item->command == CITY_COMMAND_TOGGLE_AUTO_BUDGET || item->command == CITY_COMMAND_TOGGLE_NO_DISASTERS;
  case CITY_MENU_ACTION_TOOLBAR:
    return item->parameter >= CITY_TOOLBAR_BUTTON_SHOW_BUILDINGS && item->parameter <= CITY_TOOLBAR_BUTTON_UNDERGROUND;
  default: return false;
  }
}

static int DropdownTextInset(const CityMenuItem *items, int count) {
  // Keep a checkmark column only in menus whose items can be checked.
  for (int i = 0; i < count; ++i) {
    const CityMenuItem *item = &items[i];
    if (MenuItemIsToggle(item) || item->command == CITY_COMMAND_SET_SIMULATION_SPEED) return 14;
    switch (item->action) {
      case CITY_MENU_ACTION_SUBMENU:
      case CITY_MENU_ACTION_TOOL:
      case CITY_MENU_ACTION_TREE_SPECIES:
      case CITY_MENU_ACTION_PAUSE:
        return 14;
      default:
        break;
    }
  }
  return 3;
}

static bool MenuItemHasSubmenu(const CityMenuItem *item) {
  return item->action == CITY_MENU_ACTION_SUBMENU || item->action == CITY_MENU_ACTION_CATEGORY ||
      item->action == CITY_MENU_ACTION_DISASTERS;
}

typedef struct {
  int text_inset, label_width, shortcut_width, arrow_width, width;
} CityDropdownLayout;

static CityDropdownLayout DropdownLayout(const CityCanvasDesktop *desktop, const CityMenuItem *items, int count) {
  CityDropdownLayout layout = {.text_inset = DropdownTextInset(items, count)};
  for (int i = 0; i < count; ++i) {
    layout.label_width = IntMax(layout.label_width, CityInterfaceTextWidth(desktop->interface_font, items[i].label));
    char shortcut[48];
    MenuShortcutToString(&items[i], shortcut, sizeof(shortcut));
    if (desktop->menu.show_shortcuts)
      layout.shortcut_width = IntMax(layout.shortcut_width,
          CityInterfaceTextWidth(desktop->interface_font, shortcut));
    if (MenuItemHasSubmenu(&items[i])) layout.arrow_width = 12;
  }
  layout.width = layout.text_inset + layout.label_width +
      (layout.shortcut_width ? 8 + layout.shortcut_width : 0) + layout.arrow_width + 8;
  return layout;
}

static CityRect ConstructionMenuAnchor(const CityCanvasDesktop *desktop, const Window *window) {
  const CityToolbarDefinition *definition = ConstructionWindowDefinition(window);
  CityRect anchor = window->bounds;
  for (int i = 0; i < definition->count; ++i) {
    const CityMenuItem *item = &definition->items[i];
    if ((item->action == CITY_MENU_ACTION_SUBMENU && item->parameter == desktop->menu.selected) ||
        (item->action == CITY_MENU_ACTION_DISASTERS && desktop->menu.selected == kDisasterMenu)) {
      anchor = ContextButtonBounds(window, i);
      break;
    }
  }
  CityRect pixels = Sc2kCanvasDesktopPixelBounds(desktop, window->window_id);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  int dx = pixels.left * 100 / scale - window->bounds.left;
  int dy = pixels.top * 100 / scale - window->bounds.top;
  return (CityRect){anchor.left + dx, anchor.top + dy, anchor.right + dx, anchor.bottom + dy};
}

CityRect CityMenuBounds(const CityCanvasDesktop *desktop, int overlay) {
  int height = CITY_MENU_BAR_HEIGHT;
  if (!overlay || overlay == 2 || overlay == 3) {
    CityToolbarLayout layout = ToolbarLayout(desktop);
    if (!overlay) return layout.bounds;
    if (overlay == 3) return layout.demand_bounds;
    CityRect bar = layout.status_bounds;
    int top = IntMax(height, desktop->canvas_height - kStatusHeight);
    return (CityRect){bar.left, top, bar.right, desktop->canvas_height};
  }
  int count;
  const CityMenuItem *items = DropdownItems(desktop, &count);
  if (!count) return (CityRect){0};
  int rows = DropdownRows(desktop, count), columns = (count + rows - 1) / rows;
  CityDropdownLayout dropdown = DropdownLayout(desktop, items, count);
  int width = IntMin(desktop->canvas_width, columns * dropdown.width);
  int anchor = desktop->menu.selected == kDisasterMenu ? CITY_TOPBAR_EMERGENCY : desktop->menu.selected - 1;
  if (IsToolSubmenu(desktop->menu.selected))
    anchor = kToolSubmenus[desktop->menu.selected - kResidentialDensityMenu].category;
  int left = ToolbarButtonBounds(desktop, anchor).left;
  if (desktop->menu.selected == kDisasterMenu || IsToolSubmenu(desktop->menu.selected)) {
    const Window *window = ConstructionWindow(desktop, desktop->menu.submenu_window);
    if (window) {
      CityRect anchor = ConstructionMenuAnchor(desktop, window);
      left = anchor.left;
      height = IntMin(anchor.bottom, desktop->canvas_height - kStatusHeight - 4 - rows * CITY_MENU_ROW_HEIGHT);
    }
  }
  left = IntClamp(left, 0, desktop->canvas_width - width);
  return (CityRect){left, height, left + width, height + 4 + rows * CITY_MENU_ROW_HEIGHT};
}

CityRect CityMenuPixelBounds(const CityCanvasDesktop *desktop, int overlay) {
  CityRect b = CityMenuBounds(desktop, overlay);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  return (CityRect){CanvasScaleCeil(b.left, scale, 100), CanvasScaleCeil(b.top, scale, 100),
      CanvasScaleCeil(b.right, scale, 100), CanvasScaleCeil(b.bottom, scale, 100)};
}

bool CityMenuContains(const CityCanvasDesktop *desktop, int x, int y) {
  if (desktop->game && !desktop->game->city_active) return false;
  for (int overlay = 0; overlay < CITY_MENU_OVERLAY_COUNT; ++overlay)
    if (CanvasPointInRect(CityMenuBounds(desktop, overlay), x, y)) return true;
  return false;
}

static int PrimaryButtonAt(const CityCanvasDesktop *desktop, int x, int y) {
  CityToolbarLayout layout = ToolbarLayout(desktop);
  for (int i = 0; i < CITY_TOPBAR_COUNT; ++i)
    if (CanvasPointInRect(layout.buttons[i], x, y)) return i + 1;
  if (CanvasPointInRect(layout.switch_button, x, y)) return kToolbarSwitch;
  return 0;
}

static int ToolbarControlAt(const CityCanvasDesktop *desktop, int x, int y) {
  int primary = PrimaryButtonAt(desktop, x, y);
  if (primary) return primary;
  CityRect status = CityMenuBounds(desktop, 2);
  if (CanvasPointInRect(CityMenuBounds(desktop, 3), x, y)) return kToolbarDemand;
  if (CanvasPointInRect(status, x, y)) {
    if (x < status.left + 85) return kStatusDate;
    if (x >= status.right - 100) return kStatusMoney;
  }
  return 0;
}

static int MenuItemAt(const CityCanvasDesktop *desktop, int x, int y) {
  CityRect b = CityMenuBounds(desktop, 1);
  if (x < b.left + 2 || x >= b.right - 2 || y < b.top + 2 || y >= b.bottom - 2) return 0;
  int count;
  DropdownItems(desktop, &count);
  int rows = DropdownRows(desktop, count), columns = (count + rows - 1) / rows;
  int column = (x - b.left - 2) * columns / IntMax(1, b.right - b.left - 4);
  int item = column * rows + (y - b.top - 2) / CITY_MENU_ROW_HEIGHT;
  return item < count ? item + 1 : 0;
}

static void DirtyMenu(CityCanvasDesktop *desktop) {
  Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 0));
  Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 1));
  const Window *window = ConstructionWindow(desktop, desktop->menu.submenu_window);
  if (window) Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
}

void SetCityShortcutHints(Game *game, bool visible) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  if (desktop->menu.show_shortcuts == visible) return;
  // The dropdown can shrink, grow or move at the screen edge. Restore its old
  // bounds as well as painting its new bounds when Option changes.
  DirtyMenu(desktop);
  desktop->menu.show_shortcuts = visible;
  DirtyMenu(desktop);
  for (int i = 0; i < desktop->count; ++i)
    if (desktop->windows[i].kind == CITY_CANVAS_CONSTRUCTION)
      Sc2kDirtyMarkRect(&desktop->dirty, desktop->windows[i].bounds);
  HideCityTooltip(game);
}

static void DirtyStatus(CityCanvasDesktop *desktop) {
  Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 2));
}

void CloseCityMenu(CityCanvasDesktop *desktop) {
  if (!desktop->menu.selected) return;
  DirtyMenu(desktop);
  desktop->menu.selected = desktop->menu.hovered = 0;
  desktop->menu.held = false;
  desktop->menu.submenu_pending = false;
}

static bool ButtonEnabled(const CityCanvasDesktop *desktop, int control) {
  if (control == kStatusMoney && TerrainToolbar(desktop)) return false;
  if (control - 1 >= CITY_TOPBAR_TERRAIN_RAISE && control <= CITY_TOPBAR_COUNT) return TerrainToolbar(desktop);
  if (desktop->game && desktop->game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
    switch (control - 1) {
      case CITY_TOPBAR_SPEED: case CITY_TOPBAR_BUDGET: case CITY_TOPBAR_ORDINANCES:
      case CITY_TOPBAR_GRAPHS: case CITY_TOPBAR_REPORTS: case CITY_TOPBAR_NEWSPAPER:
      case CITY_TOPBAR_RESIDENTIAL: case CITY_TOPBAR_COMMERCIAL: case CITY_TOPBAR_INDUSTRIAL:
      case CITY_TOPBAR_ROADS: case CITY_TOPBAR_RAIL: case CITY_TOPBAR_PORTS:
      case CITY_TOPBAR_POWER: case CITY_TOPBAR_WATER: case CITY_TOPBAR_EDUCATION:
      case CITY_TOPBAR_PUBLIC_SERVICES: case CITY_TOPBAR_RECREATION: case CITY_TOPBAR_REWARDS:
      case CITY_TOPBAR_EMERGENCY: case CITY_TOPBAR_UNDERWORLD: return false;
    }
  }
  if (control == CITY_TOPBAR_ZOOM_IN + 1) return desktop->map_zoom < CityMapZoomLimit(desktop->ui_scale_denominator);
  if (control == CITY_TOPBAR_ZOOM_OUT + 1) return desktop->map_zoom > CITY_CANVAS_MAP_ZOOM_SMALL;
  if (control == CITY_TOPBAR_REWARDS + 1 && desktop->interface)
    return !(GetCityToolbarStyle(desktop->interface, CITY_TOOL_GROUP_REWARDS) & CITY_TOOLBAR_STYLE_DISABLED);
  return true;
}

static bool MenuItemEnabled(const CityCanvasDesktop *desktop, const CityMenuItem *item) {
  if (item->action == CITY_MENU_ACTION_SEPARATOR) return false;
  if (item->action == CITY_MENU_ACTION_TOOL &&
      (item->parameter == CITY_CANVAS_TOOL_STRETCH_TERRAIN || item->parameter == CITY_CANVAS_TOOL_STREAM))
    return desktop->game && desktop->game->city->settings.timer_mode == CITY_TIMER_STOPPED;
  if (desktop->game && desktop->game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
    switch (item->action) {
      case CITY_MENU_ACTION_TOOL:
        return item->parameter == CITY_CANVAS_TOOL_CENTER || item->parameter == CITY_CANVAS_TOOL_QUERY ||
            item->parameter == CITY_CANVAS_TOOL_TREES ||
            item->parameter == CITY_CANVAS_TOOL_WATER || item->parameter == CITY_CANVAS_TOOL_DEMOLISH ||
            item->parameter == CITY_CANVAS_TOOL_RAISE_TERRAIN ||
            item->parameter == CITY_CANVAS_TOOL_LOWER_TERRAIN || item->parameter == CITY_CANVAS_TOOL_LEVEL_TERRAIN;
      case CITY_MENU_ACTION_BUDGET: case CITY_MENU_ACTION_POLICIES: case CITY_MENU_ACTION_PANEL:
      case CITY_MENU_ACTION_NEWSPAPER: case CITY_MENU_ACTION_DISASTERS: case CITY_MENU_ACTION_ADVISOR:
      case CITY_MENU_ACTION_SUBMENU: return false;
      case CITY_MENU_ACTION_COMMAND:
        if (item->command == CITY_COMMAND_SET_SIMULATION_SPEED ||
            item->command == CITY_COMMAND_START_DISASTER || item->command == CITY_COMMAND_TOGGLE_AUTO_BUDGET)
          return false;
        break;
      default: break;
    }
  }
  if (item->action == CITY_MENU_ACTION_TOOL && item->parameter >= CITY_CANVAS_TOOL_DISPATCH_POLICE &&
      item->parameter <= CITY_CANVAS_TOOL_DISPATCH_MILITARY)
    return (desktop->menu.emergency_enabled & (1 << (item->parameter - CITY_CANVAS_TOOL_DISPATCH_POLICE))) != 0;
  if (item->action == CITY_MENU_ACTION_GOTO_DISASTER)
    return (desktop->menu.emergency_enabled & CITY_EMERGENCY_GOTO) != 0;
  if (item->action == CITY_MENU_ACTION_COMMAND && item->command == CITY_COMMAND_RECALL_DISPATCH)
    return (desktop->menu.emergency_enabled & CITY_EMERGENCY_RECALL) != 0;
  if (item->action == CITY_MENU_ACTION_CATEGORY) return ButtonEnabled(desktop, item->parameter + 1);
  if (item->action == CITY_MENU_ACTION_TOOLBAR) {
    if (item->parameter == CITY_TOOLBAR_BUTTON_ZOOM_IN) return ButtonEnabled(desktop, CITY_TOPBAR_ZOOM_IN + 1);
    if (item->parameter == CITY_TOOLBAR_BUTTON_ZOOM_OUT) return ButtonEnabled(desktop, CITY_TOPBAR_ZOOM_OUT + 1);
  }
  return true;
}

CityRect CityDropdownCaptureBounds(CityRect popup, CityRect anchor, int margin) {
  return (CityRect){IntMin(popup.left, anchor.left) - margin, IntMin(popup.top, anchor.top) - margin,
      IntMax(popup.right, anchor.right) + margin, IntMax(popup.bottom, anchor.bottom) + margin};
}

static bool OutsideCityMenu(const CityCanvasDesktop *desktop, int x, int y) {
  CityRect popup = CityMenuBounds(desktop, 1), anchor = popup;
  int margin = CITY_DROPDOWN_DISMISS_MARGIN;
  if (IsToolSubmenu(desktop->menu.selected) || desktop->menu.selected == kDisasterMenu) {
    const Window *window = ConstructionWindow(desktop, desktop->menu.submenu_window);
    if (window) anchor = ConstructionMenuAnchor(desktop, window);
  } else anchor = ToolbarButtonBounds(desktop, desktop->menu.selected - 1);
  return !CanvasPointInRect(CityDropdownCaptureBounds(popup, anchor, margin), x, y);
}

// Pointer coordinates use UI units independently of window origins and map scale.
static bool PressCityMenu(CityCanvasDesktop *desktop, int x, int y) {
  if (desktop->game && !desktop->game->city_active) return false;
  if (!desktop->menu.selected && !CityMenuContains(desktop, x, y)) return false;
  if (IsToolSubmenu(desktop->menu.selected) && !CityMenuContains(desktop, x, y)) {
    CloseCityMenu(desktop);
    return false;
  }
  int control = ToolbarControlAt(desktop, x, y);
  if (control) {
    int previous = desktop->menu.selected;
    CloseCityMenu(desktop);
    if (!ButtonEnabled(desktop, control)) { desktop->menu.held = false; return true; }
    DirtyMenu(desktop);
    const Window *previous_window = ConstructionWindow(desktop, desktop->menu.pressed_window);
    if (previous_window) Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(previous_window));
    desktop->menu.pressed_window = 0;
    desktop->menu.submenu_pending = false;
    desktop->menu.pressed = control;
    desktop->menu.released = false;
    if (control <= CITY_TOPBAR_COUNT) {
      if (ToolbarUsesDropdown(control - 1)) {
        if (previous != control) desktop->menu.selected = control;
      } else if (kCityToolbarDefinitions[control - 1].category)
        OpenConstructionWindow(desktop, control, ConstructionGroupIndex(control - 1) >= 0);
    }
    DirtyMenu(desktop);
    DirtyStatus(desktop);
  } else if (desktop->menu.selected && !MenuItemAt(desktop, x, y)) CloseCityMenu(desktop);
  else MoveCityMenu(desktop, x, y);
  desktop->menu.held = true;
  return true;
}

static bool MoveCityMenu(CityCanvasDesktop *desktop, int x, int y) {
  if (!desktop->menu.selected) return desktop->menu.held;
  if (!desktop->menu.held && OutsideCityMenu(desktop, x, y)) {
    CloseCityMenu(desktop);
    return false;
  }
  int primary = PrimaryButtonAt(desktop, x, y);
  if (primary && primary <= CITY_TOPBAR_COUNT && ToolbarUsesDropdown(primary - 1) &&
      primary != desktop->menu.selected && ButtonEnabled(desktop, primary)) {
    DirtyMenu(desktop);
    desktop->menu.selected = primary;
    desktop->menu.hovered = 0;
    DirtyMenu(desktop);
  }
  int hovered = MenuItemAt(desktop, x, y);
  if (hovered != desktop->menu.hovered) {
    desktop->menu.hovered = hovered;
    Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 1));
  }
  return true;
}

static const CityMenuItem *ActivateMenuItem(CityCanvasDesktop *desktop, const CityMenuItem *item) {
  if (!MenuItemEnabled(desktop, item)) return NULL;
  if (item->action == CITY_MENU_ACTION_SUBMENU) {
    DirtyMenu(desktop);
    desktop->menu.selected = item->parameter;
    desktop->menu.submenu_window = desktop->menu.pressed_window;
    desktop->menu.hovered = 0;
    DirtyMenu(desktop);
    return ActivateMenuItem(desktop, RememberedSubmenuTool(desktop, item->parameter));
  }
  if (item->action == CITY_MENU_ACTION_CATEGORY) {
    int category = item->parameter + 1;
    if (!ButtonEnabled(desktop, category)) return NULL;
    OpenConstructionWindow(desktop, category, false);
    return NULL;
  }
  if (item->action == CITY_MENU_ACTION_DISASTERS) {
    DirtyMenu(desktop);
    desktop->menu.selected = kDisasterMenu;
    desktop->menu.submenu_window = desktop->menu.pressed_window;
    desktop->menu.hovered = 0;
    DirtyMenu(desktop);
    return NULL;
  }
  if (item->action == CITY_MENU_ACTION_TREE_SPECIES) {
    desktop->menu.tree_species = item->parameter;
    desktop->menu.category = CITY_TOPBAR_LANDSCAPE + 1;
    int group = ConstructionGroupIndex(CITY_TOPBAR_LANDSCAPE);
    desktop->menu.remembered_group[group] = CITY_CANVAS_TOOL_TREES + 1;
  }
  if (item->action == CITY_MENU_ACTION_TOOL) {
    desktop->menu.category = 0;
    for (int category = 0; category < CITY_TOPBAR_COUNT; ++category)
      if (kCityToolbarDefinitions[category].category)
        for (int i = 0; i < kCityToolbarDefinitions[category].count; ++i)
          if (&kCityToolbarDefinitions[category].items[i] == item) {
            desktop->menu.remembered[category] = item->parameter + 1;
            desktop->menu.category = ToolbarIsZoneCategory(category) ? CITY_TOPBAR_RESIDENTIAL + 1 : category + 1;
          }
    for (int group = 0; group < CITY_CONSTRUCTION_GROUP_COUNT; ++group)
      for (int i = 0; i < kConstructionGroups[group].count; ++i)
        if (item == &kConstructionGroups[group].items[i] ||
            (kConstructionGroups[group].items[i].action == CITY_MENU_ACTION_TOOL &&
             item->parameter != CITY_CANVAS_TOOL_DEMOLISH &&
             item->parameter == kConstructionGroups[group].items[i].parameter)) {
          desktop->menu.category = kConstructionGroupCategories[group] + 1;
          if (item->parameter == CITY_CANVAS_TOOL_AIRPORT || item->parameter == CITY_CANVAS_TOOL_SEAPORT)
            desktop->menu.remembered[CITY_TOPBAR_PORTS] = item->parameter + 1;
        }
    for (int submenu = 0; submenu < CITY_TOOL_SUBMENU_COUNT; ++submenu)
      for (int i = 0; i < kToolSubmenus[submenu].count; ++i)
        if (item->parameter == kToolSubmenus[submenu].items[i].parameter) {
          desktop->menu.remembered_submenu[submenu] = item->parameter + 1;
          desktop->menu.category = PrimaryToolbarCategory(kToolSubmenus[submenu].category) + 1;
        }
    if (desktop->menu.category) {
      int group = ConstructionGroupIndex(PrimaryToolbarCategory(desktop->menu.category - 1));
      if (group >= 0) desktop->menu.remembered_group[group] = item->parameter + 1;
    }
  }
  return item;
}

static int ConstructionItemAt(const Window *window, int x, int y) {
  const CityToolbarDefinition *category = ConstructionWindowDefinition(window);
  if (window->shaded || !CanvasPointInRect(window->bounds, x, y) ||
      (ConstructionUsesList(window->toolbar_category, window->toolbar_group) &&
       !CanvasPointInRect(ConstructionRowsBounds(window), x, y))) return 0;
  int first = ConstructionUsesList(window->toolbar_category, window->toolbar_group) ? window->toolbar_scroll : 0;
  int end = ConstructionUsesList(window->toolbar_category, window->toolbar_group) ?
      IntMin(category->count, first + ConstructionVisibleRows(window)) : category->count;
  for (int i = first; i < end; ++i) {
    if (ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group) &&
        i != 12 && i >= TreePickerCount(window->toolbar_selection)) continue;
    if (CanvasPointInRect(ContextButtonBounds(window, i), x, y)) return i + 1;
  }
  return 0;
}

static void ScrollConstructionWindow(CityCanvasDesktop *desktop, Window *window, int steps) {
  int limit = IntMax(0, ConstructionWindowDefinition(window)->count - ConstructionVisibleRows(window));
  int row = IntClamp(window->toolbar_scroll + steps, 0, limit);
  if (row == window->toolbar_scroll) return;
  window->toolbar_scroll = row;
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
}

static bool CityToolSupportsPlacementRotation(int tool) {
  if (tool == CITY_CANVAS_TOOL_HIGHWAY) return true;
  int tools = Sc2kCanvasMapPointerTools(tool, 0);
  return tools >= 0 && IsTileKindBuilding(CityToolFacilityKind(tools & 255, (tools >> 8) & 255));
}

static bool ConstructionWindowCanRotate(const Window *window) {
  const CityToolbarDefinition *definition = ConstructionWindowDefinition(window);
  for (int i = 0; i < definition->count; ++i)
    if (definition->items[i].action == CITY_MENU_ACTION_TOOL &&
        CityToolSupportsPlacementRotation(definition->items[i].parameter)) return true;
  return false;
}

static void RotateCityPlacement(CityCanvasDesktop *desktop, bool reverse) {
  CityInterfaceState *ui = desktop->interface;
  ui->placement_rotation = (ui->placement_rotation + (reverse ? 3 : 1)) & 3;
  ui->placement_rotation_set = true;
  ui->placement_dirty = true;
  for (int i = 0; i < desktop->count; ++i)
    if (desktop->windows[i].kind == CITY_CANVAS_CONSTRUCTION)
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&desktop->windows[i]));
}

bool HandleCityPlacementRotationKey(Game *game, CityHotkey key) {
  if (key != 'R' && key != (CITY_HOTKEY_SHIFT | 'R')) return false;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  bool context = CityToolSupportsPlacementRotation(desktop->selected_tool);
  if (!context) return false;
  RotateCityPlacement(desktop, key == (CITY_HOTKEY_SHIFT | 'R'));
  return true;
}

void CityConstructionTooltipAt(const Game *game, const Window *window, int x, int y,
    char *text, size_t capacity) {
  if (y < window->bounds.top + 14) {
    if (ConstructionWindowCanRotate(window) && x >= window->bounds.right - 31 &&
        x < window->bounds.right - 11) {
      CopyText(text, capacity, "Rotate placement clockwise (R). Shift+click or Shift+R rotates counterclockwise.");
      return;
    }
    if (ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group) &&
        x >= window->bounds.right - 22 && x < window->bounds.right - 11) {
      CopyText(text, capacity, window->shaded ? "Expand this window." : "Shade this window.");
      return;
    }
    CopyText(text, capacity, x >= window->bounds.right - 11 ?
        window->sticky ? "Unpin this toolbar to allow automatic closing." :
        "Pin this toolbar to prevent automatic closing." :
        "Drag the title bar to move this window.");
    return;
  }
  if (ConstructionUsesChooser(window->toolbar_category, window->toolbar_group) &&
      CanvasPointInRect(ConstructionConfirmBounds(window), x, y)) {
    CopyText(text, capacity, game->ui->desktop.menu.show_shortcuts ?
        "Confirm this building and activate its placement tool (Enter)." :
        "Confirm this building and activate its placement tool.");
    return;
  }
  if (ConstructionHasScrollbar(window) && x >= ConstructionRowsBounds(window).right - kConstructionScrollbarWidth) {
    CopyText(text, capacity, "Scroll the construction list. You can also use the mouse wheel.");
    return;
  }
  if (ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group) &&
      CanvasPointInRect(TreeClimateBounds(window), x, y)) {
    CopyText(text, capacity, "Choose trees for a climate. Plant on matching terrain; the ground stays unchanged.");
    return;
  }
  int row = ConstructionItemAt(window, x, y);
  if (!row) return;
  const CityMenuItem *item = &ConstructionWindowDefinition(window)->items[row - 1];
  if (ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group) && row != 13)
    item = &kTreeItems[TreePickerSpecies(window->toolbar_selection, row - 1)];
  if (item->action == CITY_MENU_ACTION_TOOL) {
    char status[256];
    char shortcut[48];
    CityHotkeyToString(game->ui->desktop.menu.show_shortcuts ? item->shortcut : 0, shortcut, sizeof(shortcut));
    FormatCityToolStatusText(status, sizeof(status), item->parameter);
    bool bridge_hint = item->parameter == CITY_CANVAS_TOOL_ROAD || item->parameter == CITY_CANVAS_TOOL_RAIL;
    snprintf(text, capacity, "%s%s%s%s%s", status, shortcut[0] ? " (" : "", shortcut, shortcut[0] ? ")" : "",
        bridge_hint ? ". Drag from a bank toward water to choose a bridge." : "");
    if (item->parameter == CITY_CANVAS_TOOL_DEZONE)
      CopyText(text, capacity, game->ui->desktop.menu.show_shortcuts ?
          "De-zone: click or drag on the map to remove zoning (V)." :
          "De-zone: click or drag on the map to remove zoning.");
  } else if (item->action == CITY_MENU_ACTION_SUBMENU)
    snprintf(text, capacity, "%s: click to choose a variant.", item->label);
  else if (item->action == CITY_MENU_ACTION_COMMAND && item->command == CITY_COMMAND_RECALL_DISPATCH)
    CopyText(text, capacity, "Recall all deployed police, firefighters and military units.");
  else CopyText(text, capacity, item->label);
}

static const CityMenuItem *ReleaseCityMenu(CityCanvasDesktop *desktop, int x, int y) {
  bool held = desktop->menu.held;
  desktop->menu.held = false;
  if (!held) return NULL;
  int row = MenuItemAt(desktop, x, y);
  if (row) {
    int count;
    const CityMenuItem *items = DropdownItems(desktop, &count);
    const CityMenuItem *item = &items[row - 1];
    if (!MenuItemEnabled(desktop, item)) return NULL;
    if (MenuItemIsToggle(item)) DirtyMenu(desktop);
    else CloseCityMenu(desktop);
    return ActivateMenuItem(desktop, item);
  }
  int control = ToolbarControlAt(desktop, x, y);
  if (!control || control != desktop->menu.pressed) return NULL;
  if (control == kToolbarSwitch) {
    CityToolbarLayout layout = ToolbarLayout(desktop);
    desktop->menu.toolbar_page = (desktop->menu.toolbar_page + 1) % layout.pages;
    DirtyMenu(desktop);
    return NULL;
  }
  if (control == kStatusDate) return &kCityToolbarDefinitions[CITY_TOPBAR_PAUSE].direct;
  if (control == kStatusMoney) return TerrainToolbar(desktop) ? NULL : &kCityToolbarDefinitions[CITY_TOPBAR_BUDGET].direct;
  if (control == kToolbarDemand) return &kChartItems[kChartPopulation];
  if (control <= CITY_TOPBAR_COUNT) {
    const CityToolbarDefinition *button = &kCityToolbarDefinitions[control - 1];
    if (ToolbarUsesDropdown(control - 1)) return NULL;
    if (!button->category) return &button->direct;
    return NULL;
  }
  return NULL;
}

const CityMenuItem *CityMenuKey(CityCanvasDesktop *desktop, CityHotkey key) {
  if (key == CITY_KEY_F10) {
    if (desktop->menu.selected) CloseCityMenu(desktop);
    else {
      desktop->menu.selected = CITY_TOPBAR_FILE + 1;
      RevealToolbarButton(desktop, CITY_TOPBAR_FILE);
      DirtyMenu(desktop);
    }
  } else if (desktop->menu.selected) {
    int count;
    const CityMenuItem *items = DropdownItems(desktop, &count);
    if (key == CITY_KEY_ESCAPE) CloseCityMenu(desktop);
    else if ((key == CITY_KEY_RETURN || key == CITY_KEY_RIGHT) && desktop->menu.hovered &&
        (key == CITY_KEY_RETURN || items[desktop->menu.hovered - 1].action == CITY_MENU_ACTION_SUBMENU ||
         items[desktop->menu.hovered - 1].action == CITY_MENU_ACTION_CATEGORY)) {
      const CityMenuItem *item = &items[desktop->menu.hovered - 1];
      if (MenuItemIsToggle(item)) DirtyMenu(desktop);
      else CloseCityMenu(desktop);
      return ActivateMenuItem(desktop, item);
    } else if (count && (key == CITY_KEY_UP || key == CITY_KEY_DOWN)) {
      int row = desktop->menu.hovered;
      for (int visited = 0; visited < count; ++visited) {
        row = key == CITY_KEY_DOWN ? row % count + 1 : row > 1 ? row - 1 : count;
        if (MenuItemEnabled(desktop, &items[row - 1])) break;
      }
      desktop->menu.hovered = MenuItemEnabled(desktop, &items[row - 1]) ? row : 0;
      DirtyMenu(desktop);
    } else if (key == CITY_KEY_LEFT && IsToolSubmenu(desktop->menu.selected)) {
      DirtyMenu(desktop);
      desktop->menu.hovered = desktop->menu.selected = 0;
      DirtyMenu(desktop);
    } else if (key == CITY_KEY_LEFT || key == CITY_KEY_RIGHT) {
      int index = desktop->menu.selected == kDisasterMenu ? CITY_TOPBAR_EMERGENCY :
          IsToolSubmenu(desktop->menu.selected) ?
          kToolSubmenus[desktop->menu.selected - kResidentialDensityMenu].category : desktop->menu.selected - 1;
      index = PrimaryToolbarCategory(index);
      int step = key == CITY_KEY_RIGHT ? 1 : -1;
      DirtyMenu(desktop);
      int count, position = 0;
      const u8 *order = ToolbarOrder(desktop, &count);
      while (position < count && order[position] != index) ++position;
      if (position == count) position = 0;
      do {
        position = (position + step + count) % count;
        index = order[position];
      } while (!ToolbarUsesDropdown(index) || !ButtonEnabled(desktop, index + 1));
      desktop->menu.selected = index + 1;
      RevealToolbarButton(desktop, index);
      desktop->menu.hovered = 0;
      DirtyMenu(desktop);
    }
  }
  return NULL;
}

bool HandleCityToolbarShortcut(Game *game, CityHotkey key) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  static const struct { CityHotkey key; u8 button; } kMenuShortcuts[] = {
    {CITY_HOTKEY_ALT | 'F', CITY_TOPBAR_FILE}, {CITY_HOTKEY_ALT | 'S', CITY_TOPBAR_SETTINGS},
    {CITY_HOTKEY_ALT | 'H', CITY_TOPBAR_HELP}, {CITY_HOTKEY_ALT | 'Z', CITY_TOPBAR_RESIDENTIAL},
    {CITY_HOTKEY_ALT | 'U', CITY_TOPBAR_POWER}, {CITY_HOTKEY_ALT | 'C', CITY_TOPBAR_EDUCATION},
    {CITY_HOTKEY_ALT | 'V', CITY_TOPBAR_MAP}, {CITY_HOTKEY_ALT | 'N', CITY_TOPBAR_ROADS},
    {CITY_HOTKEY_ALT | 'L', CITY_TOPBAR_LANDSCAPE}, {CITY_HOTKEY_ALT | 'E', CITY_TOPBAR_EMERGENCY},
    {CITY_HOTKEY_ALT | 'M', CITY_TOPBAR_REPORTS}, {CITY_HOTKEY_ALT | 'A', CITY_TOPBAR_BUDGET}
  };
  enum { kMenuShortcutCount = sizeof(kMenuShortcuts) / sizeof(*kMenuShortcuts) };
  for (int i = 0; i < kMenuShortcutCount; ++i) {
    if (kMenuShortcuts[i].key != key) continue;
    if (!ButtonEnabled(desktop, kMenuShortcuts[i].button + 1)) return true;
    CloseCityMenu(desktop);
    RevealToolbarButton(desktop, kMenuShortcuts[i].button);
    if (ConstructionGroupIndex(kMenuShortcuts[i].button) >= 0) {
      OpenConstructionWindow(desktop, kMenuShortcuts[i].button + 1, true);
    } else if (kCityToolbarDefinitions[kMenuShortcuts[i].button].category) {
      OpenConstructionWindow(desktop, kMenuShortcuts[i].button + 1, false);
    } else {
      desktop->menu.selected = kMenuShortcuts[i].button + 1;
      desktop->menu.hovered = 0;
    }
    DirtyMenu(desktop);
    return true;
  }
  if (!desktop->menu.selected && desktop->count) {
    Window *window = &desktop->windows[desktop->count - 1];
    if (window->kind == CITY_CANVAS_CONSTRUCTION &&
        ConstructionUsesChooser(window->toolbar_category, window->toolbar_group)) {
      const CityToolbarDefinition *category = ConstructionWindowDefinition(window);
      if (key == CITY_KEY_UP || key == CITY_KEY_DOWN) {
        int direction = key == CITY_KEY_DOWN ? 1 : -1;
        window->toolbar_selection = (window->toolbar_selection + direction + category->count) % category->count;
        if (window->toolbar_selection < window->toolbar_scroll) window->toolbar_scroll = window->toolbar_selection;
        if (window->toolbar_selection >= window->toolbar_scroll + ConstructionVisibleRows(window))
          window->toolbar_scroll = window->toolbar_selection - ConstructionVisibleRows(window) + 1;
        Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
        return true;
      }
      if (key == CITY_KEY_RETURN) {
        const CityMenuItem *item = &category->items[window->toolbar_selection];
        if (!window->sticky) Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
        ExecuteCityMenuItem(game, ActivateMenuItem(desktop, item));
        return true;
      }
    }
  }
  const CityMenuItem *selected = NULL;
  if (key == '=' || key == (CITY_HOTKEY_SHIFT | '=') || key == CITY_KEY_KEYPAD_PLUS)
    selected = &kCityToolbarDefinitions[CITY_TOPBAR_ZOOM_IN].direct;
  if (key == CITY_KEY_KEYPAD_MINUS) selected = &kCityToolbarDefinitions[CITY_TOPBAR_ZOOM_OUT].direct;
  static const struct { const CityMenuItem *items; int count; } kPopupMenus[] = {
    {kZoningMenu, kZoningItemCount},
    {kUtilitiesMenu, sizeof(kUtilitiesMenu) / sizeof(*kUtilitiesMenu)},
    {kCivicMenu, sizeof(kCivicMenu) / sizeof(*kCivicMenu)}
  };
  enum { kPopupMenuCount = sizeof(kPopupMenus) / sizeof(*kPopupMenus) };
  for (int i = 0; !selected && i < kPopupMenuCount; ++i)
    for (int j = 0; !selected && j < kPopupMenus[i].count; ++j) {
      const CityMenuItem *item = &kPopupMenus[i].items[j];
      if (item->action != CITY_MENU_ACTION_SUBMENU && item->shortcut &&
          item->shortcut == key) selected = item;
    }
  for (int i = 0; !selected && i < CITY_TOPBAR_COUNT; ++i) {
    const CityToolbarDefinition *button = &kCityToolbarDefinitions[i];
    if (!button->category && !button->count && button->direct.shortcut &&
        button->direct.shortcut == key) selected = &button->direct;
    for (int j = 0; !selected && j < button->count; ++j)
      if (button->items[j].shortcut && button->items[j].shortcut == key) selected = &button->items[j];
  }
  for (int i = 0; !selected && i < CITY_TOOL_SUBMENU_COUNT; ++i)
    for (int j = 0; !selected && j < kToolSubmenus[i].count; ++j) {
      const CityMenuItem *item = &kToolSubmenus[i].items[j];
      if (item->shortcut && item->shortcut == key) selected = item;
    }
  if (!selected) return false;
  if (selected->action == CITY_MENU_ACTION_TOOLBAR &&
      (selected->parameter == CITY_TOOLBAR_BUTTON_ZOOM_IN || selected->parameter == CITY_TOOLBAR_BUTTON_ZOOM_OUT) &&
      !ButtonEnabled(desktop, selected->parameter == CITY_TOOLBAR_BUTTON_ZOOM_IN ?
          CITY_TOPBAR_ZOOM_IN + 1 : CITY_TOPBAR_ZOOM_OUT + 1)) return true;
  CloseCityMenu(desktop);
  ExecuteCityMenuItem(game, ActivateMenuItem(desktop, selected));
  return true;
}

static bool IsCityMenuItemChecked(const Game *game, const CityMenuItem *item) {
  const City *city = game->city;
  if (item->action == CITY_MENU_ACTION_SUBMENU) {
    const CityToolSubmenu *submenu = &kToolSubmenus[item->parameter - kResidentialDensityMenu];
    for (int i = 0; i < submenu->count; ++i)
      if (submenu->items[i].parameter == game->ui->desktop.selected_tool) return true;
    return false;
  }
  if (item->action == CITY_MENU_ACTION_TOOL) {
    if (game->ui->desktop.selected_tool == item->parameter) return true;
    if (IsToolSubmenu(game->ui->desktop.menu.selected))
      return RememberedSubmenuTool(&game->ui->desktop, game->ui->desktop.menu.selected)->parameter == item->parameter;
    return false;
  }
  if (item->action == CITY_MENU_ACTION_TREE_SPECIES)
    return game->ui->desktop.selected_tool == CITY_CANVAS_TOOL_TREES &&
        game->ui->desktop.menu.tree_species == item->parameter;
  if (item->action == CITY_MENU_ACTION_PAUSE) return city->settings.speed == CITY_SPEED_PAUSED;
  if (item->action == CITY_MENU_ACTION_MUSIC) return game->audio->jukebox.playing != 0;
  switch (item->action) {
  case CITY_MENU_ACTION_AUTO_GOTO: return !game->ui->auto_goto_disabled;
  case CITY_MENU_ACTION_SUBSCRIPTION: return !game->ui->newspaper_delivery_disabled;
  case CITY_MENU_ACTION_EXTRA_NEWSPAPER: return !game->ui->extra_newspaper_disabled;
  case CITY_MENU_ACTION_MAP_LAYER: return CityMapLayerVisible(game->drawing, item->parameter);
  case CITY_MENU_ACTION_TOOLBAR:
    return item->parameter >= CITY_TOOLBAR_BUTTON_SHOW_BUILDINGS &&
        item->parameter <= CITY_TOOLBAR_BUTTON_UNDERGROUND &&
        game->drawing->map_state.display_flags[CITY_TOOLBAR_BUTTON_UNDERGROUND - item->parameter] != 0;
  default: break;
  }
  switch (item->command) {
  case CITY_COMMAND_SET_SIMULATION_SPEED:
    return (city->settings.speed == CITY_SPEED_PAUSED ? game->ui->resume_speed : city->settings.speed) == item->parameter;
  case CITY_COMMAND_TOGGLE_AUTO_BUDGET: return city->indicators.auto_budget_enabled != 0;
  case CITY_COMMAND_TOGGLE_NO_DISASTERS: return city->indicators.event_flag != 0;
  default: return false;
  }
}

void CityMenuTooltipAt(const Game *game, int x, int y, char *text, size_t capacity) {
  const CityCanvasDesktop *desktop = &game->ui->desktop;
  int row = MenuItemAt(desktop, x, y);
  if (row) {
    int count;
    const CityMenuItem *items = DropdownItems(desktop, &count);
    const CityMenuItem *item = &items[row - 1];
    char shortcut[48];
    if (desktop->menu.show_shortcuts) MenuShortcutToString(item, shortcut, sizeof(shortcut));
    else shortcut[0] = 0;
    snprintf(text, capacity, "%s%s%s%s", item->label, shortcut[0] ? " (" : "", shortcut,
        shortcut[0] ? ")" : "");
    return;
  }
  int control = ToolbarControlAt(desktop, x, y);
  if (control == kToolbarSwitch) {
    CityToolbarLayout layout = ToolbarLayout(desktop);
    snprintf(text, capacity, "Switch toolbar buttons (%d of %d).",
        desktop->menu.toolbar_page % layout.pages + 1, layout.pages);
  } else if (control && control <= CITY_TOPBAR_COUNT) {
    if (control == CITY_TOPBAR_UNDERWORLD + 1) {
      CopyText(text, capacity, game->drawing->map_state.events_suppressed ? "Switch to Overworld." :
          "Switch to Underworld to view pipes and subways.");
      return;
    }
    if (control == CITY_TOPBAR_PAUSE + 1 && game->city->settings.timer_mode == CITY_TIMER_STOPPED) {
      CopyText(text, capacity, "Start City on the edited terrain (Space).");
      return;
    }
    const CityToolbarDefinition *button = &kCityToolbarDefinitions[control - 1];
    snprintf(text, capacity, "%s%s", button->label, !ButtonEnabled(desktop, control) ? " (unavailable)" :
        ToolbarUsesDropdown(control - 1) ? " menu" :
        button->category ? ": open or reset its construction window." : "");
  } else if (control == kStatusDate && game->city->settings.timer_mode == CITY_TIMER_STOPPED)
    CopyText(text, capacity, "Start City on the edited terrain (Space).");
  else if (control == kStatusDate) CopyText(text, capacity, desktop->menu.show_shortcuts ?
      "Simulation date. Click to pause or resume (Space)." : "Simulation date. Click to pause or resume.");
  else if (control == kStatusMoney && TerrainToolbar(desktop))
    CopyText(text, capacity, "Terrain tools are free until you start the city.");
  else if (control == kStatusMoney) CopyText(text, capacity, desktop->menu.show_shortcuts ?
      "Treasury. Click to open Budget (Ctrl+B)." : "Treasury. Click to open Budget.");
  else if (control == kToolbarDemand)
    CopyText(text, capacity, "Residential, commercial and industrial demand. Click for population.");

}

static void ReleaseCityToolbarFeedback(CityCanvasDesktop *desktop, u32 milliseconds) {
  if (desktop->menu.pressed) {
    desktop->menu.released = true;
    desktop->menu.release_time = milliseconds;
  }
}

bool PollCityToolbar(Game *game, u32 milliseconds) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  if (desktop->menu.submenu_pending && desktop->menu.held) {
    const Window *window = ConstructionWindow(desktop, desktop->menu.pressed_window);
    int item = desktop->menu.pressed - CITY_TOPBAR_COUNT - 1;
    CityPoint point = {0};
    if (window)
      point = CityWindowPointFromPixels(desktop, window->window_id,
          desktop->tooltip.pointer_x, desktop->tooltip.pointer_y);
    if (!window || ConstructionItemAt(window, point.x, point.y) != item + 1) desktop->menu.submenu_pending = false;
    else if (!desktop->menu.submenu_clock_started) {
      desktop->menu.press_time = milliseconds;
      desktop->menu.submenu_clock_started = true;
    } else if ((u32)(milliseconds - desktop->menu.press_time) >= 300) {
      desktop->menu.submenu_pending = false;
      ExecuteCityMenuItem(game, ActivateMenuItem(desktop, &ConstructionWindowDefinition(window)->items[item]));
      return true;
    }
  }
  if (!desktop->menu.released || (u32)(milliseconds - desktop->menu.release_time) < 150) return false;
  desktop->menu.released = false;
  const Window *window = ConstructionWindow(desktop, desktop->menu.pressed_window);
  if (window) Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  desktop->menu.pressed_window = 0;
  desktop->menu.pressed = 0;
  DirtyMenu(desktop);
  DirtyStatus(desktop);
  return true;
}

void UpdateCityToolbar(Game *game) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  int top = CityMenuBounds(desktop, 0).bottom;
  for (int i = 0; i < desktop->count; ++i) {
    Window *window = &desktop->windows[i];
    if (!window->visible || window->bounds.top >= top) continue;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
    int height = window->bounds.bottom - window->bounds.top;
    window->bounds.top = top;
    window->bounds.bottom = top + height;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
  CityMenuState *menu = &desktop->menu;
  City *city = game->city;
  double now = ReadGameTime(game, GAME_TIME_REALTIME);
  int zoom = game->drawing->default_map_view->zoom + (game->drawing->default_map_view->magnified_large_zoom != 0);
  desktop->map_zoom = zoom;
  bool editing = city->settings.timer_mode == CITY_TIMER_STOPPED;
  if (menu->last_editing != editing) {
    menu->last_editing = editing;
    menu->toolbar_page = 0;
    CloseCityMenu(desktop);
    Sc2kDirtyMarkRect(&desktop->dirty, (CityRect){0, 0, desktop->canvas_width, CITY_MENU_BAR_HEIGHT});
  }
  bool paused = editing || game->ui->dialog_pause_requested || city->settings.speed == CITY_SPEED_PAUSED;
  bool rewards_enabled = ButtonEnabled(desktop, CITY_TOPBAR_REWARDS + 1);
  bool underworld = game->drawing->map_state.events_suppressed != 0;
  u8 emergency_enabled = 0;
  for (int service = CITY_DISPATCH_POLICE; service <= CITY_DISPATCH_MILITARY; ++service)
    if (CanDispatchCityService(city, service)) emergency_enabled |= 1 << service;
  if (HasCityDispatchedUnits(city)) emergency_enabled |= CITY_EMERGENCY_RECALL;
  if ((city->disasters.pending_type || city->disasters.map_update_pending || city->disaster_work.type) &&
      MapTileXYOrInvalid(city, city->disasters.location.x, city->disasters.location.y) != INVALID_TILE)
    emergency_enabled |= CITY_EMERGENCY_GOTO;
  if (menu->emergency_enabled != emergency_enabled) {
    menu->emergency_enabled = emergency_enabled;
    DirtyMenu(desktop);
    for (int i = 0; i < desktop->count; ++i)
      if (desktop->windows[i].kind == CITY_CANVAS_CONSTRUCTION &&
          !desktop->windows[i].toolbar_group && desktop->windows[i].toolbar_category == CITY_TOPBAR_EMERGENCY + 1)
        Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&desktop->windows[i]));
  }
  if (menu->last_tool != desktop->selected_tool) {
    menu->last_tool = desktop->selected_tool;
    if (editing) DirtyMenu(desktop);
    menu->tool_status_until = desktop->selected_tool >= 0 ? now + 10 : 0;
    for (int i = 0; i < desktop->count; ++i)
      if (desktop->windows[i].kind == CITY_CANVAS_CONSTRUCTION)
        Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(&desktop->windows[i]));
  }
  if (menu->last_zoom != zoom || menu->last_paused != paused || menu->last_rewards_enabled != rewards_enabled ||
      menu->last_underworld != underworld) {
    menu->last_zoom = zoom; menu->last_paused = paused;
    menu->last_rewards_enabled = rewards_enabled;
    menu->last_underworld = underworld;
    DirtyMenu(desktop);
  }
  char date[32], money[CITY_CURRENCY_TEXT_CAPACITY], notice[256];
  int month = city->clock.tick / CITY_PHASE_COUNT % CITY_MONTH_COUNT;
  int year = city->geography.start_year + city->clock.tick / CITY_TICKS_PER_YEAR;
  snprintf(date, sizeof(date), "%d %s %d", CityDayOfMonth(city),
      game->resources->calendar_names.abbreviated[month], year);
  FormatCityCurrency(game, money, city->indicators.cash);
  if (editing) {
    CopyText(date, sizeof(date), "Start City");
    CopyText(money, sizeof(money), "Free terrain editing");
    CopyText(notice, sizeof(notice), "Edit terrain, then click Start City or press Space.");
  } else if (desktop->selected_tool >= 0 && now < menu->tool_status_until)
    CopyText(notice, sizeof(notice), game->ui->status.tool_text);
  else if (game->ui->status.problem_description[0])
    CopyText(notice, sizeof(notice), game->ui->status.problem_description);
  else if (paused) snprintf(notice, sizeof(notice), "Paused - %.*s", 160, CityNameText(&city->name));
  else if (desktop->selected_tool >= 0) CopyText(notice, sizeof(notice), game->ui->status.tool_text);
  else snprintf(notice, sizeof(notice), "%.*s - Population %u",
      160, CityNameText(&city->name), city->indicators.population);
  i16 demand[3];
  for (int i = 0; i < 3; ++i) demand[i] = city->economic_tables.sector_rates.offsets.value[i];
  if (strcmp(date, menu->date) || strcmp(money, menu->money) || strcmp(notice, menu->notice)) {
    CopyText(menu->date, sizeof(menu->date), date); CopyText(menu->money, sizeof(menu->money), money);
    CopyText(menu->notice, sizeof(menu->notice), notice);
    DirtyStatus(desktop);
  }
  if (memcmp(demand, menu->demand, sizeof(demand))) {
    memcpy(menu->demand, demand, sizeof(demand));
    Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 3));
  }
}

static void DrawConstructionScrollbar(Canvas *canvas, const Window *window) {
  if (ConstructionHasScrollbar(window)) {
    CityRect b = ConstructionRowsBounds(window);
    int left = b.right - kConstructionScrollbarWidth, top = b.top;
    DrawCanvasButtonFrame(canvas, (CityRect){left, top, b.right, top + 12},
        kConstructionFace, kConstructionLight, kConstructionDark, true);
    DrawCanvasButtonFrame(canvas, (CityRect){left, b.bottom - 12, b.right, b.bottom},
        kConstructionFace, kConstructionLight, kConstructionDark, true);
    for (int row = 0; row < 4; ++row) {
      CanvasFill(canvas, left + 5 - row, top + 4 + row, left + 6 + row, top + 5 + row, 0xFF101010);
      CanvasFill(canvas, left + 2 + row, b.bottom - 8 + row, left + 9 - row, b.bottom - 7 + row, 0xFF101010);
    }
    int track = b.bottom - top - 24;
    if (track > 0) {
      DrawCanvasFrame(canvas, (CityRect){left, top + 12, b.right, b.bottom - 12},
          kConstructionFace, kConstructionLight, kConstructionDark, false);
      CityScrollbar scrollbar = ConstructionScrollbar(window);
      DrawCanvasFrame(canvas, scrollbar.thumb, kConstructionFace, kConstructionLight, kConstructionDark, true);
    }
  }
}

static const char *ConstructionBuildingDescription(int tool) {
  switch (tool) {
    case CITY_CANVAS_TOOL_COAL: return "A coal-fired plant. Connect it to your city's power network with power lines.";
    case CITY_CANVAS_TOOL_OIL: return "An oil-fired plant. Connect it to the power network to supply buildings.";
    case CITY_CANVAS_TOOL_GAS: return "A gas-fired plant with a smaller output than coal or oil power.";
    case CITY_CANVAS_TOOL_NUCLEAR:
      return "High power output from a nuclear plant. Nuclear power carries a meltdown risk.";
    case CITY_CANVAS_TOOL_HYDRO: return "Hydroelectric power uses a waterfall. Choose a suitable sloped water tile.";
    case CITY_CANVAS_TOOL_WIND: return "A small wind turbine. Output varies with wind and the turbine's elevation.";
    case CITY_CANVAS_TOOL_SOLAR:
      return "Solar power output varies with the weather. Connect the plant to the power grid.";
    case CITY_CANVAS_TOOL_MICROWAVE: return "A microwave receiving plant supplies a large amount of power.";
    case CITY_CANVAS_TOOL_FUSION: return "Fusion provides the largest output of the available power plants.";
    case CITY_CANVAS_TOOL_WATER_PUMP:
      return "Pumps water into the pipe network. Requires power; nearby water and rainfall affect output.";
    case CITY_CANVAS_TOOL_WATER_TOWER:
      return "Stores water for the connected pipe network. Connect it to a working water supply.";
    case CITY_CANVAS_TOOL_WATER_TREATMENT:
      return "A water treatment facility. Connect the building to your city's water network.";
    case CITY_CANVAS_TOOL_DESALINATION:
      return "Produces fresh water from nearby salt water. Requires power and access to the coast.";
    case CITY_CANVAS_TOOL_MAYORS_HOUSE: return "A home for the mayor. Choose a prominent site in your city.";
    case CITY_CANVAS_TOOL_CITY_HALL: return "Give your city government a home with a city hall.";
    case CITY_CANVAS_TOOL_STATUE: return "A commemorative statue for a public square or park.";
    case CITY_CANVAS_TOOL_LLAMA_DOME: return "A distinctive landmark celebrating your city's achievements.";
    case CITY_CANVAS_TOOL_PLYMOUTH_ARCOLOGY: return "A terraced arcology combines a large urban complex on one site.";
    case CITY_CANVAS_TOOL_FOREST_ARCOLOGY:
      return "A Forest arcology brings a self-contained urban complex to your city.";
    case CITY_CANVAS_TOOL_DARCO_ARCOLOGY: return "A tower arcology offers a compact vertical urban complex.";
    case CITY_CANVAS_TOOL_LAUNCH_ARCOLOGY: return "A Launch arcology is an advanced urban complex.";
    default: return "Choose a building, then confirm to place it on the map.";
  }
}

static void DrawConstructionChooser(Game *game, Canvas *canvas, const Window *window) {
  CityRect b = window->bounds, rows = ConstructionRowsBounds(window);
  const CityToolbarDefinition *category = ConstructionWindowDefinition(window);
  const CityMenuItem *item = &category->items[window->toolbar_selection];
  CityRect preview = {b.left + 8, b.top + 21, rows.left - 8, rows.bottom};
  DrawCanvasFrame(canvas, preview, kConstructionFace, kConstructionLight, kConstructionDark, false);
  TileKind kind = CityToolBuildingKind(item->parameter);
  int orientation = (game->ui->placement_rotation + game->city->geography.rotation +
      CityMapViewRotation(game->drawing->default_map_view)) & 3;
  if (!DrawCityBuildingPreview(game->city, canvas,
      (CityRect){preview.left + 3, preview.top + 3, preview.right - 3, preview.bottom - 3}, kind, orientation))
    game->execution_failed = true;
  CanvasTextAt(canvas, rows.left, b.top + 21, "Buildings", canvas_text);
  DrawCanvasFrame(canvas, rows, 0xFF101010, kConstructionLight, kConstructionDark, false);
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip, rows);
  int end = IntMin(category->count, window->toolbar_scroll + ConstructionVisibleRows(window));
  for (int i = window->toolbar_scroll; i < end; ++i) {
    CityRect row = ContextButtonBounds(window, i);
    if (i == window->toolbar_selection)
      CanvasFill(canvas, row.left, row.top, row.right, row.bottom, 0xFF424A90);
    CanvasTextAt(canvas, row.left + 3, row.top + 3, category->items[i].label,
        i == window->toolbar_selection ? 0xFFFFFFFF : 0xFFE8E8B0);
  }
  canvas->clip = clip;
  DrawConstructionScrollbar(canvas, window);
  int packed = Sc2kCanvasMapPointerTools(item->parameter, 0);
  int index = (packed & 255) * CITY_TOOL_VARIANT_CAPACITY + ((packed >> 8) & 255);
  int footprint = kCityToolFootprintSizes[index];
  char currency[CITY_CURRENCY_TEXT_CAPACITY], details[64];
  FormatCityCurrency(game, currency, kCityToolCosts[index]);
  int top = rows.bottom + 10;
  DrawCanvasLine(canvas, b.left + 1, top - 5, b.right - 2, top - 5, kConstructionDark, 1);
  CanvasTextAt(canvas, b.left + 10, top, item->label, 0xFFFFFF00);
  snprintf(details, sizeof(details), "Cost: %s    Size: %d x %d", currency, footprint, footprint);
  CanvasTextAt(canvas, b.left + 10, top + 13, details, canvas_text);
  int per_tile = 0;
  switch (item->parameter) {
    case CITY_CANVAS_TOOL_COAL: per_tile = 44; break;
    case CITY_CANVAS_TOOL_OIL: per_tile = 48; break;
    case CITY_CANVAS_TOOL_GAS: per_tile = 11; break;
    case CITY_CANVAS_TOOL_NUCLEAR: per_tile = 111; break;
    case CITY_CANVAS_TOOL_HYDRO: per_tile = 40; break;
    case CITY_CANVAS_TOOL_MICROWAVE: per_tile = 355; break;
    case CITY_CANVAS_TOOL_FUSION: per_tile = 555; break;
  }
  // The native power traversal measures demand in powered map tiles, not SC3000 MW-h.
  if (per_tile) snprintf(details, sizeof(details), "Capacity: %d powered tiles", per_tile * footprint * footprint);
  else if (window->toolbar_category == CITY_TOPBAR_POWER + 1)
    CopyText(details, sizeof(details), "Output varies with weather and location.");
  else if (window->toolbar_category == CITY_TOPBAR_WATER + 1)
    CopyText(details, sizeof(details), "Connect to the city's pipe network.");
  else CopyText(details, sizeof(details), "Choose a site after pressing Build.");
  CanvasTextAt(canvas, b.left + 10, top + 26, details, canvas_text);
  CanvasTextWrapped(canvas, b.left + 10, top + 42, b.right - 10, b.bottom - 28,
      ConstructionBuildingDescription(item->parameter), canvas_text);
  CityRect confirm = ConstructionConfirmBounds(window);
  const CityMenuState *menu = &game->ui->desktop.menu;
  bool pressed = menu->pressed_window == window->window_id &&
      menu->pressed == CITY_TOPBAR_COUNT + category->count + 1;
  DrawCanvasButtonFrame(canvas, confirm, kConstructionFace, kConstructionLight, kConstructionDark, !pressed);
  CityRect label = {confirm.left + pressed, confirm.top + pressed,
      confirm.right + pressed, confirm.bottom + pressed};
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, label, "Build", canvas_text, false, 10);
}

static int ConstructionItemIcon(const CityCanvasDesktop *desktop, const CityMenuItem *item) {
  int tool = CITY_TOOLBAR_TOOL_ICON_COUNT;
  if (item->action == CITY_MENU_ACTION_TOOL) tool = item->parameter;
  else if (item->action == CITY_MENU_ACTION_SUBMENU) {
    tool = RememberedSubmenuTool(desktop, item->parameter)->parameter;
  } else if (item->action == CITY_MENU_ACTION_CATEGORY) {
    switch (item->parameter) {
      case CITY_TOPBAR_TREES: tool = CITY_CANVAS_TOOL_TREES; break;
      case CITY_TOPBAR_POWER: tool = CITY_CANVAS_TOOL_NUCLEAR; break;
      case CITY_TOPBAR_WATER: tool = CITY_CANVAS_TOOL_WATER_TOWER; break;
      case CITY_TOPBAR_EDUCATION: tool = CITY_CANVAS_TOOL_SCHOOL; break;
      case CITY_TOPBAR_PUBLIC_SERVICES: tool = CITY_CANVAS_TOOL_POLICE; break;
      case CITY_TOPBAR_RECREATION: tool = CITY_CANVAS_TOOL_BIG_PARK; break;
      case CITY_TOPBAR_REWARDS: tool = CITY_CANVAS_TOOL_STATUE; break;
    }
  }
  if (item->action == CITY_MENU_ACTION_GOTO_DISASTER) return CITY_TOPBAR_MAP;
  if (item->action == CITY_MENU_ACTION_TOOL && tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN)
    return CITY_TOOLBAR_STRETCH_TERRAIN_ICON;
  return CITY_TOPBAR_ICON_COUNT + tool;
}

bool PrepareCityConstructionIcons(Game *game) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  for (int i = 0; i < desktop->count; ++i) {
    const Window *window = &desktop->windows[i];
    if (!window->visible || window->kind != CITY_CANVAS_CONSTRUCTION || window->shaded ||
        ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group)) continue;
    const CityToolbarDefinition *category = ConstructionWindowDefinition(window);
    if (ConstructionUsesChooser(window->toolbar_category, window->toolbar_group)) continue;
    if (ConstructionUsesList(window->toolbar_category, window->toolbar_group)) {
      int first = window->toolbar_scroll, last = IntMin(category->count, first + ConstructionVisibleRows(window));
      for (int item = first; item < last; ++item) {
        int icon = ConstructionItemIcon(desktop, &category->items[item]);
        if (!PrepareCityToolbarIconSize(game, icon, kConstructionRowHeight - 4)) return false;
      }
    }
  }
  return true;
}

static void DrawCityConstructionWindow(Game *game, Canvas *canvas, const Window *window) {
  CityRect b = window->bounds;
  bool emergency = !window->toolbar_group && window->toolbar_category == CITY_TOPBAR_EMERGENCY + 1;
  u32 face = emergency ? 0xFF8C3838 : kConstructionFace;
  u32 light = emergency ? 0xFFD58078 : kConstructionLight;
  u32 dark = emergency ? 0xFF4C2020 : kConstructionDark;
  DrawCanvasFrame(canvas, b, face, light, dark, true);
  bool trees = ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group);
  bool rotate = ConstructionWindowCanRotate(window);
  DrawCityWindowCaption(canvas, b, window->title, face, light, dark,
      rotate ? 31 : trees ? 22 : 11);
  if (rotate) {
    CityRect button = {b.right - 31, b.top, b.right - 11, b.top + 14};
    DrawCanvasButtonFrame(canvas, button, face, light, dark, true);
    char label[8];
    snprintf(label, sizeof(label), "R%d", game->ui->placement_rotation + 1);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button, label, canvas_text, true, 10);
  }
  if (trees) {
    CityRect shade = {b.right - 22, b.top, b.right - 11, b.top + 14};
    DrawCanvasButtonFrame(canvas, shade, face, light, dark, !window->shaded);
    DrawCityWindowControlIcon(canvas, shade, CITY_WINDOW_ICON_SHADE, window->shaded);
  }
  CityRect pin = {b.right - 11, b.top, b.right, b.top + 14};
  DrawCanvasButtonFrame(canvas, pin, face, light, dark, !window->sticky);
  DrawCityWindowControlIcon(canvas, pin, CITY_WINDOW_ICON_STICKY, window->sticky);
  if (window->shaded) return;
  if (trees) {
    int climate = window->toolbar_selection, count = TreePickerCount(climate);
    CityTreePickerWindow *picker = &game->ui->tree_picker;
    if (!picker->drawing) {
      picker->drawing = (CityTreePickerDrawing *)CreateCityWindowDrawing(game->drawing,
          CITY_WINDOW_DRAWING_TREE_PICKER, 0);
      if (!picker->drawing) { game->execution_failed = true; return; }
      picker->window_id = window->window_id;
    }
    CityTreePickerDrawing *pixels = picker->drawing;
    for (int i = 0; i < 13; ++i) {
      if (i != 12 && i >= count) continue;
      int species = TreePickerSpecies(climate, i);
      CityRect button = ContextButtonBounds(window, i);
      bool pressed = IsCityMenuItemChecked(game, &kTreeItems[i == 12 ? 12 : species]) ||
          (game->ui->desktop.menu.pressed_window == window->window_id &&
           game->ui->desktop.menu.pressed == CITY_TOPBAR_COUNT + i + 1);
      DrawCanvasButtonFrame(canvas, button, canvas_face, kConstructionLight, kConstructionDark, !pressed);
      CityRect content = {button.left + 3 + pressed, button.top + 3 + pressed,
          button.right - 3 + pressed, button.bottom - 3 + pressed};
      if (i == 12)
        DrawCityInterfaceTextCentered(canvas->interface_font, canvas, content,
            "Trees of random type", canvas_text, false, 10);
      else if (!DrawCityTreePreview(game->resources, canvas, &pixels->trees[i], content,
          CityTreeGlobalSpecies(climate, species))) game->execution_failed = true;
    }
    const char *label = kTreeClimateItems[0].text;
    for (int i = 0; i < (int)(sizeof(kTreeClimateItems) / sizeof(*kTreeClimateItems)); ++i)
      if (kTreeClimateItems[i].value == climate) label = kTreeClimateItems[i].text;
    DrawCityDropdownControl(canvas, &kTreeDropdownColors, TreeClimateBounds(window), label,
        game->ui->desktop.dropdown.window_id == window->window_id, false);
    return;
  }
  if (ConstructionUsesChooser(window->toolbar_category, window->toolbar_group)) {
    DrawConstructionChooser(game, canvas, window);
    return;
  }
  const CityToolbarDefinition *category = ConstructionWindowDefinition(window);
  const CityMenuState *menu = &game->ui->desktop.menu;
  bool list = ConstructionUsesList(window->toolbar_category, window->toolbar_group);
  int first = list ? window->toolbar_scroll : 0;
  int end = list ? IntMin(category->count, first + ConstructionVisibleRows(window)) : category->count;
  for (int i = first; i < end; ++i) {
    const CityMenuItem *item = &category->items[i];
    CityRect button = ContextButtonBounds(window, i);
    bool pressed = (menu->pressed_window == window->window_id && menu->pressed == CITY_TOPBAR_COUNT + i + 1) ||
        IsCityMenuItemChecked(game, item) ||
        (item->action == CITY_MENU_ACTION_SUBMENU && menu->selected == item->parameter);
    bool disabled = !MenuItemEnabled(&game->ui->desktop, item);
    if (disabled) DrawCanvasFrame(canvas, button, face, light, dark, !pressed);
    else pressed |= DrawCanvasButtonFrame(canvas, button, face, light, dark, !pressed);
    CityRect icon = list ? (CityRect){button.left + 1, button.top + 1, button.left + 29, button.bottom - 1} : button;
    if (item->action == CITY_MENU_ACTION_COMMAND && item->command == CITY_COMMAND_RECALL_DISPATCH) {
      int x = (button.left + button.right) / 2, y = (button.top + button.bottom) / 2;
      u32 color = disabled ? 0xFFAD9292 : 0xFFFFFFFF;
      for (int offset = -6; offset <= 6; ++offset) {
        CanvasFill(canvas, x + offset - 1, y + offset - 1, x + offset + 2, y + offset + 2, color);
        CanvasFill(canvas, x + offset - 1, y - offset - 1, x + offset + 2, y - offset + 2, color);
      }
    } else DrawCityToolbarIcon(game, canvas, icon, ConstructionItemIcon(&game->ui->desktop, item), disabled, false);
    if (list) {
      int shift = pressed ? 1 : 0;
      CityRect clip = canvas->clip;
      canvas->clip = IntersectCityRects(clip,
          (CityRect){button.left + 32, button.top + 1, button.right - 3, button.bottom - 1});
      char shortcut[48];
      CityHotkeyToString(menu->show_shortcuts ? item->shortcut : 0, shortcut, sizeof(shortcut));
      int shortcut_width = CanvasTextWidth(shortcut);
      if (shortcut_width) CanvasTextRight(canvas, button.right - 4 + shift, button.top + 16 + shift,
          shortcut, 0xFFFFFFFF);
      CanvasTextAt(canvas, button.left + 32 + shift, button.top + 3 + shift, item->label, 0xFFFFFF00);
      canvas->clip.right = IntMin(canvas->clip.right, button.right - shortcut_width - 8);
      int packed = item->action == CITY_MENU_ACTION_TOOL ? Sc2kCanvasMapPointerTools(item->parameter, 0) : -1;
      int group = packed & 255, variant = (packed >> 8) & 255;
      int cost = packed >= 0 && group < CITY_TOOL_GROUP_QUERY ?
          kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + variant] : 0;
      char currency[CITY_CURRENCY_TEXT_CAPACITY], price[32];
      if (item->action == CITY_MENU_ACTION_TOOL) FormatCityCurrency(game, currency, cost);
      else CopyText(currency, sizeof(currency), "Open menu");
      bool per_tile = group == CITY_TOOL_GROUP_PORT || group == CITY_TOOL_GROUP_RESIDENTIAL ||
          group == CITY_TOOL_GROUP_COMMERCIAL || group == CITY_TOOL_GROUP_INDUSTRIAL;
      snprintf(price, sizeof(price), "%s%s", currency, per_tile ? " / tile" : "");
      CanvasTextAt(canvas, button.left + 32 + shift, button.top + 16 + shift, price, 0xFFFFFFFF);
      canvas->clip = clip;
    }
  }
  DrawConstructionScrollbar(canvas, window);
}

static void DrawStatusField(Canvas *canvas, CityRect b, const char *text, u32 color, bool pressed, bool interactive) {
  if (interactive) pressed |= DrawCanvasButtonFrame(canvas, b, canvas_face, canvas_light, canvas_dark, !pressed);
  else {
    pressed = false;
    DrawCanvasFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  }
  Canvas label = *canvas;
  label.clip = IntersectCityRects(canvas->clip, (CityRect){b.left + 2, b.top + 1, b.right - 2, b.bottom - 1});
  // Center horizontally at half-pixel precision, but keep one font origin for every status field.
  // Measuring each string's ink height would shift dates, money and messages with descenders differently.
  label.raster = CanvasRasterView(canvas);
  label.raster.origin_x *= 2;
  label.raster.origin_y *= 2;
  label.raster.denominator *= 2;
  label.width *= 2;
  label.height *= 2;
  label.clip = (CityRect){label.clip.left * 2, label.clip.top * 2, label.clip.right * 2, label.clip.bottom * 2};
  int x = b.left + b.right - CityInterfaceTextWidth(canvas->interface_font, text) + pressed * 2;
  int y = b.top * 2 + 3 + pressed * 2;
  DrawCityInterfaceTextSized(canvas->interface_font, &label, x, y, text, color, true, 20);
}

void DrawCityMenu(Game *game, Canvas *canvas, const CityCanvasDesktop *desktop) {
  if (!game->city_active) return;
  Canvas ui_canvas = *canvas;
  const CityButtonFeedback *feedback = &game->ui->button_feedback;
  ui_canvas.pressed_button = !canvas->button_probe && feedback->active && !feedback->window_id ?
      &feedback->bounds : NULL;
  ui_canvas.interface_font = &game->resources->interface_font;
  canvas = &ui_canvas;
  const CityMenuState *menu = &desktop->menu;
  bool editing = TerrainToolbar(desktop);
  CityRect bar = CityMenuBounds(desktop, 0);
  CanvasBeveledBox(canvas, bar.left, bar.top, bar.right, bar.bottom, canvas_face, 1);
  CityToolbarLayout layout = ToolbarLayout(desktop);
  for (int i = 0; i < CITY_TOPBAR_COUNT; ++i) {
    CityRect b = layout.buttons[i];
    if (b.right <= b.left) continue;
    if (layout.pages == 1 && kCityToolbarDefinitions[i].separator && b.left > bar.left)
      DrawCanvasStipple(canvas, (CityRect){b.left - 3, b.top, b.left, b.bottom}, 0xFF3C0001, 0xFF800000);
    bool pressed = (!menu->pressed_window && menu->pressed == i + 1) || menu->selected == i + 1 ||
        (i == CITY_TOPBAR_PAUSE && menu->last_paused && !editing) ||
        (i >= CITY_TOPBAR_TERRAIN_RAISE && IsCityMenuItemChecked(game, &kCityToolbarDefinitions[i].direct)) ||
        (editing && i == CITY_TOPBAR_TREES && desktop->selected_tool == CITY_CANVAS_TOOL_TREES) ||
        (i == CITY_TOPBAR_UNDERWORLD && menu->last_underworld);
    if (ButtonEnabled(desktop, i + 1))
      DrawCanvasButtonBox(canvas, b.left, b.top, b.right, b.bottom, canvas_face, !pressed);
    else CanvasBeveledBox(canvas, b.left, b.top, b.right, b.bottom, canvas_face, !pressed);
    if (editing && i == CITY_TOPBAR_PAUSE) {
      CityRect text = {b.left + pressed, b.top + pressed, b.right + pressed, b.bottom + pressed};
      DrawCityInterfaceTextCentered(canvas->interface_font, canvas, text, "Start City", canvas_text, false, 10);
      continue;
    }
    if (i == CITY_TOPBAR_TERRAIN_STREAM) {
      DrawTerrainStreamIcon(canvas, b, pressed);
      continue;
    }
    bool counterclockwise = i == CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE;
    int icon = ToolbarButtonIcon(i);
    DrawCityToolbarIcon(game, canvas, b, icon,
        !ButtonEnabled(desktop, i + 1), counterclockwise);
  }
  if (layout.pages > 1) {
    bool pressed = menu->pressed == kToolbarSwitch;
    CityRect b = layout.switch_button;
    DrawCanvasButtonBox(canvas, b.left, b.top, b.right, b.bottom, canvas_face, !pressed);
    DrawCityToolbarIcon(game, canvas, b,
        CITY_TOPBAR_ICON_COUNT + CITY_TOOLBAR_TOOL_ICON_COUNT + 1 + CITY_TOOLBAR_DISABLED_ICON_COUNT, false, false);
  }
  CityRect status = CityMenuBounds(desktop, 2);
  int left = IntMin(status.left + 85, status.right);
  int money = IntMax(left, status.right - 100);
  DrawStatusField(canvas, (CityRect){status.left, status.top, left, status.bottom}, menu->date, 0xFFFCFCFC,
      menu->pressed == kStatusDate, true);
  DrawStatusField(canvas, (CityRect){left, status.top, money, status.bottom}, menu->notice,
      menu->last_paused ? 0xFFFFFF90 : 0xFFFCFCFC, false, false);
  DrawStatusField(canvas, (CityRect){money, status.top, status.right, status.bottom}, menu->money,
      game->city->indicators.cash < 0 ? 0xFFFF3030 : 0xFFFFFFFF, menu->pressed == kStatusMoney, !editing);
  CityRect demand = CityMenuBounds(desktop, 3);
  if (!editing) {
    DrawCanvasButtonBox(canvas, demand.left, demand.top, demand.right, demand.bottom,
        canvas_face, menu->pressed != kToolbarDemand);
    const u32 colors[] = {0xFF78B840, 0xFF74B4EC, 0xFFE8C448};
    int demand_width = (kDemandBarCount - 1) * kDemandBarPitch + kDemandBarWidth;
    int first_bar = demand.left + (demand.right - demand.left - demand_width) / 2;
    for (int sector = 0; sector < kDemandBarCount; ++sector) {
      int x = first_bar + sector * kDemandBarPitch, mid = (demand.top + demand.bottom) / 2;
      int height = IntClamp(menu->demand[sector] * 6 / 2000, -6, 6);
      DrawCanvasFrame(canvas, (CityRect){x - 1, mid - 8, x + kDemandBarWidth + 1, mid + 9},
          0xFF444842, canvas_light, canvas_dark, false);
      CanvasFill(canvas, x, mid - IntMax(0, height), x + kDemandBarWidth, mid + IntMax(1, -height), colors[sector]);
      CanvasFill(canvas, x, mid, x + kDemandBarWidth, mid + 1, canvas_text);
    }
  }
  int count;
  const CityMenuItem *items = DropdownItems(desktop, &count);
  if (!count) return;
  CityRect b = CityMenuBounds(desktop, 1);
  DrawCanvasFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  int rows = DropdownRows(desktop, count), columns = (count + rows - 1) / rows;
  CityDropdownLayout dropdown = DropdownLayout(desktop, items, count);
  for (int row = 0; row < count; ++row) {
    const CityMenuItem *item = &items[row];
    int left = b.left + 2 + row / rows * (b.right - b.left - 4) / columns;
    int right = b.left + 2 + (row / rows + 1) * (b.right - b.left - 4) / columns;
    int top = b.top + 2 + row % rows * CITY_MENU_ROW_HEIGHT;
    if (item->action == CITY_MENU_ACTION_SEPARATOR) {
      CanvasFill(canvas, left, top + 4, right, top + 5, canvas_dark);
      CanvasFill(canvas, left, top + 5, right, top + 6, canvas_light);
      continue;
    }
    bool selected = menu->hovered == row + 1 && MenuItemEnabled(desktop, item);
    if (selected) CanvasFill(canvas, left, top, right, top + CITY_MENU_ROW_HEIGHT, 0xFF101010);
    CityRect clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip, (CityRect){left, top, right, top + CITY_MENU_ROW_HEIGHT});
    u32 color = !MenuItemEnabled(desktop, item) ? canvas_dark : selected ? 0xFFFFFFFF : canvas_text;
    if (IsCityMenuItemChecked(game, item)) {
      DrawCanvasLine(canvas, left + 3, top + 4, left + 6, top + 7, color, 1);
      DrawCanvasLine(canvas, left + 6, top + 7, left + 11, top + 1, color, 1);
      DrawCanvasLine(canvas, left + 3, top + 5, left + 6, top + 8, color, 1);
      DrawCanvasLine(canvas, left + 6, top + 8, left + 11, top + 2, color, 1);
    }
    int arrow_width = MenuItemHasSubmenu(item) ? 12 : 0;
    if (arrow_width)
      DrawCityInterfaceTextInRow(canvas->interface_font, canvas,
          (CityRect){right - 5 - CityInterfaceTextWidth(canvas->interface_font, ">"), top,
              right - 5, top + CITY_MENU_ROW_HEIGHT}, ">", color);
    char shortcut[48];
    MenuShortcutToString(item, shortcut, sizeof(shortcut));
    int shortcut_left = left + dropdown.text_inset + dropdown.label_width + 8;
    canvas->clip.right = IntMin(canvas->clip.right, right - dropdown.arrow_width - 3);
    if (dropdown.shortcut_width)
      DrawCityInterfaceTextInRow(canvas->interface_font, canvas,
          (CityRect){shortcut_left, top, right, top + CITY_MENU_ROW_HEIGHT}, shortcut, color);
    if (dropdown.shortcut_width) canvas->clip.right = IntMin(canvas->clip.right, shortcut_left - 4);
    DrawCityInterfaceTextInRow(canvas->interface_font, canvas,
        (CityRect){left + dropdown.text_inset, top, right, top + CITY_MENU_ROW_HEIGHT}, item->label, color);
    canvas->clip = clip;
  }
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool ConstructionWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawCityConstructionWindow(game, canvas, window);
    return true;
  }
  if (event->kind == WINDOW_DESTROY) {
    CityCanvasDesktop *desktop = &game->ui->desktop;
    CityTreePickerWindow *picker = desktop->interface ? &desktop->interface->tree_picker : NULL;
    if (picker && picker->window_id == window->window_id) {
      if (picker->drawing) ReleaseCityWindowDrawing(&picker->drawing->base);
      *picker = (CityTreePickerWindow){0};
    }
    if (desktop->menu.pressed_window == window->window_id) {
      desktop->menu.pressed_window = 0;
      desktop->menu.pressed = 0;
      desktop->menu.held = desktop->menu.released = desktop->menu.submenu_pending = false;
    }
    if (desktop->menu.submenu_window == window->window_id) CloseCityMenu(desktop);
    if ((desktop->menu.category == window->toolbar_category ||
        (window->toolbar_category == CITY_TOPBAR_TREES + 1 && desktop->selected_tool == CITY_CANVAS_TOOL_TREES) ||
        (window->toolbar_group && desktop->menu.category &&
        PrimaryToolbarCategory(desktop->menu.category - 1) == window->toolbar_category - 1)) &&
        !ConstructionUsesChooser(window->toolbar_category, window->toolbar_group)) {
      desktop->menu.category = 0;
      desktop->selected_tool = CITY_CANVAS_TOOL_NONE;
    }
    DirtyMenu(desktop);
    DirtyStatus(desktop);
    return true;
  }
  if (event->kind == WINDOW_DROPDOWN) {
    const CityDropdownSelection *selection = event->selection;
    if (selection->control != kTreeClimateDropdown ||
        !ConstructionUsesTreePicker(window->toolbar_category, window->toolbar_group)) return false;
    CityCanvasDesktop *desktop = &game->ui->desktop;
    Window *target = FindCityCanvasWindow(desktop, window->window_id);
    desktop->menu.tree_climate = selection->value;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
    target->toolbar_selection = selection->value;
    if (game->ui->tree_picker.drawing) InvalidateCityWindowBitmaps(&game->ui->tree_picker.drawing->base);
    int count = TreePickerCount(selection->value);
    int height = 44 + ((count + 3) >> 2) * 58;
    height = IntMin(height, desktop->canvas_height - CITY_MENU_BAR_HEIGHT - kStatusHeight - 4);
    if (target->shaded) target->restore_height = height;
    else target->bounds.bottom = target->bounds.top + height;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
    ExecuteCityMenuItem(game, ActivateMenuItem(desktop, &kTreeItems[12]));
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    Window *target = FindCityCanvasWindow(desktop, window->window_id);
    int x = event->x, y = event->y;
    CityRect b = target->bounds, rows = ConstructionRowsBounds(target);
    if (y < b.top + 14 && x < b.left + 11) {
      Sc2kCanvasDesktopDismissWindow(desktop, target->window_id);
    } else if (y < b.top + 14 && x >= b.right - 11) {
      target->sticky = !target->sticky;
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
    } else if (ConstructionWindowCanRotate(target) && y < b.top + 14 && x >= b.right - 31) {
      RotateCityPlacement(desktop, input->shift != 0);
    } else if (ConstructionUsesTreePicker(target->toolbar_category, target->toolbar_group) &&
        y < b.top + 14 && x >= b.right - 22) {
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
      if (target->shaded) target->bounds.bottom = b.top + target->restore_height;
      else {
        target->restore_height = b.bottom - b.top;
        target->bounds.bottom = b.top + 14;
      }
      target->shaded = !target->shaded;
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
    } else if (ConstructionUsesTreePicker(target->toolbar_category, target->toolbar_group) && !target->shaded &&
        CanvasPointInRect(TreeClimateBounds(target), x, y)) {
      CloseCityMenu(desktop);
      CityRect anchor = TreeClimateBounds(target);
      anchor.left -= b.left; anchor.right -= b.left;
      anchor.top -= b.top; anchor.bottom -= b.top;
      int count = sizeof(kTreeClimateItems) / sizeof(*kTreeClimateItems), selected = 0;
      for (int i = 0; i < count; ++i)
        if (kTreeClimateItems[i].value == target->toolbar_selection) selected = i;
      OpenCityDropdown(desktop, target->window_id, kTreeClimateDropdown, anchor,
          kTreeClimateItems, count, selected, &kTreeDropdownColors);
    } else if (ConstructionHasScrollbar(target) && CanvasPointInRect(rows, x, y) &&
        x >= rows.right - kConstructionScrollbarWidth) {
      if (y < rows.top + 12) ScrollConstructionWindow(desktop, target, -1);
      else if (y >= rows.bottom - 12) ScrollConstructionWindow(desktop, target, 1);
      else {
        CityScrollbar scrollbar = ConstructionScrollbar(target);
        int first = CityScrollbarPosition(&scrollbar, y, 0, CITY_SCROLLBAR_JUMP);
        ScrollConstructionWindow(desktop, target, first - target->toolbar_scroll);
      }
    } else {
      int item = ConstructionItemAt(target, x, y);
      if (ConstructionUsesChooser(target->toolbar_category, target->toolbar_group) &&
          CanvasPointInRect(ConstructionConfirmBounds(target), x, y))
        item = ConstructionWindowDefinition(target)->count + 1;
      if (!item) return false;
      const Window *previous = ConstructionWindow(desktop, desktop->menu.pressed_window);
      if (previous) Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(previous));
      desktop->menu.pressed_window = target->window_id;
      desktop->menu.pressed = CITY_TOPBAR_COUNT + item;
      desktop->menu.held = true;
      desktop->menu.released = false;
      desktop->menu.submenu_pending = item <= ConstructionWindowDefinition(target)->count &&
          ConstructionWindowDefinition(target)->items[item - 1].action == CITY_MENU_ACTION_SUBMENU;
      desktop->menu.submenu_clock_started = false;
      DirtyMenu(desktop);
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
    }
    PlayCityButtonBeep(game);
    return true;
  } else if (input->kind == CANVAS_SDL_POINTER_UP && input->button == CANVAS_SDL_BUTTON_LEFT) {
    bool held = desktop->menu.held;
    desktop->menu.held = desktop->menu.submenu_pending = false;
    const CityMenuItem *selected = NULL;
    if (held && desktop->menu.pressed_window == window->window_id && !desktop->menu.selected) {
      Window *target = FindCityCanvasWindow(desktop, window->window_id);
      const CityToolbarDefinition *definition = ConstructionWindowDefinition(target);
      bool chooser = ConstructionUsesChooser(target->toolbar_category, target->toolbar_group);
      int item = ConstructionItemAt(target, event->x, event->y);
      if (chooser && CanvasPointInRect(ConstructionConfirmBounds(target), event->x, event->y))
        item = definition->count + 1;
      if (item && desktop->menu.pressed == CITY_TOPBAR_COUNT + item) {
        if (chooser && item <= definition->count) {
          target->toolbar_selection = item - 1;
          Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
        } else {
          selected = &definition->items[chooser ? target->toolbar_selection : item - 1];
          if (ConstructionUsesTreePicker(target->toolbar_category, target->toolbar_group) && item != 13)
            selected = &kTreeItems[TreePickerSpecies(target->toolbar_selection, item - 1)];
          if (chooser && !target->sticky) Sc2kCanvasDesktopDismissWindow(desktop, target->window_id);
          selected = ActivateMenuItem(desktop, selected);
        }
      }
    }
    ExecuteCityMenuItem(game, selected);
    ReleaseCityToolbarFeedback(desktop, (u32)(u64)(MonotonicSeconds() * 1000));
    game->ui->placement_hover_valid = false;
    UpdateCityPlacementPreview(game->city);
    return true;
  } else if (input->kind == CANVAS_SDL_FOCUS_LOST) {
    ReleaseCityToolbarFeedback(desktop, (u32)(u64)(MonotonicSeconds() * 1000));
    desktop->menu.held = desktop->menu.submenu_pending = false;
  } else if (input->kind == CANVAS_SDL_KEY_DOWN &&
      (input->key == 'R' || input->key == (CITY_HOTKEY_SHIFT | 'R')) && ConstructionWindowCanRotate(window)) {
    RotateCityPlacement(desktop, input->key == (CITY_HOTKEY_SHIFT | 'R'));
    return true;
  } else if (input->kind == CANVAS_SDL_SCROLL &&
      ConstructionUsesList(window->toolbar_category, window->toolbar_group)) {
    ScrollConstructionWindow(desktop, FindCityCanvasWindow(desktop, window->window_id),
        input->y > 0 ? -1 : input->y < 0 ? 1 : 0);
    return true;
  }
  return false;
}

bool HandleCityMainMenuPointer(Game *game, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = game->ui;
  CityCanvasDesktop *desktop = &ui->desktop;
  if (ui->pointer_stroke_count ||
      desktop->dragging != CITY_CANVAS_DRAG_NONE ||
      (desktop->input_capture_window && !desktop->menu.selected) ||
      NativeStartupMenuWaiting(game->city) || ui->startup_waiting) return false;
  UpdateCityToolbar(game);
  CityPoint point = CityInterfacePointerPixels(game, event);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  // Do not use window-relative hit testing: menus are anchored to the screen.
  point.x = (int)((i64)point.x * 100 / scale);
  point.y = (int)((i64)point.y * 100 / scale);
  bool handled = false;
  if (event->kind == CANVAS_SDL_POINTER_DOWN) {
    if (!desktop->menu.selected && !CityMenuContains(desktop, point.x, point.y)) return false;
    RecordGameInput(game);
    handled = PressCityMenu(desktop, point.x, point.y);
    if (handled && desktop->menu.held) PlayCityButtonBeep(game);
  } else if (event->kind == CANVAS_SDL_POINTER_MOVE)
    handled = MoveCityMenu(desktop, point.x, point.y);
  else if (event->kind == CANVAS_SDL_POINTER_UP) {
    handled = desktop->menu.held || desktop->menu.selected;
    ExecuteCityMenuItem(game, ReleaseCityMenu(desktop, point.x, point.y));
    ReleaseCityToolbarFeedback(desktop, (u32)(u64)(MonotonicSeconds() * 1000));
  }
  if (handled) {
    ui->placement_hover_valid = false;
    UpdateCityPlacementPreview(game->city);
  }
  return handled;
}
