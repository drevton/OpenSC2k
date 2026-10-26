#ifndef SC2K_ASSET_CATALOG_H
#define SC2K_ASSET_CATALOG_H

#include <stddef.h>

#include "city/types.h"

typedef struct AssetFile AssetFile;
typedef struct RunSprite RunSprite;

// Shared native run-image decoder. Dimensions must already be positive and within SPRITE_RUN_LIMIT.
// Reads size bytes from the open cursor, checking row offsets and run payloads. The caller owns one reference;
// failure returns NULL and releases the incomplete image, without rewinding or closing the cursor.
RunSprite *ReadAssetRunSprite(AssetFile *file, int width, int height, u32 size);

// Graphics Assets

// Load CITY_ASSET_ILLUSTRATIONS, CITY_ASSET_ADVISORS or CITY_ASSET_NEIGHBORS into its matching Assets slots.
// Validated images replace slots one at a time; failure preserves earlier replacements and leaves later slots
// unchanged. Unknown asset IDs fail before opening input.
// Assets owns the decoded sprite references; a failure after the final image can still leave every slot replaced.
bool LoadCityPictureAssets(CityAssets *assets, u32 asset_id);
typedef struct CityInterfaceIcons CityInterfaceIcons;
// Load CITY_ASSET_MAP_ICONS, CITY_ASSET_BRIDGES or CITY_ASSET_TOOLBAR into its slot in Assets.
// Validate the expected count and replace only after complete validation. Unknown IDs or decoding failure
// preserve the installed atlas. Assets owns decoded images and borrows lookup/variant bytes from the pack.
bool LoadCityInterfaceIcons(CityAssets *assets, u32 id);
// Release all retained image references and clear borrowed metadata. Accepts zeroed or partially loaded icons.
void ReleaseCityInterfaceIcons(CityInterfaceIcons *icons);

// OpenGFX2 music transport artwork; GPL-2.0, assets/ui/opengfx2/LICENSE.
// Successful decoding replaces the retained images; failure leaves the previous set intact.
bool LoadCityMusicButtonAssets(CityAssets *assets, const char *graphics_root);

// Resources

typedef struct CityResourceState CityResourceState;

// Keep an existing intro without reloading. Otherwise preload and replace the image set only after full validation.
// Decode failure leaves prior images unchanged; pack preload can retain successfully loaded bytes.
bool LoadApplicationIntroAssets(CityResourceState *assets);

// Advisor and notice/newspaper resource IDs select installed pictures; unknown IDs return NULL.
// The image and its palette are borrowed until the slot is replaced or Assets releases it.
const RunSprite *ReadCityPicture(const CityAssets *assets, int id);

#endif
