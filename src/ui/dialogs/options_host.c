#include "ui/startup/startup.h"
#include "base/version.h"
#include "audio/types.h"
#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include "city/presentation.h"
#include "ui/tools/types.h"
#include "ui/windows/state.h"
#include "ui/state.h"
#include "drawing/drawing_internal.h"
#include "base/time.h"
#include "ui/tools/tools.h"
#include "game/application_internal.h"
#include "ui/menus/menu.h"
#include "drawing/bitmaps.h"

// Application Options

static void KeepCityCaptionVisible(Game *game, CityRect *b);

void CityInterfacePixels(Game *game, const CanvasSDLEvent *event, int *x, int *y) {
  Drawing *drawing = game->drawing;
  *x = event->x * drawing->map_pixel_scale; *y = event->y * drawing->map_pixel_scale;
  if (drawing->ui_display) Sc2kSDLFramebufferPointer(drawing->ui_display, x, y);
}

CanvasSDLEvent TransformCityInterfaceEvent(Game *game, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = game->ui;
  CanvasSDLEvent transformed = *event;
  int x, y;
  CityInterfacePixels(game, event, &x, &y);
  u32 capture = ui->options_slider ? ui->options_slider_window : ui->jukebox_slider ?
      ui->jukebox_slider_window :
      ui->jukebox_pressed_window;
  Sc2kCanvasDesktopPixelPointer(&ui->desktop, capture, x, y, &transformed.x, &transformed.y);
  return transformed;
}

static void KeepCityCaptionVisible(Game *game, CityRect *b) {
  CityInterfaceState *ui = game->ui;
  int w = b->right - b->left, h = b->bottom - b->top;
  int x = b->left, y = b->top;
  int min_x = w <= ui->desktop.canvas_width ? 0 : 32 - w;
  int max_x = w <= ui->desktop.canvas_width ? ui->desktop.canvas_width - w : ui->desktop.canvas_width - 32;
  int max_y = h <= ui->desktop.canvas_height ? ui->desktop.canvas_height - h : ui->desktop.canvas_height - 14;
  x = IntClamp(x, min_x, max_x);
  y = IntClamp(y, CityMenuBounds(&ui->desktop, 0).bottom, max_y);
  *b = (CityRect){x, y, x + w, y + h};
}

// Appearance changes move every window edge; old UI pixels must be restored too.
static void InvalidateCityInterface(Game *game) {
  Drawing *drawing = game->drawing;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  for (CityWindowDrawing *state = drawing->window_drawing; state; state = state->next)
    InvalidateCityWindowBitmaps(state);
  InvalidateCityMapViews(drawing);
  CityRect screen = {0, 0, drawing->framebuffer.width, drawing->framebuffer.height};
  MarkScreenDirty(&drawing->screen_damage, screen);
  MarkScreenDirty(&drawing->world_damage, screen);
  Sc2kDirtyMarkRect(&desktop->window_damage, screen);
  Sc2kDirtyMarkRect(&desktop->dirty, (CityRect){0, 0, desktop->canvas_width, desktop->canvas_height});
}

void SyncCityInterface(Game *game, CanvasSDL *display, int automatic_change) {
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  if (display) {
    drawing->map_pixel_scale = Sc2kSDLPixelScale(display);
    Sc2kSDLDisplayOptions(display, &drawing->options);
  }
  int width = kDefaultCityCanvasWidth * drawing->map_pixel_scale, height = kDefaultCityCanvasHeight * drawing->map_pixel_scale;
  if (display) Sc2kSDLOutputSize(display, &width, &height);
  if (drawing->options.automatic) {
    drawing->options.scale = drawing->map_pixel_scale * 100;
    drawing->options.scale = IntMin(drawing->options.scale, 500);
  }
  if (display) {
    // Fixed startup dialogs must fit even when a manual scale was saved on a larger display.
    int menu_height = CITY_MENU_BAR_HEIGHT;
    for (int i = 0; i < ui->desktop.count; ++i) {
      const Window *window = &ui->desktop.windows[i];
      if (window->kind != CITY_CANVAS_STARTUP_MENU) continue;
      bool main_menu = window->startup_layout && window->startup_layout->main_menu;
      int available_height = main_menu ? height - CityStartupContentTop(width, height) - 8 : height;
      int fit = IntMin(width * 100 / (window->bounds.right - window->bounds.left),
          available_height * 100 / (window->bounds.bottom - window->bounds.top + (main_menu ? 0 : menu_height)));
      drawing->options.scale = IntClamp(drawing->options.scale, 100, fit);
    }
  }
  if (!PrepareCityToolbarIcons(game)) {
    game->execution_failed = true;
    return;
  }
  int canvas_width = (width * 100 + drawing->options.scale - 1) / drawing->options.scale;
  int canvas_height = (height * 100 + drawing->options.scale - 1) / drawing->options.scale;
  bool resized = ui->desktop.canvas_width != canvas_width || ui->desktop.canvas_height != canvas_height;
  ui->desktop.canvas_width = canvas_width;
  ui->desktop.canvas_height = canvas_height;
  if (resized || drawing->ui_scale_applied != drawing->options.scale ||
      drawing->ui_map_scale_applied != drawing->map_pixel_scale) {
    if (!automatic_change || !drawing->options.automatic) {
      int numerator = drawing->ui_scale_applied * drawing->map_pixel_scale,
          denominator = drawing->options.scale * drawing->ui_map_scale_applied;
      for (int i = 0; i < ui->desktop.count; ++i) {
        CityRect *b = &ui->desktop.windows[i].bounds;
        int w = b->right - b->left, h = b->bottom - b->top;
        b->left = (i32)((i64)b->left * numerator / denominator);
        b->top = (i32)((i64)b->top * numerator / denominator);
        b->right = b->left + w;
        b->bottom = b->top + h;
      }
    }
    for (int i = 0; i < ui->desktop.count; ++i)
      KeepCityCaptionVisible(game, &ui->desktop.windows[i].bounds);
    InvalidateCityInterface(game);
    ui->desktop.dragging = CITY_CANVAS_DRAG_NONE;
    drawing->ui_scale_applied = drawing->options.scale;
    drawing->ui_map_scale_applied = drawing->map_pixel_scale;
  }
  if (!ui->options_slider)
    drawing->options.preview_scale = drawing->options.scale;
  if (display)
    Sc2kSDLConfigureFramebuffer(display, drawing->options.scale);
  Sc2kCanvasSetUIScale(&ui->desktop, drawing->options.scale, drawing->map_pixel_scale * 100);
  Sc2kCanvasSetWindowSnapRadius(&ui->desktop, drawing->options.snapping ? 5 : 0);
  Sc2kCanvasDesktopPixelMetrics(&ui->desktop, drawing->options.scale, drawing->map_pixel_scale,
      width, height);
  for (int i = 0; i < ui->desktop.count; ++i) {
    const Window *window = &ui->desktop.windows[i];
    if (window->startup_layout && window->startup_layout->main_menu)
      CenterCityStartupWindow(game->city, window->window_id);
  }
  UpdateCityMapZoomLimits(game);
  if (!PrepareCityConstructionIcons(game) || !PrepareCityStartupIcons(game)) game->execution_failed = true;
}

void OpenCityGameOptions(Game *game) {
  CityInterfaceState *ui = game->ui;
  if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_GAME_OPTIONS) >= 0) return;
  int x = (ui->desktop.canvas_width - GAME_OPTIONS_WIDTH) / 2,
      y = (ui->desktop.canvas_height - GAME_OPTIONS_HEIGHT) / 2;
  x = IntMax(x, 0);
  y = IntMax(y, 0);
  Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_GAME_OPTIONS,
                                                        .bounds = {x, y, x + GAME_OPTIONS_WIDTH,
                                                                   y + GAME_OPTIONS_HEIGHT},
                                                        .title = "Game Options"});
}

void OpenCityMusicWindow(Game *game, int programme) {
  CityInterfaceState *ui = game->ui;
  WindowKind kind = programme ? CITY_CANVAS_MUSIC_PROGRAMME : CITY_CANVAS_JUKEBOX;
  if (FocusCityCanvasWindowKind(&ui->desktop, kind) >= 0) return;
  Sc2kCanvasDesktopOpen(
      &ui->desktop,
      (Window){
          .kind = kind,
          .bounds = programme ? (CityRect){84, 130, 84 + JUKEBOX_PROGRAMME_WIDTH,
                                           130 + JUKEBOX_PROGRAMME_HEIGHT}
                              : (CityRect){175, 42, 175 + JUKEBOX_PLAYER_WIDTH,
                                           42 + (ui->jukebox.collapsed ? JUKEBOX_TITLE_HEIGHT
                                                                   : JUKEBOX_PLAYER_HEIGHT)},
          .title = programme ? "Music Programme" : "Jazz Jukebox"});
  KeepCityCaptionVisible(game, &ui->desktop.windows[ui->desktop.count - 1].bounds);
}

void ApplyCityMusicAction(Game *game, int action, Window *window) {
  CityInterfaceState *ui = game->ui;
  int current = JukeboxCurrent(&ui->jukebox), playing = ui->jukebox.playing, restart = 0;
  if (action == JUKEBOX_CLOSE) {
    Sc2kCanvasDesktopDismissWindow(&ui->desktop, window->window_id);
    return;
  }
  if (action == JUKEBOX_PREVIOUS || action == JUKEBOX_NEXT) {
    JukeboxStep(&ui->jukebox, action == JUKEBOX_PREVIOUS ? -1 : 1);
    restart = 1;
  } else if (action == JUKEBOX_STOP) ui->jukebox.playing = 0;
  else if (action == JUKEBOX_PLAY) { ui->jukebox.playing = 1; restart = 1; }
  else if (action == JUKEBOX_SHUFFLE) JukeboxShuffle(&ui->jukebox);
  else if (action == JUKEBOX_PROGRAMME) OpenCityMusicWindow(game, 1);
  else if (action == JUKEBOX_CLEAR) JukeboxClear(&ui->jukebox);
  else if (action == JUKEBOX_COLLAPSE) {
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    ui->jukebox.collapsed = !ui->jukebox.collapsed;
    window->bounds.bottom =
        window->bounds.top + (ui->jukebox.collapsed ? JUKEBOX_TITLE_HEIGHT : JUKEBOX_PLAYER_HEIGHT);
  } else if (action == JUKEBOX_PIN) ui->jukebox.pinned = !ui->jukebox.pinned;
  else if (action >= JUKEBOX_PLAY_TRACK && action < JUKEBOX_PLAY_TRACK + JUKEBOX_TRACKS) {
    SelectJukeboxTrack(&ui->jukebox, action - JUKEBOX_PLAY_TRACK);
    restart = 1;
  } else if (action >= JUKEBOX_EFFECTS_VOLUME && action <= JUKEBOX_EFFECTS_VOLUME + 100)
    ui->jukebox.effects_volume = action - JUKEBOX_EFFECTS_VOLUME;
  else if (action >= JUKEBOX_MUSIC_VOLUME && action <= JUKEBOX_MUSIC_VOLUME + 100)
    ui->jukebox.volume = action - JUKEBOX_MUSIC_VOLUME;
  else if (action >= JUKEBOX_REMOVE && action < JUKEBOX_REMOVE + JUKEBOX_CAPACITY) {
    restart = action - JUKEBOX_REMOVE == ui->jukebox.position &&
        ui->jukebox.programme >= JUKEBOX_PROGRAMME_CUSTOM_1;
    JukeboxRemove(&ui->jukebox, action - JUKEBOX_REMOVE);
  } else if (action >= JUKEBOX_ADD && action < JUKEBOX_ADD + JUKEBOX_TRACKS)
    JukeboxAdd(&ui->jukebox, action - JUKEBOX_ADD);
  else if (action >= JUKEBOX_SELECT && action < JUKEBOX_SELECT + JUKEBOX_PROGRAMMES) {
    JukeboxSelect(&ui->jukebox, action - JUKEBOX_SELECT);
    restart = 1;
  }
  if (game->audio->live_open) {
    if (restart || current != JukeboxCurrent(&ui->jukebox) || playing != ui->jukebox.playing)
      NativeAudioPlay(game->audio, &ui->jukebox);
    NativeAudioVolume(game->audio, &ui->jukebox);
  }
  StoreGameSettings(game);
  DirtyCityMusicWindows(game);
}

static bool ApplyCitySettingValue(Game *game, const CitySetting *setting, int value) {
  if (value < setting->minimum || value > setting->maximum) return false;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  Drawing *drawing = game->drawing;
  if (setting->action == GAME_OPTIONS_MUSIC_ENABLED) {
    ApplyCityMusicAction(game, value ? JUKEBOX_PLAY : JUKEBOX_STOP, NULL);
    return true;
  }
  if (setting->action == GAME_OPTIONS_EFFECTS_ENABLED) {
    if ((audio->enabled != 0) != (value != 0)) ToggleCitySoundPreference(game);
    return true;
  }
  if (setting->action == GAME_OPTIONS_SOUNDFONT && !SetCitySoundFont(audio, NULL, value != 0)) {
    CopyText(ui->jukebox.error, sizeof(ui->jukebox.error),
        "Choose a soundfont with soundfont FILE.sf2 in the console.");
    return false;
  }
  if (setting->action == GAME_OPTIONS_FULLSCREEN) {
    if (!drawing->ui_display) return false;
    if (Sc2kSDLFullscreen(drawing->ui_display, value != 0)) {
      fprintf(stderr, "Fullscreen: %s\n", Sc2kSDLError());
      return false;
    }
  }
  if (setting->effects & SETTING_STOP_EFFECTS) {
    StopAllCitySound(game);
    audio->queued_sound = CITY_SOUND_NONE;
    audio->retry_delay = 0;
  }
  if (setting->command) {
    bool fix = setting->command == CITY_COMMAND_SET_FIX;
    if (!RunCitySettingCommand(game, setting, value)) return false;
    if (!fix) StoreGameCitySetting(game, setting->key, value);
  } else WriteCitySettingValue(game, setting, value, false);
  if (setting->effects & SETTING_SYNC_DISPLAY) SyncCityInterface(game, drawing->ui_display, 0);
  if (setting->effects & SETTING_REDRAW_INTERFACE) InvalidateCityInterface(game);
  if (setting->effects & SETTING_REDRAW_MAP) InvalidateCityMapViews(drawing);
  if (setting->effects & SETTING_UPDATE_VOLUME) NativeAudioVolume(audio, &ui->jukebox);
  if ((setting->effects & SETTING_CLOSE_STATUS) && !value)
    for (int i = ui->desktop.count - 1; i >= 0; --i)
      if (ui->desktop.windows[i].kind == CITY_CANVAS_TOOL_STATUS)
        Sc2kCanvasDesktopDismissWindow(&ui->desktop, ui->desktop.windows[i].window_id);
  if (setting->effects & SETTING_CONFIGURE_DIAGNOSTICS) ConfigureCityDiagnostics(game);
  StoreGameSettings(game);
  DirtyCityMusicWindows(game);
  return true;
}

void ApplyCityOptionsAction(Game *game, int action, Window *window) {
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  CitySettingsBrowser *browser = &ui->settings;
  const CitySetting *setting = NULL;
  if (action >= GAME_OPTIONS_SETTING && action < GAME_OPTIONS_SETTING + kCitySettingsCount)
    setting = &kCitySettings[action - GAME_OPTIONS_SETTING];
  else for (int i = 0; i < kCitySettingsCount; ++i)
    if (kCitySettings[i].action && kCitySettings[i].action == action) { setting = &kCitySettings[i]; break; }
  if (setting && !setting->choices) {
    if (setting->action != GAME_OPTIONS_FONT_ANTIALIAS || !drawing->options.sprite_font) {
      int current = ReadCitySettingValue(game, setting, false);
      ApplyCitySettingValue(game, setting, current == setting->maximum ? setting->minimum : current + 1);
    }
    return;
  }
  if (setting && setting->choices) {
    int index = (int)(setting - kCitySettings);
    CityRect anchor = CitySettingControlBounds(game, window->bounds, index);
    anchor.left -= window->bounds.left; anchor.right -= window->bounds.left;
    anchor.top -= window->bounds.top; anchor.bottom -= window->bounds.top;
    OpenCityDropdown(&ui->desktop, window->window_id, GAME_OPTIONS_SETTING + index, anchor,
        setting->choices, setting->maximum + 1, ReadCitySettingValue(game, setting, false), &kCityDropdownGoldColors);
  } else if (action == GAME_OPTIONS_CATEGORY || action == GAME_OPTIONS_TYPE) {
    static const CityDropdownItem kCategories[] = {
      {"All categories", 0, CITY_DROPDOWN_PLAIN},
      {"Interface", 1, CITY_DROPDOWN_PLAIN},
      {"Display", 2, CITY_DROPDOWN_PLAIN},
      {"Sound", 3, CITY_DROPDOWN_PLAIN},
      {"Economy", 4, CITY_DROPDOWN_PLAIN},
      {"Infrastructure", 5, CITY_DROPDOWN_PLAIN},
      {"Military", 6, CITY_DROPDOWN_PLAIN},
      {"Labels", 7, CITY_DROPDOWN_PLAIN},
      {"SimCity 2000 fixes", CITY_SETTINGS_SIMCITY_FIXES + 1, CITY_DROPDOWN_PLAIN},
    };
    static const CityDropdownItem kTypes[] = {
      {"All setting types", 0, CITY_DROPDOWN_PLAIN},
      {"Interface settings", 1, CITY_DROPDOWN_PLAIN},
      {"City simulation fixes", 2, CITY_DROPDOWN_PLAIN},
    };
    CityRect anchor = CitySettingsControlBounds(window->bounds, action);
    anchor.left -= window->bounds.left; anchor.right -= window->bounds.left;
    anchor.top -= window->bounds.top; anchor.bottom -= window->bounds.top;
    bool category = action == GAME_OPTIONS_CATEGORY;
    OpenCityDropdown(&ui->desktop, window->window_id, action, anchor, category ? kCategories : kTypes,
        category ? sizeof(kCategories) / sizeof(kCategories[0]) : sizeof(kTypes) / sizeof(kTypes[0]),
        category ? browser->category : browser->type, &kCityDropdownGoldColors);
  } else if (action == GAME_OPTIONS_CLEAR_FILTER) {
    browser->filter[0] = 0;
    browser->caret = browser->text_scroll = browser->scroll = browser->select_all = 0;
  } else if (action == GAME_OPTIONS_EXPAND_ALL) browser->collapsed_groups = 0;
  else if (action == GAME_OPTIONS_COLLAPSE_ALL) browser->collapsed_groups = (1 << CITY_SETTINGS_GROUP_COUNT) - 1;
  else if (action == GAME_OPTIONS_RESET_ALL) {
    ui->wheel_zoom_remainder = 0;
    ui->scroll_remainder_x = ui->scroll_remainder_y = 0;
    for (int setting = 0; setting < kCitySettingsCount; ++setting)
      if (!kCitySettings[setting].hidden &&
          ReadCitySettingValue(game, &kCitySettings[setting], false) != kCitySettings[setting].default_value)
        ApplyCitySettingValue(game, &kCitySettings[setting], kCitySettings[setting].default_value);
    InvalidateCityInterface(game);
    StoreGameSettings(game);
  } else if (action == GAME_OPTIONS_SCROLL_THUMB) {
    browser->dragging_scrollbar = true;
  } else if (action >= GAME_OPTIONS_GROUP && action < GAME_OPTIONS_GROUP + CITY_SETTINGS_GROUP_COUNT)
    browser->collapsed_groups ^= 1 << (action - GAME_OPTIONS_GROUP);
  else if (action >= GAME_OPTIONS_SCROLL_UP && action <= GAME_OPTIONS_PAGE_DOWN) {
    CityRect list = CitySettingsControlBounds(window->bounds, GAME_OPTIONS_NONE);
    int visible = IntMax(1, (list.bottom - list.top - 4) / 16);
    int step = action >= GAME_OPTIONS_PAGE_UP ? visible : 1;
    browser->scroll += (action == GAME_OPTIONS_SCROLL_UP || action == GAME_OPTIONS_PAGE_UP) ? -step : step;
    browser->scroll = IntClamp(browser->scroll, 0, CitySettingsRowCount(game) - visible);
  } else if (action >= GAME_OPTIONS_SCALE + 100 && action <= GAME_OPTIONS_SCALE + 500)
    drawing->options.preview_scale = action - GAME_OPTIONS_SCALE;
  else if (action >= GAME_OPTIONS_MASTER && action <= GAME_OPTIONS_MASTER + 100) {
    game->audio->master_volume = action - GAME_OPTIONS_MASTER;
    NativeAudioVolume(game->audio, &ui->jukebox);
    StoreGameSettings(game);
    DirtyCityMusicWindows(game);
  } else if (action >= GAME_OPTIONS_MUSIC && action <= GAME_OPTIONS_MUSIC + 100)
    ApplyCityMusicAction(game, JUKEBOX_MUSIC_VOLUME + action - GAME_OPTIONS_MUSIC, window);
  else if (action >= GAME_OPTIONS_EFFECTS && action <= GAME_OPTIONS_EFFECTS + 100)
    ApplyCityMusicAction(game, JUKEBOX_EFFECTS_VOLUME + action - GAME_OPTIONS_EFFECTS, window);
  else if (action >= GAME_OPTIONS_TAB && action < GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_COUNT)
    drawing->options.tab = action - GAME_OPTIONS_TAB;
  else if (action == GAME_OPTIONS_CLOSE)
    Sc2kCanvasDesktopDismissWindow(&ui->desktop, window->window_id);
  else if (action == GAME_OPTIONS_RESET_SIZE) {
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    ResetCityWindowSize(window);
    KeepCityCaptionVisible(game, &window->bounds);
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  } else if (action == GAME_OPTIONS_JUKEBOX)
    OpenCityMusicWindow(game, 0);

  if (action == GAME_OPTIONS_EXPAND_ALL || action == GAME_OPTIONS_COLLAPSE_ALL ||
      (action >= GAME_OPTIONS_GROUP && action < GAME_OPTIONS_GROUP + CITY_SETTINGS_GROUP_COUNT))
    browser->scroll = 0;
  StoreGameSettings(game);
  DirtyCityMusicWindows(game);
}

static void ReleaseCityOptionsControl(Game *game) {
  Drawing *drawing = game->drawing;
  CityInterfaceState *ui = game->ui;
  if (ui->options_slider == GAME_OPTIONS_SCALE) {
    drawing->options.scale = drawing->options.preview_scale;
    drawing->options.automatic = 0;
    ui->options_slider = 0;
    SyncCityInterface(game, drawing->ui_display, 0);
    StoreGameSettings(game);
  }
  ui->options_slider = 0;
}

bool PollCityMusic(Game *game) {
  if (!game->audio->live_open) return false;
  u16 device;
  int notification = NativeAudioMusicNotification(game->audio, &device);
  if (!notification) return false;
  CompleteCityMusic(game, notification, device);
  return true;
}

// Application Menu

void OpenCityPanel(Game *game, int panel) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  for (int i = 0; i < desktop->count; ++i) {
    WindowKind kind = desktop->windows[i].kind;
    if ((panel == CITY_PANEL_MAP_DATA && kind == CITY_CANVAS_MAP_DATA) || Sc2kCanvasChartCode(kind) == panel) {
      FocusCityCanvasWindowKind(desktop, kind);
      return;
    }
  }
  SetCityPanelVisibilityByCode(game->city, panel);
  if (panel == CITY_PANEL_MAP_DATA) ApplicationRefreshMapData(game->city);
}

void ExecuteCityMenuItem(Game *game, const CityMenuItem *item) {
  if (!item) return;
  PlayCityButtonBeep(game);
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  switch (item->action) {
  case CITY_MENU_ACTION_TREE_SPECIES:
  case CITY_MENU_ACTION_TOOL: {
    int tool = item->action == CITY_MENU_ACTION_TREE_SPECIES ? CITY_CANVAS_TOOL_TREES : item->parameter;
    int packed = Sc2kCanvasMapPointerTools(tool, 0);
    int button = tool == CITY_CANVAS_TOOL_CENTER ? CITY_TOOLBAR_BUTTON_CENTER : packed & 255;
    if (SelectCityToolWithModifiers(game, button, (packed >> 8) & 255, 0)) {
      ui->desktop.selected_tool = tool;
      if (tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN) ui->terrain_tool = CITY_TERRAIN_TOOL_STRETCH;
      if (tool == CITY_CANVAS_TOOL_STREAM) ui->terrain_tool = CITY_TERRAIN_TOOL_RIVER;
    }
    break;
  }
  case CITY_MENU_ACTION_PAUSE:
    if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
      if (!city->name.length) {
        RequestCityConfiguration(game);
        break;
      }
      RunCityCommand(game, CITY_COMMAND_START_CITY, INVALID_TILE, 0, 0, 0, NULL, NULL);
      break;
    }
    if (ui->dialog_pause_requested) break;
    if (city->settings.speed > CITY_SPEED_PAUSED) ui->resume_speed = city->settings.speed;
    RunCityCommand(game, CITY_COMMAND_SET_SIMULATION_SPEED,
        INVALID_TILE, city->settings.speed == CITY_SPEED_PAUSED ? ui->resume_speed : CITY_SPEED_PAUSED, 0, 0, NULL, NULL);
    StoreGameCitySetting(game, "game.speed", ui->resume_speed);
    break;
  case CITY_MENU_ACTION_SOUND_OPTIONS:
    game->drawing->options.tab = GAME_OPTIONS_TAB_SOUND;
    OpenCityGameOptions(game);
    DirtyCityMusicWindows(game);
    break;
  case CITY_MENU_ACTION_HELP: {
    static const char *const kTitles[] = {"Tool Help", "Keyboard Shortcuts", "About"};
    static const char *const kText[] = {
      "Use the icon toolbar at the top of the screen. Hover a button to see its name. "
      "Click a construction category to open its choices, then select a tool or variant. "
      "Power, water and reward choosers show previews, names and prices. "
      "Pin a window to keep it when opening another category. Each category remembers its last-used tool. "
      "Click its toolbar button again to reset the window position. Drag on the map to build. "
      "Zoning includes all zone types. Transport includes roads, rail and subway. Both include Demolish. "
      "Utilities contains power and water. Civic & Special Buildings contains schools, services, parks and rewards. "
      "Landscape, Zone, Transportation, Utilities, Civic and Emergency open green movable toolbars. "
      "Adjust & Review contains the budget, ordinances and advisors. Charts contains graphs and city statistics. "
      "Settings contains preferences and music. File contains Save, Load, Abandon and Exit.",
      "R/Shift+R: rotate placement clockwise/counterclockwise. Shift+click reverses the rotation button. "
      "Ctrl+R: road. Ctrl+Shift+R: highway. T: rail. Shift+T: subway. L: power lines. I: pipes. "
      "B: demolish. V: de-zone. Q/E: light/dense residential. A/D: commercial. Z/C: industrial. "
      "U/Shift+U: trees/water. K/Shift+K: school/college. P/Shift+P: police/prison. F: fire. H: hospital. "
      "J/Shift+J: small/large park. Ctrl+E: power. Ctrl+I: water. Ctrl+Alt+R: rewards. "
      "Ctrl+B: budget. Ctrl+O: ordinances. Ctrl+P: settings. Ctrl+L: load. Ctrl+S: save. "
      "Space: pause. +/-: zoom. PageUp/PageDown: rotate. F10: menus, arrows/Enter: choose. "
      "Alt+F/S/H: file/settings/help. Alt+Z/N/U/C: zoning/transport/utilities/civic. "
      "Alt+L/E/M/A: landscape/emergency/meet/review. F8: viewport. F12: inspect.",
      "Software-rendered city, construction tools and canvas windows."
    };
    char about[SC2K_BUILD_VERSION_CAPACITY + 96];
    const char *text = kText[item->parameter];
    if (item->parameter == 2) {
      char version[SC2K_BUILD_VERSION_CAPACITY];
      FormatBuildVersion(version, sizeof(version));
      snprintf(about, sizeof(about), "%s. %s", version, text);
      text = about;
    }
    Sc2kCanvasDesktopOpenNotice(&ui->desktop, (CityRect){40, 45, 540, 300},
        kTitles[item->parameter], text, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
    break;
  }
  case CITY_MENU_ACTION_CATEGORY: case CITY_MENU_ACTION_SUBMENU:
  case CITY_MENU_ACTION_DISASTERS: break; // Opens a UI dropdown before dispatch.
  case CITY_MENU_ACTION_SEPARATOR: break;
  case CITY_MENU_ACTION_ABANDON: OpenCityAbandonDialog(game); break;
  case CITY_MENU_ACTION_CONSOLE: OpenCityDeveloperConsole(game); break;
  case CITY_MENU_ACTION_SAVE: RequestCitySave(game); break;
  case CITY_MENU_ACTION_LOAD:
    MarkSelectedCityMenuCommand(game, CITY_MENU_LOAD_CITY);
    RequestCityFileOpen(game);
    break;
  case CITY_MENU_ACTION_QUIT:
    ui->exit_cancel_enabled = true;
    HandleCityExitCommand(city);
    break;
  case CITY_MENU_ACTION_PANEL: OpenCityPanel(game, item->parameter); break;
  case CITY_MENU_ACTION_AUTO_GOTO:
    ui->auto_goto_disabled = !ui->auto_goto_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_SUBSCRIPTION:
    ui->newspaper_delivery_disabled = !ui->newspaper_delivery_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_EXTRA_NEWSPAPER:
    ui->extra_newspaper_disabled = !ui->extra_newspaper_disabled;
    StoreGameSettings(game);
    break;
  case CITY_MENU_ACTION_MAP_LAYER: ToggleCityMapLayer(game, item->parameter); break;
  case CITY_MENU_ACTION_TOOLBAR: SelectCityToolWithModifiers(game, item->parameter, 0, 0); break;
  case CITY_MENU_ACTION_COMMAND:
    RunCityCommand(game, item->command, INVALID_TILE, item->parameter, 0, 0, NULL, NULL);
    if (item->command == CITY_COMMAND_SET_SIMULATION_SPEED)
      StoreGameCitySetting(game, "game.speed", city->settings.speed);
    else if (item->command == CITY_COMMAND_TOGGLE_AUTO_BUDGET)
      StoreGameCitySetting(game, "game.auto_budget", city->indicators.auto_budget_enabled);
    else if (item->command == CITY_COMMAND_TOGGLE_NO_DISASTERS)
      StoreGameCitySetting(game, "game.no_disasters", city->indicators.event_flag);
    break;
  case CITY_MENU_ACTION_GOTO_DISASTER:
    if (city->disasters.pending_type != CITY_DISASTER_NONE || city->disasters.map_update_pending ||
        city->disaster_work.type != CITY_DISASTER_NONE) {
      TileIndex tile = MapTileXYOrInvalid(city, city->disasters.location.x, city->disasters.location.y);
      if (tile != INVALID_TILE)
        CenterCityMapViewAtTile(city, NULL, MapTileX(city, tile), MapTileY(city, tile));
    }
    break;
  case CITY_MENU_ACTION_ADVISOR:
    OpenCityAdvisor(city, item->parameter);
    break;
  case CITY_MENU_ACTION_BUDGET:
    OpenCityBudget(city);
    break;
  case CITY_MENU_ACTION_POLICIES:
    if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_POLICIES) >= 0) break;
    InitializeCityOrdinancesDialog(game->city);
    Sc2kCanvasDesktopOpenPolicies(&ui->desktop, (CityRect){70, 18, 530, 472}, city->unlocks.ordinance_flags);
    ApplicationUpdateDialogPause(city);
    break;
  case CITY_MENU_ACTION_MAP_DATA:
    OpenCityPanel(game, CITY_PANEL_MAP_DATA);
    break;
  case CITY_MENU_ACTION_MUSIC:
    ApplyCityMusicAction(game, ui->jukebox.playing ? JUKEBOX_STOP : JUKEBOX_PLAY, NULL);
    break;
  case CITY_MENU_ACTION_JUKEBOX:
    OpenCityMusicWindow(game, 0);
    break;
  case CITY_MENU_ACTION_OPTIONS:
    OpenCityGameOptions(game);
    break;
  case CITY_MENU_ACTION_VIEWPORT:
    OpenCityMapView(city);
    break;
  case CITY_MENU_ACTION_SIGN_LIST:
    OpenCitySignList(city);
    break;
  case CITY_MENU_ACTION_LAND_USE:
    Sc2kCanvasDesktopOpenLandUse(&ui->desktop, (CityRect){110, 18, 450, 305},
        (const i16 *)city->statistics.count);
    break;
  case CITY_MENU_ACTION_NEWSPAPER:
    if (FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_NEWSPAPER) < 0)
      OpenCityManualNewspaper(city, city->newspaper.edition_index);
    break;
  }
}

bool HandleCityMainMenuPointer(Game *game, const CanvasSDLEvent *event) {
  CityInterfaceState *ui = game->ui;
  CityCanvasDesktop *desktop = &ui->desktop;
  if (ui->pointer_stroke_count ||
      desktop->dragging != CITY_CANVAS_DRAG_NONE ||
      (desktop->input_capture_window && !desktop->menu.selected) ||
      NativeStartupMenuWaiting(game->city) || ui->startup_waiting) return false;
  UpdateCityToolbar(game);
  int x, y;
  CityInterfacePixels(game, event, &x, &y);
  int scale = desktop->pixel_state.scale ? desktop->pixel_state.scale : 100;
  // Do not use window-relative hit testing: menus are anchored to the screen.
  x = (int)((i64)x * 100 / scale);
  y = (int)((i64)y * 100 / scale);
  bool handled = false;
  if (event->kind == CANVAS_SDL_POINTER_DOWN) {
    if (!desktop->menu.selected && !CityMenuContains(desktop, x, y)) return false;
    RecordGameInput(game);
    handled = PressCityMenu(desktop, x, y);
    if (handled && desktop->menu.held) PlayCityButtonBeep(game);
  } else if (event->kind == CANVAS_SDL_POINTER_MOVE)
    handled = MoveCityMenu(desktop, x, y);
  else if (event->kind == CANVAS_SDL_POINTER_UP) {
    handled = desktop->menu.held || desktop->menu.selected;
    ExecuteCityMenuItem(game, ReleaseCityMenu(desktop, x, y));
    ReleaseCityToolbarFeedback(desktop, (u32)(u64)(MonotonicSeconds() * 1000));
  }
  if (handled) {
    ui->placement_hover_valid = false;
    UpdateCityPlacementPreview(game->city);
  }
  return handled;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool OptionsWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawGameOptions(game, canvas, window->bounds.left, window->bounds.top,
        window->bounds.right, window->bounds.bottom);
    return true;
  }
  if (event->kind == WINDOW_DESTROY) {
    if (game->ui->options_slider_window == window->window_id) ReleaseCityOptionsControl(game);
    game->ui->settings.dragging_scrollbar = false;
    return true;
  }
  if (event->kind == WINDOW_DROPDOWN) {
    const CityDropdownSelection *selection = event->selection;
    int index = selection->control - GAME_OPTIONS_SETTING;
    const CitySetting *setting = index >= 0 && index < kCitySettingsCount ? &kCitySettings[index] : NULL;
    if (selection->control == GAME_OPTIONS_SCROLLWHEEL)
      for (int i = 0; i < kCitySettingsCount; ++i)
        if (kCitySettings[i].action == GAME_OPTIONS_SCROLLWHEEL) setting = &kCitySettings[i];
    if (selection->control != GAME_OPTIONS_CATEGORY && selection->control != GAME_OPTIONS_TYPE &&
        (!setting || !setting->choices)) return false;
    CitySettingsBrowser *browser = &game->ui->settings;
    if (setting) {
      if (!ApplyCitySettingValue(game, setting, selection->value)) return false;
      game->ui->wheel_zoom_remainder = 0;
      game->ui->scroll_remainder_x = game->ui->scroll_remainder_y = 0;
    } else if (selection->control == GAME_OPTIONS_CATEGORY) browser->category = selection->value;
    else browser->type = selection->value;
    if (!setting) browser->scroll = 0;
    Window *target = FindCityCanvasWindow(&game->ui->desktop, selection->window_id);
    if (target) Sc2kDirtyMarkRect(&game->ui->desktop.dirty, target->bounds);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityInterfaceState *ui = game->ui;
  if (input->kind == CANVAS_SDL_POINTER_UP || input->kind == CANVAS_SDL_FOCUS_LOST) {
    ui->settings.dragging_scrollbar = false;
    ReleaseCityOptionsControl(game);
  }
  Window *target = FindCityCanvasWindow(&ui->desktop, window->window_id);
  CityCanvasDesktop *desktop = &ui->desktop;
  CitySettingsBrowser *browser = &ui->settings;
  if (game->drawing->options.tab != GAME_OPTIONS_TAB_ADVANCED) {
    browser->filter_focused = false;
    browser->dragging_scrollbar = false;
  } else {
    if (browser->dragging_scrollbar && input->kind == CANVAS_SDL_POINTER_MOVE) {
      CityRect list = CitySettingsControlBounds(target->bounds, GAME_OPTIONS_NONE);
      int visible = IntMax(1, (list.bottom - list.top - 4) / 16);
      CityRect track = {list.right - 12, list.top + 12, list.right - 1, list.bottom - 12};
      CityScrollbar scrollbar = ReadCityScrollbar(track, CitySettingsRowCount(game), visible, browser->scroll, 8);
      browser->scroll = CityScrollbarPosition(&scrollbar, event->y, browser->scrollbar_offset,
          CITY_SCROLLBAR_DRAG_NEAREST);
      Sc2kDirtyMarkRect(&desktop->dirty, target->bounds);
      return true;
    }
    if (input->kind == CANVAS_SDL_SCROLL) {
      if (input->y)
        ApplyCityOptionsAction(game, input->y > 0 ? GAME_OPTIONS_SCROLL_UP : GAME_OPTIONS_SCROLL_DOWN, target);
      return true;
    }
    bool handled = false;
    if (browser->filter_focused && input->kind == CANVAS_SDL_TEXT_INPUT) {
      InsertCityTextInput(browser->filter, sizeof(browser->filter), &browser->caret, &browser->select_all,
          input->text, false);
      handled = true;
    } else if (input->kind == CANVAS_SDL_KEY_DOWN) {
      if (browser->filter_focused && (input->key == CITY_KEY_ESCAPE || input->key == CITY_KEY_RETURN)) {
        browser->filter_focused = false;
        handled = true;
      } else if (browser->filter_focused) {
        handled = HandleCityTextEditKey(browser->filter, &browser->caret, &browser->select_all,
            input->key);
        // Printable key-downs must not also trigger game shortcuts before text input.
        if (!input->ctrl && (input->key & CITY_HOTKEY_KEY_MASK) >= 32 &&
            (input->key & CITY_HOTKEY_KEY_MASK) < 127) handled = true;
      } else if (input->key == CITY_KEY_UP || input->key == CITY_KEY_DOWN ||
          input->key == CITY_KEY_PAGE_UP || input->key == CITY_KEY_PAGE_DOWN) {
        int action = input->key == CITY_KEY_UP ? GAME_OPTIONS_SCROLL_UP :
            input->key == CITY_KEY_DOWN ? GAME_OPTIONS_SCROLL_DOWN :
            input->key == CITY_KEY_PAGE_UP ? GAME_OPTIONS_PAGE_UP : GAME_OPTIONS_PAGE_DOWN;
        ApplyCityOptionsAction(game, action, target);
        return true;
      }
    }
    if (handled) {
      browser->scroll = 0;
      browser->text_scroll = CityTextBoxScroll(&game->resources->interface_font,
          CitySettingsControlBounds(target->bounds, GAME_OPTIONS_FILTER), browser->filter,
          browser->caret, browser->text_scroll);
      browser->caret_visible = true;
      browser->caret_blink_at = 0;
      Sc2kDirtyMarkRect(&desktop->dirty, target->bounds);
      return true;
    }
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int action =
        Sc2kCanvasGameOptionsActionAt(game, target, event->x, event->y, input->ctrl);
    ui->settings.filter_focused = action == GAME_OPTIONS_FILTER;
    if (action == GAME_OPTIONS_SCROLL_THUMB)
      ui->settings.scrollbar_offset = event->y - CitySettingsScrollbarThumb(game, target->bounds).top;
    if (action == GAME_OPTIONS_FILTER) {
      CityRect input = CitySettingsControlBounds(target->bounds, GAME_OPTIONS_FILTER);
      ui->settings.caret = CityTextBoxCaretAt(&game->resources->interface_font, input,
          ui->settings.filter, ui->settings.caret, &ui->settings.text_scroll, event->x);
      ui->settings.select_all = 0;
      ui->settings.caret_visible = true;
      ui->settings.caret_blink_at = 0;
    }
    if (action >= GAME_OPTIONS_SCALE + 100 && action <= GAME_OPTIONS_SCALE + 500)
      ui->options_slider = GAME_OPTIONS_SCALE;
    else if (action >= GAME_OPTIONS_MUSIC && action <= GAME_OPTIONS_MUSIC + 100)
      ui->options_slider = GAME_OPTIONS_MUSIC;
    else if (action >= GAME_OPTIONS_EFFECTS && action <= GAME_OPTIONS_EFFECTS + 100)
      ui->options_slider = GAME_OPTIONS_EFFECTS;
    else if (action >= GAME_OPTIONS_MASTER && action <= GAME_OPTIONS_MASTER + 100)
      ui->options_slider = GAME_OPTIONS_MASTER;
    ui->options_slider_window = target->window_id;
    if (action)
      ApplyCityOptionsAction(game, action, target);
    else
      Sc2kCanvasDesktopPointerDown(&ui->desktop, event->x, event->y);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_MOVE && ui->options_slider) {
    ApplyCityOptionsAction(game, Sc2kCanvasGameOptionsSliderAt(&game->drawing->options, target,
        ui->options_slider, event->x, input->ctrl), target);
    return true;
  }
  return false;
}
