#ifndef SC2K_UI_MENUS_CATALOG_H
#define SC2K_UI_MENUS_CATALOG_H

#include "ui/menus/menu.h"
#include "ui/windows/controls.h"

// Private menu identities extend the one-based topbar IDs. They are not City commands or resource IDs.
enum {
  kDisasterMenu = CITY_TOPBAR_COUNT + 1,
  kResidentialDensityMenu, kCommercialDensityMenu, kIndustrialDensityMenu,
  kPoliceMenu, kHealthEducationMenu, kRecreationMenu
};
enum { kChartPopulation = 1, kZoningItemCount = 7 };

typedef struct {
  const char *label;
  const CityMenuItem *items;
  u8 count;
  bool category, separator;
  CityMenuItem direct;
} CityToolbarDefinition;
typedef struct {
  const CityMenuItem *items;
  u8 count, category;
} CityToolSubmenu;

// Immutable menu definitions own no UI state. Labels and item pointers remain valid for the process lifetime,
// including nested window/command handling. Shared item identity is used when remembering construction choices;
// do not copy or recreate the rows in consumers. The UI controls availability, checked state and visible order.
extern const CityToolbarDefinition kCityToolbarDefinitions[CITY_TOPBAR_COUNT];
extern const CityToolbarDefinition kConstructionGroups[CITY_CONSTRUCTION_GROUP_COUNT];
extern const u8 kConstructionGroupCategories[CITY_CONSTRUCTION_GROUP_COUNT];
extern const CityToolSubmenu kToolSubmenus[CITY_TOOL_SUBMENU_COUNT];
extern const char *const kConstructionTitles[CITY_TOPBAR_COUNT];
extern const CityMenuItem kTerrainFileItems[5];
extern const CityMenuItem kDisasterItems[21];
extern const CityMenuItem kChartItems[5];
extern const CityDropdownItem kTreeClimateItems[5];
extern const CityMenuItem kTreeItems[13];
extern const CityMenuItem kZoningMenu[kZoningItemCount];
extern const CityMenuItem kCivicMenu[6];
extern const CityMenuItem kUtilitiesMenu[5];

#endif
