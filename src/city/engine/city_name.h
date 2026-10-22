#ifndef SC2K_CITY_ENGINE_CITY_NAME_H
#define SC2K_CITY_ENGINE_CITY_NAME_H

#include "city/engine/city_types.h"

// Initialized names own any overflow. Set copies length readable bytes and adds a terminator; text is borrowed.
// Negative lengths or allocation failure return false and set Game's error. Free resets the complete name record.
bool SetCityNameText(Game *game, CityName *name, int length, const u8 *text);
void FreeCityNameStorage(CityName *name);

// Assign and clear the City's name through its Game error owner and existing name storage.
void AssignCityName(City *city, int length, const u8 *text);
void EmptyCityName(City *city);

// Generate slot 0..3, or all four labels, from the current name and saved neighbor indices without simulation RNG.
void GenerateCityNeighborName(City *city, int slot);
void RefreshCityNeighborNames(City *city);

#endif
