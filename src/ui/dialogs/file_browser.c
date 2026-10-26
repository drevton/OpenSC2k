#include "base/file.h"
#include "ui/dialogs/file_browser.h"
#include "city/saveload/summary.h"

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "assets/state.h"
#include "base/text.h"
#include "city/saveload/file.h"
#include "city/state.h"
#include "drawing/canvas.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "game/state.h"
#include "ui/dialogs/file_dialog_internal.h"
#include "ui/dialogs/messages.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"
#include "ui/windows/window_internal.h"
#include "ui/windows/windows.h"

enum { CITY_FILE_BROWSER_ROW_HEIGHT = 10 };

typedef enum CityFileBrowserControl {
  CITY_FILE_FILTER, CITY_FILE_CLEAR_FILTER, CITY_FILE_SORT_NAME, CITY_FILE_SORT_DATE, CITY_FILE_HOME,
  CITY_FILE_LIST, CITY_FILE_SCROLL_UP, CITY_FILE_SCROLL_DOWN, CITY_FILE_SCROLL_TRACK,
  CITY_FILE_NAME, CITY_FILE_CLEAR_NAME, CITY_FILE_DELETE, CITY_FILE_ACCEPT,
  CITY_FILE_RESIZE, CITY_FILE_DEFAULT_SIZE, CITY_FILE_CONTROL_COUNT
} CityFileBrowserControl;

static CityRect CityFileBrowserBounds(const Window *window, CityFileBrowserControl control) {
  CityRect b = window->bounds;
  int middle = b.left + (b.right - b.left) / 2;
  int list_bottom = b.bottom - (window->kind == CITY_CANVAS_FILE_SAVE ? 28 : 13);
  switch (control) {
  case CITY_FILE_FILTER: return (CityRect){b.left + 44, b.top + 36, middle - 15, b.top + 50};
  case CITY_FILE_CLEAR_FILTER: return (CityRect){middle - 15, b.top + 36, middle, b.top + 50};
  case CITY_FILE_SORT_NAME: return (CityRect){b.left, b.top + 50, b.left + (middle - b.left - 12) / 2, b.top + 62};
  case CITY_FILE_SORT_DATE: return (CityRect){b.left + (middle - b.left - 12) / 2, b.top + 50,
      middle - 12, b.top + 62};
  case CITY_FILE_HOME: return (CityRect){middle - 12, b.top + 50, middle, b.top + 62};
  case CITY_FILE_LIST: return (CityRect){b.left + 2, b.top + 64, middle - 13, list_bottom - 2};
  case CITY_FILE_SCROLL_UP: return (CityRect){middle - 12, b.top + 62, middle, b.top + 74};
  case CITY_FILE_SCROLL_DOWN: return (CityRect){middle - 12, list_bottom - 12, middle, list_bottom};
  case CITY_FILE_SCROLL_TRACK: return (CityRect){middle - 12, b.top + 74, middle, list_bottom - 12};
  case CITY_FILE_NAME: return (CityRect){b.left + 1, b.bottom - 28, middle - 17, b.bottom - 13};
  case CITY_FILE_CLEAR_NAME: return (CityRect){middle - 17, b.bottom - 28, middle, b.bottom - 13};
  case CITY_FILE_DELETE: return (CityRect){b.left, b.bottom - 13, b.left + (middle - b.left) / 2, b.bottom};
  case CITY_FILE_ACCEPT: return window->kind == CITY_CANVAS_FILE_SAVE ?
      (CityRect){b.left + (middle - b.left) / 2, b.bottom - 13, middle, b.bottom} :
      (CityRect){middle, b.bottom - 13, b.right - 12, b.bottom};
  case CITY_FILE_RESIZE: return (CityRect){b.right - 12, b.bottom - 13, b.right, b.bottom};
  case CITY_FILE_DEFAULT_SIZE: return (CityRect){b.right - 12, b.top, b.right, b.top + 14};
  default: return (CityRect){0};
  }
}

static int CityFileBrowserRows(const Window *window) {
  CityRect list = CityFileBrowserBounds(window, CITY_FILE_LIST);
  return IntMax(1, (list.bottom - list.top) / CITY_FILE_BROWSER_ROW_HEIGHT);
}

static CityRect CityFileBrowserThumb(const Window *window) {
  const CityFileBrowser *browser = window->file_browser;
  CityRect track = CityFileBrowserBounds(window, CITY_FILE_SCROLL_TRACK);
  return ReadCityScrollbar(track, browser->visible_count, CityFileBrowserRows(window), browser->first, 8).thumb;
}

static void FilterCityFileBrowser(CityFileBrowser *browser) {
  browser->visible_count = 0;
  for (size_t i = 0; i < browser->count; ++i)
    if (browser->entries[i].directory || TextContains(browser->entries[i].name, browser->filter, false))
      browser->visible[browser->visible_count++] = (int)i;
  browser->selected = -1;
  browser->last_click_row = -1;
  browser->first = 0;
  browser->detail_count = 0;
}

static int CompareFileEntries(const CityFileBrowserEntry *a, const CityFileBrowserEntry *b,
    bool date, bool descending) {
  if (a->parent != b->parent) return a->parent ? -1 : 1;
  if (a->directory != b->directory) return a->directory ? -1 : 1;
  int order = !a->directory && date && a->modified != b->modified ?
      (a->modified < b->modified ? -1 : 1) : strcasecmp(a->name, b->name);
  if (!order) order = strcmp(a->native_name, b->native_name);
  return descending && !a->directory ? -order : order;
}
static int CompareFileNames(const void *a, const void *b) { return CompareFileEntries(a, b, false, false); }
static int CompareFileNamesDescending(const void *a, const void *b) { return CompareFileEntries(a, b, false, true); }
static int CompareFileDates(const void *a, const void *b) { return CompareFileEntries(a, b, true, false); }
static int CompareFileDatesDescending(const void *a, const void *b) { return CompareFileEntries(a, b, true, true); }

static void SortCityFileBrowser(CityFileBrowser *browser) {
  qsort(browser->entries, browser->count, sizeof(*browser->entries), browser->sort_date ?
      (browser->descending ? CompareFileDatesDescending : CompareFileDates) :
      (browser->descending ? CompareFileNamesDescending : CompareFileNames));
  FilterCityFileBrowser(browser);
}

typedef struct {
  CityFileBrowser *browser;
  bool root;
} CityFileListing;

static bool AddCityFileBrowserEntry(const char *name, const HostFileInfo *info, void *context) {
  CityFileListing *listing = context;
  CityFileBrowser *browser = listing->browser;
  bool parent = !strcmp(name, "..");
  if (!strcmp(name, ".") || (parent && listing->root) || (name[0] == '.' && !parent)) return true;
  const char *extension = strrchr(name, '.');
  if (!info->directory && (!info->regular || !extension ||
      (strcasecmp(extension, ".sc2") && strcasecmp(extension, ".sc2k") && strcasecmp(extension, ".scn"))))
    return true;
  if (strlen(name) >= sizeof(browser->entries[0].name)) return true;
  if (browser->count == browser->capacity) {
    size_t capacity = browser->capacity ? browser->capacity * 2 : 64;
    if (capacity > INT32_MAX) return false;
    CityFileBrowserEntry *entries = realloc(browser->entries, capacity * sizeof(*entries));
    if (!entries) return false;
    browser->entries = entries;
    int *visible = realloc(browser->visible, capacity * sizeof(*visible));
    if (!visible) return false;
    browser->visible = visible;
    browser->capacity = capacity;
  }
  CityFileBrowserEntry *entry = &browser->entries[browser->count++];
  *entry = (CityFileBrowserEntry){.modified = info->modified, .size = info->size,
      .directory = info->directory, .parent = parent};
  CopyText(entry->native_name, sizeof(entry->native_name), name);
  CopyText(entry->name, sizeof(entry->name), name);
  return true;
}

bool ReadCityFileBrowserDirectory(CityFileBrowser *browser, const char *display_directory) {
  char *resolved = ResolveExistingPath(display_directory);
  if (!resolved) return false;
  HostFileInfo info;
  if (!ReadHostFileInfo(resolved, &info) || !info.directory) { free(resolved); return false; }
  char *display = AllocateText(strlen(resolved) + 2);
  CopyText(display, strlen(resolved) + 2, resolved);
  browser->count = 0;
  CityFileListing listing = {browser, !strcmp(resolved, "/")};
  bool valid = VisitDirectory(resolved, AddCityFileBrowserEntry, &listing);
  // A failed allocation can leave a partial listing, but never stale row indices.
  free(browser->directory);
  browser->directory = display;
  size_t length = strlen(browser->directory);
  if (length && !IsPathSeparator(browser->directory[length - 1])) {
    browser->directory[length] = kPathSeparator;
    browser->directory[length + 1] = 0;
  }
  u64 available;
  browser->free_space[0] = 0;
  if (ReadDirectoryFreeBytes(resolved, &available)) {
    double free_bytes = available / (1024.0 * 1024);
    const char *unit = "MiB";
    if (free_bytes >= 1024) { free_bytes /= 1024; unit = "GiB"; }
    if (free_bytes >= 1024) { free_bytes /= 1024; unit = "TiB"; }
    snprintf(browser->free_space, sizeof(browser->free_space), "%.2f %s free", free_bytes, unit);
  }
  free(resolved);
  SortCityFileBrowser(browser);
  return valid;
}

static void SelectCityFileBrowserEntry(CityFileBrowser *browser, int selected) {
  browser->selected = selected;
  browser->detail_count = 0;
  if (selected < 0 || selected >= browser->visible_count) return;
  const CityFileBrowserEntry *entry = &browser->entries[browser->visible[selected]];
  if (entry->directory) return;
  CopyText(browser->filename, sizeof(browser->filename), entry->name);
  browser->caret[1] = (int)strlen(browser->filename);
  browser->text_scroll[1] = 0;
  browser->select_all[1] = 0;
  char *path = NULL;
  FormatText(&path, "%s%s", browser->directory, entry->name);
  CityFileSummary summary;
  if (!ReadCityFileSummary(path, &summary)) {
    CopyText(browser->details[browser->detail_count++], sizeof(browser->details[0]), "Unable to read game details");
  } else {
    snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]), "City: %s", summary.name);
    snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]),
        "Map size: %d x %d", summary.width, summary.height);
    if (summary.has_economy) {
      snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]),
          "Game date: %d-%02d", summary.year, summary.month);
      char currency[CITY_CURRENCY_TEXT_CAPACITY];
      FormatCityCurrencyForCountry(currency, summary.cash, "usa");
      snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]), "Cash: %s", currency);
    }
    if (summary.extended)
      snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]),
          "Format: SC2K (version %u)", summary.format_version);
    else CopyText(browser->details[browser->detail_count++], sizeof(browser->details[0]), "Format: SimCity 2000");
    char *version_detail = browser->details[browser->detail_count++];
    if (summary.has_writer_version) {
      static const char *const kStageLabels[] = {" Development", " Alpha", " Beta", ""};
      const CityFileVersion *version = &summary.writer_version;
      char number[40];
      if (version->patch)
        snprintf(number, sizeof(number), "%u.%u.%u", version->major, version->minor, version->patch);
      else snprintf(number, sizeof(number), "%u.%u", version->major, version->minor);
      snprintf(version_detail, sizeof(browser->details[0]), "Saved with: OpenSC2k %s%s",
          number, kStageLabels[version->stage]);
    } else CopyText(version_detail, sizeof(browser->details[0]), "Saved with: Unknown");
    snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]), "Revision: %s",
        summary.writer_version.revision[0] ? summary.writer_version.revision : "Unknown");
  }
  free(path);
  time_t timestamp = entry->modified;
  const struct tm *date = localtime(&timestamp);
  if (date) {
    char modified[64];
    strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M", date);
    snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]), "Saved: %s", modified);
  }
  snprintf(browser->details[browser->detail_count++], sizeof(browser->details[0]),
      "File size: %.1f KiB", (double)entry->size / 1024);
}

static void DrawFileButton(Canvas *canvas, CityRect b, const char *label, bool enabled) {
  if (enabled) DrawCanvasButtonFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  else DrawCanvasFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, b, label,
      enabled ? 0xFF101010 : canvas_dark, false, 10);
}

static void DrawFileArrow(Canvas *canvas, CityRect b, bool up) {
  DrawFileButton(canvas, b, "", true);
  int pressed = CityCanvasButtonPressed(canvas, b);
  int x = (b.left + b.right) / 2 + pressed, y = (b.top + b.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = up ? row + 1 : 4 - row;
    CanvasFill(canvas, x - half, y + row, x + half, y + row + 1, 0xFF101010);
  }
}

static void DrawCityFileBrowser(Canvas *canvas, const Window *window) {
  const CityFileBrowser *browser = window->file_browser;
  if (!browser) return;
  CityRect b = window->bounds;
  int middle = b.left + (b.right - b.left) / 2;
  DrawCanvasFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, b, window->title,
      canvas_face, canvas_light, canvas_dark, 12);
  DrawCityWindowControlIcon(canvas, CityFileBrowserBounds(window, CITY_FILE_DEFAULT_SIZE),
      CITY_WINDOW_ICON_DEFAULT_SIZE, false);
  Canvas local = *canvas;
  local.clip = IntersectCityRects(canvas->clip, (CityRect){b.left + 2, b.top + 14, b.right - 2, b.top + 36});
  CanvasTextAt(&local, b.left + 2, b.top + 15, browser->directory, 0xFF101010);
  CanvasTextAt(&local, b.left + 2, b.top + 25, browser->free_space, 0xFF101010);
  DrawCanvasButtonFrame(canvas, (CityRect){b.left, b.top + 36, middle, b.top + 50},
      canvas_face, canvas_light, canvas_dark, true);
  CanvasTextAt(canvas, b.left + 4, b.top + 38, "Filter:", 0xFF101010);
  DrawCityTextBox(canvas, CityFileBrowserBounds(window, CITY_FILE_FILTER), browser->filter,
      browser->caret[0], browser->text_scroll[0], browser->focus == 0, browser->select_all[0], true);
  DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_CLEAR_FILTER), "X", browser->filter[0] != 0);
  DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_SORT_NAME), "Name", true);
  DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_SORT_DATE), "Date", true);
  CityRect sort = CityFileBrowserBounds(window, browser->sort_date ? CITY_FILE_SORT_DATE : CITY_FILE_SORT_NAME);
  for (int row = 0; row < 3; ++row) {
    int half = browser->descending ? 3 - row : row + 1;
    CanvasFill(canvas, sort.right - 5 - half, sort.top + 4 + row, sort.right - 5 + half,
        sort.top + 5 + row, 0xFF101010);
  }
  CityRect home = CityFileBrowserBounds(window, CITY_FILE_HOME);
  DrawFileButton(canvas, home, "", true);
  int hx = (home.left + home.right) / 2;
  for (int row = 0; row < 4; ++row)
    CanvasFill(canvas, hx - row - 1, home.top + 2 + row, hx + row + 1, home.top + 3 + row, 0xFF101010);
  CanvasFill(canvas, hx - 3, home.top + 6, hx + 3, home.bottom - 2, 0xFF101010);
  CanvasFill(canvas, hx - 1, home.top + 7, hx + 1, home.bottom - 2, canvas_face);
  CityRect list = CityFileBrowserBounds(window, CITY_FILE_LIST);
  DrawCanvasFrame(canvas, (CityRect){list.left - 2, list.top - 2, list.right + 1, list.bottom + 2},
      0xFF101010, canvas_light, canvas_dark, false);
  local = *canvas;
  local.clip = IntersectCityRects(canvas->clip, list);
  int rows = CityFileBrowserRows(window);
  int first = IntMin(browser->first, IntMax(0, browser->visible_count - rows));
  for (int row = 0; row < rows && first + row < browser->visible_count; ++row) {
    int index = first + row;
    const CityFileBrowserEntry *entry = &browser->entries[browser->visible[index]];
    char label[300];
    snprintf(label, sizeof(label), entry->parent ? ".. (Parent directory)" :
        entry->directory ? "%s/ (Directory)" : "%s", entry->name);
    CanvasTextAt(&local, list.left + 1, list.top + row * CITY_FILE_BROWSER_ROW_HEIGHT, label,
        index == browser->selected ? 0xFFFFFFFF : entry->directory ? 0xFFB8CC70 : 0xFFFFFF80);
  }
  CityRect track = CityFileBrowserBounds(window, CITY_FILE_SCROLL_TRACK);
  DrawCanvasFrame(canvas, track, canvas_face, canvas_light, canvas_dark, false);
  DrawCanvasFrame(canvas, CityFileBrowserThumb(window), canvas_face, canvas_light, canvas_dark, true);
  DrawFileArrow(canvas, CityFileBrowserBounds(window, CITY_FILE_SCROLL_UP), true);
  DrawFileArrow(canvas, CityFileBrowserBounds(window, CITY_FILE_SCROLL_DOWN), false);
  if (!browser->open) {
    DrawCityTextBox(canvas, CityFileBrowserBounds(window, CITY_FILE_NAME), browser->filename,
        browser->caret[1], browser->text_scroll[1], browser->focus == 1, browser->select_all[1], true);
    DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_CLEAR_NAME), "X", browser->filename[0] != 0);
    bool selected = browser->selected >= 0 && !browser->entries[browser->visible[browser->selected]].directory;
    DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_DELETE), "Delete", selected);
  }
  DrawCanvasFrame(canvas, (CityRect){middle, b.top + 36, b.right, b.bottom - 13},
      canvas_face, canvas_light, canvas_dark, true);
  CanvasFill(canvas, middle + 1, b.top + 37, b.right - 1, b.top + 58, 0xFFA8A8A8);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
      (CityRect){middle + 1, b.top + 36, b.right - 1, b.top + 58}, "Game Details", 0xFF101010, false, 10);
  local = *canvas;
  local.clip = IntersectCityRects(canvas->clip, (CityRect){middle + 3, b.top + 59, b.right - 3, b.bottom - 14});
  for (int row = 0; row < browser->detail_count; ++row)
    CanvasTextAt(&local, middle + 5, b.top + 64 + row * 13, browser->details[row], 0xFF101010);
  if (!browser->open) DrawCanvasFrame(canvas, (CityRect){middle, b.bottom - 13, b.right - 12, b.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  bool can_accept = browser->filename[0] && (!browser->open || browser->selected >= 0);
  DrawFileButton(canvas, CityFileBrowserBounds(window, CITY_FILE_ACCEPT), browser->open ? "Load" : "Save", can_accept);
  CityRect resize = CityFileBrowserBounds(window, CITY_FILE_RESIZE);
  DrawFileButton(canvas, resize, "", true);
  for (int offset = 3; offset <= 9; offset += 3)
    DrawCanvasLine(canvas, resize.right - offset, resize.bottom - 2,
        resize.right - 2, resize.bottom - offset, 0xFF101010, 1);
}

void ReleaseCityFileBrowser(CityFileBrowser *browser) {
  free(browser->directory);
  free(browser->home);
  free(browser->entries);
  free(browser->visible);
}

// Browser controls validate local choices; the dialog module owns their completion and save scopes.
static WindowProcedure FileBrowserConfirmationWindowProcedure;

static Window *FileBrowserWindow(const CityInterfaceState *ui, const NativeFileDialog *dialog) {
  return FindCityCanvasWindow(ui->file_dialog.desktop, dialog->window_id);
}

static void DirtyFileBrowser(Game *game, NativeFileDialog *dialog) {
  Window *window = FileBrowserWindow(game->ui, dialog);
  if (!window) return;
  CityFileBrowser *browser = &dialog->browser;
  int focus = browser->focus == 1 ? 1 : 0;
  CityRect input = CityFileBrowserBounds(window, focus ? CITY_FILE_NAME : CITY_FILE_FILTER);
  browser->text_scroll[focus] = CityTextBoxScroll(&game->resources->interface_font, input,
      focus ? browser->filename : browser->filter, browser->caret[focus], browser->text_scroll[focus]);
  browser->first = IntClamp(browser->first, 0, browser->visible_count - CityFileBrowserRows(window));
  Sc2kDirtyMarkRect(&game->ui->file_dialog.desktop->dirty, window->bounds);
}

void SubmitCityFileDialog(City *city, NativeFileDialog *dialog, i32 accept, i32 overwrite) {
  CityFilePresentation *files = &city->game->ui->file_dialog;
  if (!accept) {
    if (!AnswerCityFileDialog(city, dialog->token, CITY_DIALOG_CANCEL, (const u8 *)"", 0))
      PresentCityFileNotice(city, "The file choice could not be cancelled.", true);
    return;
  }
  bool absolute = IsPathAbsolute(dialog->browser.filename);
  char *destination = NULL;
  FormatText(&destination, "%s%s", absolute ? "" : dialog->browser.directory, dialog->browser.filename);
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

static void NavigateFileBrowser(City *city, NativeFileDialog *dialog, const char *directory) {
  if (!ReadCityFileBrowserDirectory(&dialog->browser, directory))
    PresentCityFileNotice(city, "The selected directory could not be opened completely.", true);
  DirtyFileBrowser(city->game, dialog);
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
  FormatText(&dialog->delete_path, "%s%s", browser->directory, entry->name);
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

static void EditFileBrowser(Game *game, NativeFileDialog *dialog) {
  CityFileBrowser *browser = &dialog->browser;
  if (browser->focus == 0) FilterCityFileBrowser(browser);
  else {
    browser->selected = -1;
    browser->detail_count = 0;
    browser->last_click_row = -1;
  }
  DirtyFileBrowser(game, dialog);
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
    Window *target = FileBrowserWindow(game->ui, dialog);
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
        EditFileBrowser(game, dialog);
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
      DirtyFileBrowser(game, dialog);
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
      DirtyFileBrowser(game, dialog);
    } else if (key == CITY_KEY_UP || key == CITY_KEY_DOWN || key == CITY_KEY_PAGE_UP || key == CITY_KEY_PAGE_DOWN) {
      Window *window = FileBrowserWindow(game->ui, dialog);
      int rows = CityFileBrowserRows(window);
      int delta = key == CITY_KEY_UP ? -1 : key == CITY_KEY_DOWN ? 1 : key == CITY_KEY_PAGE_UP ? -rows : rows;
      int selected = browser->selected < 0 ? 0 : browser->selected + delta;
      selected = IntClamp(selected, 0, browser->visible_count - 1);
      if (browser->visible_count) SelectCityFileBrowserEntry(browser, selected);
      browser->first = IntClamp(browser->first, selected - rows + 1, selected);
      browser->focus = 2;
      DirtyFileBrowser(game, dialog);
    } else if (browser->focus <= 1) {
      int field = browser->focus;
      char *text = field ? browser->filename : browser->filter;
      bool changed = key == CITY_KEY_BACKSPACE || key == CITY_KEY_DELETE;
      if (HandleCityTextEditKey(text, &browser->caret[field], &browser->select_all[field], key)) {
        if (changed) EditFileBrowser(game, dialog);
        else DirtyFileBrowser(game, dialog);
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
    EditFileBrowser(game, dialog);
    return true;
  }
  case CANVAS_SDL_SCROLL: {
    int delta = input->y;
    if (!dialog->overwrite_id) {
      CityFileBrowser *browser = &dialog->browser;
      browser->wheel_remainder += delta;
      browser->first -= browser->wheel_remainder / 1024 * 3;
      browser->wheel_remainder %= 1024;
      DirtyFileBrowser(game, dialog);
    }
    return true;
  }
  case CANVAS_SDL_POINTER_MOVE:
  case CANVAS_SDL_POINTER_UP:
  case CANVAS_SDL_FOCUS_LOST: {
    CityFileBrowser *browser = &dialog->browser;
    Window *target = FileBrowserWindow(game->ui, dialog);
    int y = event->y;
    bool dragging = input->kind == CANVAS_SDL_POINTER_MOVE;
    if (!browser->dragging_scroll) return false;
    browser->dragging_scroll = dragging;
    if (dragging && target && !dialog->overwrite_id) {
      CityRect track = CityFileBrowserBounds(target, CITY_FILE_SCROLL_TRACK);
      CityScrollbar scrollbar = ReadCityScrollbar(track, browser->visible_count,
          CityFileBrowserRows(target), browser->first, 8);
      browser->first = CityScrollbarPosition(&scrollbar, y, browser->scrollbar_grab, CITY_SCROLLBAR_DRAG);
      DirtyFileBrowser(game, dialog);
    }
    return true;
  }
  default: return false;
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
