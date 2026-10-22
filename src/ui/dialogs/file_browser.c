#include "base/file.h"
#include "ui/dialogs/file_browser.h"
#include "city/saveload/summary.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "base/text.h"
#include "city/saveload/services.h"
#include "drawing/canvas.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "ui/text/text.h"
#include "ui/windows/controls.h"
#include "ui/windows/state.h"

CityRect CityFileBrowserBounds(const Window *window, CityFileBrowserControl control) {
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

int CityFileBrowserRows(const Window *window) {
  CityRect list = CityFileBrowserBounds(window, CITY_FILE_LIST);
  return IntMax(1, (list.bottom - list.top) / CITY_FILE_BROWSER_ROW_HEIGHT);
}

CityRect CityFileBrowserThumb(const Window *window) {
  const CityFileBrowser *browser = window->file_browser;
  CityRect track = CityFileBrowserBounds(window, CITY_FILE_SCROLL_TRACK);
  return ReadCityScrollbar(track, browser->visible_count, CityFileBrowserRows(window), browser->first, 8).thumb;
}

void FilterCityFileBrowser(CityFileBrowser *browser) {
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

void SortCityFileBrowser(CityFileBrowser *browser) {
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

void SelectCityFileBrowserEntry(CityFileBrowser *browser, int selected) {
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
  char *native = DuplicateText(path);
  free(path);
  CityFileSummary summary;
  if (!ReadCityFileSummary(native, &summary)) {
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
  free(native);
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

void DrawCityFileBrowser(Canvas *canvas, const Window *window) {
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
