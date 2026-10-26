#include "ui/startup/startup.h"
#include "city/terrain/terrain.h"

#include "city/engine/city_name.h"
#include "base/text.h"
#include "city/maps/labels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/resource_types.h"
#include "assets/state.h"
#include "city/saveload/runtime.h"
#include "city/saveload/summary.h"
#include "city/maps/map.h"
#include "city/maps/map_types.h"
#include "city/state.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/startup.h"
#include "drawing/state.h"
#include "game/city_lifecycle.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/messages.h"
#include "ui/input.h"
#include "ui/music/jukebox.h"
#include "ui/sdl.h"
#include "ui/startup/startup_internal.h"
#include "ui/startup/startup_layout.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"

// Startup Host
// Native resource windows return choices to the main loop; diagnostic callers may retain their stacks.

static void NotifyStartupCheckpoint(City *city, u32 kind, i32 token, i32 choice) {
  RefreshCityChartPanels(city);
  if (city->game->observation && city->game->observation->startup_choice)
    city->game->observation->startup_choice(city->game, kind, token, choice);
}

static void NotifyStartupState(City *city) {
  CityInterfaceState *ui = city->game->ui;
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i)
    ui->startup_windows[i].layout.caret_blink_at = 0;
  ui->startup_waiting = (ui->startup_menu != NULL && !ui->startup_menu->answered) ||
      (ui->setup_window != NULL && !ui->setup_window->answered);
  ApplicationUpdateDialogPause(city);
  if (ui->startup_desktop)
    for (int i = 0; i < ui->startup_desktop->count; ++i)
      if (ui->startup_desktop->windows[i].startup_layout)
        Sc2kDirtyMarkRect(&ui->startup_desktop->dirty, ui->startup_desktop->windows[i].bounds);
}

static Window *CanvasWindow(City *city, u32 id) {
  CityCanvasDesktop *desktop = city->game->ui->startup_desktop;
  return desktop ? FindCityCanvasWindow(desktop, id) : NULL;
}

bool UpdateCityTextCarets(Game *game, double now) {
  CityInterfaceState *ui = game->ui;
  CityCanvasDesktop *desktop = ui->startup_desktop;
  bool dirty = PollCitySettingsCaret(game, now);
  if (!desktop) return dirty;
  u32 focused_window = desktop->count ? desktop->windows[desktop->count - 1].window_id : 0;
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i) {
    CityStartupResourceWindow *owner = &ui->startup_windows[i];
    if (!owner->dialog) continue;
    CityCanvasStartupWindow *layout = &owner->layout;
    int focused_control = owner->window_id == focused_window ? layout->focused_control : 0;
    CityCanvasStartupControl *input = NULL;
    for (int j = 0; j < layout->control_count; ++j) {
      CityCanvasStartupControl *control = &layout->controls[j];
      if (control->id == focused_control && control->visible &&
          control->control_class == CITY_RESOURCE_CONTROL_EDIT && !(control->style & CITY_RESOURCE_STYLE_DISABLED)) {
        input = control;
        break;
      }
    }
    bool visible = layout->caret_visible;
    if (!input) {
      layout->caret_control = 0;
      layout->caret_visible = false;
      layout->caret_blink_at = 0;
    } else if (layout->caret_control != focused_control || !layout->caret_blink_at) {
      layout->caret_control = focused_control;
      layout->caret_visible = true;
      layout->caret_blink_at = now + 0.5;
    } else if (now >= layout->caret_blink_at) {
      layout->caret_visible = !layout->caret_visible;
      layout->caret_blink_at = now + 0.5;
    }
    if (visible == layout->caret_visible) continue;
    Window *window = CanvasWindow(game->city, owner->window_id);
    if (!window) continue;
    // Damage only the input control; other UI and the map retain their pixels.
    for (int j = 0; j < layout->control_count; ++j) {
      CityCanvasStartupControl *control = &layout->controls[j];
      if (control->control_class != CITY_RESOURCE_CONTROL_EDIT) continue;
      CityRect rect = control->bounds;
      rect.left += window->bounds.left + 2; rect.right += window->bounds.left + 2;
      rect.top += window->bounds.top + 16; rect.bottom += window->bounds.top + 16;
      Sc2kDirtyMarkRect(&desktop->dirty, rect);
    }
    dirty = true;
  }
  return dirty;
}
static CityStartupResourceWindow *FindStartupResourceWindow(City *city, u32 window_id) {
  CityInterfaceState *ui = city->game->ui;
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i) {
    CityStartupResourceWindow *window = &ui->startup_windows[i];
    if (window->dialog != NULL && window->window_id == window_id) return window;
  }
  return NULL;
}
const CityCanvasStartupWindow *ReadCityStartupWindowLayout(City *city, u32 id) {
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, id);
  return window ? &window->layout : NULL;
}

static bool RefreshTerrainSetup(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, ui->setup_window->dialog->base.window_id);
  if (ui->terrain_preview == NULL) ui->terrain_preview = calloc(1, sizeof(*ui->terrain_preview));
  CityTerrainSettings preview = ui->terrain_settings;
  int divisor = IntMax(1, IntMax(preview.width, preview.height) / 256);
  preview.width = IntMax(128, preview.width / divisor);
  preview.height = IntMax(128, preview.height / divisor);
  CitySetupWindowState *pending = ui->setup_window;
  bool ready = ui->terrain_preview && (pending->preserve_terrain ?
      ReadCityTerrainPreview(city, ui->terrain_preview, &ui->terrain_settings) : pending->terrain_path ?
      ReadCityFileTerrain(pending->terrain_path, &ui->terrain_settings, ui->terrain_preview, true) :
      GenerateTerrainMap(&preview, ui->terrain_preview));
  if (!ready) {
    if (!pending->terrain_path) SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  CopyText(window->layout.title, sizeof(window->layout.title), pending->terrain_path ? "New City - Loaded Map" :
      pending->preserve_terrain ? "New City - Edited Terrain" : "New City");
  if (!window->layout.drawing) {
    window->layout.drawing = (CityTerrainPreviewDrawing *)CreateCityWindowDrawing(city->drawing,
        CITY_WINDOW_DRAWING_TERRAIN_PREVIEW, 0);
    if (!window->layout.drawing) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
  }
  InvalidateCityWindowBitmaps(&window->layout.drawing->base);
  window->layout.terrain_preview = ui->terrain_preview;
  window->layout.water_colors = city->game->resources->landscape_water_colors;
  RefreshCityTerrainControls(ui, &window->layout);
  for (int i = 0; i < window->layout.control_count; ++i) {
    CityCanvasStartupControl *control = &window->layout.controls[i];
    if (control->id >= CITY_SETUP_HILLS && control->id <= CITY_SETUP_TREES) {
      int values[] = {ui->terrain_settings.hills, ui->terrain_settings.water, ui->terrain_settings.trees};
      control->checked = values[control->id - CITY_SETUP_HILLS];
    }
  }
  Sc2kDirtyMarkRect(&ui->desktop.dirty, CanvasWindow(city, window->window_id)->bounds);
  NotifyStartupState(city);
  return true;
}

static bool GenerateCityTerrainPreview(City *city) {
  CitySetupWindowState *pending = city->game->ui->setup_window;
  free(pending->terrain_path); pending->terrain_path = NULL;
  pending->preserve_terrain = false;
  return RefreshTerrainSetup(city);
}

typedef struct { int setup_token; } CityTerrainFileRequest;

static void AcceptCityTerrainFile(City *city, const char *path, void *context) {
  CityTerrainFileRequest *request = context;
  CityInterfaceState *ui = city->game->ui;
  CitySetupWindowState *pending = ui->setup_window;
  bool current = pending && pending->token == request->setup_token && !pending->answered;
  free(request);
  if (!current || !path) return;
  CityFileSummary summary;
  if (!ReadCityFileSummary(path, &summary)) {
    PresentCityFileNotice(city, "The selected map could not be read.", true);
    return;
  }
  CityTerrainSettings previous = ui->terrain_settings;
  char *old_path = pending->terrain_path;
  bool preserved = pending->preserve_terrain;
  pending->terrain_path = DuplicateText(path);
  if (!pending->terrain_path) {
    pending->terrain_path = old_path;
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  pending->preserve_terrain = false;
  ui->terrain_settings.width = summary.width; ui->terrain_settings.height = summary.height;
  if (!RefreshTerrainSetup(city)) {
    free(pending->terrain_path); pending->terrain_path = old_path;
    pending->preserve_terrain = preserved;
    ui->terrain_settings = previous;
    RefreshTerrainSetup(city);
    PresentCityFileNotice(city, "The selected map could not be read.", true);
    return;
  }
  free(old_path);
}

static void OpenCityTerrainFile(City *city) {
  CityTerrainFileRequest *request = malloc(sizeof(*request));
  if (!request) { SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE); return; }
  request->setup_token = city->game->ui->setup_window->token;
  CityPath directory = {0};
  bool ready = GetCitySaveDirectory(city->game, &directory);
  CityFilePickerOptions options = {.name = "*.sc2", .directory = CityPathText(&directory), .title = "Load Map",
      .open = true, .callback = AcceptCityTerrainFile, .context = request};
  bool opened = ready && OpenCityFileDialog(city, &options);
  FreeCityPathStorage(&directory);
  if (!opened) free(request);
}

static bool RegenerateCityTerrainSetup(City *city) {
  CityTerrainSettings *settings = &city->game->ui->terrain_settings;
  // Advance the UI seed without consuming the live city's simulation RNG.
  // Setting changes and final generation reuse this seed to retain the preview.
  settings->seed = settings->seed * 1664525 + 1013904223;
  return GenerateCityTerrainPreview(city);
}

// Input comes from a registered setup window; its layout and bounds describe that same window.
static bool MoveCityTerrainSetupPointer(City *city, const Window *window, int x) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->terrain_setup_active || ui->setup_window == NULL || ui->setup_window->answered) return false;
  const CityCanvasStartupWindow *layout = window->startup_layout;
  int id = layout->pressed_control;
  if (id < CITY_SETUP_HILLS || id > CITY_SETUP_TREES) return false;
  for (int i = 0; i < layout->control_count; ++i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    if (control->id != id) continue;
    if (control->control_class != CITY_RESOURCE_CONTROL_SLIDER) return false;
    int position = x - window->bounds.left - 2 - control->bounds.left;
    int value = IntClamp(position * 47 / (control->bounds.right - control->bounds.left - 1), 0, 47);
    int *values[] = {&ui->terrain_settings.hills, &ui->terrain_settings.water, &ui->terrain_settings.trees};
    if (*values[id - CITY_SETUP_HILLS] != value) {
      *values[id - CITY_SETUP_HILLS] = value;
      GenerateCityTerrainPreview(city);
    }
    return true;
  }
  return false;
}

static u32 RegisterCityResourceOwner(City *city, CityStartupWindow *dialog, u32 resource) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->startup_desktop == NULL || dialog == NULL) return false;
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i) {
    CityStartupResourceWindow *window = &ui->startup_windows[i];
    if (window->dialog != NULL) continue;
    memset(window, 0, sizeof(*window));
    if (resource == CITY_DIALOG_RESOURCE_NEW_CITY)
      BuildCitySetupLayout(ui, &window->layout);
    else if (resource == CITY_SETUP_YEAR_QUERY_RESOURCE)
      BuildCityYearQueryLayout(ui->setup_window->dialog, &window->layout);
    else if (!BuildCityStartupLayout(resource, &window->layout)) return false;
    Window canvas = {0};
    canvas.kind = CITY_CANVAS_STARTUP_MENU;
    canvas.procedure = resource == CITY_DIALOG_RESOURCE_STARTUP_MENU ? StartupWindowProcedure : SetupWindowProcedure;
    canvas.title = window->layout.title;
    canvas.startup_layout = &window->layout;
    canvas.bounds = (CityRect){0, 0, window->layout.client_bounds.right + 4,
        window->layout.client_bounds.bottom + 18};
    int index = Sc2kCanvasDesktopOpen(ui->startup_desktop, canvas);
    if (index < 0) return false;
    window->window_id = ui->startup_desktop->windows[index].window_id;
    window->dialog = dialog; window->resource = resource;
    NotifyStartupState(city);
    return window->window_id;
  }
  return false;
}
static bool UnregisterCityResourceOwner(City *city, u32 id) {
  CityInterfaceState *ui = city->game->ui;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, id);
  if (window == NULL || window->window_id != id) return false;
  Sc2kCanvasDesktopDismissWindow(ui->startup_desktop, window->window_id);
  if (window->layout.drawing) ReleaseCityWindowDrawing(&window->layout.drawing->base);
  window->layout.drawing = NULL;
  window->window_id = 0; window->dialog = NULL;
  NotifyStartupState(city);
  return true;
}
// Native geometry operates on canvas owners directly, without OS window adapters.
bool CenterCityStartupWindow(City *city, u32 window_id) {
  CityInterfaceState *ui = city->game->ui;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, window_id);
  Window *canvas = window != NULL ? CanvasWindow(city, window->window_id) : NULL;
  if (canvas == NULL || ui->startup_desktop == NULL) return false;
  int screen_width = ui->startup_desktop->canvas_width;
  int screen_height = ui->startup_desktop->canvas_height;
  int width = canvas->bounds.right - canvas->bounds.left;
  int height = canvas->bounds.bottom - canvas->bounds.top;
  int x = screen_width / 2 - width / 2;
  int y = screen_height / 2 - height / 2;
  if (window->layout.main_menu)
    y = (CityStartupContentTop(screen_width, screen_height) + screen_height - height) / 2;
  if (x < 0) x = 0;
  else if (x + width > screen_width) x = screen_width - width;
  if (y < 0) y = 0;
  else if (y + height > screen_height) y = screen_height - height;
  if (x == canvas->bounds.left && y == canvas->bounds.top) return true;
  Sc2kDirtyMarkRect(&ui->startup_desktop->dirty, canvas->bounds);
  canvas->bounds = (CityRect){x, y, x + width, y + height};
  Sc2kDirtyMarkRect(&ui->startup_desktop->dirty, canvas->bounds);
  NotifyStartupState(city);
  return true;
}

// Keep the original choice policy; UI owns control layout and damage.
bool InitializeCityStartupMenuDialog(City *city, CityStartupMenuDialog *dialog) {
  CityInterfaceState *ui = city->game->ui;
  ui->restore_frame_tools = false;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, dialog->window_id);
  Window *canvas = window != NULL ? CanvasWindow(city, window->window_id) : NULL;
  if (canvas == NULL || window->resource != CITY_DIALOG_RESOURCE_STARTUP_MENU) {
    return false;
  }
  CenterCityStartupWindow(city, dialog->window_id);
  return true;
}
bool EndCityStartupDialog(City *city, u32 handle, int result) {
  CityInterfaceState *ui = city->game->ui;
  for (CityStartupMenuState *pending = ui->startup_menu; pending != NULL; pending = pending->parent)
    if (pending->dialog->window_id == handle && !pending->answered) {
      pending->choice = result; pending->answered = 1;
      bool closed = UnregisterCityResourceOwner(city, pending->dialog->window_id);
      pending->dialog->window_id = 0;
      return closed;
    }
  return false;
}
bool AnswerCityStartupMenuDialog(City *city, int token, int choice) {
  CityInterfaceState *ui = city->game->ui;
  CityStartupMenuState *pending = ui->startup_menu;
  if (pending == NULL || token != pending->token || pending->answered) return false;
  switch (choice) {
  case CITY_STARTUP_LOAD_CITY: case CITY_STARTUP_NEW_CITY: case CITY_STARTUP_EDIT_MAP:
  case CITY_STARTUP_LOAD_SCENARIO: case CITY_STARTUP_QUIT:
    return EndCityStartupDialog(city, pending->dialog->window_id, choice);
  default: return false;
  }
}
bool CityStartupMenuServiceAvailable(City *city) {
  CityInterfaceState *ui = city->game->ui; return ui->startup_desktop != NULL; }

bool BeginCityStartupMenu(City *city, CityStartupMenuState *pending, CityStartupMenuDialog *dialog) {
  CityInterfaceState *ui = city->game->ui;
  if (!CityStartupMenuServiceAvailable(city)) {
    return false;
  }
  dialog->window_id = RegisterCityResourceOwner(city, dialog, CITY_DIALOG_RESOURCE_STARTUP_MENU);
  if (!dialog->window_id) return false;
  pending->parent = ui->startup_menu;
  pending->dialog = dialog;
  pending->choice = -1;
  if (ui->startup_token == INT32_MAX) ui->startup_token = 0;
  pending->token = ++ui->startup_token;
  ui->startup_menu = pending;
  ui->startup_waiting = true;
  // The journal captures entry before original OnInit clears the command flag.
  NotifyStartupCheckpoint(city, NATIVE_STARTUP_CHECKPOINT_ENTER, pending->token, 0);
  InitializeCityStartupMenuDialog(city, dialog);
  return GetGameError(city->game) == 0;
}

int FinishCityStartupMenu(City *city, CityStartupMenuState *pending) {
  UnregisterCityResourceOwner(city, pending->dialog->window_id);
  pending->dialog->window_id = 0;
  NotifyStartupCheckpoint(city, NATIVE_STARTUP_CHECKPOINT_LEAVE, pending->token, pending->choice);
  city->game->ui->startup_menu = pending->parent;
  NotifyStartupState(city);
  return pending->choice;
}

bool OpenCityStartupMenu(City *city) {
  if (city->game->ui->startup_menu) return true;
  CityStartupMenuState *pending = calloc(1, sizeof(*pending));
  if (!pending) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  pending->detached = true;
  pending->owned_dialog.resource_id = CITY_DIALOG_RESOURCE_STARTUP_MENU;
  if (!BeginCityStartupMenu(city, pending, &pending->owned_dialog)) {
    if (city->game->ui->startup_menu == pending) FinishCityStartupMenu(city, pending);
    free(pending);
    return false;
  }
  city->drawing->palette.direct_animation = 1;
  return true;
}

bool TakeCityStartupMenuChoice(City *city, int *choice) {
  CityStartupMenuState *pending = city->game->ui->startup_menu;
  if (!pending || !pending->detached) return false;
  if (!pending->answered && CanvasWindow(city, pending->dialog->window_id)) return false;
  *choice = FinishCityStartupMenu(city, pending);
  city->drawing->palette.direct_animation = 0;
  free(pending);
  return true;
}

int ChooseCityStartupMenuDialog(City *city, CityStartupMenuDialog *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_startup_window) return observer->choose_startup_window(city, dialog);
  return -1;
}

bool SetCityStartupMenuPresentation(CityInterfaceState *ui, CityCanvasDesktop *desktop) {
  if (desktop == NULL || ui->startup_menu != NULL || ui->setup_window != NULL ||
      ui->startup_desktop != NULL) return false;
  ui->startup_desktop = desktop;
  memset(ui->startup_windows, 0, sizeof(ui->startup_windows));
  return true;
}
void ClearCityStartupMenuPresentation(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->setup_window) {
    CitySetupWindowState *pending = ui->setup_window;
    if (!pending->callback) return;
    CityCreationCallback *callback = pending->callback;
    void *context = pending->context;
    pending->choice = CITY_DIALOG_CANCEL;
    FinishCitySetupWindow(city, pending);
    ui->terrain_setup_active = false;
    DestroyNewCitySetupDialog(&pending->owned_dialog);
    free(pending->terrain_path);
    free(pending);
    callback(city, NULL, (CityCreationTerrain){0}, context);
  }
  if (ui->startup_menu && !ui->startup_menu->detached) return;
  if (ui->startup_menu) {
    CityStartupMenuState *pending = ui->startup_menu;
    FinishCityStartupMenu(city, pending);
    city->drawing->palette.direct_animation = 0;
    free(pending);
  }
  for (int i = 0; i < CITY_STARTUP_WINDOW_LIMIT; ++i)
    if (ui->startup_windows[i].dialog != NULL) UnregisterCityResourceOwner(city, ui->startup_windows[i].window_id);
  ui->startup_desktop = NULL;
}

static CityCanvasStartupControl *SetupControl(City *city, int id) {
  CityInterfaceState *ui = city->game->ui;
  CityStartupResourceWindow *window = ui->setup_window != NULL ?
      FindStartupResourceWindow(city, ui->setup_window->dialog->base.window_id) : NULL;
  if (window != NULL)
    for (int i = 0; i < window->layout.control_count; ++i)
      if (window->layout.controls[i].id == id) return &window->layout.controls[i];
  return NULL;
}
static bool UpdateCitySetupControls(City *city, bool save) {
  CityInterfaceState *ui = city->game->ui;
  CitySetupWindowState *pending = ui->setup_window;
  CityNewCitySetupDialog *dialog = pending->dialog;
  CityCanvasStartupControl *name = SetupControl(city, CITY_SETUP_NAME);
  if (name == NULL) {
    return false;
  }
  if (save) CopyText(dialog->city_name, sizeof(dialog->city_name), name->text);
  else {
    CopyText(name->text, sizeof(name->text), dialog->city_name);
    name->text_caret = strlen(name->text);
    NotifyStartupState(city);
  }
  int *values[] = {&dialog->era, &dialog->difficulty};
  static const int kFirstControls[] = {CITY_SETUP_ERA_FIRST, CITY_SETUP_DIFFICULTY_FIRST};
  static const int kControlCounts[] = {CITY_SETUP_ERA_COUNT, CITY_SETUP_DIFFICULTY_COUNT};
  for (int group = 0; group < 2; ++group) {
    for (int index = 0; index < kControlCounts[group]; ++index) {
      CityCanvasStartupControl *control = SetupControl(city, kFirstControls[group] + index);
      if (control == NULL) {
        return false;
      }
      if (save) {
        if (control->checked) *values[group] = index;
      } else control->checked = index == *values[group];
    }
  }
  CityCanvasStartupControl *difficulty = SetupControl(city, CITY_SETUP_DIFFICULTY);
  if (difficulty) {
    difficulty->checked = dialog->difficulty;
    CopyText(difficulty->text, sizeof(difficulty->text), difficulty->items[dialog->difficulty].text);
  }
  CityCanvasStartupControl *year = SetupControl(city, CITY_SETUP_YEAR);
  if (year) snprintf(year->text, sizeof(year->text), "1st Jan %d",
      dialog->start_year ? dialog->start_year : 1900 + 50 * dialog->era);
  return true;
}
bool EditCitySetupControl(City *city, int token, int control, int value, const u8 *text, u32 length) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->setup_window == NULL || ui->setup_window->answered || token != ui->setup_window->token) return false;
  if (control == CITY_SETUP_NAME || control == CITY_SETUP_MAYOR) {
    u32 limit = control == CITY_SETUP_MAYOR ? CITY_OVERLAY_NAME_MAX_LENGTH : CITY_SETUP_NAME_LIMIT;
    if (length > limit || (length && text == NULL) || value) return false;
    CityCanvasStartupControl *input = SetupControl(city, control);
    if (input == NULL) return false;
    if (length && (const u8 *)input->text != text) memcpy(input->text, text, length);
    input->text[length] = 0;
    input->text_caret = length;
    if (control == CITY_SETUP_MAYOR) CopyText(ui->terrain_mayor, sizeof(ui->terrain_mayor), input->text);
    CityStartupResourceWindow *window = FindStartupResourceWindow(city, ui->setup_window->dialog->base.window_id);
    window->layout.selected_text = 0;
  } else if (control == CITY_SETUP_YEAR) {
    if (value < 1900 || value > 2050 || length) return false;
    ui->setup_window->dialog->start_year = value;
    ui->setup_window->dialog->era = (value - 1900) / 50;
    for (int i = 0; i < CITY_SETUP_ERA_COUNT; ++i)
      SetupControl(city, CITY_SETUP_ERA_FIRST + i)->checked = i == ui->setup_window->dialog->era;
    snprintf(SetupControl(city, CITY_SETUP_YEAR)->text, CITY_STARTUP_CANVAS_TEXT, "1st Jan %d", value);
  } else {
    int count = control == CITY_SETUP_ERA_FIRST ? CITY_SETUP_ERA_COUNT :
        control == CITY_SETUP_DIFFICULTY_FIRST ? CITY_SETUP_DIFFICULTY_COUNT : 0;
    if (value < 0 || value >= count || length) return false;
    for (int i = 0; i < count; ++i) SetupControl(city, control + i)->checked = i == value;
    if (control == CITY_SETUP_DIFFICULTY_FIRST) {
      ui->setup_window->dialog->difficulty = value;
      CityCanvasStartupControl *dropdown = SetupControl(city, CITY_SETUP_DIFFICULTY);
      dropdown->checked = value;
      CopyText(dropdown->text, sizeof(dropdown->text), dropdown->items[value].text);
    }
    if (control == CITY_SETUP_ERA_FIRST) {
      ui->setup_window->dialog->start_year = 1900 + 50 * value;
      snprintf(SetupControl(city, CITY_SETUP_YEAR)->text, CITY_STARTUP_CANVAS_TEXT, "1st Jan %d", 1900 + 50 * value);
    }
  }
  NotifyStartupState(city);
  return true;
}
static void CloseCityYearQuery(City *city, bool accept) {
  CitySetupWindowState *pending = city->game->ui->setup_window;
  if (!pending || !pending->year_window) return;
  CityStartupResourceWindow *query = FindStartupResourceWindow(city, pending->year_window);
  if (accept) {
    const char *text = query->layout.controls[0].text;
    if (!text[0]) return;
    int year = IntClamp(atoi(text), 1900, 2050);
    EditCitySetupControl(city, pending->token, CITY_SETUP_YEAR, year, NULL, 0);
  }
  UnregisterCityResourceOwner(city, pending->year_window);
  pending->year_window = 0;
  NotifyStartupState(city);
}

bool AnswerCitySetupDialog(City *city, int token, int answer) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->setup_window == NULL || ui->setup_window->answered || token != ui->setup_window->token ||
      (answer != CITY_DIALOG_OK && answer != CITY_DIALOG_CANCEL)) return false;
  if (answer == CITY_DIALOG_OK) {
    if (!UpdateCitySetupControls(city, true) || GetGameError(city->game) != 0) return false;
    if (ui->terrain_setup_active && !ui->setup_window->callback &&
        !GenerateTerrainMap(&ui->terrain_settings, ui->terrain_preview)) {
      SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
  }
  CloseCityYearQuery(city, false);
  ui->setup_window->choice = answer; ui->setup_window->answered = 1;
  bool closed = UnregisterCityResourceOwner(city, ui->setup_window->dialog->base.window_id);
  ui->setup_window->dialog->base.window_id = 0;
  return closed;
}
static void RandomizeCitySetupName(City *city) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->city_name_random) ui->city_name_random = ui->terrain_settings.seed ^ 0x9E3779B9;
  const char *previous = SetupControl(city, CITY_SETUP_NAME)->text;
  char name[CITY_SETUP_NAME_LIMIT + 1];
  if (GenerateCityName(&ui->city_name_random, previous, name, sizeof(name)))
    EditCitySetupControl(city, ui->setup_window->token, CITY_SETUP_NAME, 0, (const u8 *)name, (u32)strlen(name));
}

bool BeginCitySetupWindow(City *city, CitySetupWindowState *pending, CityNewCitySetupDialog *dialog) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->setup_window != NULL || !CityStartupMenuServiceAvailable(city)) {
    return false;
  }
  dialog->base.window_id = RegisterCityResourceOwner(city, &dialog->base, CITY_DIALOG_RESOURCE_NEW_CITY);
  if (!dialog->base.window_id) return false;
  if (ui->startup_token == INT32_MAX) ui->startup_token = 0;
  pending->dialog = dialog;
  pending->token = ++ui->startup_token;
  pending->choice = CITY_DIALOG_CANCEL;
  ui->setup_window = pending;
  ui->startup_waiting = true;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, dialog->base.window_id);
  Window *canvas = CanvasWindow(city, window->window_id);
  canvas->bounds = (CityRect){(ui->startup_desktop->canvas_width - canvas->bounds.right) / 2,
      (ui->startup_desktop->canvas_height - canvas->bounds.bottom) / 2,
      (ui->startup_desktop->canvas_width + canvas->bounds.right) / 2,
      (ui->startup_desktop->canvas_height + canvas->bounds.bottom) / 2};
  window->layout.focused_control = CITY_SETUP_NAME; window->layout.selected_text = 1;
  NotifyStartupCheckpoint(city, CITY_SETUP_CHECKPOINT_ENTER, pending->token, 0);
  UpdateCitySetupControls(city, false);
  if (ui->terrain_setup_active && (!dialog->city_name[0] || !strcmp(dialog->city_name, "New City")))
    RandomizeCitySetupName(city);
  if (ui->terrain_setup_active && !(pending->preserve_terrain ? RefreshTerrainSetup(city) :
      RegenerateCityTerrainSetup(city))) pending->answered = true;
  return GetGameError(city->game) == 0;
}

int FinishCitySetupWindow(City *city, CitySetupWindowState *pending) {
  CloseCityYearQuery(city, false);
  UnregisterCityResourceOwner(city, pending->dialog->base.window_id);
  pending->dialog->base.window_id = 0;
  NotifyStartupCheckpoint(city, CITY_SETUP_CHECKPOINT_LEAVE, pending->token, pending->choice);
  city->game->ui->setup_window = NULL;
  NotifyStartupState(city);
  return pending->choice;
}

bool OpenCityCreationWindow(City *city, bool terrain, CityCreationCallback *callback, void *context) {
  CityInterfaceState *ui = city->game->ui;
  if (ui->setup_window || !callback) return false;
  CitySetupWindowState *pending = calloc(1, sizeof(*pending));
  if (!pending) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return false;
  }
  pending->callback = callback;
  pending->context = context;
  pending->preserve_terrain = !terrain;
  ConstructNewCitySetupDialog(&pending->owned_dialog);
  if (!terrain) {
    ui->terrain_settings.width = city->map_width; ui->terrain_settings.height = city->map_height;
    if (city->name.length) {
      CopyText(pending->owned_dialog.city_name, sizeof(pending->owned_dialog.city_name), CityNameText(&city->name));
      pending->owned_dialog.start_year = city->geography.start_year;
      pending->owned_dialog.era = (city->geography.start_year - 1900) / 50;
      pending->owned_dialog.difficulty = city->indicators.difficulty - CITY_DIFFICULTY_EASY;
    }
    const char *mayor = (const char *)city->overlay_names.entry[0].character;
    if (mayor[0]) CopyText(ui->terrain_mayor, sizeof(ui->terrain_mayor), mayor);
  }
  ui->terrain_setup_active = true;
  if (!BeginCitySetupWindow(city, pending, &pending->owned_dialog)) {
    if (ui->setup_window == pending) FinishCitySetupWindow(city, pending);
    ui->terrain_setup_active = false;
    DestroyNewCitySetupDialog(&pending->owned_dialog);
    free(pending->terrain_path);
    free(pending);
    return false;
  }
  return true;
}

void CompleteCityCreationWindow(City *city) {
  CityInterfaceState *ui = city->game->ui;
  CitySetupWindowState *pending = ui->setup_window;
  if (!pending || !pending->callback || city->game->command_depth) return;
  if (!pending->answered && CanvasWindow(city, pending->dialog->base.window_id)) return;
  bool accepted = FinishCitySetupWindow(city, pending) == CITY_DIALOG_OK;
  ui->terrain_setup_active = false;
  CityCreationOptions options = {.terrain = ui->terrain_settings, .edit_terrain = pending->edit_terrain,
      .start_year = pending->dialog->start_year ? pending->dialog->start_year : 1900 + 50 * pending->dialog->era,
      .difficulty = pending->dialog->difficulty};
  CopyText(options.city_name, sizeof(options.city_name),
      pending->dialog->city_name[0] ? pending->dialog->city_name : "New City");
  CopyText(options.mayor_name, sizeof(options.mayor_name), ui->terrain_mayor);
  CityCreationCallback *callback = pending->callback;
  void *context = pending->context;
  CityCreationTerrain terrain = {pending->terrain_path, pending->preserve_terrain};
  DestroyNewCitySetupDialog(&pending->owned_dialog);
  free(pending);
  callback(city, accepted ? &options : NULL, terrain, context);
  free((char *)terrain.path);
}

int ChooseCitySetupWindow(City *city, CityNewCitySetupDialog *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_setup_window) return observer->choose_setup_window(city, dialog);
  return CITY_DIALOG_UNANSWERED;
}

static bool IsTerrainSetupDropdown(int id) {
  return id == CITY_SETUP_MAP_WIDTH || id == CITY_SETUP_MAP_HEIGHT || id == CITY_SETUP_DIFFICULTY ||
      id == CITY_SETUP_CLIMATE || IsTerrainGroupedDropdown(id) ||
      id == CITY_SETUP_SNOW_COVERAGE || id == CITY_SETUP_DESERT_COVERAGE;
}

static void OpenCitySetupDropdown(City *city, CityStartupResourceWindow *window, int id) {
  CityCanvasStartupControl *control = SetupControl(city, id);
  if (!control || !control->visible || (control->style & CITY_RESOURCE_STYLE_DISABLED)) return;
  CityRect anchor = control->bounds;
  anchor.left += 2; anchor.right += 2; anchor.top += 16; anchor.bottom += 16;
  OpenCityDropdown(city->game->ui->startup_desktop, window->window_id, id, anchor,
      control->items, control->item_count, control->checked, &kCityDropdownGoldColors);
  if (IsTerrainGroupedDropdown(id)) RefreshCityTerrainControls(city->game->ui, &window->layout);
}

static void ActivateCitySetupControl(City *city, CityStartupResourceWindow *window, int control, bool shift) {
  CityInterfaceState *ui = city->game->ui;
  if (!window) return;
  PlayCityButtonBeep(city->game);
  if (window->layout.year_query) {
    switch (control) {
      case CITY_DIALOG_OK: CloseCityYearQuery(city, true); break;
      case CITY_DIALOG_CANCEL: CloseCityYearQuery(city, false); break;
      case CITY_SETUP_YEAR_DEFAULT:
        CopyText(window->layout.controls[0].text, sizeof(window->layout.controls[0].text), "1900"); break;
      case CITY_SETUP_YEAR_CLEAR: window->layout.controls[0].text[0] = 0; break;
    }
    if (control == CITY_SETUP_YEAR_DEFAULT || control == CITY_SETUP_YEAR_CLEAR) {
      window->layout.focused_control = CITY_SETUP_YEAR_INPUT;
      window->layout.selected_text = 0;
      window->layout.controls[0].text_caret = strlen(window->layout.controls[0].text);
    }
    NotifyStartupState(city);
    return;
  }
  if (control == CITY_SETUP_RANDOM_NAME) {
    RandomizeCitySetupName(city);
    window->layout.focused_control = CITY_SETUP_NAME;
    window->layout.selected_text = 1;
    window->layout.caret_visible = true;
    window->layout.caret_blink_at = 0;
    SetupControl(city, CITY_SETUP_NAME)->text_scroll = 0;
    NotifyStartupState(city);
    return;
  }
  if (control == CITY_SETUP_LOAD_MAP) {
    OpenCityTerrainFile(city);
    return;
  }
  if (ui->terrain_setup_active && control == CITY_SETUP_EDIT_TERRAIN) {
    ui->setup_window->edit_terrain = true;
    if (!AnswerCitySetupDialog(city, ui->setup_window->token, CITY_DIALOG_OK))
      ui->setup_window->edit_terrain = false;
    return;
  }
  if (ui->terrain_setup_active && control == CITY_SETUP_REGENERATE) {
    RegenerateCityTerrainSetup(city);
    return;
  }
  if (control == CITY_SETUP_YEAR) {
    if (ui->setup_window->year_window) CloseCityYearQuery(city, false);
    ui->setup_window->year_window = RegisterCityResourceOwner(city,
        &ui->setup_window->dialog->base, CITY_SETUP_YEAR_QUERY_RESOURCE);
    if (ui->setup_window->year_window) CenterCityStartupWindow(city, ui->setup_window->year_window);
    return;
  }
  if (control == CITY_SETUP_YEAR_PREVIOUS || control == CITY_SETUP_YEAR_NEXT) {
    CityNewCitySetupDialog *dialog = ui->setup_window->dialog;
    int year = dialog->start_year ? dialog->start_year : 1900 + 50 * dialog->era;
    int step = shift ? 1 : 10;
    year = IntClamp(year + (control == CITY_SETUP_YEAR_NEXT ? step : -step), 1900, 2050);
    EditCitySetupControl(city, ui->setup_window->token, CITY_SETUP_YEAR, year, NULL, 0);
  } else if (IsTerrainSetupDropdown(control)) {
    OpenCitySetupDropdown(city, window, control);
  } else if (control == CITY_DIALOG_CANCEL) AnswerCitySetupDialog(city, ui->setup_window->token, CITY_DIALOG_CANCEL);
  else if (control == CITY_DIALOG_OK) AnswerCitySetupDialog(city, ui->setup_window->token, CITY_DIALOG_OK);
  else if (control >= CITY_SETUP_ERA_FIRST && control < CITY_SETUP_ERA_FIRST + CITY_SETUP_ERA_COUNT)
    EditCitySetupControl(city, ui->setup_window->token, CITY_SETUP_ERA_FIRST, control - CITY_SETUP_ERA_FIRST, NULL, 0);
  else if (control >= CITY_SETUP_DIFFICULTY_FIRST &&
      control < CITY_SETUP_DIFFICULTY_FIRST + CITY_SETUP_DIFFICULTY_COUNT)
    EditCitySetupControl(city, ui->setup_window->token, CITY_SETUP_DIFFICULTY_FIRST,
        control - CITY_SETUP_DIFFICULTY_FIRST, NULL, 0);
}

static void RefreshStartupTextCaret(City *city, CityStartupResourceWindow *window,
    CityCanvasStartupControl *input) {
  window->layout.caret_visible = true;
  window->layout.caret_blink_at = 0;
  Window *canvas = CanvasWindow(city, window->window_id);
  CityRect bounds = input->bounds;
  bounds.left += canvas->bounds.left + 2; bounds.right += canvas->bounds.left + 2;
  bounds.top += canvas->bounds.top + 16; bounds.bottom += canvas->bounds.top + 16;
  Sc2kDirtyMarkRect(&city->game->ui->startup_desktop->dirty, bounds);
}

static void ApplyStartupTextEdit(City *city, CityStartupResourceWindow *window,
    CityCanvasStartupControl *input, const char *text, int caret, int selected) {
  CityInterfaceState *ui = city->game->ui;
  Window *canvas = CanvasWindow(city, window->window_id);
  CityRect bounds = input->bounds;
  bounds.left += canvas->bounds.left + 2; bounds.right += canvas->bounds.left + 2;
  bounds.top += canvas->bounds.top + 16; bounds.bottom += canvas->bounds.top + 16;
  input->text_scroll = CityTextBoxScroll(&city->game->resources->interface_font,
      bounds, input->text, input->text_caret, input->text_scroll);
  if (strcmp(input->text, text)) {
    if (window->layout.year_query) CopyText(input->text, sizeof(input->text), text);
    else
      EditCitySetupControl(city, ui->setup_window->token, input->id, 0, (const u8 *)text, strlen(text));
  }
  input->text_caret = caret;
  input->text_scroll = CityTextBoxScroll(&city->game->resources->interface_font,
      bounds, input->text, caret, input->text_scroll);
  window->layout.selected_text = selected;
  RefreshStartupTextCaret(city, window, input);
}

bool NativeStartupMenuWaiting(City *city) {
  CityInterfaceState *ui = city->game->ui;
  return (ui->startup_menu != NULL && !ui->startup_menu->answered) ||
      (ui->setup_window != NULL && !ui->setup_window->answered);
}
static void SubmitStartupChoice(City *city, int choice) {
  CityInterfaceState *ui = city->game->ui;
  PlayCityButtonBeep(city->game);
  AnswerCityStartupMenuDialog(city, ui->startup_menu->token, choice);
}
static int ChoiceForControl(int control) {
  switch (control) {
    case CITY_STARTUP_CONTROL_LOAD_CITY: return CITY_STARTUP_LOAD_CITY;
    case CITY_STARTUP_CONTROL_NEW_CITY: return CITY_STARTUP_NEW_CITY;
    case CITY_STARTUP_CONTROL_EDIT_MAP: return CITY_STARTUP_EDIT_MAP;
    case CITY_STARTUP_CONTROL_LOAD_SCENARIO: return CITY_STARTUP_LOAD_SCENARIO;
    case CITY_STARTUP_CONTROL_QUIT: return CITY_STARTUP_QUIT;
    default: return 0;
  }
}

static void OpenCityWebsite(Game *game) {
  PlayCityButtonBeep(game);
  if (!Sc2kSDLOpenURL("https://www.opensc2k.com"))
    Sc2kCanvasDesktopOpenNotice(&game->ui->desktop, (CityRect){40, 45, 540, 180}, "OpenSC2k Website",
        "Could not open your web browser. Visit https://www.opensc2k.com.", CITY_MESSAGE_ERROR | CITY_MESSAGE_OK);
}

static void ActivateStartupMenuButton(Game *game, int control) {
  switch (control) {
    case CITY_STARTUP_CONTROL_WEBSITE: OpenCityWebsite(game); break;
    case CITY_STARTUP_CONTROL_SETTINGS: OpenCityGameOptions(game); break;
    case CITY_STARTUP_CONTROL_JUKEBOX: OpenCityMusicWindow(game, 0); break;
    default: SubmitStartupChoice(game->city, ChoiceForControl(control)); break;
  }
}

void InitializeCitySetupName(CityNewCitySetupDialog *dialog) {
  CopyText(dialog->city_name, sizeof(dialog->city_name), "New City");
}
void CaptureCitySetupName(City *city, CityNewCitySetupDialog *dialog) {
  const CityName *name = &city->name;
  SetCityNameText(city->game, &dialog->previous_name, name->length, (const u8 *)CityNameText(name));
}
void ReleaseCitySetupName(CityNewCitySetupDialog *dialog) {
  FreeCityNameStorage(&dialog->previous_name);
}
bool RegisterCityStartupWindow(City *city, CityStartupWindow *window, u32 resource) {
  if (window == NULL || window->window_id != 0) return false;
  window->window_id = RegisterCityResourceOwner(city, window, resource);
  return window->window_id != 0;
}
bool UnregisterCityStartupWindow(City *city, CityStartupWindow *window) {
  if (window == NULL) return false;
  bool closed = UnregisterCityResourceOwner(city, window->window_id);
  if (closed) window->window_id = 0;
  return closed;
}

// Setup

// Initialize setup choices and their default name.
CityNewCitySetupDialog *ConstructNewCitySetupDialog(CityNewCitySetupDialog *dialog) {
  dialog->base = (CityStartupWindow){.resource_id = CITY_DIALOG_RESOURCE_NEW_CITY};
  dialog->city_name[0] = 0;
  dialog->previous_name = (CityName){0};
  dialog->era = CITY_SETUP_ERA_1900;
  dialog->start_year = 0;
  dialog->difficulty = CITY_SETUP_DIFFICULTY_EASY;
  InitializeCitySetupName(dialog);
  return dialog;
}

void DestroyNewCitySetupDialog(CityNewCitySetupDialog *dialog) {
  ReleaseCitySetupName(dialog);
  dialog->base.window_id = 0;
}

int ChooseNewCitySetupDialog(City *city, CityNewCitySetupDialog *dialog) {
  return ChooseCitySetupWindow(city, dialog);
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool StartupWindowProcedure(Game *game, const Window *canvas_window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Sc2kCanvasDrawStartupWindow(game, event->canvas, canvas_window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, canvas_window->window_id);
  if (!window || !ui->startup_menu || ui->startup_menu->answered || window->dialog != ui->startup_menu->dialog)
    return false;
  const CanvasSDLEvent *input = event->input;
  int x = event->x, y = event->y;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    Window *canvas = CanvasWindow(city, window->window_id);
    if (!window->layout.main_menu && x >= canvas->bounds.left && x < canvas->bounds.left + 11 &&
        y >= canvas->bounds.top && y < canvas->bounds.top + 14) {
      SubmitStartupChoice(city, CITY_STARTUP_NEW_CITY); return true; // Original framework cancel result2.
    }
    int control = Sc2kCanvasStartupControlAt(&window->layout, canvas, x, y);
    if (control != CITY_STARTUP_CONTROL_WEBSITE && control != CITY_STARTUP_CONTROL_SETTINGS &&
        control != CITY_STARTUP_CONTROL_JUKEBOX && ChoiceForControl(control) == 0) return window->layout.main_menu;
    window->layout.focused_control = control;
    window->layout.pressed_control = control; NotifyStartupState(city);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_UP) {
    if (!window->layout.pressed_control) return false;
    int control = window->layout.pressed_control;
    window->layout.pressed_control = 0;
    int hit = Sc2kCanvasStartupControlAt(&window->layout, CanvasWindow(city, window->window_id), x, y);
    NotifyStartupState(city);
    if (ui->startup_menu && ui->startup_menu->dialog == window->dialog && hit == control)
      ActivateStartupMenuButton(game, control);
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN && input->key != CITY_KEY_NONE) {
    if (input->key == CITY_KEY_UP || input->key == CITY_KEY_DOWN || input->key == CITY_KEY_TAB) {
      int selected = 0;
      for (int i = 0; i < window->layout.control_count; ++i)
        if (window->layout.controls[i].id == window->layout.focused_control) selected = i;
      int count = window->layout.control_count;
      selected = (selected + (input->key == CITY_KEY_UP ? count - 1 : 1)) % count;
      window->layout.focused_control = window->layout.controls[selected].id;
      NotifyStartupState(city);
    } else if (input->key == CITY_KEY_RETURN) {
      ActivateStartupMenuButton(game, window->layout.focused_control);
    } else if (input->key == CITY_KEY_ESCAPE)
      SubmitStartupChoice(city, CITY_STARTUP_NEW_CITY);
    return true;
  }
  return false;
}

// Setup, terrain controls and year queries share resource controls, but never
// redispatch through the startup menu or infer their target from the window stack.
bool SetupWindowProcedure(Game *game, const Window *canvas_window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Sc2kCanvasDrawStartupWindow(game, event->canvas, canvas_window);
    return true;
  }
  City *city = game->city;
  CityInterfaceState *ui = game->ui;
  CityStartupResourceWindow *window = FindStartupResourceWindow(city, canvas_window->window_id);
  if (!window || !ui->setup_window || ui->setup_window->answered) return false;
  if (event->kind == WINDOW_DROPDOWN) {
    const CityDropdownSelection *selection = event->selection;
    if (selection->control == CITY_SETUP_DIFFICULTY) {
      EditCitySetupControl(city, ui->setup_window->token, CITY_SETUP_DIFFICULTY_FIRST, selection->value, NULL, 0);
      return true;
    }
    if (!ui->terrain_setup_active) return true;
    if (selection->control == CITY_SETUP_VARIETY) {
      if (selection->value < 0 ||
          selection->value >= kTerrainSmoothnessFirst + CITY_TERRAIN_SMOOTHNESS_COUNT) return true;
      CityTerrainSettings previous = ui->terrain_settings;
      if (selection->value < kTerrainSmoothnessFirst) ui->terrain_settings.variety = selection->value;
      else ui->terrain_settings.smoothness = selection->value - kTerrainSmoothnessFirst;
      if (!GenerateCityTerrainPreview(city)) ui->terrain_settings = previous;
      RefreshCityTerrainControls(ui, &window->layout);
      NotifyStartupState(city);
      return true;
    }
    if (IsTerrainFeatureDropdown(selection->control)) {
      CityCanvasStartupControl *control = SetupControl(city, selection->control);
      if (!control || !control->visible) return true;
      CityTerrainSettings previous = ui->terrain_settings;
      if (!SelectCityTerrainFeature(&ui->terrain_settings, control, selection->value)) return true;
      if (!GenerateCityTerrainPreview(city)) ui->terrain_settings = previous;
      RefreshCityTerrainControls(ui, &window->layout);
      NotifyStartupState(city);
      return true;
    }
    if (selection->control == CITY_SETUP_CLIMATE || selection->control == CITY_SETUP_SNOW_COVERAGE ||
        selection->control == CITY_SETUP_DESERT_COVERAGE) {
      CityTerrainSettings previous = ui->terrain_settings;
      CityTerrainSettings *settings = &ui->terrain_settings;
      if (selection->control == CITY_SETUP_CLIMATE) {
        if ((u32)selection->value >= CITY_TERRAIN_CLIMATE_COUNT) return true;
        settings->climate = selection->value;
      } else {
        CityCanvasStartupControl *control = SetupControl(city, selection->control);
        if (!control || (control->style & CITY_RESOURCE_STYLE_DISABLED) ||
            selection->value < 0 || selection->value > 100 || selection->value % 10) return true;
        if (selection->control == CITY_SETUP_SNOW_COVERAGE) settings->snow_coverage = selection->value;
        else settings->desert_coverage = selection->value;
      }
      // The most recently chosen coverage wins when the two targets overlap.
      if (settings->climate == CITY_TERRAIN_MIXED && settings->snow_coverage + settings->desert_coverage > 100) {
        if (selection->control == CITY_SETUP_DESERT_COVERAGE) settings->snow_coverage = 100 - settings->desert_coverage;
        else settings->desert_coverage = 100 - settings->snow_coverage;
      }
      if (!GenerateCityTerrainPreview(city)) ui->terrain_settings = previous;
      RefreshCityTerrainControls(ui, &window->layout);
      NotifyStartupState(city);
      return true;
    }
    int *value = selection->control == CITY_SETUP_MAP_WIDTH ? &ui->terrain_settings.width :
        selection->control == CITY_SETUP_MAP_HEIGHT ? &ui->terrain_settings.height : NULL;
    if (!value || !IsSupportedMapAxis(selection->value)) return true;
    int previous = *value;
    *value = selection->value;
    if (!GenerateCityTerrainPreview(city)) *value = previous;
    RefreshCityTerrainControls(ui, &window->layout);
    NotifyStartupState(city);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *sample = event->input;
  int x = event->x, y = event->y;
  switch (sample->kind) {
  case CANVAS_SDL_POINTER_DOWN: {
    if (sample->button != CANVAS_SDL_BUTTON_LEFT) return false;
    Window *canvas = CanvasWindow(city, window->window_id);
    if (!window->layout.main_menu && x >= canvas->bounds.left && x < canvas->bounds.left + 11 &&
        y >= canvas->bounds.top && y < canvas->bounds.top + 14) {
      if (window->layout.year_query) CloseCityYearQuery(city, false);
      else AnswerCitySetupDialog(city, ui->setup_window->token, CITY_DIALOG_CANCEL);
      return true;
    }
    int hit = Sc2kCanvasStartupControlAt(&window->layout, canvas, x, y);
    if (!hit) return false;
    window->layout.focused_control = hit;
    if (hit == CITY_SETUP_NAME || hit == CITY_SETUP_MAYOR || hit == CITY_SETUP_YEAR_INPUT) {
      CityCanvasStartupControl *input = SetupControl(city, hit);
      if (window->layout.year_query) input = &window->layout.controls[0];
      CityRect bounds = input->bounds;
      bounds.left += canvas->bounds.left + 2; bounds.right += canvas->bounds.left + 2;
      bounds.top += canvas->bounds.top + 16; bounds.bottom += canvas->bounds.top + 16;
      input->text_caret = CityTextBoxCaretAt(&city->game->resources->interface_font,
          bounds, input->text, input->text_caret, &input->text_scroll, x);
      window->layout.selected_text = 0;
      window->layout.caret_visible = true;
      window->layout.caret_blink_at = 0;
    } else if (hit == CITY_SETUP_YEAR) {
      ActivateCitySetupControl(city, window, hit, false);
      return true;
    } else if (hit == CITY_SETUP_YEAR_PREVIOUS || hit == CITY_SETUP_YEAR_NEXT) {
      BeginCityButtonRepeat(game, canvas_window, event);
      ActivateCitySetupControl(city, window, hit, sample->shift);
      return true;
    } else if (IsTerrainSetupDropdown(hit)) {
      OpenCitySetupDropdown(city, window, hit);
      NotifyStartupState(city);
      return true;
    } else window->layout.pressed_control = hit;
    if (ui->terrain_setup_active && hit >= CITY_SETUP_HILLS && hit <= CITY_SETUP_TREES)
      MoveCityTerrainSetupPointer(city, canvas_window, x);
    NotifyStartupState(city);
    return true;
  }
  case CANVAS_SDL_POINTER_UP: {
    bool shift = sample->shift;
    if (!window->layout.pressed_control) return false;
    int control = window->layout.pressed_control;
    window->layout.pressed_control = 0;
    int hit = Sc2kCanvasStartupControlAt(&window->layout, CanvasWindow(city, window->window_id), x, y);
    NotifyStartupState(city);
    if (hit == control) ActivateCitySetupControl(city, window, control, shift);
    return true;
  }
  case CANVAS_SDL_POINTER_MOVE: return MoveCityTerrainSetupPointer(city, canvas_window, x);
  case CANVAS_SDL_KEY_DOWN: {
    CityHotkey key = sample->key;
    if (key == CITY_KEY_NONE) return false;
    int field = window->layout.focused_control;
    CityCanvasStartupControl *input = window->layout.year_query && field == CITY_SETUP_YEAR_INPUT ?
        &window->layout.controls[0] :
        field == CITY_SETUP_NAME || field == CITY_SETUP_MAYOR ? SetupControl(city, field) : NULL;
    if (input) {
      char text[CITY_STARTUP_CANVAS_TEXT];
      CopyText(text, sizeof(text), input->text);
      int caret = input->text_caret, selected = window->layout.selected_text;
      if (HandleCityTextEditKey(text, &caret, &selected, key)) {
        ApplyStartupTextEdit(city, window, input, text, caret, selected);
        return true;
      }
    }
    bool shift = (key & CITY_HOTKEY_SHIFT) != 0;
    key &= ~CITY_HOTKEY_SHIFT;
    if (window->layout.year_query) {
      if (key == CITY_KEY_ESCAPE) CloseCityYearQuery(city, false);
      else if (key == CITY_KEY_RETURN) {
        if (field == CITY_SETUP_YEAR_INPUT) CloseCityYearQuery(city, true);
        else ActivateCitySetupControl(city, window, field, shift);
      } else if (key == CITY_KEY_TAB) {
        for (int i = 0; i < window->layout.control_count; ++i)
          if (window->layout.controls[i].id == field) {
            window->layout.focused_control = window->layout.controls[(i + 1) % window->layout.control_count].id;
            break;
          }
        window->layout.selected_text = window->layout.focused_control == CITY_SETUP_YEAR_INPUT;
        if (window->layout.selected_text)
          window->layout.controls[0].text_caret = strlen(window->layout.controls[0].text);
      }
      NotifyStartupState(city);
    } else if (key == CITY_KEY_RETURN && (IsTerrainSetupDropdown(field) || field == CITY_SETUP_YEAR)) {
      ActivateCitySetupControl(city, window, field, shift);
    } else if (key == CITY_KEY_RETURN || key == CITY_KEY_ESCAPE) {
      AnswerCitySetupDialog(city, ui->setup_window->token,
          key == CITY_KEY_RETURN ? CITY_DIALOG_OK : CITY_DIALOG_CANCEL);
    } else if (key == CITY_KEY_TAB) {
      CityCanvasStartupWindow *layout = &window->layout;
      int current = 0;
      CityCanvasStartupControl *focused = NULL;
      for (int i = 0; i < layout->control_count; i++)
        if (layout->controls[i].id == field) {
          current = i;
          focused = &layout->controls[i];
        }
      for (int step = 1; step <= layout->control_count; step++) {
        CityCanvasStartupControl *control = &layout->controls[(current + step) % layout->control_count];
        if (control->id && control->control_class && control->visible &&
            !(control->style & CITY_RESOURCE_STYLE_DISABLED)) {
          layout->focused_control = control->id;
          focused = control;
          break;
        }
      }
      layout->selected_text = focused && (focused->id == CITY_SETUP_NAME || focused->id == CITY_SETUP_MAYOR);
      if (layout->selected_text) focused->text_caret = strlen(focused->text);
      NotifyStartupState(city);
    } else if (key == CITY_KEY_LEFT || key == CITY_KEY_RIGHT || key == CITY_KEY_UP || key == CITY_KEY_DOWN) {
      int delta = key == CITY_KEY_LEFT || key == CITY_KEY_UP ? -1 : 1;
      if (IsTerrainSetupDropdown(field)) {
        OpenCitySetupDropdown(city, window, field);
      } else if (field == CITY_SETUP_YEAR || field == CITY_SETUP_YEAR_PREVIOUS || field == CITY_SETUP_YEAR_NEXT) {
        ActivateCitySetupControl(city, window, delta < 0 ? CITY_SETUP_YEAR_PREVIOUS : CITY_SETUP_YEAR_NEXT, shift);
      } else if (ui->terrain_setup_active && field >= CITY_SETUP_HILLS && field <= CITY_SETUP_TREES) {
        int *values[] = {&ui->terrain_settings.hills, &ui->terrain_settings.water, &ui->terrain_settings.trees};
        int *value = values[field - CITY_SETUP_HILLS];
        int next = IntClamp(*value + delta, 0, 47);
        if (next != *value) {
          *value = next;
          GenerateCityTerrainPreview(city);
        }
      } else {
        int first = field >= CITY_SETUP_DIFFICULTY_FIRST &&
            field < CITY_SETUP_DIFFICULTY_FIRST + CITY_SETUP_DIFFICULTY_COUNT ?
            CITY_SETUP_DIFFICULTY_FIRST : CITY_SETUP_ERA_FIRST;
        int count = first == CITY_SETUP_DIFFICULTY_FIRST ? CITY_SETUP_DIFFICULTY_COUNT : CITY_SETUP_ERA_COUNT;
        if (field >= first && field < first + count) {
          int value = 0;
          while (value < count && !SetupControl(city, first + value)->checked) ++value;
          value = (value + count + delta) % count;
          EditCitySetupControl(city, ui->setup_window->token, first, value, NULL, 0);
          window->layout.focused_control = first + value;
        }
      }
    } else if (key == CITY_KEY_SPACE) {
      ActivateCitySetupControl(city, window, field, shift);
    }
    return true;
  }
  case CANVAS_SDL_TEXT_INPUT: {
    const char *text = sample->text;
    int field = window->layout.focused_control;
    CityCanvasStartupControl *input = window->layout.year_query && field == CITY_SETUP_YEAR_INPUT ?
        &window->layout.controls[0] :
        field == CITY_SETUP_NAME || field == CITY_SETUP_MAYOR ? SetupControl(city, field) : NULL;
    if (!text || !input) return true;
    char value[CITY_STARTUP_CANVAS_TEXT];
    CopyText(value, sizeof(value), input->text);
    int caret = input->text_caret, selected = window->layout.selected_text;
    size_t limit = window->layout.year_query ? 4 : field == CITY_SETUP_MAYOR ? CITY_OVERLAY_NAME_MAX_LENGTH :
        CITY_SETUP_NAME_LIMIT;
    InsertCityTextInput(value, limit + 1, &caret, &selected, text, window->layout.year_query);
    ApplyStartupTextEdit(city, window, input, value, caret, selected);
    return true;
  }
  default: return false;
  }
}

// Request a startup choice through the UI dialog boundary. City and UI owners must remain alive for the interaction;
// returns the dialog's raw result without inventing a choice.
static int ChooseStartupMenu(City *city, void *context) {
  return ChooseCityStartupMenuDialog(city, context);
}

// Native canvas choices preserve the original pause and raw result. Requires live UI/Drawing owners; returns the raw
// choice and restores the retained presentation lifetime on return.
int ShowCityStartupMenu(City *city) {
  CityStartupMenuDialog dialog = {CITY_DIALOG_RESOURCE_STARTUP_MENU, 0};
  city->drawing->palette.direct_animation = 1;
  int result = RunPausedCityAction(city, ChooseStartupMenu, &dialog);
  city->drawing->palette.direct_animation = 0;
  return result;
}

const char *CityStartupTooltipAt(const Window *window, int x, int y, char *text, size_t size) {
  const CityCanvasStartupWindow *layout = window->startup_layout;
  if (!layout) return NULL;
  for (int i = layout->control_count - 1; i >= 0; --i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    CityRect b = control->bounds;
    b.left += window->bounds.left + 2; b.right += window->bounds.left + 2;
    b.top += window->bounds.top + 16; b.bottom += window->bounds.top + 16;
    if (!control->visible || !CanvasPointInRect(b, x, y)) continue;
    const char *help = control->tooltip ? control->tooltip : control->text;
    if (control->style & CITY_RESOURCE_STYLE_DISABLED) {
      snprintf(text, size, "%s %s", help,
          control->id == CITY_SETUP_MAP_WIDTH || control->id == CITY_SETUP_MAP_HEIGHT ?
          "Turn off SimCity 2000 compatibility to unlock larger map sizes." :
          control->id == CITY_SETUP_SNOW_COVERAGE ? "Choose Snowy or Mixed terrain to enable this control." :
          control->id == CITY_SETUP_DESERT_COVERAGE ? "Choose Desert or Mixed terrain to enable this control." :
          "Unavailable with the current selections; clear the conflicting option first.");
      return text;
    }
    return help;
  }
  return layout->terrain_setup ?
      "Terrain preview. Large maps use a reduced-size preview; Start City creates the full-size terrain." : NULL;
}
