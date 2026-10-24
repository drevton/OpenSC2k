#include "assets/resource_types.h"
#include "city/tools/types.h"
#include <stdio.h>
#include <string.h>

#include "city/engine/city_types.h"
#include "assets/catalog.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "base/text.h"

static const char *const kMonthNames[] = {
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December",
};
static const char *const kShortMonthNames[] = {
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec",
};
static const char *const kToolNames[CITY_TOOL_GROUP_COUNT][CITY_TOOL_VARIANT_CAPACITY] = {
  {
    "Clear / Demolish",
    "Level Terrain",
    "Raise Terrain",
    "Lower Terrain",
    "Remove Zoning"
  },
  {
    "Trees",
    "Water"
  },
  {
    "Dispatch Police",
    "Dispatch Firefighters",
    "Dispatch Military"
  },
  {
    "Power Lines",
    "Power Plant",
    "Coal Power",
    "Hydroelectric Power",
    "Oil Power",
    "Natural Gas Power",
    "Nuclear Power",
    "Wind Power",
    "Solar Power",
    "Microwave Power",
    "Fusion Power"
  },
  {
    "Pipes",
    "Water Pump",
    "Water Tower",
    "Water Treatment",
    "Desalination"
  },
  {
    "Mayor's House",
    "City Hall",
    "Statue",
    "Wildlife Dome",
    "Arcologies",
    "Terraced Arcology",
    "Forest Arcology",
    "Tower Arcology",
    "Launch Arcology"
  },
  {
    "Road",
    "Highway",
    "Tunnel",
    "Highway Ramp",
    "Bus Depot"
  },
  {
    "Rail",
    "Subway",
    "Rail Depot",
    "Subway Station",
    "Subway-Rail Link"
  },
  {
    "Seaport",
    "Airport"
  },
  {
    "Light Residential",
    "Dense Residential"
  },
  {
    "Light Commercial",
    "Dense Commercial"
  },
  {
    "Light Industrial",
    "Dense Industrial"
  },
  {
    "School",
    "College",
    "Library",
    "Museum"
  },
  {
    "Police Station",
    "Fire Station",
    "Hospital",
    "Prison"
  },
  {
    "Small Park",
    "Large Park",
    "Zoo",
    "Stadium",
    "Marina"
  },
  {
    "Place Sign"
  },
  {
    "Inspect Tile"
  },
  {
    "Center View"
  },
};
const char *const kCityTeamNames[] = {
  "Foxes",
  "Falcons",
  "Otters",
  "Kestrels",
  "Badgers",
};
const char *const kCitySports[] = {
  "Football",
  "Baseball",
  "Soccer",
  "Cricket",
  "Rugby",
};

// Names

// These are presentation resources, not saved simulation strings.
bool InitializeCityResourceNames(CityAssets *resources) {
  memset(&resources->calendar_names, 0, sizeof(resources->calendar_names));
  memset(&resources->tool_names, 0, sizeof(resources->tool_names));
  for (int month = 0; month < CITY_MONTH_COUNT; ++month) {
    const char *full = kMonthNames[month];
    const char *abbreviated = kShortMonthNames[month];
    if (strlen(full) >= CITY_RESOURCE_NAME_CAPACITY || strlen(abbreviated) >= CITY_RESOURCE_NAME_CAPACITY) {
      return false;
    }
    CopyText(resources->calendar_names.full[month], CITY_RESOURCE_NAME_CAPACITY, full);
    CopyText(resources->calendar_names.abbreviated[month], CITY_RESOURCE_NAME_CAPACITY, abbreviated);
  }
  for (int group = 0; group < CITY_TOOL_GROUP_COUNT; ++group)
    for (int variant = 0; variant < CITY_TOOL_VARIANT_CAPACITY; ++variant) {
      const char *name = kToolNames[group][variant];
      if (!name) continue;
      if (strlen(name) >= CITY_RESOURCE_NAME_CAPACITY) {
        return false;
      }
      CopyText(resources->tool_names.name[group][variant], CITY_RESOURCE_NAME_CAPACITY, name);
    }
  return true;
}

// Name Lookup

static const char *const kTileNames[] = {
  "",
  "Rubble",
  "Radioactive Waste",
  "Trees",
  "Park",
  "Power Lines",
  "Road",
  "Railway",
  "Tunnel Entrance",
  "Road",
  "Railway",
  "Highway",
  "Suspension Bridge",
  "Bridge",
  "Power Lines",
  "Highway Ramp",
  "Highway",
  "Highway Bridge",
  "Subway-Rail Connection",
  "Modest Homes",
  "Standard Homes",
  "Luxury Homes",
  "Gas Station",
  "Bed & Breakfast Inn",
  "Convenience Store",
  "Gas Station",
  "Small Office Building",
  "Office Building",
  "Warehouse",
  "Toy Store",
  "Warehouse",
  "Chemical Storage",
  "Warehouse",
  "Industrial Substation",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Budget Apartments",
  "Apartments",
  "Apartments",
  "Upscale Apartments",
  "Upscale Apartments",
  "Condominium",
  "Condominium",
  "Condominium",
  "Shopping Center",
  "Grocery Store",
  "Office Building",
  "Resort Hotel",
  "Office Building",
  "Office and Retail",
  "Office Building",
  "Office Building",
  "Office Building",
  "Office Building",
  "Warehouse",
  "Chemical Processing Plant",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Construction Site",
  "Construction Site",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Abandoned Building",
  "Abandoned Building",
  "Large Apartment Building",
  "Large Apartment Building",
  "Condominium",
  "Condominium",
  "Office Park",
  "Office Tower",
  "Small Shopping Center",
  "Cinema Complex",
  "Drive-in Theater",
  "Office Tower",
  "Office Tower",
  "Parking Lot",
  "Historic Office Building",
  "Corporate Headquarters",
  "Chemical Processing Plant",
  "Large Factory",
  "Industrial Facility",
  "Factory",
  "Large Warehouse",
  "Warehouse",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Hydroelectric Plant",
  "Hydroelectric Plant",
  "Wind Turbine",
  "Natural Gas Power Plant",
  "Oil Power Plant",
  "Nuclear Power Plant",
  "Solar Power Plant",
  "Microwave Power Plant",
  "Fusion Power Plant",
  "Coal Power Plant",
  "City Hall",
  "Hospital",
  "Police Station",
  "Fire Station",
  "Museum",
  "Park",
  "School",
  "Stadium",
  "Prison",
  "College",
  "Zoo",
  "Statue",
  "Water Pump",
  "Runway",
  "Runway",
  "Pier",
  "Crane",
  "Control Tower",
  "Control Tower",
  "Warehouse",
  "Building",
  "Building",
  "Tarmac",
  "Fighter Aircraft",
  "Hangar",
  "Subway Station",
  "Radar",
  "Water Tower",
  "Bus Station",
  "Rail Station",
  "Parking Lot",
  "Parking Lot",
  "Loading Bay",
  "Military Research Facility",
  "Cargo Yard",
  "Mayor's House",
  "Water Treatment Plant",
  "Library",
  "Hangar",
  "Church",
  "Marina",
  "Missile Silo",
  "Desalination Plant",
  "Terraced Arcology",
  "Forest Arcology",
  "Tower Arcology",
  "Launch Arcology",
  "Wildlife Dome",
  "",
  "",
  "",
  "",
  "Clear Terrain",
  "Fresh Water",
  "Salt Water",
  "Sailboat",
};
static const char *const kZoneNames[] = {
  "Residential",
  "Residential",
  "Commercial",
  "Commercial",
  "Industrial",
  "Industrial",
  "Military",
  "Airport",
  "Seaport",
  "Seaport",
  "Airport",
  "",
  "",
  "",
  "",
  "",
  "Low-density",
  "High-density",
  "Low-density",
  "High-density",
  "Low-density",
  "High-density",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
};
static const char *const kRatingNames[] = {
  "High-density",
  "None",
  "Low",
  "Medium",
  "High",
  "Very High",
};

// Names are immutable literals. Unknown categories or indices return empty text;
// callers borrow the result and copy only when retaining an editable snapshot.
const char *ReadCityNameResource(int category, int index) {
  switch (category) {
  case CITY_NAME_CATEGORY_TILE:
    if (index >= 0 && index < (int)(sizeof(kTileNames) / sizeof(kTileNames[0]))) return kTileNames[index];
    break;
  case CITY_NAME_CATEGORY_ZONE:
    if (index >= 1 && index <= (int)(sizeof(kZoneNames) / sizeof(kZoneNames[0]))) return kZoneNames[index - 1];
    break;
  case CITY_NAME_CATEGORY_RATING:
    if (index >= 1 && index < (int)(sizeof(kRatingNames) / sizeof(kRatingNames[0]))) return kRatingNames[index];
    break;
  default: break;
  }
  return "";
}
