#ifndef SC2K_ASSET_CATALOG_H
#define SC2K_ASSET_CATALOG_H

#include <stddef.h>

#include "city/types.h"

// Graphics Assets

typedef struct Game Game;
bool LoadApplicationGraphicsAssets(Game *game);


// Resources

typedef struct CityResourceState CityResourceState;

bool LoadApplicationIntroAssets(CityResourceState *assets);

typedef struct RunSprite RunSprite;
// Borrowed native image with its own palette; valid for the Assets lifetime.
const RunSprite *ReadCityPicture(const CityAssets *assets, int id);


#endif
