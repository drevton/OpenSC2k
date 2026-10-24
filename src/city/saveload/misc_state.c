#include "city/saveload/state.h"
#include "city/economy/history.h"
#include "city/engine/city_name.h"
#include "city/saveload/map_format.h"
#include "city/saveload/types.h"
#include "city/economy/types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "game/audio.h"
#include "audio/state.h"
#include "game/state.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/sc2k.h"
#include "assets/catalog.h"
#include "base/byte_order.h"
#include <string.h>

// MISC stores three current totals followed by alternating monthly amount/rate.
static void *CityFileEconomyField(EconomySector *sector, int field) {
  switch (field) {
  case 0: return &sector->current_amount;
  case 1: return &sector->current_rate;
  case 2: return &sector->accumulated;
  default:
    if (field & 1) return &sector->monthly_amount[(field - 3) / 2];
    return &sector->monthly_rate[(field - 3) / 2];
  }
}

// Ordinary SC2 files keep the exact original field. Large balances use a bounded legacy fallback plus an optional XNAT
// packet, never wrapped low bits. Returns the signed-32-bit clamp without modifying the native signed-64-bit treasury.
i32 LegacyCityCash(i64 cash) {
  return cash > INT32_MAX ? INT32_MAX : cash < INT32_MIN ? INT32_MIN : (i32)cash;
}

// The XNAT payload contains fixed-width CitySaveExtension fields. Size identifies the available prefix; opaque records
// may retain an unknown tail. Requires initialized native save fields; returns the needed known prefix size without
// allocating or serializing.
u32 CitySaveExtensionSize(const City *city) {
  if (city->native_map_storage || city->fixes || city->indicators.asset_value > UINT32_MAX)
    return CITY_SAVE_EXTENSION_SIZE;
  return city->indicators.cash == LegacyCityCash(city->indicators.cash) ? 0 : 8;
}

// Write the known little-endian scalar extension prefix while preserving a retained unknown tail. Bytes must provide
// size writable bytes; invalid size or climate metadata returns false, possibly after earlier prefix writes.
bool EncodeCitySaveExtension(const City *city, u8 *bytes, u32 size) {
  if (size < 8 || size > CITY_SAVE_EXTENSION_LIMIT || size < CitySaveExtensionSize(city)) return false;
  // Only overwrite our known prefix, preserving any future fields in a retained packet.
  WriteLE64(bytes, city->indicators.cash);
  if (size >= 9) bytes[8] = 0;
  if (size >= CITY_SAVE_LANDSCAPE_PREFIX_SIZE) {
    bytes[9] = 0;
    bytes[10] = bytes[11] = 0;
    WriteLE32(bytes + 12, city->landscape_cursor);
    WriteLE32(bytes + 16, city->landscape_tick);
    WriteLE32(bytes + 20, city->landscape_random);
  }
  if (size >= CITY_SAVE_TREE_PREFIX_SIZE) {
    bytes[24] = city->tree_lifecycle;
    bytes[25] = city->tree_tick_counter;
  }
  if (size >= CITY_SAVE_CLIMATE_PREFIX_SIZE) {
    if (city->snow.line > CITY_SNOW_LINE_DISABLED || city->snow.variation > CITY_SNOW_LINE_VARIATION ||
        city->terrain_climate >= CITY_TERRAIN_CLIMATE_COUNT || city->desert_limit > 256)
      return false;
    bytes[26] = city->snow.line;
    bytes[27] = city->snow.variation;
    WriteLE32(bytes + 28, city->snow.seed);
    bytes[32] = city->terrain_climate;
    bytes[33] = 0;
    WriteLE16(bytes + 34, city->desert_limit);
  }
  if (size >= CITY_SAVE_PROTECTION_PREFIX_SIZE) bytes[36] = city->protect_terrain_structures;
  if (size >= CITY_SAVE_POINT_EDIT_PREFIX_SIZE) bytes[37] = 0;
  if (size >= CITY_SAVE_NO_SEAWATER_PREFIX_SIZE) bytes[38] = 0;
  if (size >= CITY_SAVE_NATIVE_MAP_PREFIX_SIZE) bytes[39] = city->native_map_storage;
  if (size >= CITY_SAVE_ASSET_VALUE_PREFIX_SIZE) WriteLE64(bytes + 40, city->indicators.asset_value);
  if (size >= CITY_SAVE_EXTENSION_SIZE) WriteLE64(bytes + 48, city->fixes);
  return true;
}

// Validate and apply native scalar extension fields after the stock base fields. Invalid metadata returns false;
// extended cash/assets restore only when the legacy projection still agrees, preserving external stock-editor changes.
bool ApplyCitySaveExtension(City *city, const u8 *bytes, u32 size) {
  if (size < 8 || size > CITY_SAVE_EXTENSION_LIMIT) return false;
  u8 native_map = size >= CITY_SAVE_NATIVE_MAP_PREFIX_SIZE ? bytes[39] : 0;
  bool native_tiles = native_map != 0;
  u8 tree_lifecycle = size >= CITY_SAVE_TREE_PREFIX_SIZE ? bytes[24] : 0;
  u8 snow_line = size >= CITY_SAVE_CLIMATE_PREFIX_SIZE ? bytes[26] : CITY_SNOW_LINE_DISABLED;
  u8 snow_variation = size >= CITY_SAVE_CLIMATE_PREFIX_SIZE ? bytes[27] : 0;
  u8 terrain_climate = size >= CITY_SAVE_CLIMATE_PREFIX_SIZE ? bytes[32] : CITY_TERRAIN_SIMCITY;
  u16 desert_limit = size >= CITY_SAVE_CLIMATE_PREFIX_SIZE ? ReadLE16(bytes + 34) : 0;
  u8 protection = size >= CITY_SAVE_PROTECTION_PREFIX_SIZE ? bytes[36] : native_tiles;
  u64 fixes = size >= CITY_SAVE_EXTENSION_SIZE ? ReadLE64(bytes + 48) : city->game->default_city_fixes;
  u32 cells = city->map_cell_count;
  if ((size >= 9 && bytes[8] != 0) || (size >= CITY_SAVE_LANDSCAPE_PREFIX_SIZE && bytes[9] != 0) ||
      tree_lifecycle > 1 || (tree_lifecycle && !native_tiles) || protection > 1 || (protection && !native_tiles) ||
      (size >= CITY_SAVE_POINT_EDIT_PREFIX_SIZE && bytes[37]) ||
      (size >= CITY_SAVE_NO_SEAWATER_PREFIX_SIZE && bytes[38]) || native_map > 1 ||
      snow_line > CITY_SNOW_LINE_DISABLED || snow_variation > CITY_SNOW_LINE_VARIATION ||
      terrain_climate >= CITY_TERRAIN_CLIMATE_COUNT || desert_limit > 256 ||
      (fixes & ~CITY_FIX_SIMULATION_MASK)) return false;
  if (native_tiles && (!ReadLE32(bytes + 12) ||
      ReadLE32(bytes + 12) >= cells || !ReadLE32(bytes + 20))) return false;
  if (native_tiles) {
    city->landscape_cursor = ReadLE32(bytes + 12);
    city->landscape_tick = ReadLE32(bytes + 16);
    city->landscape_random = ReadLE32(bytes + 20);
  }
  u64 bits = ReadLE64(bytes);
  i64 cash;
  memcpy(&cash, &bits, sizeof(cash));
  // Retained unknown records must not undo a legacy editor's cash changes.
  if (city->indicators.cash == LegacyCityCash(cash)) city->indicators.cash = cash;
  if (size >= CITY_SAVE_ASSET_VALUE_PREFIX_SIZE) {
    u64 assets = ReadLE64(bytes + 40);
    if (city->indicators.asset_value == (assets > UINT32_MAX ? UINT32_MAX : assets))
      city->indicators.asset_value = assets;
  }
  city->native_map_storage = native_map;
  city->fixes = fixes;
  city->tree_lifecycle = tree_lifecycle;
  city->protect_terrain_structures = protection;
  city->tree_tick_counter = size >= CITY_SAVE_TREE_PREFIX_SIZE ? bytes[25] : 0;
  city->snow.line = snow_line;
  city->snow.variation = snow_variation;
  city->snow.seed = size >= CITY_SAVE_CLIMATE_PREFIX_SIZE ? ReadLE32(bytes + 28) : 0;
  city->terrain_climate = terrain_climate;
  city->desert_limit = desert_limit;
  int width = city->map_width, height = city->map_height;
  if (city->geography.rotation & 1) { int swap = width; width = height; height = swap; }
  city->snow.stride = (width >> 6) + 1;
  city->snow.moisture = city->snow.points + (TerrainSnowNoiseSize(width, height) >> 1);
  return true;
}

// Stock MISC has no native extension fields. Default them on both archive and streaming loads; a following XNAT
// restores the fields present in that save. Run before applying optional native extension fields; resets absent fields
// rather than retaining values from a previous City load.
static void ResetCitySaveExtensionState(City *city) {
  city->landscape_cursor = city->landscape_tick = city->landscape_random = 0;
  city->native_map_storage = false;
  city->fixes = city->game->default_city_fixes;
  city->tree_lifecycle = false;
  city->protect_terrain_structures = false;
  city->tree_tick_counter = 0;
  city->snow.line = CITY_SNOW_LINE_DISABLED;
  city->snow.variation = 0;
  city->terrain_climate = CITY_TERRAIN_SIMCITY;
  city->desert_limit = 0;
}

// Project live City and presentation fields into an initialized stock-version MISC staging record. Buffer must hold
// the complete record with its version set; returns false for unsupported version and does not change native storage
// encoding.
static bool EncodeCityMiscFields(City *city, u8 *misc) {
  if (ReadBE32(misc) != CITY_MISC_VERSION)
    return false;
  WriteBE32(misc + 4 * CITY_MISC_WORD_CASH, (u32)LegacyCityCash(city->indicators.cash));
  for (size_t index = 0; index < kCityMiscScalarCount; ++index) {
    const CityMiscScalar *field = &kCityMiscScalars[index];
    u8 *field_bytes = (u8 *)city + field->native_offset;
    u32 value = 0;
    if (field->file_index == CITY_MISC_WORD_ASSET_VALUE)
      value = city->indicators.asset_value > UINT32_MAX ? UINT32_MAX : city->indicators.asset_value;
    else memcpy(&value, field_bytes, field->native_width ? field->native_width : field->width);
    if (field->signed_value && field->width == 2 && city->map_width == 128 && city->map_height == 128)
      value = (u32)(int)(i16)value;
    WriteBE32(misc + 4u * field->file_index, value);
  }
  CityAudioState *audio = city->game->audio;
  for (size_t index = 0; index < sizeof(kCityMiscAudioScalars) / sizeof(kCityMiscAudioScalars[0]);
      ++index) {
    const CityMiscAudioScalar *field = &kCityMiscAudioScalars[index];
    u8 *field_bytes = (u8 *)audio + field->member_offset;
    u32 value = 0;
    memcpy(&value, field_bytes, field->width);
    if (field->signed_value && field->width == 2)
      value = (u32)(int)(i16)value;
    WriteBE32(misc + 4u * field->file_index, value);
  }
  CitySavedViewFields view = ReadCitySavedView(city);
  WriteBE32(misc + 4u * CITY_MISC_WORD_SAVED_VIEW_POSITION, (u32)(int)view.position);
  WriteBE32(misc + 4u * CITY_MISC_WORD_SAVED_VIEW_MODE, (u32)(int)view.mode);
  if (city->history == NULL)
    return false;
  u32 value = ReadHistorySample(city, CITY_HISTORY_RESIDENTS, 1);
  WriteBE32(misc + 4u * CITY_MISC_WORD_FIELD_29, value);
  u32 tile_counts[TK_COUNT];
  ReadCityLegacyTileCounts(city, tile_counts);
  for (size_t group = 0; group < sizeof(kCityMiscArrays) / sizeof(kCityMiscArrays[0]); ++group) {
    const CityMiscArray *array = &kCityMiscArrays[group];
    u32 native_width = array->native_width ? array->native_width : array->width;
    u32 stride = array->record_stride ? array->record_stride : native_width;
    u8 *records = (u8 *)city + array->native_offset;
    for (u32 index = 0; index < array->count; ++index) {
      u8 *item = records + index * stride + array->record_offset;
      u8 *file_item = misc + 4 * (array->file_index + index * array->file_stride);
      u32 value = 0;
      memcpy(&value, item, native_width);
      if (array->native_offset == offsetof(City, statistics)) value = tile_counts[index];
      if (array->signed_value && array->width == 2 && city->map_width == 128 && city->map_height == 128)
        value = (u32)(int)(i16)value;
      WriteBE32(file_item, value);
    }
  }
  for (int sector = 0; sector < CITY_ECONOMY_SECTOR_COUNT; ++sector) {
    int file_base = CITY_MISC_WORD_ECONOMY_FIRST + CITY_MISC_ECONOMY_WORDS_PER_SECTOR * sector;
    for (int field = 0; field < CITY_MISC_ECONOMY_WORDS_PER_SECTOR; ++field) {
      u32 value;
      memcpy(&value, CityFileEconomyField(&city->economy.sector[sector], field), sizeof(value));
      WriteBE32(misc + 4 * (file_base + field), value);
    }
  }
  memset(misc + 4u * CITY_MISC_WORD_RESERVED_FIRST, 0, 4u * (CITY_MISC_WORD_COUNT - CITY_MISC_WORD_RESERVED_FIRST));
  return true;
}

// Both containers apply MISC words in original order, including callback-visible side effects. Transport decoding
// stays outside this state operation. Index must select the admitted MISC word; applies its side effects in stream
// order and does not restore an earlier City snapshot.
static void ApplyCityMiscWord(City *city, u32 index, u32 value) {
  Game *game = city->game;
  CityAudioState *audio = game->audio;
  if (index == CITY_MISC_WORD_CASH) {
    city->indicators.cash = (i32)value;
  } else if (index == CITY_MISC_WORD_ASSET_VALUE) {
    city->indicators.asset_value = value;
  } else if (index == CITY_MISC_WORD_FIELD_29) {
    city->indicators.sector_population_total = value;
  } else if (index == CITY_MISC_WORD_SPECIAL_FACILITY_AVAILABILITY) {
    int mask = (int)(i16)(value);
    if ((city->unlocks.tools.shared.special_facility_mask == 0) != (mask == 0)) {
      DisableCityToolbarItem(city, CITY_TOOL_GROUP_REWARDS);
    }
    city->unlocks.tools.shared.special_facility_mask = (u32)(int)mask;
  } else if (index == CITY_MISC_WORD_SAVED_VIEW_POSITION) {
    ApplyCitySavedViewPosition(city, (i16)value);
  } else if (index == CITY_MISC_WORD_SAVED_VIEW_MODE) {
    ApplyCitySavedViewMode(city, value);
  } else {
    for (size_t field_index = 0; field_index < kCityMiscScalarCount;
        ++field_index) {
      const CityMiscScalar *field = &kCityMiscScalars[field_index];
      if (field->file_index == index) {
        u8 *destination = (u8 *)city + field->native_offset;
        memcpy(destination, &value, field->native_width ? field->native_width : field->width);
      }
    }
    for (size_t field_index = 0; field_index < sizeof(kCityMiscAudioScalars) / sizeof(kCityMiscAudioScalars[0]);
        ++field_index) {
      const CityMiscAudioScalar *field = &kCityMiscAudioScalars[field_index];
      if (field->file_index == index)
        memcpy((u8 *)audio + field->member_offset, &value, field->width);
    }
    for (size_t group = 0; group < sizeof(kCityMiscArrays) / sizeof(kCityMiscArrays[0]); ++group) {
      const CityMiscArray *array = &kCityMiscArrays[group];
      if (index < array->file_index || (index - array->file_index) % array->file_stride != 0) continue;
      u32 item = (index - array->file_index) / array->file_stride;
      if (item >= array->count) continue;
      u32 native_width = array->native_width ? array->native_width : array->width;
      u32 stride = array->record_stride ? array->record_stride : native_width;
      u8 *destination = (u8 *)city + array->native_offset + item * stride + array->record_offset;
      memcpy(destination, &value, native_width);
      if (array->file_index == CITY_MISC_WORD_ACCOUNT_NAMES_FIRST) {
        GenerateCityNeighborName(city, item);
      }
    }
    if (index >= CITY_MISC_WORD_ECONOMY_FIRST && index < CITY_MISC_WORD_ECONOMY_END) {
      u32 sector = (index - CITY_MISC_WORD_ECONOMY_FIRST) / CITY_MISC_ECONOMY_WORDS_PER_SECTOR;
      u32 field = (index - CITY_MISC_WORD_ECONOMY_FIRST) % CITY_MISC_ECONOMY_WORDS_PER_SECTOR;
      memcpy(CityFileEconomyField(&city->economy.sector[sector], field), &value, sizeof(value));
    }
  }
  if (index == CITY_MISC_WORD_NEWSPAPER_EDITION) {
    city->settings.loaded_speed = city->settings.speed;
    RefreshCityLoadedMusicPreference(audio);
    if (audio->music_enabled == 0) {
      StopCityMusicSequence(game);
    }
  }
}

// Encode or apply a complete stock MISC record in the selected direction. Requires initialized City/file owners and
// full record bytes; load effects follow original word order and are not transactionally rolled back.
bool CityFileMiscFields(City *city, u8 *misc, CityFileDirection direction) {
  if (direction == CITY_FILE_EXPORT) return EncodeCityMiscFields(city, misc);
  if (ReadBE32(misc) != CITY_MISC_VERSION) return false;
  ResetCitySaveExtensionState(city);
  for (u32 index = 1; index <= CITY_MISC_WORD_LAST_SAVED; ++index)
    ApplyCityMiscWord(city, index, ReadBE32(misc + 4 * index));
  return true;
}

// Apply the active file operation's decoded MISC workspace to live owners. Workspace must be prepared; preserves field
// ordering and callback effects, with failure retaining preceding writes.
bool ApplyLoadedCityMisc(City *city) {
  CityFileRuntime *file = city->game->file;
  ResetCitySaveExtensionState(city);
  for (u32 index = 1; index <= CITY_MISC_WORD_LAST_SAVED; ++index) {
    // Callbacks may replace or release the live workspace. Read it again for
    // each word rather than retaining a pointer across those callbacks.
    CityMiscBuffer *buffer = file->misc_active ? file->misc : NULL;
    if (buffer == NULL) return false;
    ApplyCityMiscWord(city, index, buffer->words[index]);
  }
  return true;
}

// Retain the saver’s 1200 host-endian words before transport. Requires an active MISC workspace; false leaves no valid
// complete encoded workspace and caller must stop the save path.
bool SaveCityMiscToBuffer(City *city) {
  CityFileRuntime *file = city->game->file;
  if (!file->misc_active) return false;
  CityMiscBuffer *buffer = (file->misc_active ? file->misc : NULL);
  if (buffer == NULL) return false;
  u8 serialized[sizeof(CityMiscBuffer)] = {0};
  WriteBE32(serialized, CITY_MISC_VERSION);
  if (!EncodeCityMiscFields(city, serialized)) return false;
  for (u32 index = 0; index < CITY_MISC_WORD_COUNT; ++index)
    buffer->words[index] = ReadBE32(serialized + 4 * index);
  return true;
}
