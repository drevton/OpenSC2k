#include "assets/asset_file.h"

#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

enum { kAssetInputBytes = 128 * 1024, kAssetOutputBytes = 32 * 1024, kAssetWindowLogMax = 23 };

static void ReleaseAssetDecoder(AssetFile *asset) {
  ZSTD_freeDStream(asset->decoder);
  free(asset->input);
  free(asset->output);
  asset->decoder = NULL;
  asset->input = asset->output = NULL;
}

bool CloseAssetFile(AssetFile *asset) {
  bool valid = !asset->failed && (!asset->decoder || asset->finished);
  if (asset->file) {
    if (ferror(asset->file)) valid = false;
    if (fclose(asset->file)) valid = false;
  }
  ReleaseAssetDecoder(asset);
  *asset = (AssetFile){0};
  return valid;
}

static bool InitializeAssetDecoder(AssetFile *asset) {
  asset->decoder = ZSTD_createDStream();
  asset->input = malloc(kAssetInputBytes);
  asset->output = malloc(kAssetOutputBytes);
  if (!asset->decoder || !asset->input || !asset->output || ZSTD_isError(ZSTD_initDStream(asset->decoder)) ||
      ZSTD_isError(ZSTD_DCtx_setParameter(asset->decoder, ZSTD_d_windowLogMax, kAssetWindowLogMax))) {
    ReleaseAssetDecoder(asset);
    return false;
  }
  return true;
}

int OpenAssetFile(AssetFile *asset, const char *path) {
  *asset = (AssetFile){0};
  asset->file = fopen(path, "rb");
  if (!asset->file) return errno == ENOENT ? 0 : -1;
  struct stat status;
  if (fstat(fileno(asset->file), &status) || status.st_size < 0) {
    CloseAssetFile(asset);
    return -1;
  }
  asset->length = status.st_size;
  u8 magic[4];
  size_t count = fread(magic, 1, sizeof(magic), asset->file);
  if (ferror(asset->file) || fseek(asset->file, 0, SEEK_SET)) {
    CloseAssetFile(asset);
    return -1;
  }
  if (count != sizeof(magic) || memcmp(magic, "\x28\xb5\x2f\xfd", sizeof(magic))) return 1;
  if (InitializeAssetDecoder(asset)) return 1;
  CloseAssetFile(asset);
  return -1;
}

static size_t DecodeAssetBytes(AssetFile *asset, void *destination, size_t size) {
  ZSTD_outBuffer output = {destination, size, 0};
  while (output.pos < output.size && !asset->finished && !asset->failed) {
    if (asset->input_offset == asset->input_size) {
      size_t wanted = kAssetInputBytes;
      if (asset->bounded_stream && asset->compressed_remaining < wanted) wanted = asset->compressed_remaining;
      asset->input_size = fread(asset->input, 1, wanted, asset->file);
      if (asset->bounded_stream) {
        asset->compressed_remaining -= asset->input_size;
        if (asset->input_size != wanted) asset->failed = true;
      }
      asset->input_offset = 0;
      if (ferror(asset->file)) { asset->failed = true; break; }
    }
    ZSTD_inBuffer input = {asset->input, asset->input_size, asset->input_offset};
    size_t previous = output.pos;
    size_t result = ZSTD_decompressStream(asset->decoder, &output, &input);
    if (ZSTD_isError(result)) { asset->failed = true; break; }
    bool progress = input.pos != asset->input_offset || output.pos != previous;
    asset->input_offset = input.pos;
    if (!result) {
      asset->finished = true;
      // Each bounded archive section contains one complete checksummed frame.
      asset->failed = input.pos != input.size || ferror(asset->file) ||
          (asset->bounded_stream ? asset->compressed_remaining != 0 : fgetc(asset->file) != EOF);
    } else if (!progress) {
      // Empty input can still flush buffered output. No progress means truncation.
      asset->failed = true;
    }
  }
  asset->decoded_total += output.pos;
  if (asset->bounded_stream && (asset->decoded_total > asset->length ||
          (asset->finished && asset->decoded_total != asset->length))) asset->failed = true;
  return output.pos;
}

size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size) {
  if (asset->failed) return 0;
  if (!asset->decoder) {
    if (asset->length - asset->position < size) size = asset->length - asset->position;
    if (asset->memory) {
      memcpy(destination, asset->memory + asset->position, size);
      asset->position += size;
      return size;
    }
    size_t count = fread(destination, 1, size, asset->file);
    asset->position += count;
    asset->failed = count != size || ferror(asset->file) != 0;
    return count;
  }
  u8 *target = destination;
  size_t count = 0;
  while (count < size && !asset->failed) {
    size_t buffered = asset->output_size - asset->output_offset;
    if (buffered) {
      size_t bytes = buffered < size - count ? buffered : size - count;
      memcpy(target + count, asset->output + asset->output_offset, bytes);
      asset->output_offset += bytes;
      count += bytes;
    } else if (asset->finished) break;
    else if (size - count >= kAssetOutputBytes) {
      count += DecodeAssetBytes(asset, target + count, size - count);
    } else {
      asset->output_size = DecodeAssetBytes(asset, asset->output, kAssetOutputBytes);
      asset->output_offset = 0;
    }
  }
  asset->position += count;
  return count;
}

int ReadAssetByte(AssetFile *asset) {
  u8 value;
  return ReadAssetFile(asset, &value, 1) == 1 ? value : EOF;
}

u8 *ReadAllAssetFile(AssetFile *asset, size_t limit, size_t *size) {
  *size = 0;
  size_t capacity = limit < kAssetOutputBytes ? limit : kAssetOutputBytes;
  u8 *data = malloc(capacity);
  if (!data) return NULL;
  for (;;) {
    *size += ReadAssetFile(asset, data + *size, capacity - *size);
    if (asset->failed) break;
    if (*size < capacity || capacity == limit) {
      if (ReadAssetByte(asset) == EOF && !asset->failed) return data;
      break;
    }
    capacity = capacity > limit / 2 ? limit : capacity * 2;
    u8 *grown = realloc(data, capacity);
    if (!grown) break;
    data = grown;
  }
  free(data);
  *size = 0;
  return NULL;
}

int OpenAssetSpan(AssetFile *asset, ByteSpan span) {
  *asset = (AssetFile){.memory = span.data, .length = span.size};
  return span.data ? 1 : 0;
}

bool SeekAssetFile(AssetFile *asset, u64 position) {
  if (asset->failed || asset->decoder || position > asset->length) return false;
  if (!asset->memory && (position > INT64_MAX || fseeko(asset->file, position, SEEK_SET))) return false;
  asset->position = position;
  return true;
}

bool DecodeAssetSection(FILE *file, u64 offset, u64 compressed, u8 *data, size_t size) {
  AssetFile reader = {.file = file, .bounded_stream = true, .compressed_remaining = compressed, .length = size};
  bool valid = offset <= INT64_MAX && !fseeko(file, offset, SEEK_SET);
  // The archive owns the stream; this operation owns only the decoder.
  valid = valid && InitializeAssetDecoder(&reader);
  valid = valid && ReadAssetFile(&reader, data, size) == size && ReadAssetByte(&reader) == EOF && !reader.failed;
  ReleaseAssetDecoder(&reader);
  return valid;
}
