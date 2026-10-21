#include "city/engine/command_types.h"
#include "city/presentation.h"
#include "game/application_internal.h"
#include "game/settings.h"
#include "ui/tools/tools.h"
#include "ui/dialogs/file_dialog_internal.h"

#include "ui/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "city/saveload/runtime.h"
#include "game/state.h"
#include "city/engine/initialization.h"
#include "city/state.h"
#include "city/maps/layers.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "city/saveload/services.h"
#include "assets/catalog.h"
#include "drawing/drawing_internal.h"
#include "ui/charts/charts.h"
#include "ui/commands.h"
#include "ui/dialogs/file_browser.h"
#include "ui/state.h"
#include "ui/text/text.h"

#include "game/observation.h"

// File choices own presentation state; callbacks dispatch the accepted operation.

static WindowProcedure FileBrowserConfirmationWindowProcedure;

struct NativeSaveScope {
  NativeSaveScope *parent;
  NativeCitySaveAttempt *attempt;
  char *selected;
  char *destination;
  i32 accepted_path;
  i32 opened_path;
  u8 picker;
};
static Window *FileBrowserWindow(City *city, const NativeFileDialog *dialog) {
  return FindCityCanvasWindow(city->game->ui->file_dialog.desktop, dialog->window_id);
}

static void DirtyFileBrowser(City *city, NativeFileDialog *dialog) {
  Window *window = FileBrowserWindow(city, dialog);
  if (!window) return;
  CityFileBrowser *browser = &dialog->browser;
  int focus = browser->focus == 1 ? 1 : 0;
  CityRect input = CityFileBrowserBounds(window, focus ? CITY_FILE_NAME : CITY_FILE_FILTER);
  browser->text_scroll[focus] = CityTextBoxScroll(&city->game->resources->interface_font, input,
      focus ? browser->filename : browser->filter, browser->caret[focus], browser->text_scroll[focus]);
  browser->first = IntClamp(browser->first, 0, browser->visible_count - CityFileBrowserRows(window));
  Sc2kDirtyMarkRect(&city->game->ui->file_dialog.desktop->dirty, window->bounds);
}

void RememberCityFileDialogSize(City *city, NativeFileDialog *dialog) {
  Window *window = FileBrowserWindow(city, dialog);
  if (!window) return;
  CityFilePresentation *files = &city->game->ui->file_dialog;
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
      SetText(&dialog->scope->destination, selected);
      free(selected);
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
  RememberCityFileDialogSize(city, dialog);
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->overwrite_id);
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->window_id);
  dialog->overwrite_id = dialog->window_id = 0;
  return true;
}

void SubmitCityFileDialog(City *city, NativeFileDialog *dialog, i32 accept, i32 overwrite) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (!accept) {
    if (!AnswerCityFileDialog(city, dialog->token, CITY_DIALOG_CANCEL, (const u8 *)"", 0))
      PresentCityFileNotice(city, "The file choice could not be cancelled.", true);
    return;
  }
  bool absolute = IsPathAbsolute(dialog->browser.filename);
  char *entered = NULL;
  FormatText(&entered, "%s%s", absolute ? "" : dialog->browser.directory, dialog->browser.filename);
  char *destination = DuplicateText(entered);
  free(entered);
  NormalizeFilePath(destination);
  if (!dialog->browser.open) {
    const char *extension = CityUsesSc2k(city) ? ".sc2k" : ".sc2";
    size_t length = strlen(destination), suffix = strlen(extension);
    if (length < suffix || strcasecmp(destination + length - suffix, extension))
      FormatText(&destination, "%s%s", destination, extension);
  }
  char *directory = DuplicateText(destination);
  char *separator = directory + (PathFilename(directory) - directory);
  if (separator > directory) {
    --separator;
    // Keep POSIX, drive and UNC roots intact when checking the parent directory.
    if (separator == directory || (separator == directory + 2 && directory[1] == ':')) separator[1] = 0;
    else *separator = 0;
  } else separator = NULL;
  HostFileInfo info;
  if (separator == NULL || !ReadHostFileInfo(directory[0] ? directory : "/", &info) || !info.directory) {
    PresentCityFileNotice(city, "The selected directory does not exist.", true);
    free(directory);
    free(destination);
    return;
  }
  free(directory);
  if (dialog->browser.open && (!ReadHostFileInfo(destination, &info) || !info.regular)) {
    PresentCityFileNotice(city, "The selected city file does not exist.", true);
    free(destination);
    return;
  }
  if (!dialog->browser.open && !overwrite && ReadHostFileInfo(destination, &info)) {
    if (!info.regular) {
      PresentCityFileNotice(city, "Choose a file name, rather than a directory.", true);
      free(destination);
      return;
    }
    Window window = {0};
    window.kind = CITY_CANVAS_CONFIRM;
    window.procedure = FileBrowserConfirmationWindowProcedure;
    window.bounds = (CityRect){150, 130, 450, 230};
    window.title = "Replace city file?";
    window.message = "This file already exists. Replace it?";
    i32 index = OpenCityMessageBox(files->desktop, window, CITY_MESSAGE_ERROR | CITY_MESSAGE_YES_NO);
    if (index >= 0) dialog->overwrite_id = files->desktop->windows[index].window_id;
    free(destination);
    return;
  }
  if (!AnswerCityFileDialog(city, dialog->token, CITY_DIALOG_OK, (const u8 *)destination, (u32)strlen(destination)))
    PresentCityFileNotice(city, "The file choice could not be accepted.", true);
  free(destination);
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
  RememberCityFileDialogSize(city, dialog);
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

bool OpenCitySaveDialog(City *city, CityFileChoiceCallback *callback, void *context) {
  char name[CITY_NAME_LIMIT + sizeof(".sc2")];
  int length = Utf8PrefixBytes(CityNameText(&city->name), CITY_NAME_LIMIT);
  memcpy(name, CityNameText(&city->name), length);
  memcpy(name + length, ".sc2", 5);
  CityPath directory = {0};
  bool directory_ready = GetCitySaveDirectory(city->game, &directory);
  CityFilePickerOptions options = {name, CityPathText(&directory), "Save Game", false, callback, context};
  bool opened = directory_ready && OpenCityFileDialog(city, &options);
  FreeCityPathStorage(&directory);
  return opened;
}

bool CityFileDialogWaiting(const City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  NativeFileDialog *dialog = files->dialog;
  NativeReplacementPrompt *prompt = files->prompt;
  return (dialog && dialog->callback && !dialog->answer && FindCityCanvasWindow(files->desktop, dialog->window_id)) ||
      (prompt && prompt->callback && !prompt->answer && FindCityCanvasWindow(files->desktop, prompt->window_id));
}

i32 ChooseCityCanvasOpenFile(City *city, const char *name, const char *directory, const char *title,
    char **selected) {
  NativeSaveScope *scope = NativeSaveDialogBegin(city);
  if (scope == NULL) return CITY_DIALOG_CANCEL;
  scope->picker = 1;
  CityFilePickerOptions options = {name, directory, title, true, NULL, NULL};
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
  prompt->message = DuplicateText(text);
  Window window = {0};
  window.kind = flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_CANVAS_SAVE_QUESTION : CITY_CANVAS_FILE_QUESTION;
  window.bounds = (CityRect){150, 120, 450, 224};
  window.title = "Save city?";
  window.message = prompt->message;
  i32 index = OpenCityMessageBox(files->desktop, window, CITY_MESSAGE_ERROR |
      (flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_MESSAGE_YES_NO_CANCEL : CITY_MESSAGE_YES_NO));
  if (index < 0) {
    free(prompt->text);
    free(prompt->message);
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
  free(prompt->message);
}

i32 PresentCityReplacementQuestion(City *city, const char *text, u32 flags) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->file_question) return observer->file_question(city, text, flags);

  return CITY_DIALOG_CANCEL;
}

bool OpenCityFileQuestion(City *city, const char *text, u32 flags,
    CityFileQuestionCallback *callback, void *context) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (!files->desktop || files->prompt || !callback ||
      (flags != CITY_PROMPT_YES_NO_CANCEL && flags != CITY_PROMPT_YES_NO)) return false;
  NativeReplacementPrompt *prompt = calloc(1, sizeof(*prompt));
  if (!prompt) return false;
  prompt->callback = callback;
  prompt->context = context;
  if (BeginCityFileQuestion(city, prompt, text, flags)) return true;
  free(prompt);
  return false;
}

void CompleteCityFileDialogs(City *city) {
  if (city->game->command_depth) return;
  CityFilePresentation *files = &city->game->ui->file_dialog;
  NativeReplacementPrompt *prompt = files->prompt;
  if (prompt && prompt->callback && (prompt->answer || !FindCityCanvasWindow(files->desktop, prompt->window_id))) {
    int answer = prompt->answer ? prompt->answer :
        prompt->flags == CITY_PROMPT_YES_NO_CANCEL ? CITY_DIALOG_CANCEL : CITY_DIALOG_NO;
    prompt->answer = answer;
    CityFileQuestionCallback *callback = prompt->callback;
    void *context = prompt->context;
    FinishCityFileQuestion(city, prompt);
    free(prompt);
    callback(city, answer, context);
    return;
  }
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

void PublishCityTitle(City *city, const char *title) {
  snprintf(city->game->ui->status.city_title, CITY_STATUS_TEXT_CAPACITY, "%s", title);
  SetCityHostWindowTitle(city, title);
}
void ReportCitySaveError(CityFileRuntime *file, i32 id, const char *path) {
  City *city = file->game->city;
  CityFileExceptionState caught = {0};
  CityFileExceptionState *exception = id == CITY_NOTICE_ERROR_OPENING_FILE ? &file->error : &caught;
  FillCitySaveFileError(file, exception);
  ShowCityFileException(city, (u32)id, exception, path);
}
i32 ChooseCitySaveFile(City *city, const char *name, const char *filter,
    const char *directory) {
  CityFilePickerOptions options = {name, directory, "Save city as", false, NULL, NULL};
  return ChooseRetainedCityFile(city, &options);
}
const char *ReadCitySaveSelectedPath(City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  return files->scope->selected;
}

void ReportCityLoadError(City *city, u32 resource, const char *message) {
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

bool InitializeCityFileDialogPresentation(City *city, CityCanvasDesktop *desktop, const char *loaded_path) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (desktop == NULL || loaded_path == NULL || files->scope != NULL) return false;
  files->desktop = desktop;
  SetText(&files->destination, loaded_path);
  if (!InitializeCityFileDirectory(city->game)) return false;
  return true;
}

void ShutdownCityFileDialogPresentation(City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (files->scope != NULL) return;
  if (files->prompt && files->prompt->callback) {
    NativeReplacementPrompt *prompt = files->prompt;
    prompt->answer = CITY_DIALOG_CANCEL;
    CityFileQuestionCallback *callback = prompt->callback;
    void *context = prompt->context;
    FinishCityFileQuestion(city, prompt);
    free(prompt);
    callback(city, CITY_DIALOG_CANCEL, context);
  }
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

static void NavigateFileBrowser(City *city, NativeFileDialog *dialog, const char *directory) {
  if (!ReadCityFileBrowserDirectory(&dialog->browser, directory))
    PresentCityFileNotice(city, "The selected directory could not be opened completely.", true);
  DirtyFileBrowser(city, dialog);
}

static void ActivateFileBrowserSelection(City *city, NativeFileDialog *dialog) {
  CityFileBrowser *browser = &dialog->browser;
  if (browser->selected >= 0 && browser->selected < browser->visible_count) {
    const CityFileBrowserEntry *entry = &browser->entries[browser->visible[browser->selected]];
    if (entry->directory) {
      char *path = NULL;
      FormatText(&path, "%s%s", browser->directory, entry->name);
      NavigateFileBrowser(city, dialog, path);
      free(path);
      return;
    }
  }
  if (browser->filename[0] && (!browser->open || browser->selected >= 0)) SubmitCityFileDialog(city, dialog, 1, 0);
}

static void AnswerFileBrowserConfirmation(City *city, NativeFileDialog *dialog, bool accept) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  Sc2kCanvasDesktopDismissWindow(files->desktop, dialog->overwrite_id);
  dialog->overwrite_id = 0;
  bool deleting = dialog->deleting;
  dialog->deleting = false;
  if (!accept) return;
  if (!deleting) { SubmitCityFileDialog(city, dialog, 1, 1); return; }
  if (!RemoveFile(dialog->delete_path)) {
    PresentCityFileNotice(city, "The selected file could not be deleted.", true);
    return;
  }
  NavigateFileBrowser(city, dialog, dialog->browser.directory);
}

static void ConfirmFileBrowserDelete(City *city, NativeFileDialog *dialog) {
  CityFileBrowser *browser = &dialog->browser;
  if (browser->selected < 0 || browser->selected >= browser->visible_count) return;
  const CityFileBrowserEntry *entry = &browser->entries[browser->visible[browser->selected]];
  if (entry->directory) return;
  char *path = NULL;
  FormatText(&path, "%s%s", browser->directory, entry->name);
  free(dialog->delete_path);
  dialog->delete_path = DuplicateText(path);
  free(path);
  Window window = {0};
  window.kind = CITY_CANVAS_CONFIRM;
  window.procedure = FileBrowserConfirmationWindowProcedure;
  window.bounds = (CityRect){150, 130, 450, 230};
  window.title = "Delete Game";
  window.message = "Delete the selected saved game?";
  CityCanvasDesktop *desktop = city->game->ui->file_dialog.desktop;
  int index = OpenCityMessageBox(desktop, window, CITY_MESSAGE_ERROR | CITY_MESSAGE_YES_NO);
  if (index >= 0) {
    dialog->overwrite_id = desktop->windows[index].window_id;
    dialog->deleting = true;
  }
}

static void EditFileBrowser(City *city, NativeFileDialog *dialog) {
  CityFileBrowser *browser = &dialog->browser;
  if (browser->focus == 0) FilterCityFileBrowser(browser);
  else {
    browser->selected = -1;
    browser->detail_count = 0;
    browser->last_click_row = -1;
  }
  DirtyFileBrowser(city, dialog);
}

bool NativeSaveDialogOpen(const City *city) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  return files->dialog != NULL || files->prompt != NULL;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool FileWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityFileBrowser(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  const CanvasSDLEvent *input = event->input;
  NativeFileDialog *dialog = game->ui->file_dialog.dialog;
  while (dialog && dialog->window_id != window->window_id) dialog = dialog->parent;
  if (!dialog) return false;
  if (dialog->overwrite_id &&
      input->kind != CANVAS_SDL_FOCUS_LOST && input->kind != CANVAS_SDL_POINTER_UP) return true;
  switch (input->kind) {
  case CANVAS_SDL_POINTER_DOWN: {
    if (input->button != CANVAS_SDL_BUTTON_LEFT) return false;
    int x = event->x, y = event->y;
    CityFilePresentation *files = &city->game->ui->file_dialog;
    Window *target = FileBrowserWindow(city, dialog);
    CityFileBrowser *browser = &dialog->browser;
    if (CanvasCloseAt(target->bounds, x, y)) { SubmitCityFileDialog(city, dialog, 0, 0); return true; }
    for (int control = 0; control < CITY_FILE_CONTROL_COUNT; ++control) {
      if (browser->open &&
          (control == CITY_FILE_NAME || control == CITY_FILE_CLEAR_NAME || control == CITY_FILE_DELETE))
        continue;
      CityRect b = CityFileBrowserBounds(target, control);
      if (!CanvasPointInRect(b, x, y)) continue;
      if (control == CITY_FILE_FILTER || control == CITY_FILE_NAME) {
        int field = control == CITY_FILE_NAME;
        browser->focus = field;
        browser->caret[field] = CityTextBoxCaretAt(&city->game->resources->interface_font, b,
            field ? browser->filename : browser->filter, browser->caret[field], &browser->text_scroll[field], x);
        browser->select_all[field] = 0;
      } else if (control == CITY_FILE_CLEAR_FILTER || control == CITY_FILE_CLEAR_NAME) {
        int field = control == CITY_FILE_CLEAR_NAME;
        browser->focus = field;
        (field ? browser->filename : browser->filter)[0] = 0;
        browser->caret[field] = browser->text_scroll[field] = browser->select_all[field] = 0;
        EditFileBrowser(city, dialog);
      } else if (control == CITY_FILE_SORT_NAME || control == CITY_FILE_SORT_DATE) {
        bool date = control == CITY_FILE_SORT_DATE;
        browser->descending = browser->sort_date == date ? !browser->descending : date;
        browser->sort_date = date;
        SortCityFileBrowser(browser);
      } else if (control == CITY_FILE_HOME) NavigateFileBrowser(city, dialog, browser->home);
      else if (control == CITY_FILE_LIST) {
        int first = IntMin(browser->first, IntMax(0, browser->visible_count - CityFileBrowserRows(target)));
        int row = first + (y - b.top) / CITY_FILE_BROWSER_ROW_HEIGHT;
        if (row < browser->visible_count) {
          u32 now = SDL_GetTicks();
          bool activate = browser->last_click_row == row && now - browser->last_click_time < 500;
          SelectCityFileBrowserEntry(browser, row);
          browser->focus = 2;
          browser->last_click_row = row;
          browser->last_click_time = now;
          const CityFileBrowserEntry *entry = &browser->entries[browser->visible[row]];
          if (activate || entry->directory) { ActivateFileBrowserSelection(city, dialog); return true; }
        }
      } else if (control == CITY_FILE_SCROLL_UP) --browser->first;
      else if (control == CITY_FILE_SCROLL_DOWN) ++browser->first;
      else if (control == CITY_FILE_SCROLL_TRACK) {
        CityRect thumb = CityFileBrowserThumb(target);
        if (y >= thumb.top && y < thumb.bottom) {
          browser->dragging_scroll = true;
          browser->scrollbar_grab = y - thumb.top;
        } else browser->first += (y < thumb.top ? -1 : 1) * CityFileBrowserRows(target);
      } else if (control == CITY_FILE_DELETE) { ConfirmFileBrowserDelete(city, dialog); return true; }
      else if (control == CITY_FILE_ACCEPT) {
        if (browser->filename[0] && (!browser->open || (browser->selected >= 0 &&
            !browser->entries[browser->visible[browser->selected]].directory)))
          SubmitCityFileDialog(city, dialog, 1, 0);
        return true;
      }
      else if (control == CITY_FILE_DEFAULT_SIZE) {
        Sc2kDirtyMarkRect(&files->desktop->dirty, target->bounds);
        target->bounds.right = target->bounds.left + CITY_FILE_BROWSER_WIDTH;
        target->bounds.bottom = target->bounds.top + CITY_FILE_BROWSER_HEIGHT;
      } else Sc2kCanvasDesktopPointerDown(files->desktop, x, y);
      DirtyFileBrowser(city, dialog);
      return true;
    }
    Sc2kCanvasDesktopPointerDown(files->desktop, x, y);
    return true;
  }
  case CANVAS_SDL_KEY_DOWN: {
    CityHotkey key = input->key;
    CityFileBrowser *browser = &dialog->browser;
    if (key == CITY_KEY_ESCAPE) SubmitCityFileDialog(city, dialog, 0, 0);
    else if (key == CITY_KEY_RETURN) ActivateFileBrowserSelection(city, dialog);
    else if (key == CITY_KEY_TAB) {
      browser->focus = browser->open ? (browser->focus == 0 ? 2 : 0) : (browser->focus + 1) % 3;
      DirtyFileBrowser(city, dialog);
    } else if (key == CITY_KEY_UP || key == CITY_KEY_DOWN || key == CITY_KEY_PAGE_UP || key == CITY_KEY_PAGE_DOWN) {
      Window *window = FileBrowserWindow(city, dialog);
      int rows = CityFileBrowserRows(window);
      int delta = key == CITY_KEY_UP ? -1 : key == CITY_KEY_DOWN ? 1 : key == CITY_KEY_PAGE_UP ? -rows : rows;
      int selected = browser->selected < 0 ? 0 : browser->selected + delta;
      selected = IntClamp(selected, 0, browser->visible_count - 1);
      if (browser->visible_count) SelectCityFileBrowserEntry(browser, selected);
      browser->first = IntClamp(browser->first, selected - rows + 1, selected);
      browser->focus = 2;
      DirtyFileBrowser(city, dialog);
    } else if (browser->focus <= 1) {
      int field = browser->focus;
      char *text = field ? browser->filename : browser->filter;
      bool changed = key == CITY_KEY_BACKSPACE || key == CITY_KEY_DELETE;
      if (HandleCityTextEditKey(text, &browser->caret[field], &browser->select_all[field], key)) {
        if (changed) EditFileBrowser(city, dialog);
        else DirtyFileBrowser(city, dialog);
      }
    } else if (key == CITY_KEY_BACKSPACE) {
      char *path = NULL;
      FormatText(&path, "%s..", browser->directory);
      NavigateFileBrowser(city, dialog, path);
      free(path);
    }
    // A focused picker owns keyboard input, including keys that are map shortcuts.
    return true;
  }
  case CANVAS_SDL_TEXT_INPUT: {
    const char *text = input->text;
    CityFileBrowser *browser = &dialog->browser;
    if (browser->focus > 1) return true;
    int field = browser->focus;
    InsertCityTextInput(field ? browser->filename : browser->filter,
        field ? sizeof(browser->filename) : sizeof(browser->filter),
        &browser->caret[field], &browser->select_all[field], text, false);
    EditFileBrowser(city, dialog);
    return true;
  }
  case CANVAS_SDL_SCROLL: {
    int delta = input->y;
    if (!dialog->overwrite_id) {
      CityFileBrowser *browser = &dialog->browser;
      browser->wheel_remainder += delta;
      browser->first -= browser->wheel_remainder / 1024 * 3;
      browser->wheel_remainder %= 1024;
      DirtyFileBrowser(city, dialog);
    }
    return true;
  }
  case CANVAS_SDL_POINTER_MOVE:
  case CANVAS_SDL_POINTER_UP:
  case CANVAS_SDL_FOCUS_LOST: {
    CityFileBrowser *browser = &dialog->browser;
    Window *target = FileBrowserWindow(city, dialog);
    int y = event->y;
    bool dragging = input->kind == CANVAS_SDL_POINTER_MOVE;
    if (!browser->dragging_scroll) return false;
    browser->dragging_scroll = dragging;
    if (dragging && target && !dialog->overwrite_id) {
      CityRect track = CityFileBrowserBounds(target, CITY_FILE_SCROLL_TRACK);
      CityScrollbar scrollbar = ReadCityScrollbar(track, browser->visible_count,
          CityFileBrowserRows(target), browser->first, 8);
      browser->first = CityScrollbarPosition(&scrollbar, y, browser->scrollbar_grab, CITY_SCROLLBAR_DRAG);
      DirtyFileBrowser(city, dialog);
    }
    return true;
  }
  default: return false;
  }
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

// File replacement and deletion questions answer their owning browser. Capture
// the answer before closing the question: submitting it can close both windows.
static bool FileBrowserConfirmationWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  NativeFileDialog *dialog = game->ui->file_dialog.dialog;
  while (dialog && dialog->overwrite_id != window->window_id) dialog = dialog->parent;
  if (!dialog) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE)
      AnswerFileBrowserConfirmation(game->city, dialog, input->key == CITY_KEY_RETURN);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int answer = CityMessageAnswerAt(window, event->x, event->y);
    if (answer == CITY_DIALOG_YES || answer == CITY_DIALOG_NO) {
      AnswerFileBrowserConfirmation(game->city, dialog, answer == CITY_DIALOG_YES);
      return true;
    }
  }
  return false;
}
