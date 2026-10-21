#include "city/saveload/types.h"
#include "city/saveload/legacy_text.h"
#include "city/saveload/summary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city/saveload/city_file_internal.h"
#include "city/saveload/sc2k.h"
#include "base/riff.h"
#include "city/maps/layers.h"

// Decode the selected stock MISC economy fields into caller-owned summary metadata. Misc must contain the complete
// required record; this does not touch the live City or change stream position.
static void ReadSummaryEconomy(CityFileSummary *summary, const u8 *misc) {
  u32 tick = ReadBE32(misc + 4 * 4);
  summary->year = (i16)ReadBE32(misc + 4 * 3) + tick / 300;
  summary->month = tick % 300 / 25 + 1;
  summary->cash = (i32)ReadBE32(misc + 4 * CITY_MISC_WORD_CASH);
  summary->has_economy = true;
}

// Apply the native extended cash value to a summary when the stock fallback still agrees. Bytes must provide the
// admitted scalar prefix; mismatching legacy cash is retained as an external edit.
static void ReadSummaryCash(CityFileSummary *summary, const u8 *bytes) {
  u64 bits = ReadLE64(bytes);
  i64 cash;
  memcpy(&cash, &bits, sizeof(cash));
  if (summary->cash == LegacyCityCash(cash)) summary->cash = cash;
}

// Scan a borrowed stock stream for summary name, economy and supported extension metadata. Returns false on
// malformed/missing required data; caller owns the stream and no live City is loaded.
static bool ReadLegacySummary(FILE *stream, CityFileSummary *summary) {
  u8 header[12];
  if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
      memcmp(header, "FORM", 4) || memcmp(header + 8, "SCDH", 4)) return false;
  u32 remaining = ReadBE32(header + 4);
  if (remaining < 4) return false;
  remaining -= 4;
  bool name = false, economy = false;
  while (remaining >= 8) {
    if (fread(header, 1, 8, stream) != 8) return false;
    u32 size = ReadBE32(header + 4);
    remaining -= 8;
    if (size > remaining) return false;
    if (!memcmp(header, "CNAM", 4) && size == 32) {
      u8 bytes[32];
      if (fread(bytes, 1, sizeof(bytes), stream) != sizeof(bytes)) return false;
      DecodeLegacyCityText(summary->name, sizeof(summary->name), bytes + 1, 31, CITY_TEXT_WINDOWS_1252);
      name = true;
    } else if (!memcmp(header, "MISC", 4) && size <= 2 * 4800) {
      u8 encoded[9600], misc[4800];
      if (fread(encoded, 1, size, stream) != size || !CityFileDecodeRLE(encoded, size, misc, sizeof(misc)))
        return false;
      if (ReadBE32(misc) != CITY_MISC_VERSION) return false;
      ReadSummaryEconomy(summary, misc);
      economy = true;
    } else if (!memcmp(header, "XNAT", 4) && size >= 8 && economy) {
      u8 cash[8];
      if (fread(cash, 1, 8, stream) != 8 || fseek(stream, size - 8, SEEK_CUR)) return false;
      ReadSummaryCash(summary, cash);
    } else if (fseek(stream, size, SEEK_CUR)) return false;
    remaining -= size;
  }
  summary->width = summary->height = 128;
  return name && economy;
}

// Read summary metadata from a native RIFF/Zstandard stream. Returns false for malformed entries and releases temporary
// archive data; caller retains the stream and output summary.
static bool ReadExtendedSummary(FILE *stream, CityFileSummary *summary) {
  u32 length;
  u8 *metadata = ReadRiffMember(stream, "META", CITY_SC2K_METADATA_LIMIT, &length);
  CitySc2kMetadata info;
  bool valid = metadata && ReadCitySc2kMetadata(&info, metadata, length);
  if (valid) {
    char name[257];
    memcpy(name, info.name.data, info.name.size); name[info.name.size] = 0;
    CopyText(summary->name, sizeof(summary->name), name);
    summary->width = info.width;
    summary->height = info.height;
    summary->extended = true;
    summary->format_version = info.format_version;
    u8 *version = ReadRiffMember(stream, "VERS", 16 + SC2K_GIT_REVISION_CAPACITY, &length);
    summary->has_writer_version = version && ReadCitySc2kVersion(&summary->writer_version, version, length);
    free(version);
    summary->year = info.start_year + (int)(info.tick / 300);
    summary->month = info.tick % 300 / 25 + 1;
    memcpy(&summary->cash, info.settings.data, sizeof(summary->cash));
    summary->has_economy = true;
  }
  free(metadata);
  return valid;
}

// Read bounded City summary metadata from a terminated host path without loading it into the live City. Caller owns
// the output record; returns false for an unsupported or malformed file and closes its own stream.
bool ReadCityFileSummary(const char *native_path, CityFileSummary *summary) {
  *summary = (CityFileSummary){0};
  FILE *stream = fopen(native_path, "rb");
  if (!stream) return false;
  u8 signature[4];
  bool valid = fread(signature, 1, sizeof(signature), stream) == sizeof(signature) && !fseek(stream, 0, SEEK_SET);
  if (valid) valid = !memcmp(signature, "RIFF", 4) ?
      ReadExtendedSummary(stream, summary) : ReadLegacySummary(stream, summary);
  fclose(stream);
  return valid;
}

// Preview sampling writes directly into the generator's existing five-plane allocation.
typedef struct {
  CityTerrainMap *map;
  int width, height, plane;
  bool native;
} TerrainFileReader;
enum { kTerrainFileNativeTile = -2, kTerrainFileGround, kTerrainFileAltitude, kTerrainFileShape,
    kTerrainFileKind, kTerrainFileFlags, kTerrainFileZone };

static bool ReadTerrainFileBytes(void *context, const u8 *bytes, u32 offset, u32 size) {
  TerrainFileReader *reader = context;
  CityTerrainMap *map = reader->map;
  u32 cells = reader->width * reader->height;
  for (u32 i = 0; i < size; ++i) {
    u32 at = offset + i;
    int plane = reader->plane, component = 0;
    bool native_tile = plane == kTerrainFileNativeTile;
    if (native_tile) {
      component = at % sizeof(TileInfo);
      at /= sizeof(TileInfo);
      if (component == offsetof(TileInfo, altitude) || component == offsetof(TileInfo, altitude) + 1)
        plane = kTerrainFileAltitude;
      else if (component == offsetof(TileInfo, terrain)) plane = kTerrainFileShape;
      else if (component == offsetof(TileInfo, zone)) plane = kTerrainFileZone;
      else if (component == offsetof(TileInfo, kind)) plane = kTerrainFileKind;
      else if (component == offsetof(TileInfo, flags)) plane = kTerrainFileFlags;
      else continue;
    }
    if (plane == kTerrainFileGround) {
      if (at >= 6 * cells) continue;
      if (at < cells) plane = kTerrainFileShape;
      else if (at < 2 * cells) { plane = kTerrainFileZone; at -= cells; }
      else if (at < 3 * cells) { plane = kTerrainFileFlags; at -= 2 * cells; }
      else if (at < 4 * cells) { plane = kTerrainFileKind; at -= 3 * cells; }
      else { plane = kTerrainFileAltitude; at -= 4 * cells; }
    }
    if (plane == kTerrainFileAltitude && !native_tile) { component = at & 1; at >>= 1; }
    if (at >= cells) return false;
    int x = reader->native ? at % reader->width : at / reader->height;
    int y = reader->native ? at / reader->width : at % reader->height;
    int step_x = reader->width / map->width, step_y = reader->height / map->height;
    if (x % step_x || y % step_y) continue;
    TileIndex tile = y / step_y * map->width + x / step_x;
    int value = bytes[i];
    if (plane == kTerrainFileAltitude) {
      int shift = (reader->native ? component : 1 - component) * 8;
      map->altitude[tile] = (map->altitude[tile] & ~(255 << shift)) | value << shift;
    } else if (plane == kTerrainFileShape)
      map->terrain[tile] = native_tile ? (map->terrain[tile] & 0xF0) | (value & 15) :
          reader->native ? value & 15 : value;
    else if (plane == kTerrainFileZone)
      map->terrain[tile] = (map->terrain[tile] & 15) |
          (value & CITY_TILE_SLOPED ? value & CITY_TERRAIN_SHAPE_MASK : 0) << 4;
    else if (plane == kTerrainFileKind) map->buildings[tile] = value;
    else if (plane == kTerrainFileFlags) map->flags[tile] = value;
  }
  return true;
}

static bool ReadStockTerrainPlane(FILE *stream, TerrainFileReader *reader, u32 size) {
  if (reader->plane == kTerrainFileAltitude || reader->plane == kTerrainFileGround) {
    u8 bytes[8192];
    u32 position = 0;
    while (position < size) {
      u32 count = IntMin(sizeof(bytes), size - position);
      if (fread(bytes, 1, count, stream) != count || !ReadTerrainFileBytes(reader, bytes, position, count))
        return false;
      position += count;
    }
    return true;
  }
  u32 position = 0;
  while (size) {
    u8 encoded[129], decoded[128];
    if (fread(encoded, 1, 1, stream) != 1) return false;
    --size;
    u32 count = encoded[0] < 128 ? encoded[0] : 1;
    if (count > size || fread(encoded + 1, 1, count, stream) != count) return false;
    size -= count;
    u32 source = 0, target = 0;
    if (!DecodeCityRlePacket(encoded, count + 1, &source, decoded, sizeof(decoded), &target, 1) ||
        !ReadTerrainFileBytes(reader, decoded, position, target)) return false;
    position += target;
  }
  return position == (u32)(reader->width * reader->height);
}

static bool ReadStockTerrain(FILE *stream, TerrainFileReader *reader) {
  u8 header[12];
  if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
      memcmp(header, "FORM", 4) || memcmp(header + 8, "SCDH", 4) || ReadBE32(header + 4) < 4) return false;
  u32 remaining = ReadBE32(header + 4) - 4;
  u32 offsets[5] = {0}, sizes[5] = {0};
  static const char *const kTags[] = {"ALTM", "XTER", "XBLD", "XBIT", "XGND"};
  while (remaining >= 8) {
    if (fread(header, 1, 8, stream) != 8) return false;
    u32 size = ReadBE32(header + 4);
    remaining -= 8;
    if (size > remaining) return false;
    for (int plane = 0; plane < 5; ++plane) if (!memcmp(header, kTags[plane], 4)) {
      offsets[plane] = (u32)ftell(stream); sizes[plane] = size;
    }
    if (fseek(stream, size, SEEK_CUR)) return false;
    remaining -= size;
  }
  if (remaining) return false;
  u32 cells = reader->width * reader->height;
  if (offsets[4]) {
    reader->native = true; reader->plane = kTerrainFileGround;
    u32 expected = 6 * cells + CITY_SAVE_CLIMATE_HEADER_SIZE + TerrainSnowNoiseSize(reader->width, reader->height);
    return sizes[4] == expected &&
        !fseek(stream, offsets[4], SEEK_SET) && ReadStockTerrainPlane(stream, reader, sizes[4]);
  }
  for (int plane = 0; plane < 4; ++plane) {
    reader->plane = plane;
    if (!offsets[plane] || (plane == kTerrainFileAltitude && sizes[plane] != 2 * cells) ||
        fseek(stream, offsets[plane], SEEK_SET) || !ReadStockTerrainPlane(stream, reader, sizes[plane])) return false;
  }
  return true;
}

bool ReadCityFileTerrain(const char *path, const CityTerrainSettings *settings, CityTerrainMap *map, bool preview) {
  if (!AreCityTerrainSettingsValid(settings) || !settings->width || !settings->height) return false;
  FILE *stream = fopen(path, "rb");
  if (!stream) return false;
  int divisor = preview ? IntMax(1, IntMax(settings->width, settings->height) / 256) : 1;
  int width = IntMax(128, settings->width / divisor), height = IntMax(128, settings->height / divisor);
  TerrainFileReader reader = {.map = map, .width = settings->width, .height = settings->height};
  u8 signature[4];
  bool valid = fread(signature, 1, sizeof(signature), stream) == sizeof(signature) && !fseek(stream, 0, SEEK_SET);
  if (valid) valid = PrepareCityTerrainMap(map, width, height);
  if (valid && !memcmp(signature, "RIFF", 4)) {
    u32 size;
    u8 *metadata = ReadRiffMember(stream, "META", CITY_SC2K_METADATA_LIMIT, &size);
    CitySc2kMetadata info;
    valid = metadata && ReadCitySc2kMetadata(&info, metadata, size) &&
        info.width == settings->width && info.height == settings->height;
    free(metadata);
    reader.native = true; reader.plane = kTerrainFileNativeTile;
    u32 expected = settings->width * settings->height * sizeof(TileInfo);
    if (valid)
      valid = VisitRiffMember(stream, "TILE", expected, ReadTerrainFileBytes, &reader, &size) && size == expected;
  } else if (valid) {
    valid = settings->width == 128 && settings->height == 128 && ReadStockTerrain(stream, &reader);
  }
  fclose(stream);
  if (!valid) return false;
  map->legacy = !reader.native;
  for (int tile = 0; tile < map->cell_count; ++tile) {
    int shape = map->terrain[tile] >> 4;
    TileInfo tp = {.altitude = map->altitude[tile],
        .terrain = reader.native ? map->terrain[tile] & 15 : map->terrain[tile],
        .zone = reader.native && shape ? CITY_TILE_SLOPED | shape : 0,
        .kind = map->buildings[tile], .flags = map->flags[tile]};
    bool water = reader.native ? HasTileWater(&tp) : (tp.terrain & CITY_TERRAIN_KIND_MASK) >= CITY_TERRAIN_WATER_FIRST;
    int ground = reader.native ? ReadCityWaterGroundKind(&tp) :
        tp.terrain >= CITY_TERRAIN_BANK_48 ? TK_RIVER : tp.terrain >= CITY_TERRAIN_BANK_32 ? TK_WATER_BANK : TK_WATER;
    if (reader.native && (ReadTerrainCode(&tp) > CITY_TERRAIN_SHAPE_13 ||
        (water && !IsTileKindExposedWater(ground)))) return false;
    map->buildings[tile] = water ? ground : IsTileKindTree(tp.kind) ? tp.kind : TK_CLEAR;
    map->terrain[tile] = reader.native ? ReadTerrainCode(&tp) : tp.terrain & CITY_TERRAIN_KIND_MASK;
    map->flags[tile] = water ? (tp.flags & TF_SALTWATER) | (map->legacy ? CITY_LEGACY_MAP_FLAG_WATER : 0) : 0;
    map->altitude[tile] = tp.altitude & (CITY_ALTITUDE_LAND_MASK | (water ? CITY_ALTITUDE_WATER_MASK : 0));
    if (water) {
      int level = (tp.altitude & CITY_ALTITUDE_WATER_MASK) >> CITY_ALTITUDE_WATER_SHIFT;
      map->water_level = IntMax(map->water_level, level);
      map->ocean |= (tp.flags & TF_SALTWATER) != 0;
      map->river |= ground == TK_RIVER;
    }
  }
  map->process_state = map->game_state = settings->seed;
  FinishCityTerrainMap(map, settings);
  return true;
}
