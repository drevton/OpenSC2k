#ifndef SC2K_ASSET_FILE_H
#define SC2K_ASSET_FILE_H

#include <stdio.h>
#include "city/types.h"

typedef enum AssetFileKind {
  ASSET_FILE_RAW, ASSET_FILE_MEMORY, ASSET_FILE_ZSTD, ASSET_FILE_SECTION
} AssetFileKind;

// Startup cursor. Opening fixes its source kind until close: raw files and borrowed memory support seeking;
// compressed files and bounded archive sections decode forward only. Decoder fields retain owned scratch.
typedef struct AssetFile {
  FILE *file;
  const u8 *memory;
  struct ZSTD_DCtx_s *decoder;
  u8 *input, *output;
  size_t input_size, input_offset, output_size, output_offset;
  bool failed, finished;
  // RAW/MEMORY byte length, ZSTD encoded file size, or SECTION expected decoded size.
  u64 length;
  u64 position; // Consumed decoded bytes; buffered decoder output can be ahead of this cursor.
  AssetFileKind kind;
  u64 compressed_remaining, decoded_total; // Bounded input remaining and total decoder output, before buffering.
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
AssetOpenResult OpenAssetSource(AssetFile *file, const CityAssets *assets, u32 id, const char *root);
// Raw disk files and decoded spans support seeking; compressed input is forward-only.
bool SeekAssetFile(AssetFile *asset, u64 position);
size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size);
int ReadAssetByte(AssetFile *asset);
// Borrow and consume the next size bytes from an open memory span, without copying or allocating.
// Other source kinds, a failed cursor or insufficient bytes return an empty span without changing the cursor.
// Successful zero-length reads retain a nonnull data pointer. Bytes keep the original span's lifetime.
ByteSpan ReadAssetSpan(AssetFile *asset, size_t size);
// Does not drain compressed input: its decoder must have reached the frame end for closing to succeed.
// Also reports truncation, checksum errors, trailing data and close errors, then clears the cursor.
bool CloseAssetFile(AssetFile *asset);

// Read the remaining decoded bytes, at most limit. Known-length sources allocate once; compressed input grows
// within that bound. The caller owns the returned allocation; failure returns NULL and clears size.
// Leaves the cursor open; CloseAssetFile must still report any final stream/close error before publishing the data.
u8 *ReadAllAssetFile(AssetFile *asset, size_t limit, size_t *size);
// Decode one bounded Zstandard frame. The caller retains the FILE; only decoder scratch is released here.
bool DecodeAssetSection(FILE *file, u64 offset, u64 compressed, u8 *data, size_t size);

#endif
