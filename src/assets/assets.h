#ifndef SC2K_ASSETS_H
#define SC2K_ASSETS_H

#include "assets/state.h"

// IDs are validated while loading. Borrowed bytes remain valid until decoded blocks are released.
static inline ByteSpan GetAsset(CityAssets *assets, u32 id) {
  return assets->asset_spans[id];
}

// Startup preload: intro_only selects the loading-screen pack; false loads all categories and startup overrides.
// Reuses retained categories. Call before decoding non-intro resources; failure clears pack spans and blocks.
bool PreloadCityAssetPacks(CityAssets *assets, bool intro_only);

// Decode audio catalogs from the successfully preloaded pack bytes.
bool LoadCityAudioAssets(CityAssets *assets);
// Release decoded resources and their backing packs after Drawing/UI/Audio stop borrowing them.
void ReleaseCityAssets(CityAssets *assets);

#endif
