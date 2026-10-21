#ifndef SC2K_DRAWING_MAPS_MAP_EFFECTS_H
#define SC2K_DRAWING_MAPS_MAP_EFFECTS_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef enum CityMapSpriteEffect {
  CITY_MAP_EFFECT_WIND, CITY_MAP_EFFECT_POWER_SMOKE
} CityMapSpriteEffect;
// Mark an effect as visible and return its current presentation frame; timing remains Drawing-owned.
int UseCityMapSpriteEffect(Drawing *drawing, CityMapSpriteEffect effect);
// Capture/replay a scoped sprite footprint. A successful begin must be paired with end; inactive in stock comparisons.
bool BeginCityMapOverlayAnimation(City *city, Drawing *drawing, int x, int y, int overlay);
bool BeginCityMapSpriteAnimation(City *city, Drawing *drawing, TileIndex tile);
void EndCityMapOverlayAnimation(City *city, Drawing *drawing);

#endif
