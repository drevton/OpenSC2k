#include "ui/dialogs/file_dialog.h"

#include "city/saveload/state.h"
#include "base/text.h"
#include "base/utf8.h"
#include "base/file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city/engine/city_name.h"
#include "city/presentation.h"
#include "city/saveload/file.h"
#include "city/saveload/runtime.h"
#include "city/saveload/services.h"
#include "city/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/settings.h"
#include "game/state.h"
#include "ui/windows/window_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/file_browser.h"
#include "ui/dialogs/file_dialog_internal.h"
#include "ui/dialogs/messages.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/windows.h"

// File choices own presentation state; callbacks dispatch the accepted operation.

static void RecordCityFileDialogCheckpoint(City *city, u32 kind, i32 token, i32 answer, const char *path) {
  ObserveCityPresentation(city->game, kind, token, 0, 0, answer, path);
}

struct NativeSaveScope {
  NativeSaveScope *parent;
  NativeCitySaveAttempt *attempt;
  char *selected;
  char *destination;
  i32 accepted_path;
  i32 opened_path;
  u8 picker;
};

void RememberCityFileDialogSize(CityInterfaceState *ui, const NativeFileDialog *dialog) {
  CityFilePresentation *files = &ui->file_dialog;
  const Window *window = FindCityCanvasWindow(files->desktop, dialog->window_id);
  if (!window) return;
  files->browser_width = window->bounds.right - window->bounds.left;
  files->browser_height = window->bounds.bottom - window->bounds.top;
}

bool AnswerCityFileDialog(City *city, i32 token, i32 answer, const u8 *path, u32 length) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  NativeFileDialog *dialog = files->dialog;
  while (dialog != NULL && dialog->token != token) dialog = dialog->parent;
  if (dialog == NULL || dialog->answer || (length && memchr(path, 0, length))) return false;
  if (answer == CITY_DIALOG_OK) {
    char *selected = NULL;
    SetTextN(&selected, (const char *)path, length);
    NormalizeFilePath(selected);
    if (!dialog->browser.open) {
      const char *extension = CityUsesSc2k(city) ? ".sc2k" : ".sc2";
      size_t suffix = strlen(extension);
      if (length < suffix || strcasecmp(selected + length - suffix, extension))
        FormatText(&selected, "%s%s", selected, extension);
    }
    if (dialog->scope) {
      // The normalized choice becomes scope-owned before destination selection can fail.
      free(dialog->scope->destination);
      dialog->scope->destination = selected;
      if (!dialog->browser.open && !SelectCitySaveDestination(city->game->file, dialog->scope->attempt,
          dialog->scope->destination)) return false;
      SetText(&dialog->scope->selected, dialog->scope->destination);
      dialog->scope->accepted_path = !dialog->browser.open;
      dialog->scope->opened_path = dialog->browser.open;
    } else {
      dialog->selected = selected;
    }
  }
  dialog->answer = answer;
  RememberCityFileDialogSize(city->game->ui, dialog);
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->overwrite_id);
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->window_id);
  dialog->overwrite_id = dialog->window_id = 0;
  return true;
}

bool BeginCityFileDialog(City *city, NativeFileDialog *dialog, const CityFilePickerOptions *options) {
  const char *name = options->name, *directory = options->directory, *title = options->title;
  bool open = options->open;
  CityFilePresentation *files = &city->game->ui->file_dialog;
  dialog->parent = files->dialog;
  CityFileBrowser *browser = &dialog->browser;
  browser->open = open;
  browser->focus = open ? 0 : 1;
  browser->selected = browser->last_click_row = -1;
  browser->sort_date = files->browser_width ? files->browser_sort_date : true;
  browser->descending = files->browser_width ? files->browser_descending : true;
  if (files->next_token == 0x7FFFFFFF) files->next_token = 0;
  dialog->token = ++files->next_token;
  browser->directory = DuplicateText(directory);
  browser->home = DuplicateText("");
  CopyText(browser->filename, sizeof(browser->filename), name);
  if (!ReadCityFileBrowserDirectory(browser, browser->directory)) {
    if (!ReadCityFileBrowserDirectory(browser, ".")) {
      ReleaseCityFileBrowser(browser);
      PresentCityFileNotice(city, "The save directory could not be opened.", true);
      return false;
    }
  }
  SetText(&browser->home, browser->directory);
  if (open) browser->filename[0] = 0;
  else if (CityUsesSc2k(city)) {
    size_t length = strlen(browser->filename);
    if (length >= 4 && !strcasecmp(browser->filename + length - 4, ".sc2") &&
        length + 1 < sizeof(browser->filename)) memcpy(browser->filename + length - 4, ".sc2k", 6);
  }
  browser->select_all[1] = 1;
  browser->caret[1] = (int)strlen(browser->filename);
  Window window = {0};
  window.kind = open ? CITY_CANVAS_FILE_OPEN : CITY_CANVAS_FILE_SAVE;
  int left = IntMax(0, (files->desktop->canvas_width - CITY_FILE_BROWSER_WIDTH) / 2);
  int top = IntMax(30, (files->desktop->canvas_height - CITY_FILE_BROWSER_HEIGHT) / 2);
  window.bounds = (CityRect){left, top, left + IntMax(CITY_FILE_BROWSER_WIDTH, files->browser_width),
      top + IntMax(180, files->browser_height ? files->browser_height : CITY_FILE_BROWSER_HEIGHT)};
  window.title = title;
  window.file_browser = browser;
  i32 index = Sc2kCanvasDesktopOpen(files->desktop, window);
  if (index < 0) {
    ReleaseCityFileBrowser(browser);
    return false;
  }
  dialog->window_id = files->desktop->windows[index].window_id;
  files->dialog = dialog;
  RefreshCityChartPanels(city);
  RecordCityFileDialogCheckpoint(city, CITY_PRESENTATION_FILE_PICKER_OPEN, dialog->token, 0, "");
  return true;
}

void FinishCityFileDialog(City *city, NativeFileDialog *dialog) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  CityFileBrowser *browser = &dialog->browser;
  RememberCityFileDialogSize(city->game->ui, dialog);
  files->browser_sort_date = browser->sort_date;
  files->browser_descending = browser->descending;
  RefreshCityChartPanels(city);
  const char *selected = dialog->scope ? dialog->scope->selected : dialog->selected;
  RecordCityFileDialogCheckpoint(city, CITY_PRESENTATION_FILE_PICKER_ANSWER, dialog->token, dialog->answer,
      dialog->answer == CITY_DIALOG_OK ? selected : "");

  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->overwrite_id);
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->window_id);
  files->dialog = dialog->parent;
  ReleaseCityFileBrowser(browser);
  free(dialog->delete_path);
}

// Legacy synchronous entry points are implemented only by the optional observer.
static i32 ChooseRetainedCityFile(City *city, const CityFilePickerOptions *options) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->choose_file) return observer->choose_file(city, options);

  return CITY_DIALOG_CANCEL;
}

bool OpenCityFileDialog(City *city, const CityFilePickerOptions *options) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (!files->desktop || files->dialog || files->prompt || !options->callback) return false;
  NativeFileDialog *dialog = calloc(1, sizeof(*dialog));
  if (!dialog) return false;
  dialog->callback = options->callback;
  dialog->context = options->context;
  bool opened = BeginCityFileDialog(city, dialog, options);
  if (!opened) free(dialog);
  return opened;
}

static bool OpenCitySaveDialog(City *city, CityFileChoiceCallback *callback, void *context) {
  char name[CITY_NAME_LIMIT + sizeof(".sc2")];
  int length = Utf8PrefixBytes(CityNameText(&city->name), CITY_NAME_LIMIT);
  memcpy(name, CityNameText(&city->name), length);
  memcpy(name + length, ".sc2", 5);
  CityPath directory = {0};
  bool directory_ready = GetCitySaveDirectory(city->game, &directory);
  CityFilePickerOptions options = {.name = name, .directory = CityPathText(&directory), .title = "Save Game",
      .callback = callback, .context = context};
  bool opened = directory_ready && OpenCityFileDialog(city, &options);
  FreeCityPathStorage(&directory);
  return opened;
}

bool CityFileDialogWaiting(const CityInterfaceState *ui) {
  const CityFilePresentation *files = &ui->file_dialog;
  const NativeFileDialog *dialog = files->dialog;
  return dialog && dialog->callback && !dialog->answer && FindCityCanvasWindow(files->desktop, dialog->window_id);
}

i32 ChooseCityCanvasOpenFile(City *city, const char *name, const char *directory, const char *title,
    char **selected) {
  NativeSaveScope *scope = NativeSaveDialogBegin(city);
  if (scope == NULL) return CITY_DIALOG_CANCEL;
  scope->picker = 1;
  CityFilePickerOptions options = {.name = name, .directory = directory, .title = title, .open = true};
  i32 answer = ChooseRetainedCityFile(city, &options);
  if (answer == CITY_DIALOG_OK) SetText(selected, scope->selected);
  NativeSaveDialogEnd(city, scope);
  return answer;
}

bool AnswerCityReplacementPrompt(City *city, i32 token, i32 answer) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  NativeReplacementPrompt *prompt = files->prompt;
  while (prompt != NULL && prompt->token != token) prompt = prompt->parent;
  if (prompt == NULL || prompt->answer || (prompt->flags == CITY_PROMPT_YES_NO &&
      answer == CITY_DIALOG_CANCEL)) return false;
  prompt->answer = answer;
  Sc2kCanvasDesktopDismissWindow(files->desktop, prompt->window_id);
  prompt->window_id = 0;
  return true;
}

void SubmitCityFileQuestion(City *city, NativeReplacementPrompt *prompt, i32 answer) {
  if (!AnswerCityReplacementPrompt(city, prompt->token, answer))
    PresentCityFileNotice(city, "The save question could not be answered.", true);
}

bool BeginCityFileQuestion(City *city, NativeReplacementPrompt *prompt, const char *text, u32 flags) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  prompt->flags = flags;
  prompt->parent = files->prompt;
  if (files->next_token == 0x7FFFFFFF) files->next_token = 0;
  prompt->token = ++files->next_token;
  prompt->text = DuplicateText(text);
  Window window = {0};
  window.kind = flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_CANVAS_SAVE_QUESTION : CITY_CANVAS_FILE_QUESTION;
  window.bounds = (CityRect){150, 120, 450, 224};
  window.title = "Save city?";
  window.message = prompt->text;
  i32 index = OpenCityMessageBox(files->desktop, window, CITY_MESSAGE_ERROR |
      (flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_MESSAGE_YES_NO_CANCEL : CITY_MESSAGE_YES_NO));
  if (index < 0) {
    free(prompt->text);
    return false;
  }
  prompt->window_id = files->desktop->windows[index].window_id;
  files->prompt = prompt;
  RefreshCityChartPanels(city);
  RecordCityFileDialogCheckpoint(city, CITY_PRESENTATION_QUESTION_OPEN, prompt->token, 0, text);
  return true;
}

void FinishCityFileQuestion(City *city, NativeReplacementPrompt *prompt) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  RefreshCityChartPanels(city);
  RecordCityFileDialogCheckpoint(city, CITY_PRESENTATION_QUESTION_ANSWER, prompt->token, prompt->answer, prompt->text);
  Sc2kCanvasDesktopDismissWindow(files->desktop, prompt->window_id);
  files->prompt = prompt->parent;
  free(prompt->text);
}

i32 PresentCityReplacementQuestion(City *city, const char *text, u32 flags) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->file_question) return observer->file_question(city, text, flags);

  return CITY_DIALOG_CANCEL;
}

void CompleteCityFileDialogs(City *city) {
  if (city->game->command_depth) return;
  CityFilePresentation *files = &city->game->ui->file_dialog;
  NativeFileDialog *dialog = files->dialog;
  if (!dialog || !dialog->callback || (!dialog->answer && FindCityCanvasWindow(files->desktop, dialog->window_id))) return;
  if (!dialog->answer) dialog->answer = CITY_DIALOG_CANCEL;
  CityFileChoiceCallback *callback = dialog->callback;
  void *context = dialog->context;
  char *selected = dialog->selected;
  FinishCityFileDialog(city, dialog);
  free(dialog);
  callback(city, selected, context);
  free(selected);
}

// Resolve a file error's resource text into a borrowed string or caller-provided fallback buffer. Truncated must
// provide 256 bytes; the returned pointer must not outlive the resource owner or that buffer.
static const char *ReadFileErrorText(u32 resource, char truncated[256]) {
  const char *message = CityNoticeText(resource);
  if (message == NULL)
    return "";
  if (strlen(message) <= 255)
    return message; // Immutable literals outlive every dialog.
  CopyText(truncated, 256, message);
  return truncated;
}

// Native file errors use the host's description rather than stock MFC resource text. Requires attached presentation
// owners; temporary formatted text is released by this function after the notice call returns.
void ShowCityFileException(City *city, u32 resource, const CityFileExceptionState *exception, const char *path) {
  char prefix_storage[256];
  const char *prefix = ReadFileErrorText(resource, prefix_storage);
  char *cause = exception && exception->os_error ? FormatCityErrorReason(strerror(exception->os_error)) : NULL;
  const char *cause_text = cause ? cause : "";
  const char *filename = path != NULL ? path : "";
  size_t size = strlen(prefix) + strlen(filename) + strlen(cause_text) + 3;
  char *message = malloc(size);
  if (message == NULL) {
    free(cause);
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return;
  }
  snprintf(message, size, "%s%s%s%s%s", prefix, *filename ? "\n" : "", filename,
      *cause_text ? "\n" : "", cause_text);
  ReportCityLoadError(city, resource, message);
  free(cause);
  free(message);
}

// Report the current operation's error without repairing or reloading partial City data.
void ReportCityFileError(CityFileRuntime *file, u32 resource, CityFileErrorDetail detail) {
  City *city = file->game->city;
  if (detail == CITY_FILE_ERROR_WITH_CAUSE) {
    ShowCityFileException(city, resource, &file->error, NULL);
  } else {
    char text[256];
    ReportCityLoadError(city, resource, ReadFileErrorText(resource, text));
  }
}

void ReportCitySaveError(CityFileRuntime *file, i32 id, const char *path) {
  City *city = file->game->city;
  CityFileExceptionState caught = {0};
  CityFileExceptionState *exception = id == CITY_NOTICE_ERROR_OPENING_FILE ? &file->error : &caught;
  FillCitySaveFileError(file, exception);
  ShowCityFileException(city, (u32)id, exception, path);
}
const char *ReadCitySaveSelectedPath(const City *city) {
  const CityFilePresentation *files = &city->game->ui->file_dialog;
  return files->scope->selected;
}

void ReportCityLoadError(City *city, u32 resource, const char *message) {
  (void)resource;
  PresentCityFileNotice(city, message, true);
}
void ReportCityFileCommandFailure(City *city) {
  PresentCityFileNotice(city, CityNoticeText(CITY_NOTICE_ACTION_FAILED), true);
}
void ShowCityLoadCommandResource(City *city, u32 resource) {
  PresentCityFileNotice(city, CityNoticeText((i32)resource), true);
}

void ShowCityLoadWarnings(City *city) {
  if (city->load_sloped_zone_tiles) {
    char text[256];
    snprintf(text, sizeof(text), "Warning: removed zoning from %u sloped tiles while importing this city.\n\n"
        "Their terrain and occupants were preserved.", city->load_sloped_zone_tiles);
    PresentCityFileNotice(city, text, false);
  }
  int cleared = city->load_cleared_building_tiles;
  int submerged = city->load_submerged_occupant_tiles;
  if (submerged) {
    char text[256];
    snprintf(text, sizeof(text), "Warning: converted %d unsupported submerged tile%s to water while importing "
        "this city.\n\nBuildings, trees or other occupants on those tiles were removed.",
        submerged, submerged == 1 ? "" : "s");
    PresentCityFileNotice(city, text, false);
  }
  if (!cleared) return;
  char text[256];
  snprintf(text, sizeof(text), "Warning: cleared %d damaged building tile%s while loading this city.\n\n"
      "Their terrain and zoning were preserved. Complete buildings were left intact.",
      cleared, cleared == 1 ? "" : "s");
  PresentCityFileNotice(city, text, false);
}

bool InitializeCityFileDialogPresentation(Game *game, CityCanvasDesktop *desktop, const char *loaded_path) {
  CityFilePresentation *files = &game->ui->file_dialog;
  if (desktop == NULL || loaded_path == NULL || files->scope != NULL) return false;
  files->desktop = desktop;
  SetText(&files->destination, loaded_path);
  if (!InitializeCityFileDirectory(game)) return false;
  return true;
}

void ShutdownCityFileDialogPresentation(City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (files->scope != NULL) return;
  if (files->dialog && files->dialog->callback) {
    NativeFileDialog *dialog = files->dialog;
    dialog->answer = CITY_DIALOG_CANCEL;
    CityFileChoiceCallback *callback = dialog->callback;
    void *context = dialog->context;
    FinishCityFileDialog(city, dialog);
    free(dialog->selected);
    free(dialog);
    callback(city, NULL, context);
  }
  files->desktop = NULL;
  free(files->destination);
  files->destination = NULL;
}

NativeSaveScope *NativeSaveDialogBegin(City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (files->desktop == NULL) return NULL;
  NativeSaveScope *scope = calloc(1, sizeof(*scope));
  if (scope == NULL) return NULL;
  scope->attempt = BeginCitySaveAttempt(city->game->file);
  if (scope->attempt == NULL) { free(scope); return NULL; }
  scope->selected = DuplicateText("");
  scope->destination = DuplicateText("");
  scope->parent = files->scope;
  files->scope = scope;
  if (city->game->city_path.length != 0) {
    SetText(&scope->destination, files->destination);
    SelectCitySaveDestination(city->game->file, scope->attempt, scope->destination);
  }
  return scope;
}

void NativeSaveDialogEnd(City *city, NativeSaveScope *scope) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (scope == NULL || scope != files->scope) return;
  i32 error = EndCitySaveAttempt(city->game->file, scope->attempt);
  if (!error && scope->accepted_path && !strcmp(scope->selected, CityPathText(&city->game->city_path)))
    SetText(&files->destination, scope->destination);
  if (scope->opened_path && city->game->city_active &&
      !strcmp(scope->selected, CityPathText(&city->game->city_path)))
    SetText(&files->destination, scope->destination);
  // The picker returns before its caller commits the loaded path. Preserve
  // that selection in the enclosing command scope so it can commit the host
  // destination after the load finishes.
  if (scope->picker && scope->opened_path && scope->parent != NULL) {
    scope->parent->opened_path = 1;
    SetText(&scope->parent->selected, scope->selected);
    SetText(&scope->parent->destination, scope->destination);
  }
  files->scope = scope->parent;
  // An interleaved SaveAs may change the authoritative path while the outer
  // replacement question is waiting. Its later ordinary Save follows that
  // committed path, not the destination captured before the question opened.
  if (files->scope != NULL && !(scope->picker && scope->opened_path) &&
      !files->scope->accepted_path && city->game->city_path.length != 0) {
    SetText(&files->scope->destination, files->destination);
    SelectCitySaveDestination(city->game->file, files->scope->attempt, files->scope->destination);
  }
  free(scope->selected);
  free(scope->destination);
  free(scope);
}

bool NativeSaveDialogOpen(const City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  return files->dialog != NULL || files->prompt != NULL;
}

// Replacement questions are bound when opened, so browser input never probes them.
bool FileQuestionWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  CityFilePresentation *files = &game->ui->file_dialog;
  NativeReplacementPrompt *prompt = files->prompt;
  while (prompt && prompt->window_id != window->window_id) prompt = prompt->parent;
  if (!prompt) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key;
    if (key == CITY_KEY_ESCAPE)
      SubmitCityFileQuestion(city, prompt,
          prompt->flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_DIALOG_CANCEL : CITY_DIALOG_NO);
    else if (key == CITY_KEY_RETURN || key == 'Y' || key == 'y')
      SubmitCityFileQuestion(city, prompt, CITY_DIALOG_YES);
    else if (key == 'N' || key == 'n') SubmitCityFileQuestion(city, prompt, CITY_DIALOG_NO);
    else return false;
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int x = event->x, y = event->y;
    int answer = CityMessageAnswerAt(window, x, y);
    if (answer) SubmitCityFileQuestion(city, prompt, answer);
    else Sc2kCanvasDesktopPointerDown(files->desktop, x, y);
    return true;
  }
  return false;
}

// Save the current path, or dispatch Save As for an unnamed city. Requires an active City/file context; unchanged host
// path and save-as prompting are handled by their owning command continuation.
void HandleSaveCityCommand(City *city) {
  CityPath *path = &city->game->city_path;
  if (city->game->city_active) {
    if (path->length != 0) {
      SaveCityFile(city, path);
    } else {
      city->game->ui->save_prompt_active = true;
      HandleSaveCityAsCommand(city);
    }
    city->game->ui->save_prompt_active = false;
  }
}

// Derive the name before saving; commit the normalized path only on success. Requires the active file/UI interaction;
// cancellation leaves the host destination uncommitted and cleanup follows the real dialog lifetime.
void HandleSaveCityAsCommand(City *city) {
  CityPath path = {0};
  if (city->game->city_active) {
    char default_name[CITY_NAME_LIMIT + sizeof(".sc2")];
    int length = Utf8PrefixBytes(CityNameText(&city->name), CITY_NAME_LIMIT);
    memcpy(default_name, CityNameText(&city->name), (size_t)length);
    memcpy(default_name + length, ".sc2", 5);
    CityPath directory = {0};
    GetCitySaveDirectory(city->game, &directory);
    CityFilePickerOptions options = {.name = default_name, .directory = CityPathText(&directory),
        .title = "Save city as"};
    int choice = ChooseRetainedCityFile(city, &options);
    if (choice != CITY_DIALOG_CANCEL) {
      const char *text = ReadCitySaveSelectedPath(city);
      AssignCityNameFromSavePath(city, text);
      // Stock asks for the selected path again after changing the city name.
      text = ReadCitySaveSelectedPath(city);
      SetCityPathBytes(city->game, &path, (int)strlen(text), text);
      if (SaveCityFile(city, &path)) {
        SetCityPathBytes(city->game, &city->game->city_path, path.length, CityPathText(&path));
      }
    }
    if (city->game->ui) city->game->ui->save_prompt_active = false;
    FreeCityPathStorage(&directory);
  }
  FreeCityPathStorage(&path);
}

static void SaveCityFileChoice(City *city, const char *path, void *context) {
  (void)context;
  // Restore the live speed before serializing; the temporary picker pause is UI state.
  ApplicationUpdateDialogPause(city);
  if (path) {
    u32 error_serial = city->game->ui->command_error_serial;
    ClearGameError(city->game);
    if (SaveCityToPath(city, (const u8 *)path, (u32)strlen(path), true))
      SetText(&city->game->ui->file_dialog.destination, CityPathText(&city->game->city_path));
    else if (error_serial == city->game->ui->command_error_serial)
      PresentCityFileNotice(city, "The city could not be saved.", true);
  }
  ApplicationUpdateDialogPause(city);
}

void RequestCitySave(Game *game) {
  if (game->observation && game->observation->open_save_dialog) {
    game->observation->open_save_dialog(game->city);
    return;
  }
  if (!game->city_active || NativeSaveDialogOpen(game->city)) return;
  if (!OpenCitySaveDialog(game->city, SaveCityFileChoice, NULL))
    PresentCityFileNotice(game->city, "The save dialog could not be opened.", true);
  ApplicationUpdateDialogPause(game->city);
}

void PresentCityFileNotice(City *city, const char *text, bool error) {
  if (city->game->execution_failed || city->game->close_requested) return;
  CityInterfaceState *ui = city->game->ui;
  int index;
  if (error) {
    const char *summary = ui->active_command ? CityCommandFailureSummary(ui->active_command) : NULL;
    if (!summary && (city->game->idle_state == CITY_IDLE_LOAD_CITY ||
        city->game->idle_state == CITY_IDLE_LOAD_SCENARIO)) summary = "Can't load game...";
    // The error window owns the formatted detail; the source text is borrowed only during this call.
    index = OpenCityCommandErrorWindow(city->game, summary ? summary : "Can't complete file operation...",
        text, true, INVALID_TILE);
    ++ui->command_error_serial;
    if (ui->command_feedback) ui->command_feedback->notice_shown = true;
  } else {
    size_t length = strlen(text) + 1;
    char *message = malloc(length);
    if (!message) { SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE); return; }
    memcpy(message, text, length);
    index = OpenCityMessageBox(&ui->desktop, (Window){.kind = CITY_CANVAS_NOTICE,
        .bounds = {110, 90, 490, 210}, .title = "City file", .message = message, .owned_message = message},
        CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
    if (index < 0) free(message);
  }
  if (index < 0) return;
  ApplicationUpdateDialogPause(city);
}
