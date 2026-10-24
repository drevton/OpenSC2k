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

// Missing optional data permits a caller's fallback; unreadable/invalid data must remain an error.
typedef enum AssetOpenResult {
  ASSET_OPEN_ERROR = -1, ASSET_OPEN_MISSING = 0, ASSET_OPEN_OK = 1
} AssetOpenResult;

// Only ASSET_OPEN_OK opens need CloseAssetFile. Failed disk opens release their own temporary resources.
// A successful open owns the file and decoder until CloseAssetFile.
AssetOpenResult OpenAssetFile(AssetFile *asset, const char *path);
// Opening a span does no I/O, decoding or allocation, and does not take ownership.
AssetOpenResult OpenAssetSpan(AssetFile *asset, ByteSpan span);
// A null root borrows retained Assets bytes. An explicit root selects a loose startup/diagnostic source;
// absence and invalid input keep their distinct results, without selecting another source here.
AssetOpenResult OpenAssetSource(AssetFile *file, CityAssets *assets, u32 id, const char *root);
bool SeekAssetFile(AssetFile *asset, u64 position);
size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size);
int ReadAssetByte(AssetFile *asset);
// Also reports truncation, checksum errors, trailing data and close errors.
bool CloseAssetFile(AssetFile *asset);

// Read the remaining decoded bytes, at most limit. Known-length sources allocate once; compressed input grows
// within that bound. The caller owns the returned allocation; failure returns NULL and clears size.
// Leaves the cursor open; CloseAssetFile must still report any final stream/close error before publishing the data.
u8 *ReadAllAssetFile(AssetFile *asset, size_t limit, size_t *size);
// Decode one bounded Zstandard frame. The caller retains the FILE; only decoder scratch is released here.
bool DecodeAssetSection(FILE *file, u64 offset, u64 compressed, u8 *data, size_t size);

#endif
