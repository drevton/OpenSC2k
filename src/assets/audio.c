#include "audio/types.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "assets/assets.h"
#include "audio/audio.h"
#include "base/byte_order.h"
#include "base/file.h"
#include "base/text.h"

// 1 = loose development override loaded, 0 = absent, -1 = invalid/unreadable.
static int LoadCityLooseEffect(const char *name, u8 **samples, u32 *bytes) {
  const char *directory = SDL_getenv("SC2K_AUDIO_OVERRIDES");
  if (!directory || !directory[0]) return 0;
  char *path = NULL;
  FormatText(&path, "%s/%s", directory, name);
  HostFileInfo info;
  if (!ReadHostFileInfo(path, &info)) {
    int result = errno == ENOENT ? 0 : -1;
    free(path);
    return result;
  }
  SDL_RWops *source = SDL_RWFromFile(path, "rb");
  if (!source) { free(path); return -1; }
  SDL_AudioSpec format;
  bool valid = SDL_LoadWAV_RW(source, 1, &format, samples, bytes) != NULL;
  valid = valid && format.freq == 44100 && format.format == AUDIO_S16LSB && format.channels == 1 &&
      *bytes > 0 && !(*bytes & 1) && *bytes <= 16 * 1024 * 1024;
  if (!valid) {
    fprintf(stderr, "Invalid development sound override: %s\n", path);
    SDL_FreeWAV(*samples);
    *samples = NULL;
    *bytes = 0;
  }
  free(path);
  return valid ? 1 : -1;
}

// The native pack stores an ID directory and raw little-endian mono PCM.
// Startup resolves every ID to a borrowed span before any playback begins.
// tools/bake_sound_effects.py is the authoritative mapping. Its APPROXIMATION
// entries identify roles without their own recording: rejection, tree/zone, query,
// aircraft crash, helicopter stage (exact role unproved), unknown 526/528,
// sailboat/zoo and terrain generation. No legacy sample fallback.
// 1 = loaded, 0 = optional pack/ID absent, -1 = invalid/unreadable.
enum { kSoundHeaderBytes = 24, kSoundRecordBytes = 80 };
typedef struct { ByteSpan bytes; u32 count; } CitySoundPack;

static bool ReadCitySoundPack(CityAssets *assets, u32 asset_id, CitySoundPack *out) {
  ByteSpan pack = GetAsset(assets, asset_id);
  bool required = asset_id == CITY_ASSET_SOUNDS;
  *out = (CitySoundPack){0};
  if (!pack.data) return !required;
  const u8 *header = pack.data;
  bool valid = pack.size >= kSoundHeaderBytes && !memcmp(header, "SC2KSFX\0", 8) &&
      ReadLE32(header + 12) == 44100 && ReadLE16(header + 16) == 1 && ReadLE16(header + 18) == 16 &&
      ReadLE32(header + 20) == pack.size;
  u32 count = valid ? ReadLE32(header + 8) : 0;
  valid = valid && count > 0 && count <= CITY_SOUND_ASSET_COUNT &&
      (!required || count == CITY_SOUND_ASSET_COUNT) &&
      count <= (pack.size - kSoundHeaderBytes) / kSoundRecordBytes;
  u32 previous_id = CITY_SOUND_FIRST - 1;
  for (u32 index = 0; valid && index < count; ++index) {
    const u8 *record = header + kSoundHeaderBytes + index * kSoundRecordBytes;
    u32 id = ReadLE32(record), offset = ReadLE32(record + 4), frames = ReadLE32(record + 8);
    const char *name = (const char *)record + 16;
    valid = id > previous_id && IsCitySoundEffectId(id) && ReadLE32(record + 12) <= 2 &&
        offset >= kSoundHeaderBytes + count * kSoundRecordBytes && !(offset & 1) && offset <= pack.size &&
        frames > 0 && frames <= 8 * 1024 * 1024 && frames <= (pack.size - offset) / sizeof(i16) &&
        name[0] && name[0] != '/' && memchr(name, 0, 64) && !strstr(name, "..") && !strchr(name, '\\');
    previous_id = id;
  }
  if (!valid) return false;
  *out = (CitySoundPack){pack, count};
  return true;
}

static const char *FindPackedEffect(const CitySoundPack *pack, int sound_id, ByteSpan *samples) {
  for (u32 index = 0; index < pack->count; ++index) {
    const u8 *record = pack->bytes.data + kSoundHeaderBytes + index * kSoundRecordBytes;
    if (ReadLE32(record) != (u32)sound_id) continue;
    *samples = (ByteSpan){pack->bytes.data + ReadLE32(record + 4), ReadLE32(record + 8) * sizeof(i16)};
    return (const char *)record + 16;
  }
  return NULL;
}

bool LoadCityAudioAssets(CityAssets *assets) {
  CitySoundPack base, overrides;
  if (!ReadCitySoundPack(assets, CITY_ASSET_SOUNDS, &base) ||
      !ReadCitySoundPack(assets, CITY_ASSET_SOUND_OVERRIDES, &overrides)) return false;
  for (int id = CITY_SOUND_FIRST; id <= CITY_SOUND_BRIDGE_CONSTRUCTION; ++id) {
    if (!IsCitySoundEffectId(id)) continue;
    ByteSpan samples = {0};
    const char *source = FindPackedEffect(&overrides, id, &samples);
    if (!source) source = FindPackedEffect(&base, id, &samples);
    if (!source) return false;
    u8 *override = NULL;
    u32 bytes = 0;
    int result = LoadCityLooseEffect(source, &override, &bytes);
    if (result < 0) return false;
    if (result) {
      u8 *data = malloc(bytes);
      if (data) memcpy(data, override, bytes);
      SDL_FreeWAV(override);
      if (!data) return false;
      assets->asset_blocks[assets->asset_block_count++] = data;
      samples = (ByteSpan){data, bytes};
    }
    assets->asset_spans[CITY_ASSET_SOUND_FIRST + id - CITY_SOUND_FIRST] = samples;
  }
  return true;
}
