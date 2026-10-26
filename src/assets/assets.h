#ifndef SC2K_ASSETS_H
#define SC2K_ASSETS_H

#include "assets/state.h"

// IDs are validated while loading. Borrowed bytes have no alignment guarantee;
// they remain valid until decoded blocks are released.
static inline ByteSpan GetAsset(const CityAssets *assets, u32 id) {
  return assets->asset_spans[id];
}

// Startup preload: intro_only selects the loading-screen pack; false loads all categories and startup overrides.
// Reuses retained categories. Call before decoding non-intro resources; failure clears pack spans and blocks.
bool PreloadCityAssetPacks(CityAssets *assets, bool intro_only);

// Resolve sound IDs from preloaded packs, preferring the sparse pack and then any loose development recording.
// Packed PCM is borrowed; loose PCM is copied into Assets-owned blocks. Call once during startup before playback.
// Invalid present overrides fail. Failure retains earlier published sound spans and blocks for normal release.
bool LoadCityAudioAssets(CityAssets *assets);
// Release decoded resources and their backing packs after Drawing/UI/Audio stop borrowing them.
void ReleaseCityAssets(CityAssets *assets);

#endif
