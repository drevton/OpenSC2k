#include "ui/startup/startup_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "assets/resource_types.h"
#include "base/text.h"
#include "base/version.h"
#include "city/terrain/terrain.h"
#include "ui/dialogs/messages.h"
#include "ui/state.h"
#include "ui/windows/controls.h"

// Build startup controls and present local setup choices. The host owns windows and terrain generation.

enum {
  kTerrainCoastEdgeShift = kTerrainFeatureCount, kTerrainNoLakes = -1,
  kTerrainSmallRiversFirst = 1 << (kTerrainCoastEdgeShift + 4)
};
static const u32 kTerrainGroupMasks[] = {
  kOcean | kRandomCoast,
  kRiver | kBranch | kCrossing | kRejoin,
  kRolling | kPlateau | kRidge | kBasin | kValley | kCanyon
};

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
      control->tooltip =
          "More water widens broad rivers, expands coasts and lakes, and adds scattered freshwater ponds.";
      break;
    case CITY_SETUP_TREES: control->tooltip = "Adjust the amount of tree cover."; break;
    case CITY_SETUP_EDIT_TERRAIN:
      control->tooltip = "Edit this terrain for free before starting the city. Keep the chosen city settings."; break;
    case CITY_DIALOG_OK: control->tooltip = "Start the city with this terrain and these settings."; break;
    case CITY_DIALOG_CANCEL: control->tooltip = "Cancel without replacing the current city."; break;
  }
}

void BuildCitySetupLayout(const CityInterfaceState *ui, CityCanvasStartupWindow *layout) {
  *layout = (CityCanvasStartupWindow){.client_bounds = {0, 0, 488, 306}, .terrain_setup = true};
  CopyText(layout->title, sizeof(layout->title), "New City");
  const int button = CITY_RESOURCE_CONTROL_BUTTON, edit = CITY_RESOURCE_CONTROL_EDIT;
  AddTerrainControl(layout, 0, (CityRect){8, 6, 75, 20}, 0, 0, "City name");
  AddTerrainControl(layout, CITY_SETUP_NAME, (CityRect){78, 6, 254, 20}, edit, 0, "New City");
  AddTerrainControl(layout, CITY_SETUP_RANDOM_NAME, (CityRect){256, 6, 270, 20}, button, 0, "");
  AddTerrainControl(layout, 0, (CityRect){8, 24, 75, 38}, 0, 0, "Mayor");
  AddTerrainControl(layout, CITY_SETUP_MAYOR, (CityRect){78, 24, 270, 38}, edit, 0,
      ui->terrain_mayor);
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

static void UpdateTerrainFeatureDropdown(CityInterfaceState *ui, CityCanvasStartupControl *control) {
  const CityTerrainSettings *settings = &ui->terrain_settings;
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
  CityCanvasDesktop *desktop = ui->startup_desktop;
  if (desktop->dropdown.items && desktop->dropdown.control == control->id) {
    desktop->dropdown.selected = control->checked;
    SetCityDropdownChecks(desktop, checked, disabled, control->text);
  }
}

void RefreshCityTerrainControls(CityInterfaceState *ui, CityCanvasStartupWindow *layout) {
  const CityTerrainSettings *settings = &ui->terrain_settings;
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
      CityCanvasDesktop *desktop = ui->startup_desktop;
      if (desktop->dropdown.items && desktop->dropdown.control == control->id) {
        desktop->dropdown.selected = control->checked;
        SetCityDropdownChecks(desktop, (1u << control->checked) | (1u << smooth_item), 0, control->text);
      }
      continue;
    }
    if (IsTerrainFeatureDropdown(control->id)) {
      UpdateTerrainFeatureDropdown(ui, control);
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

void BuildCityYearQueryLayout(const CityNewCitySetupDialog *dialog, CityCanvasStartupWindow *layout) {
  *layout = (CityCanvasStartupWindow){.client_bounds = {0, 0, 250, 40}, .year_query = true};
  CopyText(layout->title, sizeof(layout->title), "Change starting year");
  int year = dialog->start_year;
  if (!year) year = 1900 + 50 * dialog->era;
  char text[16]; snprintf(text, sizeof(text), "%d", year);
  AddTerrainControl(layout, CITY_SETUP_YEAR_INPUT, (CityRect){2, 2, 232, 16}, CITY_RESOURCE_CONTROL_EDIT, 0, text);
  AddTerrainControl(layout, CITY_SETUP_YEAR_CLEAR, (CityRect){232, 2, 248, 16}, CITY_RESOURCE_CONTROL_BUTTON, 0, "X");
  AddTerrainControl(layout, CITY_SETUP_YEAR_DEFAULT, (CityRect){2, 19, 84, 38},
      CITY_RESOURCE_CONTROL_BUTTON, 0, "Default");
  AddTerrainControl(layout, CITY_DIALOG_CANCEL, (CityRect){84, 19, 166, 38}, CITY_RESOURCE_CONTROL_BUTTON, 0, "Cancel");
  AddTerrainControl(layout, CITY_DIALOG_OK, (CityRect){166, 19, 248, 38}, CITY_RESOURCE_CONTROL_BUTTON, 0, "OK");
  layout->controls[3].tooltip = "Keep the previous starting year.";
  layout->controls[4].tooltip = "Use this starting year.";
  layout->focused_control = CITY_SETUP_YEAR_INPUT;
  layout->selected_text = 1;
  layout->caret_visible = true;
}

bool BuildCityStartupLayout(u32 resource, CityCanvasStartupWindow *layout) {
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

bool SelectCityTerrainFeature(CityTerrainSettings *settings, const CityCanvasStartupControl *control, int value) {
  int index = 0;
  while (index < control->item_count && (control->items[index].value != value ||
      control->items[index].kind == CITY_DROPDOWN_SEPARATOR)) ++index;
  if (index == control->item_count ||
      TerrainDropdownItemDisabled(settings, control->id, &control->items[index])) return false;
  const CityDropdownItem *item = &control->items[index];
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
  return true;
}
