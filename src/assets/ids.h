#ifndef SC2K_CITY_ASSET_IDS_H
#define SC2K_CITY_ASSET_IDS_H

#include "audio/types.h"

typedef enum CityAssetId {
#define CITY_ASSET(symbol, category, path) CITY_ASSET_##symbol,
#include "assets/asset_catalog.inc"
#undef CITY_ASSET
  CITY_ASSET_FILE_COUNT,
  CITY_ASSET_SOUND_FIRST = CITY_ASSET_FILE_COUNT,
  CITY_ASSET_COUNT = CITY_ASSET_SOUND_FIRST + CITY_SOUND_BRIDGE_CONSTRUCTION - CITY_SOUND_FIRST + 1,
  CITY_ASSET_MUSIC_FIRST = CITY_ASSET_MUSIC_TTTHEME2
} CityAssetId;

#endif
