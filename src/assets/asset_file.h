#ifndef SC2K_ASSET_FILE_H
#define SC2K_ASSET_FILE_H

#include <stdio.h>
#include "city/types.h"

// Startup file decoder, or a bounded cursor over an already decoded asset.
typedef struct AssetFile {
  FILE *file;
  const u8 *memory;
  struct ZSTD_DCtx_s *decoder;
  u8 *input, *output;
  size_t input_size, input_offset, output_size, output_offset;
  bool failed, finished;
  u64 length, position;
  bool bounded_stream;
  u64 compressed_remaining, decoded_total;
} AssetFile;

// Returns 1 when opened, 0 when missing, -1 for unreadable or invalid input.
// A successful open owns the file and decoder until CloseAssetFile.
int OpenAssetFile(AssetFile *asset, const char *path);
// Opening a span does no I/O, decoding or allocation, and does not take ownership.
int OpenAssetSpan(AssetFile *asset, ByteSpan span);
bool SeekAssetFile(AssetFile *asset, u64 position);
size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size);
int ReadAssetByte(AssetFile *asset);
// Also reports truncation, checksum errors, trailing data and close errors.
bool CloseAssetFile(AssetFile *asset);

// Read at most limit bytes. The caller owns the returned allocation; failure returns NULL and clears size.
u8 *ReadAllAssetFile(AssetFile *asset, size_t limit, size_t *size);
// Decode one bounded Zstandard frame. The caller retains the FILE; only decoder scratch is released here.
bool DecodeAssetSection(FILE *file, u64 offset, u64 compressed, u8 *data, size_t size);

#endif
