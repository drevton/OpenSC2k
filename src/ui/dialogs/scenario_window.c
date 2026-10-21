#include "city/engine/command_types.h"
#include "ui/state.h"
#include "game/settings.h"
#include "city/saveload/legacy_text.h"
#include "ui/tools/tools.h"
#include <string.h>

#include "ui/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "game/state.h"
#include "city/state.h"
#include "assets/catalog.h"
#include "ui/startup/services.h"
#include "ui/charts/charts.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/text/text.h"

#include "game/application_internal.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "city/saveload/services.h"
#include "drawing/drawing_internal.h"
#include "ui/dialogs/scenario_internal.h"
#include "ui/startup/startup.h"

#include "game/observation.h"

// Scenario Window
// Scenario picker drawn and operated by the native canvas window system.

i32 GetCityScenarioListTextLength(CityScenarioSelectionDialog *dialog, i32 index) {
  City *city = dialog->city;
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (model != NULL)
    return index >= 0 && (size_t)index < model->count ? (i32)strlen(model->paths[index]) : -1;
  return index == dialog->selected_index ? (i32)strlen(dialog->selected_path) : -1;
}
i32 GetCityScenarioListText(CityScenarioSelectionDialog *dialog, i32 index, char *text) {
  City *city = dialog->city;
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (model != NULL) {
    if (index < 0 || (size_t)index >= model->count) return -1;
    memmove(text, model->paths[index], strlen(model->paths[index]) + 1);
    return (i32)strlen(text);
  }
  if (index != dialog->selected_index) return -1;
  size_t length = strlen(dialog->selected_path);
  memmove(text, dialog->selected_path, length + 1);
  return (i32)length;
}
void CloseCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog, i32 result) {
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (model != NULL) { model->answer = result; return; }
  if (dialog->base.window_id != 0)
    EndCityStartupDialog(city, dialog->base.window_id, result);
}

static Window *ScenarioWindow(const ScenarioListModel *model) {
  return FindCityCanvasWindow(&model->dialog->city->game->ui->desktop, model->canvas_id);
}
static CityRect ScenarioListBounds(const ScenarioListModel *model, const Window *window) {
  CityRect bounds = model->list_bounds;
  bounds.left += window->bounds.left + 2; bounds.right += window->bounds.left + 2;
  bounds.top += window->bounds.top + 16; bounds.bottom += window->bounds.top + 16;
  return bounds;
}
static void DrawScenarioDescription(Canvas *canvas, const char *text) {
  int length = strlen(text), start = 0;
  for (int y = 3; start < length && y < canvas->height; y += 12) {
    char line[769];
    int end = start, last_space = -1;
    while (end < length && end - start < (int)sizeof(line) - 1 && text[end] != '\n') {
      line[end - start] = text[end];
      line[end - start + 1] = 0;
      if (Sc2kCanvasChartTextWidth(line) > canvas->width - 85) break;
      if (text[end] == ' ') last_space = end;
      ++end;
    }
    if (end < length && text[end] != '\n' && last_space > start) end = last_space;
    if (end == start && text[end] != '\n') ++end;
    memcpy(line, text + start, end - start);
    line[end - start] = 0;
    Sc2kCanvasChartText(canvas, 75, y, line, 0xFF000000);
    start = end;
    if (start < length && (text[start] == '\n' || text[start] == ' ')) ++start;
  }
}

static void DrawScenarioRow(ScenarioListModel *model, int index) {
  Canvas row = {model->row_pixels + (size_t)index * model->row_width * CITY_SCENARIO_LIST_ROW_HEIGHT,
      model->row_width, CITY_SCENARIO_LIST_ROW_HEIGHT, model->row_width,
      {0, 0, model->row_width, CITY_SCENARIO_LIST_ROW_HEIGHT}};
  u8 picture[4300] = {0};
  char description[4300] = {0};
  FILE *stream = fopen(model->paths[index], "rb");
  if (stream != NULL) {
    ReadCityPreview(stream, picture, sizeof(picture), description, sizeof(description));
    fclose(stream);
  }
  description[sizeof(description) - 1] = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 65; ++x)
      CanvasStore(&row, x + 3, y + 3, model->palette[picture[4 + y * 66 + x]]);
  DecodeCityScenarioText(description, sizeof(description));
  for (char *p = description; *p; ++p)
    if (*p == '\r' || *p == '\t') *p = ' ';
  DrawScenarioDescription(&row, description);
}

static bool AddScenarioPath(const char *name, const HostFileInfo *info, void *context) {
  ScenarioListModel *model = context;
  size_t length = strlen(name);
  if (!info->regular || length < 4 || name[0] == '.' || strcasecmp(name + length - 4, ".scn")) return true;
  if (model->count == model->capacity) {
    size_t capacity = model->capacity ? model->capacity * 2 : 16;
    void *paths = realloc(model->paths, capacity * sizeof(*model->paths));
    if (!paths) return false;
    model->paths = paths;
    model->capacity = capacity;
  }
  char *path = NULL;
  if (!ResolveFilePath(name, "Scenario", &path)) return false;
  if (!model->callback && strlen(path) >= sizeof(model->dialog->selected_path)) { free(path); return true; }
  size_t index = 0;
  while (index < model->count && strcmp(model->paths[index], path) <= 0) ++index;
  memmove(model->paths + index + 1, model->paths + index, (model->count - index) * sizeof(*model->paths));
  model->paths[index] = path;
  ++model->count;
  return true;
}

static bool LoadScenarioPaths(City *city, ScenarioListModel *model) {
  if (!VisitDirectory("Scenario", AddScenarioPath, model)) return false;
  for (int index = 0; index < 256; ++index) {
    CityPaletteEntry color = city->game->drawing->palette.source.entry[index];
    model->palette[index] = 0xFF000000 | color.red << 16 | color.green << 8 | color.blue;
  }
  return model->count != 0;
}

static i32 ScenarioVisibleRows(const ScenarioListModel *model) {
  i32 count = (model->list_bounds.bottom - model->list_bounds.top - 4) / CITY_SCENARIO_LIST_ROW_HEIGHT;
  return count > 0 ? count : 1;
}
static i32 ScenarioLastScroll(const ScenarioListModel *model) {
  i32 last = (i32)model->count - ScenarioVisibleRows(model);
  return IntMax(last, 0);
}
bool EditCityScenarioSelectionControl(City *city, i32 token, i32 control, i32 value) {
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (model == NULL || model->answer != 0 || model->token != token) return false;
  if (control == CITY_DIALOG_OK && value == 0) AcceptCityScenarioSelection(city, model->dialog);
  else if (control == CITY_DIALOG_CANCEL && value == 0) CancelCityScenarioSelection(city, model->dialog);
  else if (control == CITY_SCENARIO_LIST_CONTROL && value >= 0 && (size_t)value < model->count) {
    i32 previous = model->dialog->selected_index;
    if (previous != value) {
      model->dialog->selected_index = value;
    }
    i32 visible = ScenarioVisibleRows(model);
    model->first_visible = IntMin(model->first_visible, value);
    if (value >= model->first_visible + visible) model->first_visible = value - visible + 1;
  } else if (control == CITY_SCENARIO_LIST_SCROLL && value >= 0 && value <= ScenarioLastScroll(model))
    model->first_visible = value;
  else return false;
  Window *window = ScenarioWindow(model);
  if (window != NULL) Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
  return GetGameError(city->game) == 0;
}
bool NativeScenarioDialogWaiting(City *city) {
  return city->game->ui->scenario_window != NULL && city->game->ui->scenario_window->answer == 0;
}
static bool ScenarioCommand(City *city, i32 control, i32 value) {
  return EditCityScenarioSelectionControl(city, city->game->ui->scenario_window->token, control, value);
}
static void ScenarioScrollTo(City *city, i64 first) {
  i32 last = ScenarioLastScroll(city->game->ui->scenario_window);
  first = IntClamp(first, 0, last);
  if (first != city->game->ui->scenario_window->first_visible)
    ScenarioCommand(city, CITY_SCENARIO_LIST_SCROLL, (i32)first);
}
void DrawCityScenarioSelectionWindow(City *city, Canvas *canvas, const Window *window) {
  ScenarioListModel *model = city->game->ui->scenario_window;
  while (model != NULL && model->canvas_id != window->window_id) model = model->previous;
  Sc2kCanvasDrawStartupWindow(city->game, canvas, window);
  if (model == NULL || model->row_pixels == NULL) return;
  CityRect list = ScenarioListBounds(model, window);
  Sc2kCanvasChartBox(canvas, list, 0xFFFFFFFFu, 0);
  i32 left = list.left + 2, top = list.top + 2;
  for (i32 y = top; y < list.bottom - 2; ++y) {
    if (y < 0 || y >= canvas->height || y < canvas->clip.top || y >= canvas->clip.bottom) continue;
    size_t row = (size_t)model->first_visible + (size_t)(y - top) / CITY_SCENARIO_LIST_ROW_HEIGHT;
    if (row >= model->count) continue;
    const u32 *pixels = model->row_pixels +
        (row * CITY_SCENARIO_LIST_ROW_HEIGHT + (y - top) % CITY_SCENARIO_LIST_ROW_HEIGHT) * model->row_width;
    for (i32 x = left; x < list.right - 18; ++x)
      if (x >= 0 && x < canvas->width && x >= canvas->clip.left && x < canvas->clip.right)
        CanvasStore(canvas, x, y, pixels[x - left] ^
            (row == (size_t)model->dialog->selected_index ? 0x00FFFFFF : 0));
  }
  CityRect track = {list.right - 18, list.top + 18, list.right - 2, list.bottom - 18};
  Sc2kCanvasChartFill(canvas, track, 0xFFE0E0E0u);
  DrawCanvasButtonBox(canvas, track.left, list.top + 2, track.right, track.top, 0xFFC0C0C0, true);
  DrawCanvasButtonBox(canvas, track.left, track.bottom, track.right, list.bottom - 2, 0xFFC0C0C0, true);
  Sc2kCanvasChartText(canvas, track.left + 5, list.top + 4, "^", 0xFF000000u);
  Sc2kCanvasChartText(canvas, track.left + 5, track.bottom + 2, "v", 0xFF000000u);
  i32 height = track.bottom - track.top, visible = ScenarioVisibleRows(model);
  i32 thumb_height = model->count > (size_t)visible ? (i32)((i64)height * visible / model->count) : height;
  thumb_height = IntMax(thumb_height, 12);
  thumb_height = IntMin(thumb_height, height);
  i32 last = ScenarioLastScroll(model);
  i32 thumb_top = track.top + (last > 0 ? (i32)((i64)(height - thumb_height) * model->first_visible / last) : 0);
  Sc2kCanvasChartBox(canvas, (CityRect){track.left,thumb_top,track.right,thumb_top+thumb_height}, 0xFFC0C0C0u, 1);
}
bool BeginCityScenarioSelection(City *city, ScenarioListModel *model, CityScenarioSelectionDialog *dialog) {
  if (city->game->ui->scenario_token == INT32_MAX) return false;
  model->previous = city->game->ui->scenario_window;
  model->dialog = dialog;
  model->token = ++city->game->ui->scenario_token;
  dialog->city = city;
  dialog->selected_index = CITY_SCENARIO_NO_SELECTION;
  if (!RegisterCityStartupWindow(city, &dialog->base, CITY_SCENARIO_SELECTION_RESOURCE)) return false;
  model->canvas_id = dialog->base.window_id;
  Window *window = ScenarioWindow(model);
  if (window != NULL) {
    window->kind = CITY_CANVAS_SCENARIO_LIST;
    window->title = "Scenarios";
    int width = window->bounds.right - window->bounds.left;
    int height = window->bounds.bottom - window->bounds.top;
    int left = IntMax((city->game->ui->desktop.canvas_width - width) / 2, 0);
    int top = IntMax((city->game->ui->desktop.canvas_height - height) / 2, 0);
    window->bounds = (CityRect){left, top, left + width, top + height};
  }
  const CityCanvasStartupWindow *layout = ReadCityStartupWindowLayout(city, model->canvas_id);
  for (i32 index = 0; index < layout->control_count; ++index)
    if (layout->controls[index].id == CITY_SCENARIO_LIST_CONTROL) model->list_bounds = layout->controls[index].bounds;
  model->row_width = model->list_bounds.right - model->list_bounds.left - 20;
  city->game->ui->scenario_window = model;
  bool initialized = LoadScenarioPaths(city, model);
  bool ready = initialized && model->count != 0 && model->row_width > 0 &&
      model->count <= SIZE_MAX / ((size_t)model->row_width * CITY_SCENARIO_LIST_ROW_HEIGHT * sizeof(u32));
  if (ready) {
    size_t pixels = model->count * model->row_width * CITY_SCENARIO_LIST_ROW_HEIGHT;
    model->row_pixels = malloc(pixels * sizeof(*model->row_pixels));
    if (model->row_pixels == NULL) {
      SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE); ready = false;
    } else {
      for (size_t index = 0; index < pixels; ++index) model->row_pixels[index] = 0xFFFFFFFFu;
      for (size_t index = 0; index < model->count; ++index) DrawScenarioRow(model, index);
    }
  }
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  ObserveCityPresentation(city->game, CITY_PRESENTATION_SCENARIO_OPEN, model->token, 0, 0, 0, NULL);

  if (!ready) CancelCityScenarioSelection(city, dialog);
  return true;
}

int FinishCityScenarioSelection(City *city, ScenarioListModel *model) {
  if (!model->answer) CancelCityScenarioSelection(city, model->dialog);
  free(model->row_pixels);
  for (size_t index = 0; index < model->count; ++index) free(model->paths[index]);
  free(model->paths);
  UnregisterCityStartupWindow(city, &model->dialog->base);
  city->game->ui->scenario_window = model->previous;
  ApplicationUpdateDialogPause(city);
  RefreshCityChartPanels(city);
  ObserveCityPresentation(city->game, CITY_PRESENTATION_SCENARIO_CLOSE, model->token, 0, 0, model->answer, NULL);
  return model->answer;
}

bool OpenCityScenarioSelection(City *city, CityFileChoiceCallback *callback, void *context) {
  if (city->game->ui->scenario_window || !callback) return false;
  ScenarioListModel *model = calloc(1, sizeof(*model));
  if (!model) return false;
  model->callback = callback;
  model->context = context;
  ConstructCityScenarioSelectionDialog(&model->owned_dialog, 0);
  if (!BeginCityScenarioSelection(city, model, &model->owned_dialog)) {
    free(model);
    return false;
  }
  city->game->drawing->palette.direct_animation = 1;
  return true;
}

void CompleteCityScenarioSelection(City *city) {
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (!model || !model->callback || city->game->command_depth) return;
  if (!model->answer && ScenarioWindow(model)) return;
  char *path = model->answer == CITY_DIALOG_OK ? DuplicateText(model->paths[model->dialog->selected_index]) : NULL;
  CityFileChoiceCallback *callback = model->callback;
  void *context = model->context;
  FinishCityScenarioSelection(city, model);
  DestroyCityScenarioSelectionDialog(&model->owned_dialog);
  city->game->drawing->palette.direct_animation = 0;
  free(model);
  callback(city, path, context);
  free(path);
}

void ClearCityScenarioSelection(City *city) {
  ScenarioListModel *model = city->game->ui->scenario_window;
  if (!model || !model->callback) return;
  CancelCityScenarioSelection(city, model->dialog);
  CompleteCityScenarioSelection(city);
}

// Retained diagnostic callers use the same windows through an optional adapter.
i32 ChooseCityScenarioDialog(City *city, CityScenarioSelectionDialog *dialog) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_scenario) return observer->choose_scenario(city, dialog);

  return CITY_DIALOG_CANCEL;
}

i32 ShowCityScenarioTextDialog(CityTextMessageDialog *dialog, const char *text) {
  const GameObservation *observer = dialog->city->game->observation;
  if (observer && observer->scenario_text) return observer->scenario_text(dialog, text);

  return CITY_DIALOG_CANCEL;
}

static int ChooseRetainedScenario(City *city, void *context) {
  return ChooseCityScenarioDialog(city, context);
}

int RunCityScenarioSelectionDialog(City *city, CityScenarioSelectionDialog *dialog) {
  int answer = RunPausedCityAction(city, ChooseRetainedScenario, dialog);
  return answer == CITY_DIALOG_NONE ? CITY_DIALOG_CANCEL : answer;
}

// Scenario Dialog

CityScenarioSelectionDialog *ConstructCityScenarioSelectionDialog(CityScenarioSelectionDialog *dialog, u32 parent) {
  memset(dialog, 0, sizeof(*dialog));
  dialog->base.resource_id = CITY_SCENARIO_SELECTION_RESOURCE;
  dialog->selected_index = CITY_SCENARIO_NO_SELECTION;
  return dialog;
}

void DestroyCityScenarioSelectionDialog(CityScenarioSelectionDialog *dialog) {
  // Canvas lifetime and list storage are owned by the UI picker.
}

void CancelCityScenarioSelection(City *city, CityScenarioSelectionDialog *dialog) {
  dialog->selected_index = CITY_SCENARIO_NO_SELECTION;
  CloseCityScenarioSelectionDialog(city, dialog, CITY_DIALOG_CANCEL);
}

void AcceptCityScenarioSelection(City *city, CityScenarioSelectionDialog *dialog) {
  if (dialog->selected_index == CITY_SCENARIO_NO_SELECTION) {
    ShowCityLoadResourceMessage(city, CITY_SCENARIO_SELECT_REQUIRED_RESOURCE, 0, -1);
    return;
  }
  if (!city->game->ui->scenario_window->callback)
    GetCityScenarioListText(dialog, dialog->selected_index, dialog->selected_path);
  CloseCityScenarioSelectionDialog(city, dialog, CITY_DIALOG_OK);
}

int ShowCityTextMessageDialog(City *city, const char *text, int option, u32 parent) {
  if (*text == 0) return 0;
  CityTextMessageDialog dialog = {.city = city};
  return ShowCityScenarioTextDialog(&dialog, text);
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool ScenarioWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityScenarioSelectionWindow(game->city, event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  if (!NativeScenarioDialogWaiting(city) || game->ui->scenario_window->canvas_id != window->window_id) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int x = event->x, y = event->y;
    ScenarioListModel *model = city->game->ui->scenario_window;
    Window *window = ScenarioWindow(model);
    if (window == NULL) { ScenarioCommand(city, CITY_DIALOG_CANCEL, 0); return true; }
    CityRect bounds = window->bounds;
    if (!CanvasPointInRect(bounds, x, y)) return true;
    if (y < bounds.top + 14) {
      if (x >= bounds.right - 14) ScenarioCommand(city, CITY_DIALOG_CANCEL, 0);
      else Sc2kCanvasDesktopPointerDown(&city->game->ui->desktop, x, y);
      return true;
    }
    CityRect list = ScenarioListBounds(model, window);
    if (x >= list.right - 18 && x < list.right - 2 && y >= list.top + 2 && y < list.bottom - 2) {
      i32 step = y < list.top + 18 ? -1 : y >= list.bottom - 18 ? 1 :
          y < (list.top + list.bottom) / 2 ? -ScenarioVisibleRows(model) : ScenarioVisibleRows(model);
      ScenarioScrollTo(city, (i64)model->first_visible + step);
      return true;
    }
    if (x >= list.left + 2 && x < list.right - 18 && y >= list.top + 2 && y < list.bottom - 2) {
      i32 row = model->first_visible + (y - list.top - 2) / CITY_SCENARIO_LIST_ROW_HEIGHT;
      if ((size_t)row < model->count) ScenarioCommand(city, CITY_SCENARIO_LIST_CONTROL, row);
      return true;
    }
    model->pressed_control = Sc2kCanvasStartupControlAt(
        ReadCityStartupWindowLayout(city, model->canvas_id), window, x, y);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_UP) {
    int x = event->x, y = event->y;
    ScenarioListModel *model = city->game->ui->scenario_window;
    Window *window = ScenarioWindow(model);
    i32 pressed = model->pressed_control; model->pressed_control = 0;
    if (window != NULL && pressed != 0 &&
        Sc2kCanvasStartupControlAt(ReadCityStartupWindowLayout(city, model->canvas_id), window, x, y) == pressed &&
        (pressed == CITY_DIALOG_OK || pressed == CITY_DIALOG_CANCEL)) ScenarioCommand(city, pressed, 0);
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key;
    ScenarioListModel *model = city->game->ui->scenario_window;
    if (key == CITY_KEY_ESCAPE) ScenarioCommand(city, CITY_DIALOG_CANCEL, 0);
    else if (key == CITY_KEY_RETURN) ScenarioCommand(city, CITY_DIALOG_OK, 0);
    else if (model->count != 0) {
      i32 index = model->dialog->selected_index, selected = index;
      if (key == CITY_KEY_HOME) selected = 0;
      else if (key == CITY_KEY_END) selected = (i32)model->count - 1;
      else if (key == CITY_KEY_UP) selected = IntMax(index, 0) - 1;
      else if (key == CITY_KEY_DOWN) selected = index + 1;
      else if (key == CITY_KEY_PAGE_UP) selected = IntMax(index, 0) - ScenarioVisibleRows(model);
      else if (key == CITY_KEY_PAGE_DOWN) selected = index < 0 ? 0 : index + ScenarioVisibleRows(model);
      else return true;
      selected = IntMax(selected, 0);
      if ((size_t)selected >= model->count) selected = (i32)model->count - 1;
      if (selected != index) ScenarioCommand(city, CITY_SCENARIO_LIST_CONTROL, selected);
    }
    return true;
  }
  if (input->kind == CANVAS_SDL_SCROLL) {
    int delta = input->y;
    ScenarioListModel *model = city->game->ui->scenario_window;
    model->wheel_remainder += delta;
    i64 rows = model->wheel_remainder / 1024;
    model->wheel_remainder %= 1024;
    ScenarioScrollTo(city, (i64)model->first_visible - rows);
    return true;
  }
  return false;
}
