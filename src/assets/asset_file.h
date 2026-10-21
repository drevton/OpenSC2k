#ifndef SC2K_ASSET_FILE_H
#define SC2K_ASSET_FILE_H

#include <stdio.h>
#include "city/types.h"

// Startup file decoder, or a bounded cursor over an already decoded asset.
typedef struct {
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

typedef struct RunSprite RunSprite;
// Dimensions are already validated; the returned native run image owns one reference.
RunSprite *ReadAssetRunSprite(AssetFile *file, int width, int height, u32 size);
// Static picture record: ID, dimensions, optional RGB palette, then native runs.
// Zero expected dimensions accept the bounded dimensions stored in the record.
RunSprite *ReadAssetPicture(AssetFile *file, int id, int width, int height);

// Opening a span does no I/O, decoding or allocation, and does not take ownership.
int OpenAssetSpan(AssetFile *asset, ByteSpan span);
// Returns 1 when opened, 0 when missing, -1 for unreadable or invalid input.
// A null root reads retained bytes; a root is an explicit startup/diagnostic loose source.
int OpenAssetSource(AssetFile *file, CityAssets *assets, u32 id, const char *root);
bool SeekAssetFile(AssetFile *asset, u64 position);
size_t ReadAssetFile(AssetFile *asset, void *destination, size_t size);
int ReadAssetByte(AssetFile *asset);
// Also reports truncation, checksum errors, trailing data and close errors.
bool CloseAssetFile(AssetFile *asset);

#endif
