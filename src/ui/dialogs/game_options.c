#include "assets/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/state.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"
#include "base/text.h"

// Drawing subsystem: game options.

static const u32 kOptionsFace = 0xFF646488, kOptionsLight = 0xFFACACC0, kOptionsDark = 0xFF404064;
static const u32 kOptionsGold = 0xFFE8B810, kOptionsBlue = 0xFF80C4FC, kOptionsLabel = 0xFFFCB030;

// Descriptors share drawing, hit testing, search, tooltips and reset behavior.
const char *const kCitySettingsGroups[CITY_SETTINGS_GROUP_COUNT] = {
  "Interface", "Display", "Sound", "Economy", "Infrastructure", "Military", "Labels", "SimCity 2000 fixes"
};

CityRect CitySettingsControlBounds(CityRect b, int control) {
  if (control == GAME_OPTIONS_SCOPE) return (CityRect){b.right - 29, b.top + 1, b.right - 13, b.top + 13};
  if (control == GAME_OPTIONS_CATEGORY) return (CityRect){b.left + 70, b.top + 89, b.right - 12, b.top + 102};
  if (control == GAME_OPTIONS_TYPE) return (CityRect){b.left + 70, b.top + 105, b.right - 12, b.top + 118};
  if (control == GAME_OPTIONS_FILTER) return (CityRect){b.left + 50, b.top + 121, b.right - 26, b.top + 134};
  if (control == GAME_OPTIONS_CLEAR_FILTER) return (CityRect){b.right - 26, b.top + 121, b.right - 12, b.top + 134};
  if (control >= GAME_OPTIONS_EXPAND_ALL && control <= GAME_OPTIONS_RESET_ALL) {
    int width = b.right - b.left - 24, column = control - GAME_OPTIONS_EXPAND_ALL;
    return (CityRect){b.left + 12 + column * width / 3, b.bottom - 28,
        b.left + 12 + (column + 1) * width / 3, b.bottom - 14};
  }
  return (CityRect){b.left + 12, b.top + 139, b.right - 12, b.bottom - 34};
}

// A bounded list of descriptor IDs, rebuilt without allocating or retaining City values.
static int SettingsRows(const Game *game, int *rows) {
  const CitySettingsBrowser *browser = &game->ui->settings;
  int count = 0;
  for (int group = 0; group < CITY_SETTINGS_GROUP_COUNT; ++group) {
    if (browser->category && browser->category != group + 1) continue;
    int header = count;
    rows[count++] = GAME_OPTIONS_GROUP + group;
    bool matches = false;
    for (int setting = 0; setting < kCitySettingsCount; ++setting) {
      const CitySetting *entry = &kCitySettings[setting];
      bool simulation = entry->command != 0;
      if (entry->hidden || (int)entry->group != group ||
          (browser->type == 1 && simulation) || (browser->type == 2 && !simulation) ||
          (!TextContains(entry->label, browser->filter, false) &&
              !TextContains(entry->tooltip, browser->filter, false) &&
              !TextContains(kCitySettingsGroups[group], browser->filter, false))) continue;
      matches = true;
      if (!(browser->collapsed_groups & (1 << group)) || browser->filter[0])
        rows[count++] = GAME_OPTIONS_SETTING + setting;
    }
    if (!matches) count = header;
  }
  return count;
}

int CitySettingsRowCount(const Game *game) {
  int rows[kCitySettingsCount + CITY_SETTINGS_GROUP_COUNT];
  return SettingsRows(game, rows);
}

static int SettingsScroll(const Game *game, CityRect list, int count) {
  int visible = IntMax(1, (list.bottom - list.top - 4) / 16);
  return IntClamp(game->ui->settings.scroll, 0, count - visible);
}

CityRect CitySettingsScrollbarThumb(const Game *game, CityRect window) {
  CityRect list = CitySettingsControlBounds(window, GAME_OPTIONS_NONE);
  int count = CitySettingsRowCount(game), scroll = SettingsScroll(game, list, count);
  int visible = IntMax(1, (list.bottom - list.top - 4) / 16);
  CityRect track = {list.right - 12, list.top + 12, list.right - 1, list.bottom - 12};
  return ReadCityScrollbar(track, count, visible, scroll, 8).thumb;
}

static int SettingsActionAt(const Game *game, CityRect window, int x, int y) {
  for (int control = GAME_OPTIONS_CATEGORY; control <= GAME_OPTIONS_RESET_ALL; ++control) {
    if (CanvasPointInRect(CitySettingsControlBounds(window, control), x, y)) return control;
  }
  CityRect list = CitySettingsControlBounds(window, GAME_OPTIONS_NONE);
  if (!CanvasPointInRect(list, x, y)) return GAME_OPTIONS_NONE;
  int rows[kCitySettingsCount + CITY_SETTINGS_GROUP_COUNT];
  int count = SettingsRows(game, rows), scroll = SettingsScroll(game, list, count);
  if (x >= list.right - 12) {
    if (y < list.top + 12) return GAME_OPTIONS_SCROLL_UP;
    if (y >= list.bottom - 12) return GAME_OPTIONS_SCROLL_DOWN;
    CityRect thumb = CitySettingsScrollbarThumb(game, window);
    return y < thumb.top ? GAME_OPTIONS_PAGE_UP : y >= thumb.bottom ?
        GAME_OPTIONS_PAGE_DOWN : GAME_OPTIONS_SCROLL_THUMB;
  }
  int row = scroll + (y - list.top - 2) / 16;
  return y >= list.top + 2 && row < count ? rows[row] : GAME_OPTIONS_NONE;
}

enum { OPTIONS_RESOLUTION = 41, OPTIONS_REFRESH, OPTIONS_DRIVER };

#define OW_LEAF(k, ident, width, height, label)                                                    \
  {.kind = k, .id = ident, .min_width = width, .min_height = height, .text = label}
#define OW_HELP(k, ident, width, height, label, help) \
  {.kind = k, .id = ident, .min_width = width, .min_height = height, .text = label, .tooltip = help}
#define OW_SPACE(height) OW_LEAF(CITY_WIDGET_SPACER, 0, 0, height, NULL)
#define OW_CHILDREN(k, width, height, pad, spacing, ...)                                           \
  {.kind = k,                                                                                      \
   .min_width = width,                                                                             \
   .min_height = height,                                                                           \
   .padding = pad,                                                                                 \
   .gap = spacing,                                                                                 \
   .children = (const CityWidget[]){__VA_ARGS__},                                                  \
   .count = sizeof((const CityWidget[]){__VA_ARGS__}) / sizeof(CityWidget)}
#define OW_GROUP(height, title, ...)                                                               \
  {.kind = CITY_WIDGET_GROUP,                                                                      \
   .min_height = height,                                                                           \
   .padding = 6,                                                                                   \
   .text = title,                                                                                  \
   .children = (const CityWidget[]){__VA_ARGS__},                                                  \
   .count = sizeof((const CityWidget[]){__VA_ARGS__}) / sizeof(CityWidget)}
#define OW_VOLUME(name, ident, height)                                                             \
  OW_CHILDREN(CITY_WIDGET_ROW, 0, height, 0, 0, OW_LEAF(CITY_WIDGET_LABEL, 0, 82, height, name),   \
              {.kind = CITY_WIDGET_SLIDER,                                                         \
               .id = ident,                                                                        \
               .min_width = 180,                                                                   \
               .min_height = height,                                                               \
               .fill = 1, \
               .tooltip = ident == GAME_OPTIONS_SCALE ? \
                   "Change interface size. Hold Ctrl while dragging for continuous adjustment." : \
                   ident == GAME_OPTIONS_MUSIC ? "Adjust music volume." : ident == GAME_OPTIONS_MASTER ? \
                       "Adjust all music and effects together." : "Adjust sound effect volume."})
static const CityWidget options_tabs = OW_CHILDREN
(CITY_WIDGET_ROW, 0, 20, 0, 0,
                                                   {.kind = CITY_WIDGET_BUTTON,
                                                    .id = GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_GENERAL,
                                                    .min_width = 60,
                                                    .min_height = 20,
                                                    .fill = 1,
                                                    .text = "General", .tooltip = "General interface and window settings."},
                                                   {.kind = CITY_WIDGET_BUTTON,
                                                    .id = GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_GRAPHICS,
                                                    .min_width = 60,
                                                    .min_height = 20,
                                                    .fill = 1,
                                                    .text = "Graphics", .tooltip = "Graphics, display and font settings."},
                                                   {.kind = CITY_WIDGET_BUTTON,
                                                    .id = GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_SOUND,
                                                    .min_width = 60,
                                                    .min_height = 20,
                                                    .fill = 1,
                                                    .text = "Sound", .tooltip = "Music and sound settings."},
                                                   {.kind = CITY_WIDGET_BUTTON,
                                                    .id = GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_ADVANCED,
                                                    .min_width = 60, .min_height = 20, .fill = 1,
                                                    .text = "Advanced",
                                                    .tooltip = "Browse and search individual settings and optional fixes."});
static const CityWidget options_graphics = OW_CHILDREN
(
    CITY_WIDGET_COLUMN, 0, 233, 0, 18,
    OW_GROUP(121, "Interface", OW_SPACE(6), OW_VOLUME("Interface size", GAME_OPTIONS_SCALE, 28),
             OW_SPACE(7), OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_AUTO, 0, 14, "Auto-detect size",
                 "Automatically choose an interface size for your display."),
             OW_SPACE(3), OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_BEVELS, 0, 14, "Scale bevels",
                 "Scale control borders along with the interface."),
             OW_SPACE(3), OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_SPRITE_FONT, 0, 14, "Use traditional sprite font",
                 "Use the traditional OpenTTD-style sprite font. Turn off for scalable Noto Sans."),
             OW_SPACE(3), OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_FONT_ANTIALIAS, 0, 14, "Anti-alias fonts",
                 "Smooth scalable font edges. Unavailable with the traditional sprite font.")),
    OW_GROUP(94, "Display", OW_SPACE(7),
             OW_LEAF(CITY_WIDGET_LABEL, OPTIONS_RESOLUTION, 0, 12, NULL), OW_SPACE(4),
             OW_LEAF(CITY_WIDGET_LABEL, OPTIONS_REFRESH, 0, 12, NULL), OW_SPACE(6),
             OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_FULLSCREEN, 0, 14, "Fullscreen",
                 "Switch between fullscreen and a desktop window."), OW_SPACE(7),
             OW_LEAF(CITY_WIDGET_LABEL, OPTIONS_DRIVER, 0, 10, NULL)));
static const CityWidget options_general = OW_CHILDREN
(
    CITY_WIDGET_COLUMN, 0, 211, 0, 21,
    OW_GROUP(84, "Windows", OW_SPACE(11),
             OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_SNAP, 0, 14, "Snap windows",
                 "Snap windows to nearby window and screen edges while dragging."), OW_SPACE(11),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "Drag title bars to move windows."), OW_SPACE(8),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "Nearby window and screen edges snap together.")),
    OW_GROUP(106, "Controls", OW_SPACE(9),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "G: Game Options    J: Jazz Jukebox"),
             OW_SPACE(9), OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "Space: start / pause    B: budget"),
             OW_SPACE(9), OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "M: map data    O: ordinances"),
             OW_SPACE(9),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "Ctrl+0: auto-detect interface size")));
static const CityWidget options_sound = OW_CHILDREN
(
    CITY_WIDGET_COLUMN, 0, 193, 0, 12,
    OW_GROUP(116, "Volume", OW_SPACE(4), OW_VOLUME("Master", GAME_OPTIONS_MASTER, 24),
             OW_SPACE(4), OW_VOLUME("Sound effects", GAME_OPTIONS_EFFECTS, 24),
             OW_SPACE(4), OW_VOLUME("Music", GAME_OPTIONS_MUSIC, 24), OW_SPACE(4),
             OW_HELP(CITY_WIDGET_TOGGLE, GAME_OPTIONS_AMBIENT_SOUNDS, 0, 14, "Ambient sounds",
                 "Enable routine vehicle, bird and other background city sounds.")),
    OW_GROUP(93, "Music set", OW_SPACE(10),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "OpenMSX - 31 General MIDI tracks"), OW_SPACE(8),
             OW_LEAF(CITY_WIDGET_LABEL, 0, 0, 10, "OpenMSX authors - GPL version 2"), OW_SPACE(16),
             OW_CHILDREN(CITY_WIDGET_ROW, 0, 17, 0, 0,
                         OW_HELP(CITY_WIDGET_BUTTON, GAME_OPTIONS_JUKEBOX, 132, 17, "Jazz Jukebox",
                 "Open the music player and choose tracks (J)."),
                         {.kind = CITY_WIDGET_SPACER, .fill = 1})));
#undef OW_HELP
#undef OW_LEAF
#undef OW_SPACE
#undef OW_CHILDREN
#undef OW_GROUP
#undef OW_VOLUME
static i32 OptionsLayout(int tab, CityWidgetLayout *out,
    int left, int top, int right, int bottom) {
  CityRect b = {left, top, right, bottom};
  i32 first = Sc2kCanvasWidgetLayout(
      &options_tabs, (CityRect){b.left + 12, b.top + 23, b.right - 12, b.top + 43}, out, 96);
  if (first < 0)
    return 0;
  if (tab == GAME_OPTIONS_TAB_ADVANCED) return first;
  const CityWidget *body = tab == GAME_OPTIONS_TAB_GRAPHICS ? &options_graphics :
      tab == GAME_OPTIONS_TAB_GENERAL ? &options_general : &options_sound;
  i32 rest = Sc2kCanvasWidgetLayout(
      body, (CityRect){b.left + 12, b.top + 63, b.right - 12, b.bottom - 26}, out + first,
      96 - first);
  return rest < 0 ? first : first + rest;
}

CityRect CitySettingControlBounds(const Game *game, CityRect window, int index) {
  CityRect list = CitySettingsControlBounds(window, GAME_OPTIONS_NONE);
  int rows[kCitySettingsCount + CITY_SETTINGS_GROUP_COUNT];
  int count = SettingsRows(game, rows), scroll = SettingsScroll(game, list, count);
  for (int row = scroll; row < count; ++row)
    if (rows[row] == GAME_OPTIONS_SETTING + index) {
      int y = list.top + 2 + (row - scroll) * 16;
      return (CityRect){list.left + 26, y + 1, list.right - 13, y + 13};
    }
  return window;
}

static void DrawOptionsBox(Canvas *canvas, CityRect b, u32 fill, bool raised) {
  bool gold = fill == kOptionsGold || fill == 0xFFFCD400;
  u32 light = gold ? 0xFFFCF880 : kOptionsLight;
  u32 dark = gold ? 0xFFB87818 : kOptionsDark;
  if (gold) DrawCanvasButtonFrame(canvas, b, fill, light, dark, raised);
  else DrawCanvasFrame(canvas, b, fill, light, dark, raised);
}

static void DrawOptionsText(const CityInterfaceFont *font, Canvas *canvas, int x, int y,
    const char *text, u32 color) {
  DrawCityInterfaceText(font, canvas, x, y, text, color, color != 0xFF101010);
}

static void DrawOptionsGroup(const CityInterfaceFont *font, Canvas *canvas, CityRect b, const char *label) {
  int gap = b.left + 8 + CityInterfaceTextWidth(font, label);
  if (canvas->unscaled_bevels) {
    DrawCanvasFrame(canvas, b, kOptionsFace, kOptionsLight, kOptionsDark, false);
    CanvasFill(canvas, b.left + 4, b.top, gap, b.top + 2, kOptionsFace);
    DrawOptionsText(font, canvas, b.left + 6, b.top - 5, label, kOptionsLabel);
    return;
  }
  CanvasFill(canvas, b.left, b.top, b.left + 4, b.top + 1, kOptionsDark);
  CanvasFill(canvas, b.left + 1, b.top + 1, b.left + 4, b.top + 2, kOptionsLight);
  CanvasFill(canvas, gap, b.top, b.right - 1, b.top + 1, kOptionsDark);
  CanvasFill(canvas, gap, b.top + 1, b.right - 2, b.top + 2, kOptionsLight);
  CanvasFill(canvas, b.left, b.top + 1, b.left + 1, b.bottom - 1, kOptionsDark);
  CanvasFill(canvas, b.left + 1, b.top + 2, b.left + 2, b.bottom - 2, kOptionsLight);
  CanvasFill(canvas, b.right - 2, b.top + 1, b.right - 1, b.bottom - 2, kOptionsDark);
  CanvasFill(canvas, b.right - 1, b.top, b.right, b.bottom - 1, kOptionsLight);
  CanvasFill(canvas, b.left + 1, b.bottom - 2, b.right - 1, b.bottom - 1, kOptionsDark);
  CanvasFill(canvas, b.left, b.bottom - 1, b.right, b.bottom, kOptionsLight);
  DrawOptionsText(font, canvas, b.left + 6, b.top - 5, label, kOptionsLabel);
}

static void DrawSettingsButton(const CityInterfaceFont *font, Canvas *canvas, CityRect b,
    const char *text) {
  DrawOptionsBox(canvas, b, kOptionsGold, true);
  DrawCityInterfaceTextCentered(font, canvas, b, text, 0xFF101010, false, 10);
}

static void DrawSettingsArrow(Canvas *canvas, CityRect b, bool up) {
  DrawOptionsBox(canvas, b, kOptionsGold, true);
  int pressed = CityCanvasButtonPressed(canvas, b);
  int x = (b.left + b.right) / 2 + pressed, y = (b.top + b.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = up ? row + 1 : 4 - row;
    CanvasFill(canvas, x - half, y + row, x + half, y + row + 1, 0xFF101010);
  }
}

static void DrawCitySettings(Game *game, Canvas *canvas, CityRect window) {
  CitySettingsBrowser *browser = &game->ui->settings;
  const CityInterfaceFont *font = &game->resources->interface_font;
  Canvas scope = *canvas;
  scope.clip = IntersectCityRects(scope.clip, (CityRect){window.left + 12, window.top + 55,
      window.right - 12, window.top + 83});
  DrawCityInterfaceTextSized(font, &scope, window.left + 12, window.top + 57,
      browser->city_defaults ? "Gameplay changes become new-city defaults." :
          "Gameplay changes are saved with this city.", kOptionsLabel, true, 9);
  DrawCityInterfaceTextSized(font, &scope, window.left + 12, window.top + 69,
      "Interface and audio changes apply to all cities.", kOptionsBlue, true, 9);
  CityRect b = CitySettingsControlBounds(window, GAME_OPTIONS_CATEGORY);
  DrawOptionsText(font, canvas, window.left + 12, b.top + 1, "Category:", kOptionsBlue);
  DrawOptionsBox(canvas, b, kOptionsGold, true);
  Canvas text = *canvas;
  text.clip = IntersectCityRects(text.clip, (CityRect){b.left + 2, b.top, b.right - 14, b.bottom});
  DrawOptionsText(font, &text, b.left + 2, b.top + 1,
      browser->category ? kCitySettingsGroups[browser->category - 1] : "All categories", 0xFF101010);
  DrawSettingsArrow(canvas, (CityRect){b.right - 13, b.top, b.right, b.bottom}, false);
  b = CitySettingsControlBounds(window, GAME_OPTIONS_TYPE);
  DrawOptionsText(font, canvas, window.left + 12, b.top + 1, "Type:", kOptionsBlue);
  DrawOptionsBox(canvas, b, kOptionsGold, true);
  DrawOptionsText(font, canvas, b.left + 2, b.top + 1,
      browser->type == 1 ? "Interface settings" : browser->type == 2 ? "City gameplay settings" : "All setting types",
      0xFF101010);
  DrawSettingsArrow(canvas, (CityRect){b.right - 13, b.top, b.right, b.bottom}, false);
  b = CitySettingsControlBounds(window, GAME_OPTIONS_FILTER);
  DrawOptionsText(font, canvas, window.left + 12, b.top + 1, "Filter:", kOptionsBlue);
  DrawCityTextBox(canvas, b, browser->filter, browser->caret, browser->text_scroll,
      browser->filter_focused, browser->select_all, browser->caret_visible);
  b = CitySettingsControlBounds(window, GAME_OPTIONS_CLEAR_FILTER);
  DrawCanvasButtonFrame(canvas, b, kOptionsFace, kOptionsLight, kOptionsDark, true);
  DrawCityInterfaceTextCentered(font, canvas, b, "X", 0xFF101010, false, 10);
  b = CitySettingsControlBounds(window, GAME_OPTIONS_NONE);
  DrawOptionsBox(canvas, b, kOptionsFace, false);
  Canvas list = *canvas;
  list.clip = IntersectCityRects(list.clip, (CityRect){b.left + 2, b.top + 2, b.right - 13, b.bottom - 2});
  int rows[kCitySettingsCount + CITY_SETTINGS_GROUP_COUNT];
  int count = SettingsRows(game, rows), scroll = SettingsScroll(game, b, count);
  for (int row = scroll, y = b.top + 2; row < count && y < b.bottom - 2; ++row, y += 16) {
    int id = rows[row];
    if (id < GAME_OPTIONS_SETTING) {
      int group = id - GAME_OPTIONS_GROUP;
      bool collapsed = browser->collapsed_groups & (1 << group);
      DrawSettingsButton(font, &list, (CityRect){b.left + 4, y + 1, b.left + 16, y + 13},
          collapsed && !browser->filter[0] ? "+" : "-");
      DrawOptionsText(font, &list, b.left + 21, y + 2, kCitySettingsGroups[group], kOptionsLabel);
    } else {
      int index = id - GAME_OPTIONS_SETTING;
      const CitySetting *entry = &kCitySettings[index];
      int current = ReadCitySettingValue(game, entry, browser->city_defaults);
      bool enabled = current != 0;
      bool disabled = entry->action == GAME_OPTIONS_FONT_ANTIALIAS && game->drawing->options.sprite_font;
      CanvasFill(&list, b.left + 10, y, b.left + 11, y + 16, kOptionsLabel);
      CanvasFill(&list, b.left + 10, y + 8, b.left + 24, y + 9, kOptionsLabel);
      if (entry->choices)
        DrawSettingsButton(font, &list, (CityRect){b.left + 26, y + 1, b.left + 46, y + 13}, "v");
      else DrawCityCheckbox(&list, (CityRect){b.left + 26, y + 1, b.left + 46, y + 13},
          enabled, kOptionsFace, kOptionsLight, kOptionsDark);
      char number[32];
      snprintf(number, sizeof(number), "%lld", (long long)current);
      const char *value = entry->choices ? entry->choices[current].text : entry->maximum == 1 ?
          (enabled ? "On" : "Off") : number;
      int value_width = CityInterfaceTextWidth(font, value);
      // Keep the value visible at narrow widths; labels clip within the row.
      Canvas label = list;
      label.clip.right = IntMin(label.clip.right, b.right - 18 - value_width - 4);
      DrawOptionsText(font, &label, b.left + 52, y + 2, entry->label, disabled ? kOptionsLight : kOptionsBlue);
      DrawOptionsText(font, &list, b.right - 18 - value_width, y + 2, value,
          disabled ? kOptionsLight : kOptionsLabel);
    }
  }
  if (!count) DrawOptionsText(font, &list, b.left + 6, b.top + 7, "No matching settings", kOptionsBlue);
  CityRect track = {b.right - 12, b.top + 12, b.right - 1, b.bottom - 12};
  DrawOptionsBox(canvas, track, kOptionsDark, false);
  DrawOptionsBox(canvas, CitySettingsScrollbarThumb(game, window), kOptionsFace, true);
  DrawSettingsArrow(canvas, (CityRect){b.right - 12, b.top + 1, b.right - 1, b.top + 12}, true);
  DrawSettingsArrow(canvas, (CityRect){b.right - 12, b.bottom - 12, b.right - 1, b.bottom - 1}, false);
  DrawSettingsButton(font, canvas, CitySettingsControlBounds(window, GAME_OPTIONS_EXPAND_ALL), "Expand all");
  DrawSettingsButton(font, canvas, CitySettingsControlBounds(window, GAME_OPTIONS_COLLAPSE_ALL), "Collapse all");
  DrawSettingsButton(font, canvas, CitySettingsControlBounds(window, GAME_OPTIONS_RESET_ALL), "Reset all values");
}

bool PollCitySettingsCaret(Game *game, double now) {
  CitySettingsBrowser *browser = &game->ui->settings;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  Window *window = desktop->count ? &desktop->windows[desktop->count - 1] : NULL;
  bool focused = browser->filter_focused && window && window->kind == CITY_CANVAS_GAME_OPTIONS &&
      browser->tab == GAME_OPTIONS_TAB_ADVANCED;
  bool old = browser->caret_visible;
  if (!focused) { browser->caret_visible = false; browser->caret_blink_at = 0; }
  else if (!browser->caret_blink_at) { browser->caret_visible = true; browser->caret_blink_at = now + 0.5; }
  else if (now >= browser->caret_blink_at) {
    browser->caret_visible = !browser->caret_visible;
    browser->caret_blink_at = now + 0.5;
  }
  if (old == browser->caret_visible || !window) return false;
  Sc2kDirtyMarkRect(&desktop->dirty, CitySettingsControlBounds(window->bounds, GAME_OPTIONS_FILTER));
  return true;
}

// Draw the wedge in physical pixels so its slope stays smooth at every UI scale.
static void DrawOptionsSliderTrack(Canvas *canvas, CityRect bounds, int steps, int label_step) {
  CanvasRaster r = CanvasRasterView(canvas);
  Canvas track = *canvas;
  track.clip = CanvasRasterRect(canvas, canvas->clip);
  track.width = r.width;
  track.height = r.height;
  track.raster = (CanvasRaster){0};
  int left = CanvasRasterEdge(bounds.left, r.origin_x, r.numerator, r.denominator) + r.offset_x;
  int handle_width = CanvasRasterEdge(bounds.left + 3, r.origin_x, r.numerator, r.denominator) +
      r.offset_x - left;
  left += handle_width / 2;
  int right = CanvasRasterEdge(bounds.right - 3, r.origin_x, r.numerator, r.denominator) +
      r.offset_x + handle_width / 2;
  int top = CanvasRasterEdge(bounds.top + 2, r.origin_y, r.numerator, r.denominator) + r.offset_y;
  int bottom = CanvasRasterEdge(bounds.top + 10, r.origin_y, r.numerator, r.denominator) + r.offset_y;
  int thickness = canvas->unscaled_bevels ? 1 : IntMax(1, (r.numerator + r.denominator - 1) / r.denominator);
  for (int x = left; x <= right; ++x) {
    int slope = bottom - (x - left) * (bottom - top) / (right - left);
    CanvasFill(&track, x, slope, x + 1, bottom + 1, 0xFF8484A4);
  }
  DrawCanvasLine(&track, left, bottom, right, bottom, kOptionsLight, thickness);
  DrawCanvasLine(&track, right, top, right, bottom, kOptionsLight, thickness);
  DrawCanvasLine(&track, left, bottom, right, top, kOptionsDark, thickness);
  for (int mark = 0; mark <= steps; ++mark) {
    int x = bounds.left + mark * (bounds.right - bounds.left - 3) / steps;
    x = CanvasRasterEdge(x, r.origin_x, r.numerator, r.denominator) + r.offset_x + handle_width / 2;
    int end = bounds.top + (mark % label_step == 0 ? 14 : 12);
    end = CanvasRasterEdge(end, r.origin_y, r.numerator, r.denominator) + r.offset_y - 1;
    DrawCanvasLine(&track, x, bottom + thickness, x, end, kOptionsDark, thickness);
  }
}

void DrawGameOptions(Game *game, Canvas *c, const Window *window) {
  int left = window->bounds.left, top = window->bounds.top;
  int right = window->bounds.right, bottom = window->bounds.bottom;
  const DrawingOptions *p = &game->drawing->options;
  const CityInterfaceState *ui = game->ui;
  const CityInterfaceFont *font = &game->resources->interface_font;
  CityRect b = {left, top, right, bottom};
  DrawOptionsBox(c, b, kOptionsFace, true);
  DrawCityWindowCaption(c, b, window->title, kOptionsFace, kOptionsLight, kOptionsDark, 11);
  DrawCanvasButtonFrame(c, (CityRect){right - 11, top, right, top + 14},
      kOptionsFace, kOptionsLight, kOptionsDark, true);
  CanvasFill(c, left, top + 14, right, top + 15, kOptionsLight);
  DrawCityWindowControlIcon(c, (CityRect){right - 11, top, right, top + 14}, CITY_WINDOW_ICON_DEFAULT_SIZE, false);
  DrawSettingsArrow(c, CitySettingsControlBounds(b, GAME_OPTIONS_SCOPE), false);
  CanvasFill(c, left, top + 51, right, top + 52, kOptionsDark);
  CanvasFill(c, left, top + 52, right, top + 53, kOptionsLight);
  for (int line = 0; line < 3; ++line)
    for (int pixel = 0; pixel < 3 + line * 2; ++pixel)
      CanvasFill(c, right - 3 - pixel, bottom - 3 - line * 2 + pixel,
          right - 2 - pixel, bottom - 2 - line * 2 + pixel, kOptionsDark);
  CityWidgetLayout layout[96];
  i32 count = OptionsLayout(ui->settings.tab, layout, left, top, right, bottom);
  for (i32 i = 0; i < count; ++i) {
    const CityWidget *widget = layout[i].widget;
    b = layout[i].bounds;
    char text[96];
    const char *label = widget->text;
    if (widget->kind == CITY_WIDGET_GROUP) {
      DrawOptionsGroup(font, c, b, label);
    } else if (widget->kind == CITY_WIDGET_BUTTON) {
      bool active = widget->id == GAME_OPTIONS_TAB + ui->settings.tab;
      DrawOptionsBox(c, b, active ? 0xFFFCD400 : kOptionsGold, !active);
      CityRect text_bounds = {b.left + active, b.top + active, b.right + active, b.bottom + active};
      DrawCityInterfaceTextCentered(font, c, text_bounds, label, 0xFF101010, false, 10);
    } else if (widget->kind == CITY_WIDGET_TOGGLE) {
      i32 enabled = widget->id == GAME_OPTIONS_AUTO   ? p->automatic
                    : widget->id == GAME_OPTIONS_SNAP ? p->snapping
                    : widget->id == GAME_OPTIONS_BEVELS ? p->scale_bevels
                    : widget->id == GAME_OPTIONS_SPRITE_FONT ? p->sprite_font
                    : widget->id == GAME_OPTIONS_FONT_ANTIALIAS ? p->font_antialias
                    : widget->id == GAME_OPTIONS_AMBIENT_SOUNDS ? game->audio->ambient_sounds
                                                      : p->fullscreen;
      bool disabled = widget->id == GAME_OPTIONS_FONT_ANTIALIAS && p->sprite_font;
      DrawCityCheckbox(c, (CityRect){b.left, b.top, b.left + 20, b.top + 12},
          enabled, kOptionsFace, kOptionsLight, kOptionsDark);
      if (disabled)
        for (int row = 0; row < 12; ++row)
          for (int col = row & 1; col < 20; col += 2)
            CanvasFill(c, b.left + col, b.top + row, b.left + col + 1, b.top + row + 1, kOptionsFace);
      snprintf(text, sizeof(text), "%s: ", label);
      DrawOptionsText(font, c, b.left + 26, b.top, text, disabled ? kOptionsLight : kOptionsBlue);
      DrawOptionsText(font, c, b.left + 26 + CityInterfaceTextWidth(font, text), b.top,
          enabled ? "On" : "Off", disabled ? kOptionsLight : kOptionsLabel);
    } else if (widget->kind == CITY_WIDGET_LABEL) {
      if (widget->id == OPTIONS_RESOLUTION)
        snprintf(text, sizeof(text), "Screen resolution: %d x %d", p->window_width,
                 p->window_height);
      else if (widget->id == OPTIONS_REFRESH) {
        if (p->refresh_rate)
          snprintf(text, sizeof(text), "Display refresh rate: %d Hz", p->refresh_rate);
        else
          snprintf(text, sizeof(text), "Display refresh rate: unavailable");
      } else if (widget->id == OPTIONS_DRIVER)
        snprintf(text, sizeof(text), "Current driver: %s", p->driver);
      if (!label)
        label = text;
      DrawOptionsText(font, c, b.left, b.top + (b.bottom - b.top > 17 ? 3 : 0), label, kOptionsBlue);
    } else if (widget->kind == CITY_WIDGET_SLIDER) {
      i32 span = b.right - b.left - 3, marks = widget->id == GAME_OPTIONS_SCALE;
      i32 maximum = marks ? 400 : 100;
      i32 value = marks ? ui->settings.preview_scale - 100 : widget->id == GAME_OPTIONS_MASTER ?
          game->audio->master_volume : !ui->jukebox_display ? 0 :
          widget->id == GAME_OPTIONS_MUSIC ? ui->jukebox_display->volume : ui->jukebox_display->effects_volume;
      int steps = marks ? 16 : 8, label_step = marks ? 4 : 2;
      DrawOptionsSliderTrack(c, b, steps, label_step);
      for (int mark = 0; mark <= steps; ++mark) {
        int left = b.left + mark * span / steps + 1;
        bool labeled = mark % label_step == 0;
        if (labeled) {
          char number[8];
          if (marks) snprintf(number, sizeof(number), "%dx", mark / 4 + 1);
          else snprintf(number, sizeof(number), "%d%%", mark * 100 / steps);
          int size = marks ? 10 : 6;
          int width = (CityInterfaceTextWidth(font, number) * size + 9) / 10;
          int x = left - width / 2;
          if (!marks && mark == 0) x = b.left;
          else if (!marks && mark == steps) x = b.right - width;
          DrawCityInterfaceTextSized(font, c, x, b.top + 15, number, 0xFF101010, false, size);
        }
      }
      i32 left = b.left + value * span / maximum;
      DrawCanvasFrame(c, (CityRect){left, b.top, left + 3, b.top + 13},
          kOptionsGold, 0xFFFCF880, 0xFFB87818, true);
    }
  }
  if (ui->settings.tab == GAME_OPTIONS_TAB_ADVANCED) DrawCitySettings(game, c, (CityRect){left, top, right, bottom});
}

const char *CityOptionsTooltipAt(const Game *game, const Window *window, int x, int y) {
  int tab = game->ui->settings.tab;
  if (CanvasPointInRect(CitySettingsControlBounds(window->bounds, GAME_OPTIONS_SCOPE), x, y))
    return "Choose city settings or global defaults, or copy gameplay settings between them.";
  if (tab == GAME_OPTIONS_TAB_ADVANCED) {
    int action = SettingsActionAt(game, window->bounds, x, y);
    if (action >= GAME_OPTIONS_SETTING && action < GAME_OPTIONS_SETTING + kCitySettingsCount)
      return kCitySettings[action - GAME_OPTIONS_SETTING].tooltip;
    if (action >= GAME_OPTIONS_GROUP && action < GAME_OPTIONS_GROUP + CITY_SETTINGS_GROUP_COUNT)
      return "Expand or collapse this group of settings.";
    switch (action) {
    case GAME_OPTIONS_CATEGORY: return "Show all categories or just one group of settings.";
    case GAME_OPTIONS_TYPE: return "Filter interface preferences or city gameplay settings.";
    case GAME_OPTIONS_FILTER: return "Search setting names, categories and descriptions. Clear with the X button.";
    case GAME_OPTIONS_CLEAR_FILTER: return "Clear the text filter.";
    case GAME_OPTIONS_EXPAND_ALL: return "Expand every settings group.";
    case GAME_OPTIONS_COLLAPSE_ALL: return "Collapse every settings group. Search reveals matching settings.";
    case GAME_OPTIONS_RESET_ALL: return game->ui->settings.city_defaults ?
        "Reset new-city gameplay defaults and global interface/audio preferences." :
        "Reset this city's gameplay settings and global interface/audio preferences.";
    case GAME_OPTIONS_SCROLL_UP: case GAME_OPTIONS_SCROLL_DOWN: case GAME_OPTIONS_PAGE_UP:
    case GAME_OPTIONS_PAGE_DOWN: case GAME_OPTIONS_SCROLL_THUMB: return "Scroll the settings list.";
    }
  }
  CityWidgetLayout layout[96];
  int count = OptionsLayout(tab, layout, window->bounds.left, window->bounds.top,
      window->bounds.right, window->bounds.bottom);
  return CityWidgetTooltipAt(layout, count, x, y);
}

i32 Sc2kCanvasGameOptionsSliderAt(const CitySettingsBrowser *browser, const Window *w,
    i32 slider, i32 x, i32 continuous) {
  if (!browser || !w || w->kind != CITY_CANVAS_GAME_OPTIONS)
    return GAME_OPTIONS_NONE;
  CityWidgetLayout layout[96];
  i32 count = OptionsLayout(browser->tab, layout, w->bounds.left, w->bounds.top,
      w->bounds.right, w->bounds.bottom);
  for (i32 i = 0; i < count; ++i)
    if (layout[i].widget->kind == CITY_WIDGET_SLIDER && layout[i].widget->id == slider) {
      CityRect b = layout[i].bounds;
      i32 span = b.right - b.left - 3;
      i32 position = x - b.left - 1;
      position = IntClamp(position, 0, span);
      i32 value = (position * (slider == GAME_OPTIONS_SCALE ? 400 : 100) + span / 2) / span;
      if (slider == GAME_OPTIONS_SCALE) {
        value += 100;
        if (!continuous)
          value = (value + 12) / 25 * 25;
      }
      return slider + value;
    }
  return GAME_OPTIONS_NONE;
}

i32 Sc2kCanvasGameOptionsActionAt(const Game *game, const Window *w,
    i32 x, i32 y, i32 continuous) {
  const DrawingOptions *options = &game->drawing->options;
  const CitySettingsBrowser *browser = &game->ui->settings;
  if (!w->visible || w->kind != CITY_CANVAS_GAME_OPTIONS)
    return GAME_OPTIONS_NONE;
  if (CanvasPointInRect(CitySettingsControlBounds(w->bounds, GAME_OPTIONS_SCOPE), x, y)) return GAME_OPTIONS_SCOPE;
  if (x >= w->bounds.left && x < w->bounds.left + 11 && y >= w->bounds.top &&
      y < w->bounds.top + 14)
    return GAME_OPTIONS_CLOSE;
  if (x >= w->bounds.right - 11 && x < w->bounds.right && y >= w->bounds.top && y < w->bounds.top + 14)
    return GAME_OPTIONS_RESET_SIZE;
  CityWidgetLayout layout[96];
  i32 count = OptionsLayout(browser->tab, layout, w->bounds.left, w->bounds.top,
      w->bounds.right, w->bounds.bottom);
  i32 id = Sc2kCanvasWidgetHit(layout, count, x, y);
  if (!id && browser->tab == GAME_OPTIONS_TAB_ADVANCED) id = SettingsActionAt(game, w->bounds, x, y);
  if (id == GAME_OPTIONS_FONT_ANTIALIAS && options->sprite_font) return GAME_OPTIONS_NONE;
  if (id == GAME_OPTIONS_SCALE || id == GAME_OPTIONS_MUSIC || id == GAME_OPTIONS_EFFECTS || id == GAME_OPTIONS_MASTER)
    return Sc2kCanvasGameOptionsSliderAt(browser, w, id, x, continuous);
  return id;
}
