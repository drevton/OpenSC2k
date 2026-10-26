#ifndef SC2K_GAME_OPTIONS_H
#define SC2K_GAME_OPTIONS_H

#include "city/types.h"
#include "game/settings.h"

typedef struct CanvasSDL CanvasSDL;

typedef struct Canvas Canvas;
typedef struct Window Window;

enum { CITY_SCROLLWHEEL_SCROLL, CITY_SCROLLWHEEL_ZOOM, CITY_SCROLLWHEEL_OFF };

typedef enum GameOptionsTab {
  GAME_OPTIONS_TAB_GENERAL = 0,
  GAME_OPTIONS_TAB_GRAPHICS = 1,
  GAME_OPTIONS_TAB_SOUND = 2,
  GAME_OPTIONS_TAB_ADVANCED = 3,
  GAME_OPTIONS_TAB_COUNT = 4,
} GameOptionsTab;

// Interaction state belongs to UI; values remain with their actual owners.
typedef struct {
  char filter[96];
  int category, type, scroll, caret, text_scroll, select_all;
  u32 collapsed_groups;
  bool filter_focused, caret_visible, dragging_scrollbar, city_defaults;
  int scrollbar_offset;
  double caret_blink_at;
  u16 preview_scale; // Pending slider value; applied to Drawing on release.
  u8 tab; // GameOptionsTab.
} CitySettingsBrowser;

// Choices in the scope dropdown; copy actions select their destination for subsequent edits.
typedef enum CitySettingsScopeChoice {
  CITY_SETTINGS_EDIT_CITY, CITY_SETTINGS_EDIT_DEFAULTS,
  CITY_SETTINGS_COPY_DEFAULTS_TO_CITY, CITY_SETTINGS_COPY_CITY_TO_DEFAULTS
} CitySettingsScopeChoice;

extern const char *const kCitySettingsGroups[CITY_SETTINGS_GROUP_COUNT];

enum {
  GAME_OPTIONS_WIDTH = 430,
  GAME_OPTIONS_MIN_WIDTH = 300,
  GAME_OPTIONS_HEIGHT = 322,
  GAME_OPTIONS_NONE = 0,
  GAME_OPTIONS_AUTO,
  GAME_OPTIONS_SNAP,
  GAME_OPTIONS_FULLSCREEN,
  GAME_OPTIONS_JUKEBOX,
  GAME_OPTIONS_CLOSE,
  GAME_OPTIONS_RESET_SIZE,
  GAME_OPTIONS_BEVELS,
  GAME_OPTIONS_SPRITE_FONT,
  GAME_OPTIONS_FONT_ANTIALIAS,
  GAME_OPTIONS_TAB = 20,
  GAME_OPTIONS_CATEGORY = 30,
  GAME_OPTIONS_TYPE,
  GAME_OPTIONS_FILTER,
  GAME_OPTIONS_CLEAR_FILTER,
  GAME_OPTIONS_EXPAND_ALL,
  GAME_OPTIONS_COLLAPSE_ALL,
  GAME_OPTIONS_RESET_ALL,
  GAME_OPTIONS_SCROLL_UP,
  GAME_OPTIONS_SCROLL_DOWN,
  GAME_OPTIONS_PAGE_UP,
  GAME_OPTIONS_PAGE_DOWN,
  GAME_OPTIONS_SCROLL_THUMB,
  GAME_OPTIONS_SCOPE = 50,
  GAME_OPTIONS_GROUP = 64,
  GAME_OPTIONS_SETTING = 128,
  GAME_OPTIONS_FIX = 256,
  GAME_OPTIONS_MUSIC_ENABLED = 512,
  GAME_OPTIONS_EFFECTS_ENABLED,
  GAME_OPTIONS_BACKUP_SAVES,
  GAME_OPTIONS_FLOATING_STATUS = 550,
  GAME_OPTIONS_ADVANCED_QUERY,
  GAME_OPTIONS_MIDDLE_CENTER,
  GAME_OPTIONS_DEVELOPER,
  GAME_OPTIONS_LUA,
  GAME_OPTIONS_BACKGROUND_MUSIC,
  GAME_OPTIONS_LOG,
  GAME_OPTIONS_CRASH,
  GAME_OPTIONS_SOUNDFONT,
  GAME_OPTIONS_EXTENDED_SAVE,
  GAME_OPTIONS_SAVE_VIEW_POSITION,
  GAME_OPTIONS_REPAIR_SAVE_HEADERS,
  GAME_OPTIONS_BLACK_BACKGROUND,
  GAME_OPTIONS_SCROLLWHEEL,
  GAME_OPTIONS_SIMULATION_BATCHING = 580,
  GAME_OPTIONS_TREE_LIFECYCLE,
  GAME_OPTIONS_TERRAIN_PROTECTION,
  GAME_OPTIONS_TERRAIN_POINT_EDIT,
  GAME_OPTIONS_TERRAIN_SEAWATER,
  GAME_OPTIONS_OVERLAPPING_EFFECTS,
  GAME_OPTIONS_INDIVIDUAL_FACILITIES,
  GAME_OPTIONS_AMBIENT_SOUNDS,
  GAME_OPTIONS_DRAWING_FIX = 600,
  GAME_OPTIONS_SCALE = 1000,
  GAME_OPTIONS_MUSIC = 2000,
  GAME_OPTIONS_EFFECTS = 3000,
  GAME_OPTIONS_MASTER = 4000
};

CityRect CitySettingsControlBounds(CityRect window, int control);
CityRect CitySettingsScrollbarThumb(const Game *game, CityRect window);
int CitySettingsRowCount(const Game *game);
CityRect CitySettingControlBounds(const Game *game, CityRect window, int index);
bool PollCitySettingsCaret(Game *game, double now);

// Action hit testing requires live Game/UI/Drawing owners and a live window.
// Slider hit testing returns GAME_OPTIONS_NONE for a missing browser or window.
// Hit testing computes layout once, reusing it when the hit control is a slider.
// Slider results encode control ID plus value; scale snaps to 25-percent steps unless continuous is nonzero.
i32 Sc2kCanvasGameOptionsActionAt(const Game *game, const Window *window,
    i32 x, i32 y, i32 continuous);
i32 Sc2kCanvasGameOptionsSliderAt(const CitySettingsBrowser *browser, const Window *window,
    i32 slider, i32 x, i32 continuous);
const char *CityOptionsTooltipAt(const Game *game, const Window *window, int x, int y);
// Paint a prepared canvas with initialized Game/UI/Drawing/Assets owners. Reads live preference values;
// changing settings and submitting City commands belong to the window's input handler.
void DrawGameOptions(Game *game, Canvas *c, const Window *window);

void SyncCityInterface(Game *game, CanvasSDL *display, i32 automatic_change);
void OpenCityGameOptions(Game *game);

#endif
