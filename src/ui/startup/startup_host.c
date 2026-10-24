#include "city/engine/city_name.h"
#include "base/text.h"
#include "city/maps/labels.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "assets/catalog.h"
#include "assets/resource_types.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "audio/state.h"
#include "base/byte_order.h"
#include "base/version.h"
#include "city/engine/command_types.h"
#include "city/engine/commands.h"
#include "city/maps/map_types.h"
#include "city/saveload/runtime.h"
#include "city/saveload/summary.h"
#include "city/state.h"
#include "drawing/raster/canvas_primitives.h"
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
#include "ui/startup/startup.h"
#include "ui/startup/startup_internal.h"
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

static bool BuildNativeStartupLayout(u32 resource, CityCanvasStartupWindow *layout);

static bool StartupMenuLoadCity(City *city, CityStartupMenuDialog *dialog);
static bool StartupMenuNewCity(City *city, CityStartupMenuDialog *dialog);
static bool StartupMenuEditMap(City *city, CityStartupMenuDialog *dialog);
static bool StartupMenuLoadScenario(City *city, CityStartupMenuDialog *dialog);
static bool StartupMenuQuit(City *city, CityStartupMenuDialog *dialog);

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

enum {
  kTerrainCoastEdgeShift = kTerrainFeatureCount, kTerrainNoLakes = -1,
  kTerrainSmallRiversFirst = 1 << (kTerrainCoastEdgeShift + 4),
  kTerrainSmoothnessFirst = CITY_TERRAIN_VARIETY_COUNT
};
static const u32 kTerrainGroupMasks[] = {
  kOcean | kRandomCoast,
  kRiver | kBranch | kCrossing | kRejoin,
  kRolling | kPlateau | kRidge | kBasin | kValley | kCanyon
};

static bool IsTerrainFeatureDropdown(int id) {
  return id >= CITY_SETUP_COAST && id <= CITY_SETUP_LANDFORM;
}

static bool IsTerrainGroupedDropdown(int id) {
  return IsTerrainFeatureDropdown(id) || id == CITY_SETUP_VARIETY;
}

static bool IsTerrainSmallRiverItem(int control, const CityDropdownItem *item) {
  return control == CITY_SETUP_RIVER && item->value >= kTerrainSmallRiversFirst &&
      item->value < kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_COUNT;
}

static u32 TerrainDropdownRadioGroup(int control, const CityDropdownItem *item) {
  if (IsTerrainSmallRiverItem(control, item)) return 0;
  if (control == CITY_SETUP_LANDFORM &&
      (item->value == kTerrainNoLakes || (item->value & (kLake | kLakes)))) return kLake | kLakes;
  return kTerrainGroupMasks[control - CITY_SETUP_COAST];
}

static bool TerrainDropdownItemDisabled(const CityTerrainSettings *settings, int group,
    const CityDropdownItem *item) {
  u32 features = settings->features;
  if (item->kind == CITY_DROPDOWN_SEPARATOR) return true;
  if (IsTerrainSmallRiverItem(group, item)) return false;
  if (item->kind == CITY_DROPDOWN_RADIO) {
    if (group == CITY_SETUP_RIVER && item->value) return (features & kCanyon) != 0;
    if (group == CITY_SETUP_LANDFORM)
      return item->value != kTerrainNoLakes &&
          (DisabledCityTerrainFeatures(features & ~TerrainDropdownRadioGroup(group, item)) & item->value) != 0;
    return false;
  }
  if (item->value == kCliffs) return !(features & kOcean);
  if (item->value == kMeander || item->value == kSalty) return !(features & kRiver);
  if (item->value == kDelta) return !(features & kRiver) || !(features & kOcean);
  return false;
}

static int TerrainDropdownRadioItem(const CityTerrainSettings *settings, const CityCanvasStartupControl *control,
    u32 group) {
  int value = settings->features & group;
  if (control->id == CITY_SETUP_COAST && (value & ~kOcean)) value &= ~kOcean;
  if (control->id == CITY_SETUP_RIVER && (value & ~kRiver)) value &= ~kRiver;
  for (int i = 0; i < control->item_count; ++i) {
    const CityDropdownItem *item = &control->items[i];
    if (item->kind != CITY_DROPDOWN_RADIO || TerrainDropdownRadioGroup(control->id, item) != group) continue;
    if ((item->value == kTerrainNoLakes ? 0 : item->value) == value) return i;
  }
  // Existing UI choices can predate the single-choice groups. Prefer the first
  // matching layout until the player chooses another base layout.
  for (int i = 0; i < control->item_count; ++i)
    if (control->items[i].kind == CITY_DROPDOWN_RADIO &&
        TerrainDropdownRadioGroup(control->id, &control->items[i]) == group &&
        (control->items[i].value & value)) return i;
  return 0;
}

static CityCanvasStartupControl *SetupControl(City *city, int id);

static void AddTerrainControl(CityCanvasStartupWindow *layout, int id, CityRect bounds,
    int kind, int style, const char *text) {
  assert(layout->control_count < CITY_STARTUP_CANVAS_CONTROLS);
  CityCanvasStartupControl *control = &layout->controls[layout->control_count++];
  *control = (CityCanvasStartupControl){.id = id, .visible = true, .control_class = kind,
      .style = style | CITY_RESOURCE_STYLE_VISIBLE, .bounds = bounds};
  CopyText(control->text, sizeof(control->text), text);
  control->text_caret = strlen(control->text);
  if (id >= CITY_SETUP_DIFFICULTY_FIRST && id < CITY_SETUP_DIFFICULTY_FIRST + CITY_SETUP_DIFFICULTY_COUNT)
    control->tooltip = "Choose the city's difficulty and starting funds.";
  else if (id >= CITY_SETUP_ERA_FIRST && id < CITY_SETUP_ERA_FIRST + CITY_SETUP_ERA_COUNT)
    control->tooltip = "Choose the starting year. Technology becomes available as the city advances.";
  else switch (id) {
    case CITY_SETUP_YEAR: control->tooltip = "Change the starting year (1900 to 2050). Click to enter a year."; break;
    case CITY_SETUP_YEAR_PREVIOUS: control->tooltip = "Start ten years earlier. Hold Shift for one year."; break;
    case CITY_SETUP_YEAR_NEXT: control->tooltip = "Start ten years later. Hold Shift for one year."; break;
    case CITY_SETUP_YEAR_INPUT: control->tooltip = "Enter a starting year from 1900 to 2050."; break;
    case CITY_SETUP_RANDOM_NAME: control->tooltip = "Generate another random city name."; break;
    case CITY_SETUP_LOAD_MAP:
      control->tooltip = "Import terrain and trees from a city save, without its buildings. "
          "Changing generation settings replaces the imported terrain.";
      break;
    case CITY_SETUP_REGENERATE: control->tooltip = "Generate different terrain with the same settings."; break;
    case CITY_SETUP_NAME: control->tooltip = "Enter a name for the new city."; break;
    case CITY_SETUP_MAYOR: control->tooltip = "Enter the mayor's name."; break;
    case CITY_SETUP_HILLS: control->tooltip = "Adjust terrain height."; break;
    case CITY_SETUP_WATER:
      control->tooltip = "More water widens broad rivers, expands coasts and lakes, and adds scattered freshwater ponds.";
      break;
    case CITY_SETUP_TREES: control->tooltip = "Adjust the amount of tree cover."; break;
    case CITY_SETUP_EDIT_TERRAIN:
      control->tooltip = "Edit this terrain for free before starting the city. Keep the chosen city settings."; break;
    case CITY_DIALOG_OK: control->tooltip = "Start the city with this terrain and these settings."; break;
    case CITY_DIALOG_CANCEL: control->tooltip = "Cancel without replacing the current city."; break;
  }
}

static void BuildCitySetupLayout(City *city, CityCanvasStartupWindow *layout) {
  *layout = (CityCanvasStartupWindow){.client_bounds = {0, 0, 488, 306}, .terrain_setup = true};
  CopyText(layout->title, sizeof(layout->title), "New City");
  const int button = CITY_RESOURCE_CONTROL_BUTTON, edit = CITY_RESOURCE_CONTROL_EDIT;
  AddTerrainControl(layout, 0, (CityRect){8, 6, 75, 20}, 0, 0, "City name");
  AddTerrainControl(layout, CITY_SETUP_NAME, (CityRect){78, 6, 254, 20}, edit, 0, "New City");
  AddTerrainControl(layout, CITY_SETUP_RANDOM_NAME, (CityRect){256, 6, 270, 20}, button, 0, "");
  AddTerrainControl(layout, 0, (CityRect){8, 24, 75, 38}, 0, 0, "Mayor");
  AddTerrainControl(layout, CITY_SETUP_MAYOR, (CityRect){78, 24, 270, 38}, edit, 0,
      city->game->ui->terrain_mayor);
  AddTerrainControl(layout, 0, (CityRect){8, 42, 75, 56}, 0, 0, "Difficulty");
  static const CityDropdownItem kDifficulty[] = {
    {"Easy - $20,000", CITY_SETUP_DIFFICULTY_EASY, CITY_DROPDOWN_PLAIN},
    {"Medium - $10,000", CITY_SETUP_DIFFICULTY_MEDIUM, CITY_DROPDOWN_PLAIN},
    {"Hard - $10,000 bond at 3%", CITY_SETUP_DIFFICULTY_HARD, CITY_DROPDOWN_PLAIN},
  };
  AddTerrainControl(layout, CITY_SETUP_DIFFICULTY, (CityRect){78, 42, 270, 56},
      CITY_RESOURCE_CONTROL_DROPDOWN, 0, kDifficulty[0].text);
  layout->controls[layout->control_count - 1].items = kDifficulty;
  layout->controls[layout->control_count - 1].item_count = CITY_SETUP_DIFFICULTY_COUNT;
  layout->controls[layout->control_count - 1].tooltip = "Choose starting funds and the city's difficulty.";
  for (int i = 0; i < CITY_SETUP_DIFFICULTY_COUNT; ++i) {
    AddTerrainControl(layout, CITY_SETUP_DIFFICULTY_FIRST + i, (CityRect){0}, button, CITY_RESOURCE_BUTTON_RADIO, "");
    layout->controls[layout->control_count - 1].visible = false;
  }
  AddTerrainControl(layout, 0, (CityRect){8, 60, 75, 74}, 0, 0, "Start year");
  AddTerrainControl(layout, CITY_SETUP_YEAR_PREVIOUS, (CityRect){78, 60, 92, 74}, button, 0, "");
  AddTerrainControl(layout, CITY_SETUP_YEAR, (CityRect){92, 60, 256, 74}, button, 0, "1st Jan 1900");
  AddTerrainControl(layout, CITY_SETUP_YEAR_NEXT, (CityRect){256, 60, 270, 74}, button, 0, "");
  // Original era commands remain available to replay callers, without visible radio buttons.
  for (int i = 0; i < CITY_SETUP_ERA_COUNT; ++i) {
    AddTerrainControl(layout, CITY_SETUP_ERA_FIRST + i, (CityRect){0}, button, CITY_RESOURCE_BUTTON_RADIO, "");
    layout->controls[layout->control_count - 1].visible = false;
  }
  AddTerrainControl(layout, 0, (CityRect){8, 78, 75, 92}, 0, 0, "Map size");
  static const CityDropdownItem kMapSizes[] = {
    {"128", 128, CITY_DROPDOWN_PLAIN},
    {"256", 256, CITY_DROPDOWN_PLAIN},
    {"512", 512, CITY_DROPDOWN_PLAIN},
    {"1024", 1024, CITY_DROPDOWN_PLAIN},
    {"2048", 2048, CITY_DROPDOWN_PLAIN},
    {"4096", 4096, CITY_DROPDOWN_PLAIN},
  };
  for (int axis = 0; axis < 2; ++axis) {
    int left = axis ? 182 : 78;
    AddTerrainControl(layout, CITY_SETUP_MAP_WIDTH + axis, (CityRect){left, 78, left + 88, 92},
        CITY_RESOURCE_CONTROL_DROPDOWN, 0, "128");
    CityCanvasStartupControl *control = &layout->controls[layout->control_count - 1];
    control->items = kMapSizes; control->item_count = sizeof(kMapSizes) / sizeof(*kMapSizes);
    control->tooltip = axis ? "Choose the map height in tiles." : "Choose the map width in tiles.";
  }
  AddTerrainControl(layout, 0, (CityRect){168, 78, 182, 92}, 0, 0, "x");
  static const CityDropdownItem kClimates[] = {
    {"Normal", CITY_TERRAIN_NORMAL, CITY_DROPDOWN_PLAIN},
    {"Desert", CITY_TERRAIN_DESERT, CITY_DROPDOWN_PLAIN},
    {"Snowy", CITY_TERRAIN_SNOWY, CITY_DROPDOWN_PLAIN},
    {"Mixed", CITY_TERRAIN_MIXED, CITY_DROPDOWN_PLAIN},
    {"SimCity Classic", CITY_TERRAIN_SIMCITY, CITY_DROPDOWN_PLAIN},
  };
  AddTerrainControl(layout, 0, (CityRect){8, 96, 75, 110}, 0, 0, "Terrain");
  AddTerrainControl(layout, CITY_SETUP_CLIMATE, (CityRect){78, 96, 270, 110},
      CITY_RESOURCE_CONTROL_DROPDOWN, 0, "Normal");
  CityCanvasStartupControl *climate = &layout->controls[layout->control_count - 1];
  climate->items = kClimates; climate->item_count = sizeof(kClimates) / sizeof(*kClimates);
  climate->tooltip = "Choose Normal, Desert, Snowy, Mixed or SimCity Classic's brown ground. "
      "Snow follows elevation; Mixed combines snow, normal land and desert.";
  static const CityDropdownItem kCoverage[] = {
    {"0%", 0, CITY_DROPDOWN_PLAIN},
    {"10%", 10, CITY_DROPDOWN_PLAIN},
    {"20%", 20, CITY_DROPDOWN_PLAIN},
    {"30%", 30, CITY_DROPDOWN_PLAIN},
    {"40%", 40, CITY_DROPDOWN_PLAIN},
    {"50%", 50, CITY_DROPDOWN_PLAIN},
    {"60%", 60, CITY_DROPDOWN_PLAIN},
    {"70%", 70, CITY_DROPDOWN_PLAIN},
    {"80%", 80, CITY_DROPDOWN_PLAIN},
    {"90%", 90, CITY_DROPDOWN_PLAIN},
    {"100%", 100, CITY_DROPDOWN_PLAIN},
  };
  static const char *const kCoverageLabels[] = {"Snow", "Desert"};
  static const char *const kCoverageHelp[] = {
    "Target snow coverage of land, rounded to the nearest height band. Available in Snowy and Mixed terrain.",
    "Target desert coverage of land. Height bands and green water margins make this approximate. "
        "Available in Desert and Mixed terrain."
  };
  for (int i = 0; i < 2; ++i) {
    int left = i ? 150 : 8;
    AddTerrainControl(layout, 0, (CityRect){left, 114, left + 67, 128}, 0, 0, kCoverageLabels[i]);
    AddTerrainControl(layout, CITY_SETUP_SNOW_COVERAGE + i, (CityRect){left + 70, 114, left + 120, 128},
        CITY_RESOURCE_CONTROL_DROPDOWN, 0, "");
    CityCanvasStartupControl *control = &layout->controls[layout->control_count - 1];
    control->items = kCoverage; control->item_count = sizeof(kCoverage) / sizeof(*kCoverage);
    control->tooltip = kCoverageHelp[i];
  }
  static const CityDropdownItem kCoasts[] = {
    {"None", 0, CITY_DROPDOWN_RADIO},
    {"Coast", kOcean, CITY_DROPDOWN_RADIO},
    {"Random", kRandomCoast, CITY_DROPDOWN_RADIO},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"Cliffs", kCliffs, CITY_DROPDOWN_CHECKBOX},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"North", CITY_TERRAIN_COAST_NORTH << kTerrainCoastEdgeShift, CITY_DROPDOWN_CHECKBOX},
    {"East", CITY_TERRAIN_COAST_EAST << kTerrainCoastEdgeShift, CITY_DROPDOWN_CHECKBOX},
    {"South", CITY_TERRAIN_COAST_SOUTH << kTerrainCoastEdgeShift, CITY_DROPDOWN_CHECKBOX},
    {"West", CITY_TERRAIN_COAST_WEST << kTerrainCoastEdgeShift, CITY_DROPDOWN_CHECKBOX},
  };
  static const CityDropdownItem kRivers[] = {
    {"None", 0, CITY_DROPDOWN_RADIO},
    {"Normal", kRiver, CITY_DROPDOWN_RADIO},
    {"Forked", kBranch, CITY_DROPDOWN_RADIO},
    {"Crossing", kCrossing, CITY_DROPDOWN_RADIO},
    {"Rejoining", kRejoin, CITY_DROPDOWN_RADIO},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"Meanders", kMeander, CITY_DROPDOWN_CHECKBOX},
    {"Delta", kDelta, CITY_DROPDOWN_CHECKBOX},
    {"Saltwater", kSalty, CITY_DROPDOWN_CHECKBOX},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"No streams", kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_NONE, CITY_DROPDOWN_RADIO},
    {"Few streams", kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_FEW, CITY_DROPDOWN_RADIO},
    {"Streams", kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_MEDIUM, CITY_DROPDOWN_RADIO},
    {"Many streams", kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_MANY, CITY_DROPDOWN_RADIO},
  };
  static const CityDropdownItem kLandforms[] = {
    {"Natural", 0, CITY_DROPDOWN_RADIO},
    {"Rolling hills", kRolling, CITY_DROPDOWN_RADIO},
    {"Plateau", kPlateau, CITY_DROPDOWN_RADIO},
    {"Mountain ridge", kRidge, CITY_DROPDOWN_RADIO},
    {"Basin", kBasin, CITY_DROPDOWN_RADIO},
    {"River valley", kValley, CITY_DROPDOWN_RADIO},
    {"Canyon", kCanyon, CITY_DROPDOWN_RADIO},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"No lakes", kTerrainNoLakes, CITY_DROPDOWN_RADIO},
    {"Few lakes", kLake, CITY_DROPDOWN_RADIO},
    {"Many lakes", kLakes, CITY_DROPDOWN_RADIO},
  };
  static const char *const kGroupLabels[] = {"Coast", "River", "Landform"};
  static const char *const kGroupHelp[] = {
    "Choose a coast shape, optional cliffs, and North/East/South/West edges of the preview. "
        "Random varies the number of coast sides, bays and offshore island groups. "
        "Checked edges limit its eligible sides; without checked edges all four are eligible. "
        "Adjacent sides are allowed; sides need not oppose each other. "
        "Random admits partial ocean inlets along the chosen sides. "
        "Each coast edge follows seeded random targets, with one- and two-tile steps making local shallows and inlets. "
        "Water increases its breadth; the shore stays at least three tiles deep. "
        "No checked edges defaults to East.",
    "Choose a broad waterway layout, optional meanders and a coastal delta, and the amount of streams. "
        "Main systems increase slowly with map size, up to four. "
        "Streams are independent of the broad waterway; larger maps get more without increasing their width. "
        "Streams follow downhill routes to existing water. Canyons disable only the broad waterway. "
        "A delta requires both a river and coast. "
        "Saltwater makes the entire broad waterway seawater at sea level with sloped shores. "
        "Streams and separate lakes remain freshwater.",
    "Choose one landform and independently choose no extra lakes, few lakes or many lakes. "
        "Larger maps get more lakes of similar size. "
        "River valley enables a river; canyon replaces rivers. Islands disable plateau."
  };
  const CityDropdownItem *groups[] = {kCoasts, kRivers, kLandforms};
  const int counts[] = {sizeof(kCoasts) / sizeof(*kCoasts), sizeof(kRivers) / sizeof(*kRivers),
      sizeof(kLandforms) / sizeof(*kLandforms)};
  for (int group = 0; group < 3; ++group) {
    int y = 136 + group * 18;
    AddTerrainControl(layout, 0, (CityRect){8, y, 75, y + 14}, 0, 0, kGroupLabels[group]);
    AddTerrainControl(layout, CITY_SETUP_COAST + group, (CityRect){78, y, 270, y + 14},
        CITY_RESOURCE_CONTROL_DROPDOWN, 0, "");
    CityCanvasStartupControl *control = &layout->controls[layout->control_count - 1];
    control->items = groups[group]; control->item_count = counts[group]; control->tooltip = kGroupHelp[group];
  }
  static const CityDropdownItem kVariety[] = {
    {"No regional variety", CITY_TERRAIN_VARIETY_NONE, CITY_DROPDOWN_RADIO},
    {"Low variety", CITY_TERRAIN_VARIETY_LOW, CITY_DROPDOWN_RADIO},
    {"Medium variety", CITY_TERRAIN_VARIETY_MEDIUM, CITY_DROPDOWN_RADIO},
    {"High variety", CITY_TERRAIN_VARIETY_HIGH, CITY_DROPDOWN_RADIO},
    {"", 0, CITY_DROPDOWN_SEPARATOR},
    {"Very smooth", kTerrainSmoothnessFirst + CITY_TERRAIN_VERY_SMOOTH, CITY_DROPDOWN_RADIO},
    {"Smooth", kTerrainSmoothnessFirst + CITY_TERRAIN_SMOOTH, CITY_DROPDOWN_RADIO},
    {"Rough", kTerrainSmoothnessFirst + CITY_TERRAIN_ROUGH, CITY_DROPDOWN_RADIO},
    {"Very rough", kTerrainSmoothnessFirst + CITY_TERRAIN_VERY_ROUGH, CITY_DROPDOWN_RADIO}
  };
  AddTerrainControl(layout, 0, (CityRect){8, 190, 75, 204}, 0, 0, "Variety");
  AddTerrainControl(layout, CITY_SETUP_VARIETY, (CityRect){78, 190, 270, 204},
      CITY_RESOURCE_CONTROL_DROPDOWN, 0, "");
  CityCanvasStartupControl *variety = &layout->controls[layout->control_count - 1];
  variety->items = kVariety; variety->item_count = sizeof(kVariety) / sizeof(*kVariety);
  variety->tooltip = "Choose regional variety and terrain smoothness independently. "
      "Higher variety creates smaller areas of plains, hills and plateaus; roughness adds small-scale terrain detail.";
  AddTerrainControl(layout, 0, (CityRect){8, 214, 55, 228}, 0, 0, "Height");
  AddTerrainControl(layout, CITY_SETUP_HILLS, (CityRect){58, 214, 270, 228},
      CITY_RESOURCE_CONTROL_SLIDER, 0, "");
  AddTerrainControl(layout, 0, (CityRect){8, 232, 55, 246}, 0, 0, "Water");
  AddTerrainControl(layout, CITY_SETUP_WATER, (CityRect){58, 232, 270, 246},
      CITY_RESOURCE_CONTROL_SLIDER, 0, "");
  AddTerrainControl(layout, 0, (CityRect){8, 250, 55, 264}, 0, 0, "Trees");
  AddTerrainControl(layout, CITY_SETUP_TREES, (CityRect){58, 250, 270, 264},
      CITY_RESOURCE_CONTROL_SLIDER, 0, "");
  AddTerrainControl(layout, CITY_SETUP_REGENERATE, (CityRect){8, 276, 119, 298}, button, 0, "Regenerate");
  AddTerrainControl(layout, CITY_SETUP_LOAD_MAP, (CityRect){129, 276, 240, 298}, button, 0, "Load Map");
  AddTerrainControl(layout, CITY_SETUP_EDIT_TERRAIN, (CityRect){250, 276, 361, 298}, button, 0, "Edit Terrain");
  AddTerrainControl(layout, CITY_DIALOG_OK, (CityRect){371, 276, 480, 298}, button,
      CITY_RESOURCE_BUTTON_DEFAULT, "Start City");
}

static const char *TerrainDropdownCaption(int control, const CityDropdownItem *item) {
  if (control == CITY_SETUP_COAST && item->kind == CITY_DROPDOWN_CHECKBOX) {
    switch (item->value >> kTerrainCoastEdgeShift) {
      case CITY_TERRAIN_COAST_NORTH: return "N";
      case CITY_TERRAIN_COAST_EAST: return "E";
      case CITY_TERRAIN_COAST_SOUTH: return "S";
      case CITY_TERRAIN_COAST_WEST: return "W";
    }
  }
  switch (item->value) {
    case kCrossing: return "Crossing";
    case kRejoin: return "Rejoining";
    case kRolling: return "Hills";
    case kRidge: return "Ridge";
    case kValley: return "Valley";
    default: return item->text;
  }
}

static void UpdateTerrainFeatureDropdown(City *city, CityCanvasStartupControl *control) {
  const CityTerrainSettings *settings = &city->game->ui->terrain_settings;
  control->checked = TerrainDropdownRadioItem(settings, control, kTerrainGroupMasks[control->id - CITY_SETUP_COAST]);
  int lake_item = control->id == CITY_SETUP_LANDFORM ?
      TerrainDropdownRadioItem(settings, control, kLake | kLakes) : -1;
  u32 checked = 0, disabled = 0;
  int edges = settings->coast_sides;
  for (int i = 0; i < control->item_count; ++i) {
    const CityDropdownItem *item = &control->items[i];
    int edge = control->id == CITY_SETUP_COAST ? item->value >> kTerrainCoastEdgeShift : 0;
    bool selected = item->kind == CITY_DROPDOWN_RADIO && (i == control->checked || i == lake_item);
    if (IsTerrainSmallRiverItem(control->id, item))
      selected = item->value == kTerrainSmallRiversFirst + settings->small_rivers;
    if (item->kind == CITY_DROPDOWN_CHECKBOX) {
      selected = edge ? (edges & edge) != 0 : (settings->features & item->value) != 0;
    }
    if (TerrainDropdownItemDisabled(settings, control->id, item)) disabled |= 1u << i;
    if (selected) checked |= 1u << i;
  }
  control->text[0] = 0;
  // Show both radio sections before optional modifiers so the river amount
  // stays visible. The shared control ellipsizes any remaining overflow.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < control->item_count; ++i) {
      const CityDropdownItem *item = &control->items[i];
      if (!(checked & (1u << i)) || (item->kind == CITY_DROPDOWN_RADIO) != (pass == 0)) continue;
      if (control->id == CITY_SETUP_LANDFORM && item->value == kTerrainNoLakes) continue;
      if (IsTerrainSmallRiverItem(control->id, item) &&
          item->value == kTerrainSmallRiversFirst + CITY_TERRAIN_SMALL_RIVERS_NONE) continue;
      if (control->id == CITY_SETUP_RIVER && !item->value &&
          settings->small_rivers != CITY_TERRAIN_SMALL_RIVERS_NONE) continue;
      size_t length = strlen(control->text);
      snprintf(control->text + length, sizeof(control->text) - length, "%s%s", length ? ", " : "",
          TerrainDropdownCaption(control->id, item));
    }
  }
  CityCanvasDesktop *desktop = city->game->ui->startup_desktop;
  if (desktop->dropdown.items && desktop->dropdown.control == control->id) {
    desktop->dropdown.selected = control->checked;
    SetCityDropdownChecks(desktop, checked, disabled, control->text);
  }
}

static void UpdateTerrainSizeControls(City *city, CityCanvasStartupWindow *layout) {
  CityTerrainSettings *settings = &city->game->ui->terrain_settings;
  layout->map_width = settings->width ? settings->width : 128;
  layout->map_height = settings->height ? settings->height : 128;
  for (int i = 0; i < layout->control_count; ++i) {
    CityCanvasStartupControl *control = &layout->controls[i];
    if (control->id == CITY_SETUP_VARIETY) {
      static const char *const kVarietyNames[] = {"None", "Low", "Medium", "High"};
      int smooth_item = CITY_TERRAIN_VARIETY_COUNT + 1 + settings->smoothness;
      control->checked = settings->variety;
      snprintf(control->text, sizeof(control->text), "%s, %s",
          kVarietyNames[settings->variety], control->items[smooth_item].text);
      CityCanvasDesktop *desktop = city->game->ui->startup_desktop;
      if (desktop->dropdown.items && desktop->dropdown.control == control->id) {
        desktop->dropdown.selected = control->checked;
        SetCityDropdownChecks(desktop, (1u << control->checked) | (1u << smooth_item), 0, control->text);
      }
      continue;
    }
    if (IsTerrainFeatureDropdown(control->id)) {
      UpdateTerrainFeatureDropdown(city, control);
      continue;
    }
    if (control->id == CITY_SETUP_CLIMATE) {
      control->checked = settings->climate;
      CopyText(control->text, sizeof(control->text), control->items[settings->climate].text);
      continue;
    }
    if (control->id == CITY_SETUP_SNOW_COVERAGE || control->id == CITY_SETUP_DESERT_COVERAGE) {
      bool snow = control->id == CITY_SETUP_SNOW_COVERAGE;
      int value = snow ? settings->snow_coverage : settings->desert_coverage;
      bool enabled = settings->climate == CITY_TERRAIN_MIXED ||
          settings->climate == (snow ? CITY_TERRAIN_SNOWY : CITY_TERRAIN_DESERT);
      control->style &= ~CITY_RESOURCE_STYLE_DISABLED;
      if (!enabled) control->style |= CITY_RESOURCE_STYLE_DISABLED;
      snprintf(control->text, sizeof(control->text), "%d%%", value);
      for (int item = 0; item < control->item_count; ++item)
        if (control->items[item].value == value) control->checked = item;
      continue;
    }
    if (control->id != CITY_SETUP_MAP_WIDTH && control->id != CITY_SETUP_MAP_HEIGHT) continue;
    int value = control->id == CITY_SETUP_MAP_WIDTH ? settings->width : settings->height;
    if (!value) value = 128;
    snprintf(control->text, sizeof(control->text), "%d", value);
    for (int item = 0; item < control->item_count; ++item)
      if (control->items[item].value == value) control->checked = item;
  }
}

static void BuildYearQueryLayout(City *city, CityCanvasStartupWindow *layout) {
  *layout = (CityCanvasStartupWindow){.client_bounds = {0, 0, 250, 40}, .year_query = true};
  CopyText(layout->title, sizeof(layout->title), "Change starting year");
  int year = city->game->ui->setup_window->dialog->start_year;
  if (!year) year = 1900 + 50 * city->game->ui->setup_window->dialog->era;
  char text[16]; snprintf(text, sizeof(text), "%d", year);
  AddTerrainControl(layout, CITY_SETUP_YEAR_INPUT, (CityRect){2, 2, 232, 16}, CITY_RESOURCE_CONTROL_EDIT, 0, text);
  AddTerrainControl(layout, CITY_SETUP_YEAR_CLEAR, (CityRect){232, 2, 248, 16}, CITY_RESOURCE_CONTROL_BUTTON, 0, "X");
  AddTerrainControl(layout, CITY_SETUP_YEAR_DEFAULT, (CityRect){2, 19, 84, 38}, CITY_RESOURCE_CONTROL_BUTTON, 0, "Default");
  AddTerrainControl(layout, CITY_DIALOG_CANCEL, (CityRect){84, 19, 166, 38}, CITY_RESOURCE_CONTROL_BUTTON, 0, "Cancel");
  AddTerrainControl(layout, CITY_DIALOG_OK, (CityRect){166, 19, 248, 38}, CITY_RESOURCE_CONTROL_BUTTON, 0, "OK");
  layout->controls[3].tooltip = "Keep the previous starting year.";
  layout->controls[4].tooltip = "Use this starting year.";
  layout->focused_control = CITY_SETUP_YEAR_INPUT;
  layout->selected_text = 1;
  layout->caret_visible = true;
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
  UpdateTerrainSizeControls(city, &window->layout);
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
  CityFilePickerOptions options = {"*.sc2", CityPathText(&directory), "Load Map", true, AcceptCityTerrainFile, request};
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

static bool MoveCityTerrainSetupPointer(City *city, CityStartupResourceWindow *window, int x, int y) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui->terrain_setup_active || ui->setup_window == NULL || ui->setup_window->answered) return false;
  int id = window->layout.pressed_control;
  if (id < CITY_SETUP_HILLS || id > CITY_SETUP_TREES) return false;
  Window *canvas = CanvasWindow(city, window->window_id);
  CityCanvasStartupControl *control = NULL;
  for (int i = 0; i < window->layout.control_count; ++i)
    if (window->layout.controls[i].id == id) control = &window->layout.controls[i];
  if (control->control_class != CITY_RESOURCE_CONTROL_SLIDER) return false;
  int position = x - canvas->bounds.left - 2 - control->bounds.left;
  int value = IntClamp(position * 47 / (control->bounds.right - control->bounds.left - 1), 0, 47);
  int *values[] = {&ui->terrain_settings.hills, &ui->terrain_settings.water, &ui->terrain_settings.trees};
  if (*values[id - CITY_SETUP_HILLS] != value) {
    *values[id - CITY_SETUP_HILLS] = value;
    GenerateCityTerrainPreview(city);
  }
  (void)y;
  return true;
}

static bool BuildNativeStartupLayout(u32 resource, CityCanvasStartupWindow *layout) {
  if (resource == CITY_DIALOG_RESOURCE_STARTUP_MENU) {
    static const struct { int control; const char *caption, *tooltip; } kButtons[] = {
      {CITY_STARTUP_CONTROL_NEW_CITY, "New City", "Choose city settings and generate or edit terrain."},
      {CITY_STARTUP_CONTROL_LOAD_CITY, "Load City", "Open a saved city."},
      {CITY_STARTUP_CONTROL_SETTINGS, "Settings", "Open the game settings."},
      {CITY_STARTUP_CONTROL_JUKEBOX, "Music Jukebox", "Choose music and adjust playback and volume."},
      {CITY_STARTUP_CONTROL_WEBSITE, "Website", "Open www.opensc2k.com in your default browser."},
      {CITY_STARTUP_CONTROL_QUIT, "Exit", "Exit OpenSC2k."}
    };
    CopyText(layout->title, sizeof(layout->title), SC2K_APP_TITLE);
    layout->main_menu = true;
    layout->focused_control = CITY_STARTUP_CONTROL_NEW_CITY;
    layout->client_bounds = (CityRect){0, 0, 216, 280};
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(*kButtons); ++i) {
      int top = (int)i * 40;
      AddTerrainControl(layout, kButtons[i].control, (CityRect){8, top, 208, top + 36},
          CITY_RESOURCE_CONTROL_BUTTON, 0, kButtons[i].caption);
      layout->controls[layout->control_count - 1].tooltip = kButtons[i].tooltip;
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
      BuildCitySetupLayout(city, &window->layout);
    else if (resource == CITY_SETUP_YEAR_QUERY_RESOURCE) BuildYearQueryLayout(city, &window->layout);
    else if (!BuildNativeStartupLayout(resource, &window->layout)) return false;
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
    case CITY_STARTUP_LOAD_CITY: return StartupMenuLoadCity(city, pending->dialog);
    case CITY_STARTUP_NEW_CITY: return StartupMenuNewCity(city, pending->dialog);
    case CITY_STARTUP_EDIT_MAP: return StartupMenuEditMap(city, pending->dialog);
    case CITY_STARTUP_LOAD_SCENARIO: return StartupMenuLoadScenario(city, pending->dialog);
    case CITY_STARTUP_QUIT: return StartupMenuQuit(city, pending->dialog);
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

bool SetCityStartupMenuPresentation(City *city, CityCanvasDesktop *desktop) {
  CityInterfaceState *ui = city->game->ui;
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
  ConstructNewCitySetupDialog(city, &pending->owned_dialog);
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
  if (IsTerrainGroupedDropdown(id)) UpdateTerrainSizeControls(city, &window->layout);
}

static void ActivateCitySetupControl(City *city, CityStartupResourceWindow *window, int control, bool shift);

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

void InitializeCitySetupName(City *city, CityNewCitySetupDialog *dialog) {
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

// Setup defaults and the resource895 city name.
CityNewCitySetupDialog *ConstructNewCitySetupDialog(City *city, CityNewCitySetupDialog *dialog) {
  dialog->base = (CityStartupWindow){.resource_id = CITY_DIALOG_RESOURCE_NEW_CITY};
  dialog->city_name[0] = 0;
  dialog->previous_name = (CityName){0};
  dialog->era = CITY_SETUP_ERA_1900;
  dialog->start_year = 0;
  dialog->difficulty = CITY_SETUP_DIFFICULTY_EASY;
  InitializeCitySetupName(city, dialog);
  return dialog;
}

// Release setup name storage; original base destruction is comparison-only.
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
      UpdateTerrainSizeControls(city, &window->layout);
      NotifyStartupState(city);
      return true;
    }
    if (IsTerrainFeatureDropdown(selection->control)) {
      CityCanvasStartupControl *control = SetupControl(city, selection->control);
      if (!control || !control->visible) return true;
      int index = 0;
      while (index < control->item_count && (control->items[index].value != selection->value ||
          control->items[index].kind == CITY_DROPDOWN_SEPARATOR)) ++index;
      if (index == control->item_count ||
          TerrainDropdownItemDisabled(&ui->terrain_settings, control->id, &control->items[index])) return true;
      const CityDropdownItem *item = &control->items[index];
      CityTerrainSettings previous = ui->terrain_settings;
      CityTerrainSettings *settings = &ui->terrain_settings;
      if (IsTerrainSmallRiverItem(control->id, item)) {
        settings->small_rivers = item->value - kTerrainSmallRiversFirst;
      } else if (item->kind == CITY_DROPDOWN_RADIO) {
        settings->features &= ~TerrainDropdownRadioGroup(control->id, item);
        if (control->id == CITY_SETUP_COAST && !item->value) {
          settings->features &= ~(kCliffs | kDelta);
          settings->coast_sides = 0;
        } else if (control->id == CITY_SETUP_RIVER) {
          if (item->value) settings->features |= kRiver | item->value;
          else settings->features &= ~(kMeander | kDelta | kValley | kSalty);
        } else if (item->value > 0) {
          settings->features &= ~DisabledCityTerrainFeatures(item->value);
          ToggleCityTerrainFeature(settings, item->value);
        }
      } else if (control->id == CITY_SETUP_COAST && item->value >> kTerrainCoastEdgeShift) {
        settings->coast_sides ^= item->value >> kTerrainCoastEdgeShift;
        settings->features |= kOcean;
      } else ToggleCityTerrainFeature(settings, item->value);
      if (!GenerateCityTerrainPreview(city)) ui->terrain_settings = previous;
      UpdateTerrainSizeControls(city, &window->layout);
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
      UpdateTerrainSizeControls(city, &window->layout);
      NotifyStartupState(city);
      return true;
    }
    int *value = selection->control == CITY_SETUP_MAP_WIDTH ? &ui->terrain_settings.width :
        selection->control == CITY_SETUP_MAP_HEIGHT ? &ui->terrain_settings.height : NULL;
    if (!value || !IsSupportedMapAxis(selection->value)) return true;
    int previous = *value;
    *value = selection->value;
    if (!GenerateCityTerrainPreview(city)) *value = previous;
    UpdateTerrainSizeControls(city, &window->layout);
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
      MoveCityTerrainSetupPointer(city, window, x, y);
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
  case CANVAS_SDL_POINTER_MOVE: return MoveCityTerrainSetupPointer(city, window, x, y);
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
      for (int i = 0; i < layout->control_count; i++)
        if (layout->controls[i].id == field) current = i;
      for (int step = 1; step <= layout->control_count; step++) {
        CityCanvasStartupControl *control = &layout->controls[(current + step) % layout->control_count];
        if (control->id && control->control_class && control->visible &&
            !(control->style & CITY_RESOURCE_STYLE_DISABLED)) {
          layout->focused_control = control->id;
          break;
        }
      }
      layout->selected_text = layout->focused_control == CITY_SETUP_NAME || layout->focused_control == CITY_SETUP_MAYOR;
      if (layout->selected_text) {
        CityCanvasStartupControl *focused = SetupControl(city, layout->focused_control);
        focused->text_caret = strlen(focused->text);
      }
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

// Complete the startup menu with the terrain-editing choice. Requires the live startup interaction; returns
// EndCityStartupDialog's completion result.
static bool StartupMenuEditMap(City *city, CityStartupMenuDialog *dialog) {
  return EndCityStartupDialog(city, dialog->window_id, CITY_STARTUP_EDIT_MAP);
}

// Complete the startup menu with the load-city choice. Requires the live startup interaction; returns
// EndCityStartupDialog's completion result and does not load a file itself.
static bool StartupMenuLoadCity(City *city, CityStartupMenuDialog *dialog) {
  return EndCityStartupDialog(city, dialog->window_id, CITY_STARTUP_LOAD_CITY);
}

// Complete the startup menu with the scenario choice. Requires the live startup interaction; actual scenario
// selection/loading belongs to the caller after the dialog returns.
static bool StartupMenuLoadScenario(City *city, CityStartupMenuDialog *dialog) {
  return EndCityStartupDialog(city, dialog->window_id, CITY_STARTUP_LOAD_SCENARIO);
}

// Complete the startup menu with the new-city choice. Requires the live startup interaction; returns the dialog
// completion result without generating terrain here.
static bool StartupMenuNewCity(City *city, CityStartupMenuDialog *dialog) {
  return EndCityStartupDialog(city, dialog->window_id, CITY_STARTUP_NEW_CITY);
}

// Complete the startup menu with the quit choice. Requires the live startup interaction; shutdown remains the owning
// caller's decision after the dialog closes.
static bool StartupMenuQuit(City *city, CityStartupMenuDialog *dialog) {
  return EndCityStartupDialog(city, dialog->window_id, CITY_STARTUP_QUIT);
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
