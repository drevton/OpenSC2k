#ifndef SC2K_ASSET_CATALOG_H
#define SC2K_ASSET_CATALOG_H

#include <stddef.h>

#include "city/types.h"

// Graphics Assets

// Picture IDs select illustrations, advisors or neighbors. Successful decodes replace the retained image references.
bool LoadCityPictureAssets(CityAssets *assets, u32 asset_id);
typedef struct CityInterfaceIcons CityInterfaceIcons;
// Validate the expected count before replacement. The destination owns decoded images and borrows pack lookup data.
bool LoadCityInterfaceIcons(CityAssets *assets, CityInterfaceIcons *destination, u32 id, u32 count);
// Release all retained image references and clear borrowed metadata. Accepts zeroed or partially loaded icons.
void ReleaseCityInterfaceIcons(CityInterfaceIcons *icons);

// OpenGFX2 music transport artwork; GPL-2.0, assets/ui/opengfx2/LICENSE.
// Successful decoding replaces the retained images; failure leaves the previous set intact.
bool LoadCityMusicButtonAssets(CityAssets *assets, const char *graphics_root);

// Resources

typedef struct CityResourceState CityResourceState;

bool LoadApplicationIntroAssets(CityResourceState *assets);

typedef struct RunSprite RunSprite;
// Borrowed native image with its own palette; valid for the Assets lifetime.
const RunSprite *ReadCityPicture(const CityAssets *assets, int id);

#endif
