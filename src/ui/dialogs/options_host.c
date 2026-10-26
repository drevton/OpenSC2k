#include "drawing/maps/map_view.h"
#include "base/text.h"
#include "assets/state.h"
#include "game/audio.h"
#include "audio/backend.h"
#include "audio/state.h"
#include "audio/types.h"
#include "base/time.h"
#include "base/version.h"
#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include "city/presentation.h"
#include "drawing/bitmaps.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/startup.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/messages.h"
#include "ui/map_views.h"
#include "ui/menus/menu.h"
#include "ui/music/jukebox.h"
#include "ui/sdl.h"
#include "ui/startup/startup.h"
#include "ui/state.h"
#include "ui/tools/tools.h"
#include "ui/tools/types.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"
#include "ui/windows/windows.h"
#include "ui/text/text.h"

// Application Options

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
      KeepCityWindowCaptionVisible(&ui->desktop, &ui->desktop.windows[i].bounds);
    InvalidateCityInterface(game);
    ui->desktop.dragging = CITY_CANVAS_DRAG_NONE;
    drawing->ui_scale_applied = drawing->options.scale;
    drawing->ui_map_scale_applied = drawing->map_pixel_scale;
  }
  if (!ui->options_slider)
    ui->settings.preview_scale = drawing->options.scale;
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
  ui->settings.city_defaults = !game->city_active || NativeStartupMenuWaiting(game->city);
  const char *title = ui->settings.city_defaults ? "Global settings" : "City settings";
  int existing = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_GAME_OPTIONS);
  if (existing >= 0) {
    ui->desktop.windows[existing].title = title;
    DirtyCityMusicWindows(game);
    return;
  }
  int x = (ui->desktop.canvas_width - GAME_OPTIONS_WIDTH) / 2,
      y = (ui->desktop.canvas_height - GAME_OPTIONS_HEIGHT) / 2;
  x = IntMax(x, 0);
  y = IntMax(y, 0);
  Sc2kCanvasDesktopOpen(&ui->desktop, (Window){.kind = CITY_CANVAS_GAME_OPTIONS,
                                                        .bounds = {x, y, x + GAME_OPTIONS_WIDTH,
                                                                   y + GAME_OPTIONS_HEIGHT},
                                                        .title = title});
}

static bool ApplyCitySettingValue(Game *game, const CitySetting *setting, int value) {
  if (value < setting->minimum || value > setting->maximum) return false;
  CityInterfaceState *ui = game->ui;
  if (setting->command && ui->settings.city_defaults) {
    if (!StoreGameCityDefault(game, setting, value)) return false;
    DirtyCityMusicWindows(game);
    return true;
  }
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
    CopyText(game->audio->jukebox.error, sizeof(game->audio->jukebox.error),
        "Choose a soundfont with soundfont FILE.sf2 in the console.");
    return false;
  }
  if (setting->action == GAME_OPTIONS_FULLSCREEN) {
    if (!ui->display) return false;
    if (Sc2kSDLFullscreen(ui->display, value != 0)) {
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
    if (!RunCitySettingCommand(game, setting, value)) return false;
  } else WriteLocalSettingValue(game, setting, value);
  if (setting->effects & SETTING_SYNC_DISPLAY) SyncCityInterface(game, ui->display, 0);
  if (setting->effects & SETTING_REDRAW_INTERFACE) InvalidateCityInterface(game);
  if (setting->effects & SETTING_REDRAW_MAP) InvalidateCityMapViews(drawing);
  if (setting->effects & SETTING_UPDATE_VOLUME) NativeAudioVolume(audio);
  if ((setting->effects & SETTING_CLOSE_STATUS) && !value) {
    WindowIds windows = SnapshotCityWindowIds(&ui->desktop);
    for (int i = windows.count - 1; i >= 0; --i) {
      Window *window = FindCityCanvasWindow(&ui->desktop, windows.ids[i]);
      if (window && window->kind == CITY_CANVAS_TOOL_STATUS)
        Sc2kCanvasDesktopDismissWindow(&ui->desktop, window->window_id);
    }
  }
  if (setting->effects & SETTING_CONFIGURE_DIAGNOSTICS) ConfigureCityDiagnostics(game);
  StoreGameSettings(game);
  DirtyCityMusicWindows(game);
  return true;
}

static void ApplyCityOptionsAction(Game *game, int action, Window *window) {
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
      int current = ReadCitySettingValue(game, setting, browser->city_defaults);
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
        setting->choices, setting->maximum + 1, ReadCitySettingValue(game, setting, browser->city_defaults),
        &kCityDropdownGoldColors);
  } else if (action == GAME_OPTIONS_SCOPE) {
    static const CityDropdownItem kScopes[] = {
      {"Edit this city", CITY_SETTINGS_EDIT_CITY, CITY_DROPDOWN_PLAIN},
      {"Edit global defaults", CITY_SETTINGS_EDIT_DEFAULTS, CITY_DROPDOWN_PLAIN},
      {"Copy global defaults to this city", CITY_SETTINGS_COPY_DEFAULTS_TO_CITY, CITY_DROPDOWN_PLAIN},
      {"Copy this city to global defaults", CITY_SETTINGS_COPY_CITY_TO_DEFAULTS, CITY_DROPDOWN_PLAIN},
    };
    CityRect anchor = CitySettingsControlBounds(window->bounds, action);
    anchor.left -= window->bounds.left; anchor.right -= window->bounds.left;
    anchor.top -= window->bounds.top; anchor.bottom -= window->bounds.top;
    OpenCityDropdown(&ui->desktop, window->window_id, action, anchor, kScopes,
        sizeof(kScopes) / sizeof(kScopes[0]), browser->city_defaults ? 1 : 0, &kCityDropdownGoldColors);
    u32 disabled = game->city_active ? 0 : (1 << CITY_SETTINGS_EDIT_CITY) |
        (1 << CITY_SETTINGS_COPY_DEFAULTS_TO_CITY) | (1 << CITY_SETTINGS_COPY_CITY_TO_DEFAULTS);
    SetCityDropdownMenu(&ui->desktop, disabled);
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
      {"City gameplay settings", 2, CITY_DROPDOWN_PLAIN},
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
          ReadCitySettingValue(game, &kCitySettings[setting], browser->city_defaults) !=
              kCitySettings[setting].default_value)
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
    browser->preview_scale = action - GAME_OPTIONS_SCALE;
  else if (action >= GAME_OPTIONS_MASTER && action <= GAME_OPTIONS_MASTER + 100) {
    game->audio->master_volume = action - GAME_OPTIONS_MASTER;
    NativeAudioVolume(game->audio);
    StoreGameSettings(game);
    DirtyCityMusicWindows(game);
  } else if (action >= GAME_OPTIONS_MUSIC && action <= GAME_OPTIONS_MUSIC + 100)
    ApplyCityMusicAction(game, JUKEBOX_MUSIC_VOLUME + action - GAME_OPTIONS_MUSIC, window);
  else if (action >= GAME_OPTIONS_EFFECTS && action <= GAME_OPTIONS_EFFECTS + 100)
    ApplyCityMusicAction(game, JUKEBOX_EFFECTS_VOLUME + action - GAME_OPTIONS_EFFECTS, window);
  else if (action >= GAME_OPTIONS_TAB && action < GAME_OPTIONS_TAB + GAME_OPTIONS_TAB_COUNT)
    browser->tab = action - GAME_OPTIONS_TAB;
  else if (action == GAME_OPTIONS_CLOSE)
    Sc2kCanvasDesktopDismissWindow(&ui->desktop, window->window_id);
  else if (action == GAME_OPTIONS_RESET_SIZE) {
    Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    ResetCityWindowSize(window);
    KeepCityWindowCaptionVisible(&ui->desktop, &window->bounds);
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
    drawing->options.scale = ui->settings.preview_scale;
    drawing->options.automatic = 0;
    ui->options_slider = 0;
    SyncCityInterface(game, ui->display, 0);
    StoreGameSettings(game);
  }
  ui->options_slider = 0;
}

// Application Menu

// Handles this window's painting and input; paint coordinates are local to the window.
bool OptionsWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    DrawGameOptions(game, canvas, window);
    return true;
  }
  if (event->kind == WINDOW_DESTROY) {
    if (game->ui->options_slider_window == window->window_id) ReleaseCityOptionsControl(game);
    game->ui->settings.dragging_scrollbar = false;
    return true;
  }
  if (event->kind == WINDOW_DROPDOWN) {
    const CityDropdownSelection *selection = event->selection;
    if (selection->control == GAME_OPTIONS_SCOPE) {
      if (selection->value != CITY_SETTINGS_EDIT_DEFAULTS && !game->city_active) return false;
      CitySettingsBrowser *browser = &game->ui->settings;
      switch (selection->value) {
      case CITY_SETTINGS_EDIT_CITY:
        browser->city_defaults = false;
        break;
      case CITY_SETTINGS_EDIT_DEFAULTS:
        browser->city_defaults = true;
        break;
      case CITY_SETTINGS_COPY_DEFAULTS_TO_CITY:
        if (!ApplyGameCityDefaults(game)) return false;
        browser->city_defaults = false;
        break;
      case CITY_SETTINGS_COPY_CITY_TO_DEFAULTS:
        if (!CopyCitySettingsToDefaults(game)) return false;
        browser->city_defaults = true;
        break;
      default: return false;
      }
      Window *target = FindCityCanvasWindow(&game->ui->desktop, selection->window_id);
      if (target) target->title = browser->city_defaults ? "Global settings" : "City settings";
      DirtyCityMusicWindows(game);
      return true;
    }
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
  if (browser->tab != GAME_OPTIONS_TAB_ADVANCED) {
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
    ApplyCityOptionsAction(game, Sc2kCanvasGameOptionsSliderAt(&ui->settings, target,
        ui->options_slider, event->x, input->ctrl), target);
    return true;
  }
  return false;
}
