#include "city/saveload/services.h"
#include "city/saveload/state.h"
#include "city/saveload/types.h"
#include "game/state.h"
#include "city/saveload/legacy_text.h"
#include "city/maps/map.h"
#include "city/saveload/city_file_internal.h"
#include "base/text.h"
#include "base/byte_order.h"
#include "city/state.h"
#include "city/saveload/map_format.h"
#include <stdlib.h>

static bool SaveCityArchiveNameChunk(City *city, CityFile *archive);
static bool EnsureSaveChunks(CityFile *archive);

// Rewrite the existing in-memory CNAM chunk from the current City name. Archive must contain its standard name chunk;
// returns false if unavailable and retains archive ownership.
static bool SaveCityArchiveNameChunk(City *city, CityFile *archive) {
  CityFileChunk *name_chunk = NULL;
  for (u32 index = 0; index < archive->count; ++index)
    if (memcmp(archive->chunk[index].tag, "CNAM", 4) == 0)
      name_chunk = &archive->chunk[index];
  if (name_chunk == NULL)
    return false;
  if (name_chunk->size != 32) {
    u8 *decoded = malloc(32);
    u8 *original = calloc(32, 1);
    if (decoded == NULL || original == NULL) {
      free(decoded);
      free(original);
      return false;
    }
    free(name_chunk->decoded);
    free(name_chunk->original);
    name_chunk->decoded = decoded;
    name_chunk->original = original;
    name_chunk->size = 32;
  }
  memset(name_chunk->decoded, 0, 32);
  name_chunk->decoded[0] = 31;
  EncodeLegacyCityText(name_chunk->decoded + 1, 31, CityNameText(&city->name));
  return true;
}

// Reorder known archive chunks into stock order, create missing standard chunks and retain unknown records. May
// replace the chunk array and release discarded duplicates; false reports size/allocation failure and successful
// growth invalidates old slot pointers.
static bool EnsureSaveChunks(CityFile *city) {
  size_t capacity = CITY_FILE_STANDARD_CHUNKS;
  for (u32 index = 0; index < city->count; ++index)
    capacity += city->chunk[index].spec == NULL;
  if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(CityFileChunk))
    return false;
  CityFileChunk *ordered = calloc(capacity, sizeof(*ordered));
  u8 *retained = calloc(city->count != 0 ? city->count : 1u, 1);
  u8 created[CITY_FILE_STANDARD_CHUNKS] = {0};
  if (ordered == NULL || retained == NULL) {
    free(ordered);
    free(retained);
    return false;
  }
  u32 count = 0;
  for (size_t spec_index = 0; spec_index < CITY_FILE_STANDARD_CHUNKS; ++spec_index) {
    const CityFileChunkSpec *spec = &kCityFileChunks[spec_index];
    CityFileChunk *chunk = NULL;
    for (u32 index = 0; index < city->count; ++index)
      if (memcmp(city->chunk[index].tag, spec->tag, 4) == 0)
        chunk = &city->chunk[index];
    if (chunk != NULL) {
      ordered[count] = *chunk;
      retained[chunk - city->chunk] = 1;
    } else {
      CityFileChunk *added = &ordered[count];
      created[count] = 1;
      memcpy(added->tag, spec->tag, 4);
      added->spec = spec;
      added->size = spec->size;
      added->decoded = calloc(spec->size, 1);
      added->original = calloc(spec->size, 1);
      if (added->decoded == NULL || added->original == NULL)
        goto fail;
      if (memcmp(spec->tag, "MISC", 4) == 0)
        WriteBE32(added->decoded, 290);
    }
    ++count;
  }
  for (u32 index = 0; index < city->count; ++index)
    if (city->chunk[index].spec == NULL) {
      ordered[count++] = city->chunk[index];
      retained[index] = 1;
    }
  // Canonical save keeps the last known record; release every discarded duplicate.
  for (u32 index = 0; index < city->count; ++index)
    if (!retained[index]) {
      free(city->chunk[index].decoded);
      free(city->chunk[index].original);
      free(city->chunk[index].encoded);
    }
  free(city->chunk);
  free(retained);
  city->chunk = ordered;
  city->count = count;
  city->capacity = (u32)capacity;
  return true;
fail:
  for (u32 index = 0; index < CITY_FILE_STANDARD_CHUNKS; ++index)
    if (created[index]) {
      free(ordered[index].decoded);
      free(ordered[index].original);
    }
  free(ordered);
  free(retained);
  return false;
}

// Replace or append the selected native scalar/ground extension in an in-memory stock archive. Requires valid archive
// ownership; allocation/encoding failure returns false and unknown tail bytes are retained where supported.
static bool SaveCityArchiveExtension(City *city, CityFile *archive, bool ground) {
  const char *tag = ground ? "XGND" : "XNAT";
  u32 known_size = ground ? CityGroundExtensionSize(city) : CITY_SAVE_EXTENSION_SIZE;
  const CityFileChunk *previous = NULL;
  for (u32 index = 0; index < archive->count; ++index)
    if (!memcmp(archive->chunk[index].tag, tag, 4)) previous = &archive->chunk[index];
  u32 size = ground ? known_size : CitySaveExtensionSize(city);
  if (previous && previous->size > known_size) {
    if (previous->size > CITY_SAVE_EXTENSION_LIMIT) return false;
    if (previous->size > size) size = previous->size;
  }
  u8 *decoded = size ? calloc(size, 1) : NULL, *original = size ? calloc(size, 1) : NULL;
  if (size && (!decoded || !original)) { free(decoded); free(original); return false; }
  if (size && previous && previous->size == size) memcpy(decoded, previous->decoded, size);
  // Replace our extension while retaining its future tail and other unknown records.
  for (u32 index = 0; index < archive->count;) {
    CityFileChunk *chunk = &archive->chunk[index];
    if (memcmp(chunk->tag, tag, 4)) { ++index; continue; }
    free(chunk->decoded); free(chunk->original); free(chunk->encoded);
    --archive->count;
    memmove(chunk, chunk + 1, (archive->count - index) * sizeof(*chunk));
  }
  if (!size) return true;
  if (archive->count == archive->capacity) {
    u32 capacity = archive->count + 1;
    CityFileChunk *chunks = realloc(archive->chunk, (size_t)capacity * sizeof(*chunks));
    if (!chunks) { free(decoded); free(original); return false; }
    archive->chunk = chunks;
    archive->capacity = capacity;
  }
  bool encoded = ground ? EncodeCityGroundExtension(city, decoded, size) : EncodeCitySaveExtension(city, decoded, size);
  if (!encoded) { free(decoded); free(original); return false; }
  CityFileChunk *chunk = &archive->chunk[archive->count++];
  *chunk = (CityFileChunk){.size = size, .decoded = decoded, .original = original};
  memcpy(chunk->tag, tag, 4);
  return true;
}

// Update an admitted stock archive from live City and rebuild its serialized source bytes. Requires an active file
// runtime and complete metadata; recounts statistics, retains unknown records and returns false without promising
// rollback of archive edits.
bool SaveCityArchive(City *city, CityFile *archive) {
  CityFileRuntime *file = city->game->file;
  if (archive == NULL || !archive->metadata_complete || !EnsureSaveChunks(archive))
    return false;
  RecountCityTileStatistics(city);
  if (!SaveCityArchiveNameChunk(city, archive))
    return false;
  for (u32 index = 0; index < archive->count; ++index) {
    CityFileChunk *chunk = &archive->chunk[index];
    const CityFileChunkSpec *spec = chunk->spec;
    if (spec != NULL && memcmp(spec->tag, "MISC", 4) == 0) {
      WriteBE32(chunk->decoded, CITY_MISC_VERSION); // Original save sets version independently of input.
      if (file->misc_active) {
        if (!SaveCityMiscToBuffer(city)) return false;
        const CityMiscBuffer *buffer = (file->misc_active ? file->misc : NULL);
        for (u32 word = 0; word < CITY_MISC_WORD_COUNT; ++word)
          WriteBE32(chunk->decoded + 4 * word, buffer->words[word]);
      } else if (!CityFileMiscFields(city, chunk->decoded, 0)) return false;
      continue;
    }
    if (spec == NULL || spec->storage == CITY_FILE_ONLY)
      continue;
    u8 *converted = AllocateCityFileScratch(file, spec->size);
    if (converted == NULL) {
      return false;
    }
    i32 copied = CityFileStateChunkBytes(city, spec, converted, 0);
    if (copied) {
      if (spec->conversion != CITY_FILE_LABELS) CityFileConvertChunk(converted, spec, 0);
      memcpy(chunk->decoded, converted, spec->size);
    }
    FreeCityFileScratch(file, converted);
    if (!copied)
      return false;
  }
  if (!SaveCityArchiveExtension(city, archive, false) || !SaveCityArchiveExtension(city, archive, true)) return false;
  u32 size;
  u8 *source = BuildCityFileBytes(archive, &size);
  if (source == NULL) return false;
  free(archive->source);
  archive->source = source;
  archive->source_size = size;
  return true;
}
