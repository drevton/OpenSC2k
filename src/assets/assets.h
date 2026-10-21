#ifndef SC2K_ASSETS_H
#define SC2K_ASSETS_H

#include "assets/state.h"

// IDs are validated while loading. Borrowed bytes remain valid until Assets shutdown.
static inline ByteSpan GetAsset(CityAssets *assets, u32 id) {
  return assets->asset_spans[id];
}

// Bootstrap the loading screen, retaining any shared graphics block it needs.
bool LoadCityIntroAsset(CityAssets *assets, const char *bundle_root);
// Load every category and decode every content block before play begins.
bool LoadCityAssets(CityAssets *assets, const char *bundle_root);
bool LoadCityAudioAssets(CityAssets *assets);
// Release decoded resources and their backing packs after Drawing/UI stop borrowing them.
void ReleaseCityAssets(CityAssets *assets);
void ReleaseDecodedCityAssets(CityAssets *assets);

#endif
