#ifndef SC2K_TERRAIN_H
#define SC2K_TERRAIN_H

#include "city/terrain/types.h"

// Reset a zeroed or valid output owner, reusing/replacing its five planes and climate backing. Success clears plane
// bytes and prior metadata, then binds the requested dimensions. Borrowed views expire even when storage is reused.
// Unsupported dimensions or allocation failure preserve the previous map. Release through ReleaseCityTerrainMap.
bool ResetCityTerrainMap(CityTerrainMap *map, int width, int height);
// Recompute counts, height extrema and climate from prepared planes and valid settings. Reuses climate backing;
// input planes, format marker and generation RNG state remain unchanged. No new land/trees or City RNG advances.
// Producers need not initialize the derived counts, extrema or climate before this call.
void FinishCityTerrainMap(CityTerrainMap *map, const CityTerrainSettings *settings);
// Release the output backing and clear the descriptor; NULL and zeroed owners are accepted. Borrowed views expire.
void ReleaseCityTerrainMap(CityTerrainMap *map);
// Read excluded feature bits without changing settings. Input uses supported feature bits.
u32 DisabledCityTerrainFeatures(u32 features);
// Toggle one supported choice bit, applying dependencies and exclusions. Disabled choices leave settings unchanged.
void ToggleCityTerrainFeature(CityTerrainSettings *settings, u32 feature);
// Check field ranges and supported axes without allocation or random draws. NULL settings fail;
// a zero width or height selects CITY_TERRAIN_DEFAULT_SIZE for that axis.
bool AreCityTerrainSettingsValid(const CityTerrainSettings *settings);
bool GenerateTerrainMap(const CityTerrainSettings *settings, CityTerrainMap *map);
// Read climate appearance from a valid prepared output tile without changing planes, metadata or RNG.
int CityTerrainTileClimate(const CityTerrainMap *map, TileIndex tile);

#endif
