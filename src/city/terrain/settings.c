#include "city/terrain/terrain.h"

#include "city/maps/map_types.h"

// Terrain option dependencies, field ranges and default generation dimensions.
// These operations use caller-owned settings without generation work, allocation or random state.

// Setup constraints adapted from OpenSC2K's new_city_terrain_dialog.gd.
// See LICENSE.OpenSC2K for its MIT notice.
static const u32 kRiverFeatures = kDelta | kMeander | kCrossing | kBranch | kRejoin | kValley | kSalty;
static const u32 kOceanFeatures = kDelta | kCliffs | kRandomCoast;
static const u32 kExclusiveTerrainFeatures[] = {
  kPlateau | kRidge | kRolling | kBasin,
  kValley | kCanyon | kBasin,
  kLake | kLakes
};

// Compute feature choices excluded by the current terrain feature mask. Input must use supported feature bits; this
// query does not alter settings or generate terrain.
u32 DisabledCityTerrainFeatures(u32 features) {
  u32 disabled = features & kCanyon ? kRiver | kRiverFeatures : 0;
  for (size_t i = 0; i < sizeof(kExclusiveTerrainFeatures) / sizeof(kExclusiveTerrainFeatures[0]); ++i) {
    u32 group = kExclusiveTerrainFeatures[i];
    if (features & group) disabled |= group & ~features;
  }
  return disabled;
}

// Toggle an admitted terrain feature while enforcing its dependency and exclusion groups. Feature must be a supported
// choice bit; disabled choices are ignored and settings remain caller-owned.
void ToggleCityTerrainFeature(CityTerrainSettings *settings, u32 feature) {
  u32 features = settings->features;
  if (DisabledCityTerrainFeatures(features) & feature) return;
  if (features & feature) {
    features &= ~feature;
    if (feature == kOcean) features &= ~kOceanFeatures;
    if (feature == kRiver) features &= ~kRiverFeatures;
  } else {
    for (size_t i = 0; i < sizeof(kExclusiveTerrainFeatures) / sizeof(kExclusiveTerrainFeatures[0]); ++i) {
      u32 group = kExclusiveTerrainFeatures[i];
      if (feature & group) features &= ~group;
    }
    if (feature & kCanyon) features &= ~(kRiver | kRiverFeatures);
    if (feature & (kRiver | kRiverFeatures)) features &= ~kCanyon;
    if (feature & kRiverFeatures) features |= kRiver;
    if (feature & kOceanFeatures) features |= kOcean;
    features |= feature;
  }
  settings->features = features;
}

// Validate feature bits, slider ranges, climate values and supported map dimensions. NULL and unsupported settings
// return false; no work buffers or random state are created.
bool AreCityTerrainSettingsValid(const CityTerrainSettings *settings) {
  if (!settings || (settings->features >> kTerrainFeatureCount) ||
      settings->hills < 0 || settings->hills > 47 || settings->water < 0 || settings->water > 47 ||
      settings->trees < 0 || settings->trees > 47 || settings->climate >= CITY_TERRAIN_CLIMATE_COUNT ||
      settings->smoothness >= CITY_TERRAIN_SMOOTHNESS_COUNT || settings->variety >= CITY_TERRAIN_VARIETY_COUNT ||
      settings->small_rivers >= CITY_TERRAIN_SMALL_RIVERS_COUNT ||
      settings->snow_coverage > 100 || settings->desert_coverage > 100 ||
      (settings->coast_sides & ~CITY_TERRAIN_COAST_ALL)) return false;
  int width = settings->width ? settings->width : CITY_TERRAIN_DEFAULT_SIZE;
  int height = settings->height ? settings->height : CITY_TERRAIN_DEFAULT_SIZE;
  return IsSupportedMapAxis(width) && IsSupportedMapAxis(height);
}

