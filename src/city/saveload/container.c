#include "city/saveload/file.h"

#include "city/saveload/city_file_internal.h"
#include "base/byte_order.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Container

// Drop the optional editing index while preserving retained file bytes and path. Each slot owns its three payloads.
static void ReleaseCityFileChunkIndex(CityFile *archive) {
  for (u32 index = 0; index < archive->count; ++index) {
    free(archive->chunk[index].decoded);
    free(archive->chunk[index].original);
    free(archive->chunk[index].encoded);
  }
  free(archive->chunk);
  archive->chunk = NULL;
  archive->count = archive->capacity = 0;
  archive->metadata_complete = false;
}

// Release an in-memory stock archive and all owned chunk payloads. NULL is accepted; no pointers into the archive
// remain valid after release.
void CityFileFree(CityFile *archive) {
  if (archive == NULL)
    return;
  free(archive->source_path);
  free(archive->source);
  ReleaseCityFileChunkIndex(archive);
  free(archive);
}

// Grow an archive's chunk array and initialize its next slot. Returns a borrowed slot or NULL on allocation failure;
// growth can invalidate earlier pointers into that array.
static CityFileChunk *AppendCityFileChunk(CityFile *archive) {
  if (archive->count == archive->capacity) {
    u32 capacity = archive->capacity != 0 ? archive->capacity * 2 : 32u;
    if (capacity <= archive->capacity || (u64)capacity * sizeof(CityFileChunk) > SIZE_MAX)
      return NULL;
    CityFileChunk *chunks = realloc(archive->chunk, (size_t)capacity * sizeof(*chunks));
    if (chunks == NULL)
      return NULL;
    archive->chunk = chunks;
    archive->capacity = capacity;
  }
  CityFileChunk *chunk = &archive->chunk[archive->count++];
  memset(chunk, 0, sizeof(*chunk));
  return chunk;
}

// Consume and close an opened stock file, retaining its original bytes and any parsed chunk metadata. Returns NULL on
// resource/I/O failure. The optional editing index may be incomplete; loading still uses the retained original bytes.
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
  CityFile *archive = calloc(1, sizeof(*archive));
  if (archive == NULL)
    goto fail;
  size_t path_size = strlen(path) + 1;
  archive->source_path = malloc(path_size);
  if (archive->source_path == NULL)
    goto fail;
  memcpy(archive->source_path, path, path_size);
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
    CityFileChunk *chunk = AppendCityFileChunk(archive);
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
  archive->metadata_complete = true;
  archive->source = source;
  archive->source_size = (u32)length;
  return archive;
metadata_incomplete:
  // Loading uses the retained bytes, not this optional round-trip index.
  ReleaseCityFileChunkIndex(archive);
  archive->source = source;
  archive->source_size = (u32)length;
  return archive;
fail:
  free(source);
  CityFileFree(archive);
  return NULL;
}

// Open a host path and return an owned archive with original bytes and available chunk metadata. NULL reports
// open/resource/I/O failure. Loading uses retained bytes even when the editing index is incomplete; release the result
// with CityFileFree.
CityFile *CityFileRead(const char *path) {
  FILE *input = fopen(path, "rb");
  if (input == NULL)
    return NULL;
  return CityFileReadOpened(path, input);
}

// Check whether an in-memory archive contains a four-byte chunk tag. Inputs must be valid; the query neither decodes
// nor mutates its payloads.
bool CityFileHasChunk(const CityFile *archive, const char tag[4]) {
  for (u32 index = 0; index < archive->count; ++index)
    if (memcmp(archive->chunk[index].tag, tag, 4) == 0)
      return true;
  return false;
}

// Serialize an in-memory stock archive into an owned byte buffer and report its length. Returns NULL on failure;
// caller frees a successful result and archive ownership remains unchanged.
u8 *BuildCityFileBytes(const CityFile *archive, u32 *size) {
  if (archive == NULL)
    return NULL;
  const u8 **data = calloc(archive->count != 0 ? archive->count : 1u, sizeof(*data));
  u8 **generated = calloc(archive->count != 0 ? archive->count : 1u, sizeof(*generated));
  u32 *length = calloc(archive->count != 0 ? archive->count : 1u, sizeof(*length));
  if (data == NULL || generated == NULL || length == NULL) {
    free(data);
    free(generated);
    free(length);
    return NULL;
  }
  u64 body_size = 4;
  u8 *result = NULL;
  for (u32 index = 0; index < archive->count; ++index) {
    const CityFileChunk *chunk = &archive->chunk[index];
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
  for (u32 index = 0; index < archive->count; ++index) {
    memcpy(result + position, archive->chunk[index].tag, 4);
    WriteBE32(result + position + 4, length[index]);
    memcpy(result + position + 8, data[index], length[index]);
    position += 8 + length[index];
  }
  *size = (u32)body_size + 8;
finish:
  for (u32 index = 0; index < archive->count; ++index)
    free(generated[index]);
  free(data);
  free(generated);
  free(length);
  return result;
}

// Serialize an in-memory stock archive to the supplied host path. Returns false on serialization or I/O failure;
// archive storage remains caller-owned and output may be partial.
bool CityFileWrite(const CityFile *archive, const char *path) {
  if (archive == NULL) return false;
  u32 size = archive->source_size;
  u8 *generated = archive->metadata_complete ? BuildCityFileBytes(archive, &size) : NULL;
  const u8 *bytes = archive->metadata_complete ? generated : archive->source;
  if (bytes == NULL) return false;
  FILE *output = fopen(path, "wb");
  if (output == NULL) { free(generated); return false; }
  bool success = fwrite(bytes, 1, size, output) == size;
  if (fclose(output) != 0) success = false;
  free(generated);
  return success;
}
