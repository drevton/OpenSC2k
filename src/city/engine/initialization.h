#ifndef SC2K_CITY_ENGINE_INITIALIZATION_H
#define SC2K_CITY_ENGINE_INITIALIZATION_H

#include "city/types.h"

// Reset the allocated City's map metadata and gameplay accounts for new-city setup.
// The simulation initializer retains terrain-editor account state according to Game's current idle mode.
void InitializeNewCityDocument(City *city);
void InitializeNewCitySimulationState(City *city);

// Initialize year/difficulty, economy, milestones and neighbor accounts after reset. Pass an actual calendar year
// and CityDifficulty value. economy_year selects the 1900/1950/2000/2050 national-population baseline;
// other baseline years leave national population unchanged.
// Unlock/neighbor presentation is synchronous; the caller applies launch policy and final status afterward.
void InitializeCitySetupState(City *city, int start_year, int economy_year, int difficulty);

// Found the configured city after terrain editing. Requires INVALID_TILE, zero reserved fields, a stopped timer
// and a nonempty name. TEST has no effects; execution records the founding event before entering running mode.
i64 CmdStartCity(City *city, u32 flags, TileIndex tile, int reserved1, int reserved2, int reserved3);

// Set the pre-load disaster default before creation options or saved MISC fields replace it.
void InitializeCityDisasterDefaults(City *city);

#endif
