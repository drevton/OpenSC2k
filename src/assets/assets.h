#ifndef SC2K_ASSETS_H
#define SC2K_ASSETS_H

#include "assets/state.h"

// IDs are validated while loading. Borrowed bytes remain valid until decoded blocks are released.
static inline ByteSpan GetAsset(CityAssets *assets, u32 id) {
  return assets->asset_spans[id];
}

typedef struct AssetFile AssetFile;
// A null root borrows retained bytes. An explicit root opens a loose startup/diagnostic source.
// Returns 1 when opened, 0 when missing, -1 for unreadable or invalid input.
// Close successful opens with CloseAssetFile.
int OpenAssetSource(AssetFile *file, CityAssets *assets, u32 id, const char *root);
// Select the executable's bundled packs before decoding immutable resources.
bool PreloadCityAssetPacks(CityAssets *assets, bool intro_only);

// Bootstrap the loading screen, retaining any shared graphics block it needs.
bool LoadCityIntroAsset(CityAssets *assets, const char *bundle_root);
// Load every category and decode every content block before play begins.
bool LoadCityAssets(CityAssets *assets, const char *bundle_root);
bool LoadCityAudioAssets(CityAssets *assets);
// Release decoded resources and their backing packs after Drawing/UI stop borrowing them.
void ReleaseCityAssets(CityAssets *assets);

#endif
