#include "city/saveload/city_file_internal.h"
#include "base/byte_order.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Container

// Release an in-memory stock archive and all owned chunk payloads. NULL is accepted; no pointers into the archive
// remain valid after release.
void CityFileFree(CityFile *city) {
  if (city == NULL)
    return;
  free(city->source_path);
  free(city->source);
  for (u32 index = 0; index < city->count; ++index) {
    free(city->chunk[index].decoded);
    free(city->chunk[index].original);
    free(city->chunk[index].encoded);
  }
  free(city->chunk);
  free(city);
}

// Grow an archive's chunk array and initialize its next slot. Returns a borrowed slot or NULL on allocation failure;
// growth can invalidate earlier pointers into that array.
static CityFileChunk *AppendCityFileChunk(CityFile *city) {
  if (city->count == city->capacity) {
    u32 capacity = city->capacity != 0 ? city->capacity * 2 : 32u;
    if (capacity <= city->capacity || (u64)capacity * sizeof(CityFileChunk) > SIZE_MAX)
      return NULL;
    CityFileChunk *chunks = realloc(city->chunk, (size_t)capacity * sizeof(*chunks));
    if (chunks == NULL)
      return NULL;
    city->chunk = chunks;
    city->capacity = capacity;
  }
  CityFileChunk *chunk = &city->chunk[city->count++];
  memset(chunk, 0, sizeof(*chunk));
  return chunk;
}

// Consume and close an opened stock file, retaining its original bytes and any parsed chunk metadata. Returns NULL on
// resource/I/O failure; a returned archive may have metadata_complete false and must be checked before loading or
// saving.
static CityFile *CityFileReadOpened(const char *path, FILE *input) {
  if (fseek(input, 0, SEEK_END) != 0) {
    fclose(input);
    return NULL;
  }
  long length = ftell(input);
  if (length < 0 || (unsigned long)length > 0xFFFFFFFFu || fseek(input, 0, SEEK_SET) != 0) {
    fclose(input);
    return NULL;
  }
  u8 *source = malloc(length != 0 ? (size_t)length : 1);
  if (source == NULL || fread(source, 1, (size_t)length, input) != (size_t)length) {
    free(source);
    fclose(input);
    return NULL;
  }
  fclose(input);
  CityFile *city = calloc(1, sizeof(*city));
  if (city == NULL)
    goto fail;
  city->source_path = malloc(strlen(path) + 1);
  if (city->source_path == NULL)
    goto fail;
  memcpy(city->source_path, path, strlen(path) + 1);
  if (length < 12 || memcmp(source, "FORM", 4) != 0 || ReadBE32(source + 4) != (u32)length - 8 ||
      memcmp(source + 8, "SCDH", 4) != 0)
    goto metadata_incomplete;
  u32 position = 12;
  while (position < (u32)length) {
    if ((u32)length - position < 8)
      goto metadata_incomplete;
    const u8 *header = source + position;
    u32 encoded_size = ReadBE32(header + 4);
    position += 8;
    if (encoded_size > (u32)length - position)
      goto metadata_incomplete;
    // Scenario files legitimately repeat TEXT chunks; preserve file order.
    CityFileChunk *chunk = AppendCityFileChunk(city);
    if (chunk == NULL)
      goto fail;
    memcpy(chunk->tag, header, 4);
    chunk->spec = FindCityFileChunkSpec(chunk->tag);
    chunk->size = chunk->spec != NULL && chunk->spec->conversion != CITY_FILE_NAME ?
        chunk->spec->size : encoded_size;
    chunk->encoded_size = encoded_size;
    chunk->encoded = malloc(encoded_size ? encoded_size : 1);
    chunk->decoded = malloc(chunk->size ? chunk->size : 1);
    chunk->original = malloc(chunk->size ? chunk->size : 1);
    if (chunk->encoded == NULL || chunk->decoded == NULL || chunk->original == NULL)
      goto fail;
    memcpy(chunk->encoded, source + position, encoded_size);
    if (chunk->spec == NULL || chunk->spec->conversion == CITY_FILE_NAME ||
        memcmp(chunk->tag, "ALTM", 4) == 0) {
      if (encoded_size != chunk->size)
        goto metadata_incomplete;
      memcpy(chunk->decoded, chunk->encoded, chunk->size);
    } else if (!CityFileDecodeRLE(chunk->encoded, encoded_size, chunk->decoded, chunk->size)) {
      goto metadata_incomplete;
    }
    memcpy(chunk->original, chunk->decoded, chunk->size);
    position += encoded_size;
  }
  city->metadata_complete = 1;
  city->source = source;
  city->source_size = (u32)length;
  return city;
metadata_incomplete:
  // Loading uses the retained bytes, not this optional round-trip index.
  for (u32 index = 0; index < city->count; ++index) {
    free(city->chunk[index].decoded);
    free(city->chunk[index].original);
    free(city->chunk[index].encoded);
  }
  free(city->chunk);
  city->chunk = NULL;
  city->count = city->capacity = 0;
  city->source = source;
  city->source_size = (u32)length;
  return city;
fail:
  free(source);
  CityFileFree(city);
  return NULL;
}

// Open a host path and return an owned archive with original bytes and available chunk metadata. NULL reports
// open/resource/I/O failure; callers also inspect metadata_complete before using parsed contents and release results
// with CityFileFree.
CityFile *CityFileRead(const char *path) {
  FILE *input = fopen(path, "rb");
  if (input == NULL)
    return NULL;
  return CityFileReadOpened(path, input);
}

// Check whether an in-memory archive contains a four-byte chunk tag. Inputs must be valid; the query neither decodes
// nor mutates its payloads.
bool CityFileHasChunk(const CityFile *city, const char tag[4]) {
  for (u32 index = 0; index < city->count; ++index)
    if (memcmp(city->chunk[index].tag, tag, 4) == 0)
      return true;
  return false;
}

// Serialize an in-memory stock archive into an owned byte buffer and report its length. Returns NULL on failure;
// caller frees a successful result and archive ownership remains unchanged.
u8 *BuildCityFileBytes(const CityFile *city, u32 *size) {
  if (city == NULL)
    return NULL;
  const u8 **data = calloc(city->count != 0 ? city->count : 1u, sizeof(*data));
  u8 **generated = calloc(city->count != 0 ? city->count : 1u, sizeof(*generated));
  u32 *length = calloc(city->count != 0 ? city->count : 1u, sizeof(*length));
  if (data == NULL || generated == NULL || length == NULL) {
    free(data);
    free(generated);
    free(length);
    return NULL;
  }
  u64 body_size = 4;
  u8 *result = NULL;
  for (u32 index = 0; index < city->count; ++index) {
    const CityFileChunk *chunk = &city->chunk[index];
    if (chunk->encoded != NULL &&
        (chunk->size == 0 || memcmp(chunk->decoded, chunk->original, chunk->size) == 0)) {
      data[index] = chunk->encoded;
      length[index] = chunk->encoded_size;
    } else if (chunk->spec == NULL || chunk->spec->conversion == CITY_FILE_NAME ||
               memcmp(chunk->tag, "ALTM", 4) == 0) {
      data[index] = chunk->decoded;
      length[index] = chunk->size;
    } else {
      generated[index] = malloc((size_t)chunk->size * 3 / 2 + 2);
      if (generated[index] == NULL)
        goto finish;
      length[index] = EncodeCityFileRle(chunk->decoded, chunk->size, generated[index]);
      data[index] = generated[index];
    }
    body_size += 8 + length[index];
    if (body_size > UINT32_MAX - 8)
      goto finish;
  }
  result = malloc((size_t)body_size + 8);
  if (result == NULL) goto finish;
  memcpy(result, "FORM", 4);
  WriteBE32(result + 4, (u32)body_size);
  memcpy(result + 8, "SCDH", 4);
  u32 position = 12;
  for (u32 index = 0; index < city->count; ++index) {
    memcpy(result + position, city->chunk[index].tag, 4);
    WriteBE32(result + position + 4, length[index]);
    memcpy(result + position + 8, data[index], length[index]);
    position += 8 + length[index];
  }
  *size = (u32)body_size + 8;
finish:
  for (u32 index = 0; index < city->count; ++index)
    free(generated[index]);
  free(data);
  free(generated);
  free(length);
  return result;
}

// Serialize an in-memory stock archive to the supplied host path. Returns false on serialization or I/O failure;
// archive storage remains caller-owned and output may be partial.
bool CityFileWrite(const CityFile *city, const char *path) {
  if (city == NULL) return false;
  u32 size = city->source_size;
  u8 *generated = city->metadata_complete ? BuildCityFileBytes(city, &size) : NULL;
  const u8 *bytes = city->metadata_complete ? generated : city->source;
  if (bytes == NULL) return false;
  FILE *output = fopen(path, "wb");
  if (output == NULL) { free(generated); return false; }
  i32 success = fwrite(bytes, 1, size, output) == size;
  if (fclose(output) != 0) success = 0;
  free(generated);
  return success;
}
