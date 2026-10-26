#include "ui/menus/catalog.h"

#include "city/disasters/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/trees.h"
#include "drawing/maps/map_view.h"
#include "ui/charts/charts.h"

// Immutable choices shared by toolbar layout, dropdowns, shortcuts and construction windows.
// Each button is keyed by its UI identity, independently of the artwork and visible toolbar order.

static const CityMenuItem kFileItems[] = {
  {"Save game", (CITY_HOTKEY_CTRL | 'S'), CITY_MENU_ACTION_SAVE, 0, 0},
  {"Load game", (CITY_HOTKEY_CTRL | 'L'), CITY_MENU_ACTION_LOAD, 0, 0},
  {"Abandon game", CITY_KEY_NONE, CITY_MENU_ACTION_ABANDON, 0, 0},
  {"", CITY_KEY_NONE, CITY_MENU_ACTION_SEPARATOR, 0, 0},
  {"Exit", (CITY_HOTKEY_CTRL | 'Q'), CITY_MENU_ACTION_QUIT, 0, 0}
};
const CityMenuItem kTerrainFileItems[] = {
  {"Save terrain", (CITY_HOTKEY_CTRL | 'S'), CITY_MENU_ACTION_SAVE, 0, 0},
  {"Load game", (CITY_HOTKEY_CTRL | 'L'), CITY_MENU_ACTION_LOAD, 0, 0},
  {"Abandon terrain", CITY_KEY_NONE, CITY_MENU_ACTION_ABANDON, 0, 0},
  {"", CITY_KEY_NONE, CITY_MENU_ACTION_SEPARATOR, 0, 0},
  {"Exit", (CITY_HOTKEY_CTRL | 'Q'), CITY_MENU_ACTION_QUIT, 0, 0}
};
static const CityMenuItem kSpeedItems[] = {
  {"Slow", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_SET_SIMULATION_SPEED, CITY_SPEED_SLOW},
  {"Normal", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_SET_SIMULATION_SPEED, CITY_SPEED_NORMAL},
  {"Fast", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_SET_SIMULATION_SPEED, CITY_SPEED_FAST},
  {"Unlimited", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_SET_SIMULATION_SPEED, CITY_SPEED_UNLIMITED}
};
static const CityMenuItem kOptionItems[] = {
  {"Game Options", (CITY_HOTKEY_CTRL | 'P'), CITY_MENU_ACTION_OPTIONS, 0, 0},
  {"", CITY_KEY_NONE, CITY_MENU_ACTION_SEPARATOR, 0, 0},
  {"Developer console", (CITY_HOTKEY_CTRL | CITY_KEY_F12), CITY_MENU_ACTION_CONSOLE, 0, 0},
  {"Auto Budget", CITY_KEY_F6, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_TOGGLE_AUTO_BUDGET, 0},
  {"Auto Goto", CITY_KEY_F7, CITY_MENU_ACTION_AUTO_GOTO, 0, 0},
  {"Sound & Music...", CITY_KEY_NONE, CITY_MENU_ACTION_SOUND_OPTIONS, 0, 0},
  {"Music", CITY_KEY_F9, CITY_MENU_ACTION_MUSIC, 0, 0},
  {"Jazz Jukebox...", (CITY_HOTKEY_CTRL | 'J'), CITY_MENU_ACTION_JUKEBOX, 0, 0}
};
const CityMenuItem kDisasterItems[] = {
  {"No Disasters", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_TOGGLE_NO_DISASTERS, 0},
  {"", CITY_KEY_NONE, CITY_MENU_ACTION_SEPARATOR, 0, 0},
  {"Fire", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_FIRE},
  {"Flood", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_FLOOD},
  {"Riot", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_RIOTS},
  {"Plane Crash", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_SCHEDULED_PLANE_CRASH},
  {"Tornado", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_TORNADO},
  {"Earthquake", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_EARTHQUAKE},
  {"Monster", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_MONSTER},
  {"Hurricane", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_HURRICANE},
  {"", CITY_KEY_NONE, CITY_MENU_ACTION_SEPARATOR, 0, 0},
  {"Toxic Spill", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_POLLUTION},
  {"Meltdown", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_MELTDOWN},
  {"Microwave", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_MICROWAVE},
  {"Volcano", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_VOLCANO},
  {"Firestorm", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_FIRE_STORM},
  {"Mass Riots", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_MASS_RIOTS},
  {"Mass Floods", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_MAJOR_FLOOD},
  {"Pollution", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_CHEMICAL_SPILL},
  {"Air Crash", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_CRASH},
  {"Helicopter Crash", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_START_DISASTER,
      CITY_DISASTER_HELICOPTER_CRASH},
};
static const CityMenuItem kMapItems[] = {
  {"Map Data", 'M', CITY_MENU_ACTION_MAP_DATA, 0, 0},
  {"Extra Viewport", CITY_KEY_F8, CITY_MENU_ACTION_VIEWPORT, 0, 0},
  {"Sign List", CITY_KEY_F4, CITY_MENU_ACTION_SIGN_LIST, 0, 0},
  {"Center View", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_CENTER}
};
static const CityMenuItem kReviewItems[] = {
  {"Budget", (CITY_HOTKEY_CTRL | 'B'), CITY_MENU_ACTION_BUDGET, 0, 0},
  {"Ordinances", (CITY_HOTKEY_CTRL | 'O'), CITY_MENU_ACTION_POLICIES, 0, 0},
  {"Advisors", CITY_KEY_NONE, CITY_MENU_ACTION_ADVISOR, 0, CITY_BUDGET_PROPERTY_TAX},
};
const CityMenuItem kChartItems[] = {
  {"Graphs", (CITY_HOTKEY_CTRL | 'H'), CITY_MENU_ACTION_PANEL, 0, CITY_PANEL_HISTORY},
  [kChartPopulation] = {"Population", (CITY_HOTKEY_CTRL | CITY_HOTKEY_SHIFT | 'P'),
      CITY_MENU_ACTION_PANEL, 0, CITY_PANEL_DEMOGRAPHICS},
  {"Industry", (CITY_HOTKEY_CTRL | CITY_HOTKEY_SHIFT | 'I'), CITY_MENU_ACTION_PANEL, 0, CITY_PANEL_INDUSTRY},
  {"Neighbors", (CITY_HOTKEY_CTRL | 'W'), CITY_MENU_ACTION_PANEL, 0,
      CITY_PANEL_NEIGHBORS},
  {"Land Use", CITY_KEY_NONE, CITY_MENU_ACTION_LAND_USE, 0, 0}
};
static const CityMenuItem kNewspaperItems[] = {
  {"Latest Edition", 'N', CITY_MENU_ACTION_NEWSPAPER, 0, 0},
  {"Subscription", CITY_KEY_NONE, CITY_MENU_ACTION_SUBSCRIPTION, 0, 0},
  {"Extra Editions", CITY_KEY_NONE, CITY_MENU_ACTION_EXTRA_NEWSPAPER, 0, 0}
};
static const CityMenuItem kLayersItems[] = {
  {"Buildings", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0,
      CITY_MAP_LAYER_BUILDINGS},
  {"Networks", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0,
      CITY_MAP_LAYER_NETWORKS},
  {"Water", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0, CITY_MAP_LAYER_WATER},
  {"Trees", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0, CITY_MAP_LAYER_TREES},
  {"Signs", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0, CITY_MAP_LAYER_SIGNS},
  {"Vehicles", CITY_KEY_NONE, CITY_MENU_ACTION_MAP_LAYER, 0, CITY_MAP_LAYER_VEHICLES}
};
static const CityMenuItem kBulldozeItems[] = {
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH},
  {"De-zone", 'V', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEZONE}
};
const CityDropdownItem kTreeClimateItems[] = {
  {"Temperate", CLIMATE_TEMPERATE, CITY_DROPDOWN_PLAIN},
  {"Winter", CLIMATE_ARCTIC, CITY_DROPDOWN_PLAIN},
  {"Tropical", CLIMATE_TROPICAL, CITY_DROPDOWN_PLAIN},
  {"Desert", CLIMATE_DESERT_50, CITY_DROPDOWN_PLAIN},
  {"SimCity Classic", CLIMATE_SIMCITY, CITY_DROPDOWN_PLAIN}
};

const CityMenuItem kTreeItems[] = {
  {"Tree type 1", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 1},
  {"Tree type 2", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 2},
  {"Tree type 3", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 3},
  {"Tree type 4", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 4},
  {"Tree type 5", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 5},
  {"Tree type 6", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 6},
  {"Tree type 7", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 7},
  {"Tree type 8", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 8},
  {"Tree type 9", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 9},
  {"Tree type 10", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 10},
  {"Tree type 11", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 11},
  {"Tree type 12", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, 12},
  {"Trees of random type", CITY_KEY_NONE, CITY_MENU_ACTION_TREE_SPECIES, 0, CITY_TREE_RANDOM}
};
static const CityMenuItem kLandscapeItems[] = {
  {"Trees", 'U', CITY_MENU_ACTION_CATEGORY, 0, CITY_TOPBAR_TREES},
  {"Build Canals", (CITY_HOTKEY_SHIFT | 'U'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WATER},
  {"Lower Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LOWER_TERRAIN},
  {"Raise Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RAISE_TERRAIN},
  {"Stretch Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_STRETCH_TERRAIN},
  {"Level Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LEVEL_TERRAIN},
  {"Place Sign", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SIGN},
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}
};
static const CityMenuItem kResidentialItems[] = {
  {"Light Residential", 'Q', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RESIDENTIAL},
  {"Dense Residential", 'E', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DENSE_RESIDENTIAL}
};
static const CityMenuItem kCommercialItems[] = {
  {"Light Commercial", 'A', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_COMMERCIAL},
  {"Dense Commercial", 'D', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DENSE_COMMERCIAL}
};
static const CityMenuItem kIndustrialItems[] = {
  {"Light Industrial", 'Z', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_INDUSTRIAL},
  {"Dense Industrial", 'C', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DENSE_INDUSTRIAL}
};
static const CityMenuItem kTransportItems[] = {
  {"Road", (CITY_HOTKEY_CTRL | 'R'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_ROAD},
  {"Road Bridge", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_ROAD_BRIDGE},
  {"Tunnel", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_TUNNEL},
  {"Highway", (CITY_HOTKEY_CTRL | CITY_HOTKEY_SHIFT | 'R'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_HIGHWAY},
  {"Onramp", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_ONRAMP},
  {"Bus Depot", (CITY_HOTKEY_ALT | 'R'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_BUS_DEPOT},
  {"Railway", 'T', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RAIL},
  {"Rail Bridge", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RAIL_BRIDGE},
  {"Subway", (CITY_HOTKEY_SHIFT | 'T'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SUBWAY},
  {"Rail Station", (CITY_HOTKEY_ALT | 'T'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RAIL_DEPOT},
  {"Subway Station", (CITY_HOTKEY_ALT | CITY_HOTKEY_SHIFT | 'T'), CITY_MENU_ACTION_TOOL, 0,
      CITY_CANVAS_TOOL_SUBWAY_STATION},
  {"Subway-to-Rail Connection", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SUBWAY_RAIL_CONNECTION},
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}
};
enum { kTransportItemCount = sizeof(kTransportItems) / sizeof(*kTransportItems) };
static const CityMenuItem kPortsItems[] = {
  {"Seaport Zone", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SEAPORT},
  {"Airport Zone", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_AIRPORT}
};
static const CityMenuItem kPowerItems[] = {
  {"Coal Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_COAL},
  {"Oil Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_OIL},
  {"Gas Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_GAS},
  {"Nuclear Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_NUCLEAR},
  {"Hydroelectric Power", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_HYDRO},
  {"Wind Turbine", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WIND},
  {"Solar Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SOLAR},
  {"Microwave Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_MICROWAVE},
  {"Fusion Power Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_FUSION}
};
static const CityMenuItem kWaterItems[] = {
  {"Water Pump", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WATER_PUMP},
  {"Water Tower", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WATER_TOWER},
  {"Water Treatment Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WATER_TREATMENT},
  {"Desalination Plant", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DESALINATION}
};
static const CityMenuItem kEducationItems[] = {
  {"Hospital", 'H', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_HOSPITAL},
  {"School", 'K', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SCHOOL},
  {"College", (CITY_HOTKEY_SHIFT | 'K'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_COLLEGE},
  {"Library", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LIBRARY},
  {"Museum", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_MUSEUM}
};
static const CityMenuItem kServicesItems[] = {
  {"Police Station", 'P', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_POLICE},
  {"Prison", (CITY_HOTKEY_SHIFT | 'P'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_PRISON}
};
static const CityMenuItem kRecreationItems[] = {
  {"Small Park", 'J', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SMALL_PARK},
  {"Large Park", (CITY_HOTKEY_SHIFT | 'J'), CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_BIG_PARK},
  {"Zoo", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_ZOO},
  {"Stadium", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_STADIUM},
  {"Marina", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_MARINA}
};
static const CityMenuItem kRewardsItems[] = {
  {"Mayor's House", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_MAYORS_HOUSE},
  {"City Hall", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_CITY_HALL},
  {"Statue", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_STATUE},
  {"Wildlife Dome", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LLAMA_DOME},
  {"Terraced Arcology", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_PLYMOUTH_ARCOLOGY},
  {"Woodland Arcology", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_FOREST_ARCOLOGY},
  {"Tower Arcology", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DARCO_ARCOLOGY},
  {"Spaceport Arcology", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LAUNCH_ARCOLOGY}
};
static const CityMenuItem kEmergencyItems[] = {
  {"Go to Disaster", CITY_KEY_NONE, CITY_MENU_ACTION_GOTO_DISASTER, 0, 0},
  {"Dispatch Police", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DISPATCH_POLICE},
  {"Dispatch Firefighters", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DISPATCH_FIRE},
  {"Dispatch Military", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DISPATCH_MILITARY},
  {"Cancel Dispatch", CITY_KEY_NONE, CITY_MENU_ACTION_COMMAND, CITY_COMMAND_RECALL_DISPATCH, 0},
  {"Start Disaster...", CITY_KEY_NONE, CITY_MENU_ACTION_DISASTERS, 0, 0}
};
static const CityMenuItem kHelpItems[] = {
  {"Inspect Tile", CITY_KEY_F12, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_QUERY},
  {"Tool Help", CITY_KEY_NONE, CITY_MENU_ACTION_HELP, 0, 0},
  {"Keyboard Shortcuts", CITY_KEY_NONE, CITY_MENU_ACTION_HELP, 0, 1},
  {"About", CITY_KEY_NONE, CITY_MENU_ACTION_HELP, 0, 2}
};

const CityToolbarDefinition kCityToolbarDefinitions[CITY_TOPBAR_COUNT] = {
  [CITY_TOPBAR_PAUSE] = {"Pause", NULL, 0, false, false, {"Pause", CITY_KEY_SPACE, CITY_MENU_ACTION_PAUSE, 0, 0}},
  [CITY_TOPBAR_SPEED] = {"Speed", kSpeedItems, sizeof(kSpeedItems) / sizeof(*kSpeedItems), false, false, {0}},
  [CITY_TOPBAR_FILE] = {"File", kFileItems, sizeof(kFileItems) / sizeof(*kFileItems), false, false, {0}},
  [CITY_TOPBAR_SETTINGS] = {"Settings", kOptionItems, sizeof(kOptionItems) / sizeof(*kOptionItems), false, false, {0}},
  [CITY_TOPBAR_BUDGET] = {"Adjust & Review", kReviewItems, sizeof(kReviewItems) / sizeof(*kReviewItems), false, true,
      {"Budget", (CITY_HOTKEY_CTRL | 'B'), CITY_MENU_ACTION_BUDGET, 0, 0}},
  [CITY_TOPBAR_ORDINANCES] = {"Ordinances", NULL, 0, false, false, {"Ordinances", (CITY_HOTKEY_CTRL | 'O'),
      CITY_MENU_ACTION_POLICIES, 0, 0}},
  [CITY_TOPBAR_GRAPHS] = {"Charts", kChartItems, sizeof(kChartItems) / sizeof(*kChartItems), false, false, {0}},
  // Retain the artwork index; advisors are accessed through Adjust & Review.
  [CITY_TOPBAR_REPORTS] = {"Advisors", NULL, 0, false, false, {0}},
  [CITY_TOPBAR_NEWSPAPER] = {"Newspaper", kNewspaperItems, sizeof(kNewspaperItems) / sizeof(*kNewspaperItems),
      false, false, {0}},
  [CITY_TOPBAR_MAP] = {"Map", kMapItems, sizeof(kMapItems) / sizeof(*kMapItems), false, true, {0}},
  [CITY_TOPBAR_ZOOM_IN] = {"Zoom In", NULL, 0, false, false, {"Zoom In", '+', CITY_MENU_ACTION_TOOLBAR, 0,
      CITY_TOOLBAR_BUTTON_ZOOM_IN}},
  [CITY_TOPBAR_ZOOM_OUT] = {"Zoom Out", NULL, 0, false, false, {"Zoom Out", '-', CITY_MENU_ACTION_TOOLBAR, 0,
      CITY_TOOLBAR_BUTTON_ZOOM_OUT}},
  // The isometric projection reverses the grid rotation's screen direction.
  [CITY_TOPBAR_ROTATE] = {"Rotate Clockwise", NULL, 0, false, false,
      {"Rotate Clockwise", CITY_KEY_PAGE_DOWN, CITY_MENU_ACTION_TOOLBAR, 0,
          CITY_TOOLBAR_BUTTON_ROTATE_COUNTERCLOCKWISE}},
  [CITY_TOPBAR_LAYERS] = {"Layers", kLayersItems, sizeof(kLayersItems) / sizeof(*kLayersItems), false, false, {0}},
  [CITY_TOPBAR_BULLDOZE] = {"Bulldoze", kBulldozeItems, sizeof(kBulldozeItems) / sizeof(*kBulldozeItems), true,
      true, {0}},
  [CITY_TOPBAR_LANDSCAPE] = {"Landscape", kLandscapeItems, sizeof(kLandscapeItems) / sizeof(*kLandscapeItems),
      true, true, {0}},
  [CITY_TOPBAR_RESIDENTIAL] = {"Zone", kResidentialItems,
      sizeof(kResidentialItems) / sizeof(*kResidentialItems), true, true, {0}},
  [CITY_TOPBAR_COMMERCIAL] = {"Commercial", kCommercialItems,
      sizeof(kCommercialItems) / sizeof(*kCommercialItems), true, false, {0}},
  [CITY_TOPBAR_INDUSTRIAL] = {"Industrial", kIndustrialItems,
      sizeof(kIndustrialItems) / sizeof(*kIndustrialItems), true, false, {0}},
  [CITY_TOPBAR_ROADS] = {"Build Transportation", kTransportItems, kTransportItemCount, true, true, {0}},
  [CITY_TOPBAR_RAIL] = {"Rail", NULL, 0, false, false, {0}},
  [CITY_TOPBAR_PORTS] = {"Airport & Seaport Zones", kPortsItems, sizeof(kPortsItems) / sizeof(*kPortsItems),
      true, false, {0}},
  [CITY_TOPBAR_POWER] = {"Utilities", kPowerItems, sizeof(kPowerItems) / sizeof(*kPowerItems), true, true, {0}},
  [CITY_TOPBAR_WATER] = {"Water", kWaterItems, sizeof(kWaterItems) / sizeof(*kWaterItems), true, false, {0}},
  [CITY_TOPBAR_EDUCATION] = {"Civic & Special Buildings", kEducationItems,
      sizeof(kEducationItems) / sizeof(*kEducationItems), true, false, {0}},
  [CITY_TOPBAR_PUBLIC_SERVICES] = {"Police", kServicesItems, sizeof(kServicesItems) / sizeof(*kServicesItems),
      true, false, {0}},
  [CITY_TOPBAR_RECREATION] = {"Recreation", kRecreationItems,
      sizeof(kRecreationItems) / sizeof(*kRecreationItems), true, false, {0}},
  [CITY_TOPBAR_REWARDS] = {"Rewards", kRewardsItems, sizeof(kRewardsItems) / sizeof(*kRewardsItems), true, false, {0}},
  [CITY_TOPBAR_EMERGENCY] = {"Emergency", kEmergencyItems, sizeof(kEmergencyItems) / sizeof(*kEmergencyItems),
      true, true, {0}},
  [CITY_TOPBAR_HELP] = {"Help", kHelpItems, sizeof(kHelpItems) / sizeof(*kHelpItems), false, false, {0}},
  [CITY_TOPBAR_ROTATE_COUNTERCLOCKWISE] = {"Rotate Counterclockwise", NULL, 0, false, false,
      {"Rotate Counterclockwise", CITY_KEY_PAGE_UP, CITY_MENU_ACTION_TOOLBAR, 0,
          CITY_TOOLBAR_BUTTON_ROTATE_CLOCKWISE}},
  [CITY_TOPBAR_TREES] = {"Trees", kTreeItems, sizeof(kTreeItems) / sizeof(*kTreeItems), true, false, {0}},
  [CITY_TOPBAR_UNDERWORLD] = {"Underworld / Overworld", NULL, 0, false, false,
      {"Underworld", CITY_KEY_NONE, CITY_MENU_ACTION_TOOLBAR, 0,
          CITY_TOOLBAR_BUTTON_UNDERGROUND}},
  [CITY_TOPBAR_TERRAIN_RAISE] = {"Raise Terrain", NULL, 0, false, true,
      {"Raise Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_RAISE_TERRAIN}},
  [CITY_TOPBAR_TERRAIN_LOWER] = {"Lower Terrain", NULL, 0, false, false,
      {"Lower Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LOWER_TERRAIN}},
  [CITY_TOPBAR_TERRAIN_STRETCH] = {"Stretch Terrain", NULL, 0, false, false,
      {"Stretch Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_STRETCH_TERRAIN}},
  [CITY_TOPBAR_TERRAIN_LEVEL] = {"Level Terrain", NULL, 0, false, false,
      {"Level Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_LEVEL_TERRAIN}},
  [CITY_TOPBAR_TERRAIN_WATER] = {"Place Water", NULL, 0, false, false,
      {"Place Water", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_WATER}},
  [CITY_TOPBAR_TERRAIN_CLEAR] = {"Clear Terrain", NULL, 0, false, false,
      {"Clear Terrain", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}},
  [CITY_TOPBAR_TERRAIN_CENTER] = {"Center View", NULL, 0, false, true,
      {"Center View", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_CENTER}},
  [CITY_TOPBAR_TERRAIN_STREAM] = {"Place Stream", NULL, 0, false, false,
      {"Place Stream", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_STREAM}},
};

const CityMenuItem kZoningMenu[] = {
  {"Residential...", CITY_KEY_NONE, CITY_MENU_ACTION_SUBMENU, 0, kResidentialDensityMenu},
  {"Commercial...", CITY_KEY_NONE, CITY_MENU_ACTION_SUBMENU, 0, kCommercialDensityMenu},
  {"Industrial...", CITY_KEY_NONE, CITY_MENU_ACTION_SUBMENU, 0, kIndustrialDensityMenu},
  {"Airport Zone", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_AIRPORT},
  {"Seaport Zone", CITY_KEY_NONE, CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_SEAPORT},
  {"De-zone", 'V', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEZONE},
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}
};
const CityMenuItem kCivicMenu[] = {
  {"Police", 'P', CITY_MENU_ACTION_SUBMENU, 0, kPoliceMenu},
  {"Fire Station", 'F', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_FIRE},
  {"Health & Education", CITY_KEY_NONE, CITY_MENU_ACTION_SUBMENU, 0, kHealthEducationMenu},
  {"Parks & Recreation", CITY_KEY_NONE, CITY_MENU_ACTION_SUBMENU, 0, kRecreationMenu},
  {"Rewards & Arcologies...", (CITY_HOTKEY_CTRL | CITY_HOTKEY_ALT | 'R'), CITY_MENU_ACTION_CATEGORY,
      0, CITY_TOPBAR_REWARDS},
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}
};
const CityMenuItem kUtilitiesMenu[] = {
  {"Power Lines", 'L', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_POWER_LINES},
  {"Power Plants...", (CITY_HOTKEY_CTRL | 'E'), CITY_MENU_ACTION_CATEGORY, 0, CITY_TOPBAR_POWER},
  {"Water Pipes", 'I', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_PIPES},
  {"Water Buildings...", (CITY_HOTKEY_CTRL | 'I'), CITY_MENU_ACTION_CATEGORY, 0, CITY_TOPBAR_WATER},
  {"Demolish", 'B', CITY_MENU_ACTION_TOOL, 0, CITY_CANVAS_TOOL_DEMOLISH}
};

const CityToolbarDefinition kConstructionGroups[CITY_CONSTRUCTION_GROUP_COUNT] = {
  {"Zone", kZoningMenu, kZoningItemCount, true, false, {0}},
  {"Build Utilities", kUtilitiesMenu, sizeof(kUtilitiesMenu) / sizeof(*kUtilitiesMenu), true, false, {0}},
  {"Place Civic/Special Buildings", kCivicMenu, sizeof(kCivicMenu) / sizeof(*kCivicMenu), true, false, {0}},
  {"Build Transportation", kTransportItems, kTransportItemCount, true, false, {0}},
  {"Landscape", kLandscapeItems, sizeof(kLandscapeItems) / sizeof(*kLandscapeItems), true, false, {0}}
};
const u8 kConstructionGroupCategories[CITY_CONSTRUCTION_GROUP_COUNT] = {
  CITY_TOPBAR_RESIDENTIAL, CITY_TOPBAR_POWER, CITY_TOPBAR_EDUCATION, CITY_TOPBAR_ROADS, CITY_TOPBAR_LANDSCAPE
};

_Static_assert(kRecreationMenu - kResidentialDensityMenu + 1 == CITY_TOOL_SUBMENU_COUNT, "tool submenu count");

const CityToolSubmenu kToolSubmenus[CITY_TOOL_SUBMENU_COUNT] = {
  {kResidentialItems, sizeof(kResidentialItems) / sizeof(*kResidentialItems), CITY_TOPBAR_RESIDENTIAL},
  {kCommercialItems, sizeof(kCommercialItems) / sizeof(*kCommercialItems), CITY_TOPBAR_COMMERCIAL},
  {kIndustrialItems, sizeof(kIndustrialItems) / sizeof(*kIndustrialItems), CITY_TOPBAR_INDUSTRIAL},
  {kServicesItems, sizeof(kServicesItems) / sizeof(*kServicesItems), CITY_TOPBAR_PUBLIC_SERVICES},
  {kEducationItems, sizeof(kEducationItems) / sizeof(*kEducationItems), CITY_TOPBAR_EDUCATION},
  {kRecreationItems, sizeof(kRecreationItems) / sizeof(*kRecreationItems), CITY_TOPBAR_RECREATION}
};

const char *const kConstructionTitles[CITY_TOPBAR_COUNT] = {
  [CITY_TOPBAR_BULLDOZE] = "Demolition",
  [CITY_TOPBAR_LANDSCAPE] = "Landscaping",
  [CITY_TOPBAR_TREES] = "Trees",
  [CITY_TOPBAR_RESIDENTIAL] = "Residential Zones",
  [CITY_TOPBAR_COMMERCIAL] = "Commercial Zones",
  [CITY_TOPBAR_INDUSTRIAL] = "Industrial Zones",
  [CITY_TOPBAR_ROADS] = "Transport Construction",
  [CITY_TOPBAR_RAIL] = "Rail Construction",
  [CITY_TOPBAR_PORTS] = "Air & Sea Ports",
  [CITY_TOPBAR_POWER] = "Select a Power Plant",
  [CITY_TOPBAR_WATER] = "Select a Water Building",
  [CITY_TOPBAR_EDUCATION] = "Education",
  [CITY_TOPBAR_PUBLIC_SERVICES] = "Public Services",
  [CITY_TOPBAR_RECREATION] = "Recreation",
  [CITY_TOPBAR_REWARDS] = "Select a Reward or Arcology",
  [CITY_TOPBAR_EMERGENCY] = "Emergency Services"
};
