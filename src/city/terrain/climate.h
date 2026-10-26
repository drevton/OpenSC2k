#ifndef SC2K_TERRAIN_CLIMATE_H
#define SC2K_TERRAIN_CLIMATE_H

#include "city/terrain/types.h"

// The dimensions must be supported map axes. Size covers both 64-tile lattices, including their outside edge.
int TerrainSnowNoiseSize(int width, int height);
// points must provide TerrainSnowNoiseSize bytes. Fills both fields from seed and binds stride/moisture;
// does not change line/variation or advance a City or generation random stream.
void InitializeTerrainSnowNoise(CitySnowClimate *snow, int width, int height);
// Read prepared climate fields at an in-range map position. No allocation, mutation or random draws.
int TerrainClimateValue(const CitySnowClimate *snow, CityPoint position, bool moisture);
int TerrainGrassPaletteAt(const CitySnowClimate *snow, CityPoint position);
// Disabled/constant snow lines do not need lattice samples; varying lines require initialized backing.
int TerrainSnowLineAt(const CitySnowClimate *snow, CityPoint position);
// Convert a valid TileClimate to permanent ground or snow appearance without changing tile storage.
int TerrainBaseClimate(int climate);
bool TerrainClimateSupportsSnow(int climate);
int TerrainSnowClimate(int grass, int height_above_line);

#endif
