#include "city/saveload/services.h"
#include "city/saveload/state.h"
#include "city/saveload/events_format.h"
#include "city/saveload/map_format.h"
#include "city/events/events.h"
#include "city/maps/tile_storage.h"
#include "game/state.h"
#include "city/presentation.h"
#include "city/saveload/legacy_text.h"
#include "city/maps/map.h"
#include "city/saveload/city_file_internal.h"
#include "city/maps/layers.h"
#include "base/text.h"
#include "base/byte_order.h"
#include "game/errors.h"

// Write a staged block through the active save stream and report its error at that boundary. Bytes must provide size
// readable bytes; false stops normal serialization while retaining partial output and the first I/O cause.
static bool WriteCitySaveBytes(CityFileRuntime *file, void *stream, const void *bytes, u32 size) {
  i32 result = WriteCitySaveStream(stream, bytes, size);
  if (!result) ReportCitySaveError(file, CITY_NOTICE_ERROR_WRITING_FILE, NULL);
  return result;
}

// Seek and three independent writes. Callers ignore the final rewrite's result. Requires an open save stream; returns
// write success and retains individual seek/write observations without hiding an earlier failure.
static bool SaveCityFileHeader(CityFileRuntime *file, void *stream, u32 size) {
  i32 result = SeekCitySaveStreamStart(stream);
  if (!result) {
    ReportCitySaveError(file, CITY_NOTICE_ERROR_WRITING_FILE, NULL);
    return false;
  }
  u8 length[4];
  WriteBE32(length, size);
  return WriteCitySaveBytes(file, stream, "FORM", 4) && WriteCitySaveBytes(file, stream, length, 4) &&
      WriteCitySaveBytes(file, stream, "SCDH", 4);
}

// Four writes preserve the original chunk and error ordering. Bytes after the name are explicitly zeroed instead of
// reading past its terminator. Requires an open save stream and valid terminated City name; returns false at the
// failing write and retains partial output.
static bool SaveCityNameChunk(City *city, void *stream) {
  CityFileRuntime *file = city->game->file;
  u8 size[4];
  WriteBE32(size, CITY_NAME_CAPACITY);
  u8 name[CITY_NAME_LIMIT] = {0};
  EncodeLegacyCityText(name, sizeof(name), CityNameText(&city->name));
  u8 prefix = CITY_NAME_LIMIT;
  bool saved = WriteCitySaveBytes(file, stream, "CNAM", 4) && WriteCitySaveBytes(file, stream, size, 4) &&
      WriteCitySaveBytes(file, stream, &prefix, 1) && WriteCitySaveBytes(file, stream, name, sizeof(name));
  if (saved) *file->save_size += 40;
  return saved;
}

// Signed lookahead bytes, repeat pairs, literal overlap, and final singleton emission are deliberate. Both scratch
// buffers belong to this call. Source must hold size initialized bytes; returns false on scratch/I/O failure and
// releases both temporary codec buffers before return.
static bool SaveCityCompressedChunk(City *city, void *stream, const char tag[4], const u8 *source, u32 size,
    CityFileConversion conversion) {
  CityFileRuntime *file = city->game->file;
  u8 *input = AllocateCityFileScratch(file, size);
  if (input == NULL) {
    SetGameError(file->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return false;
  }
  const CityFileChunkSpec *spec = FindCityFileChunkSpec(tag);
  if (spec && spec->storage == CITY_FILE_TILE) ExportCityMapChunk(city, spec, input, size);
  else if (spec && (spec->storage == CITY_FILE_HALF_MAP || spec->storage == CITY_FILE_QUARTER_MAP))
    CopyCityFileChunkBytes(city, spec, input, size, false);
  else if (conversion == CITY_FILE_LABELS) ExportStockCityOverlayNames(city, input);
  else memcpy(input, source, size);

  u8 *output = AllocateCityFileScratch(file, size * 3 / 2);
  if (output == NULL) {
    SetGameError(file->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    FreeCityFileScratch(file, input);
    return false;
  }
  if (conversion == CITY_FILE_WORDS32) CityFileConvertWords(input, size, 4);
  if (conversion == CITY_FILE_FACILITIES)
    ByteSwapCityFacilityRecords(input, size);
  u32 target = EncodeCityFileRle(input, size, output);
  *file->save_size += target + 8;
  u8 length[4];
  WriteBE32(length, target);
  i32 saved = WriteCitySaveBytes(file, stream, tag, 4) && WriteCitySaveBytes(file, stream, length, 4) &&
      WriteCitySaveBytes(file, stream, output, target);
  FreeCityFileScratch(file, input);
  FreeCityFileScratch(file, output);
  return saved;
}

// Project altitude into a stock big-endian staging buffer. Native bridge metadata stays in XGND;
// write failures retain completed output without changing the live map.
static bool SaveCityAltitudeChunk(City *city, CityFileRuntime *file, void *stream) {
  const CityFileChunkSpec *spec = &kCityFileChunks[2];
  u8 size[4];
  WriteBE32(size, spec->size);
  if (!WriteCitySaveBytes(file, stream, spec->tag, 4) || !WriteCitySaveBytes(file, stream, size, 4)) return false;
  u8 cells[CITY_MAP_CELL_COUNT * sizeof(u16)];
  for (TileIndex tile = 0; tile < CITY_MAP_CELL_COUNT; ++tile) {
    TileInfo record = TranslateCityTileToLegacy(city, tile);
    WriteBE16(cells + 2 * tile, record.altitude);
  }
  i32 saved = WriteCitySaveBytes(file, stream, cells, spec->size);
  if (saved) *file->save_size += spec->size + 8u;
  return saved;
}

// Consumes its temporary path and emits the 21 original chunks. Requires an open stream and the owned temporary path
// contract; failures stop output while retaining exact completed chunk writes.
bool SaveCityFileChunks(City *city, void *stream, CityPath *path) {
  CityFileRuntime *file = city->game->file;
  i32 saved = 0;
  for (int slot = CITY_STOCK_EVENT_SLOT_COUNT; slot < city->events.capacity; ++slot)
    if (city->events.event[slot].type != CITY_MAP_EVENT_UNUSED) goto cleanup;
  RecountCityTileStatistics(city);
  if (!SaveCityFileHeader(file, stream, 0)) goto cleanup;
  *file->save_size = 4;
  if (!SaveCityNameChunk(city, stream)) goto cleanup;
  if (!SaveCityMiscToBuffer(city)) goto cleanup;
  if (!SaveCityCompressedChunk(city, stream, "MISC", (const u8 *)(file->misc_active ? file->misc : NULL),
      sizeof(CityMiscBuffer), CITY_FILE_WORDS32)) goto cleanup;
  if (!SaveCityAltitudeChunk(city, file, stream)) goto cleanup;
  for (u32 index = 3; index < CITY_FILE_STANDARD_CHUNKS; ++index) {
    const CityFileChunkSpec *spec = &kCityFileChunks[index];
    const u8 *source = CityFileChunkData(city, spec);
    u8 event_bytes[CITY_STOCK_EVENT_BYTES * CITY_STOCK_EVENT_SLOT_COUNT];
    if (spec->conversion == CITY_FILE_EVENTS) {
      CityFileStateChunkBytes(city, spec, event_bytes, 0);
      source = event_bytes;
    }
    i32 result = SaveCityCompressedChunk(city, stream, spec->tag, source, spec->size, spec->conversion);
    if (!result) goto cleanup;
  }
  for (int packet = 0; packet < 2; ++packet) {
    u32 extension_size = packet ? CityGroundExtensionSize(city) : CitySaveExtensionSize(city);
    if (!extension_size) continue;
    u8 *bytes = AllocateCityFileScratch(file, extension_size), size[4];
    if (!bytes) goto cleanup;
    WriteBE32(size, extension_size);
    bool encoded = packet ? EncodeCityGroundExtension(city, bytes, extension_size) :
        EncodeCitySaveExtension(city, bytes, extension_size);
    bool written = encoded && WriteCitySaveBytes(file, stream, packet ? "XGND" : "XNAT", 4) &&
        WriteCitySaveBytes(file, stream, size, 4) && WriteCitySaveBytes(file, stream, bytes, extension_size);
    FreeCityFileScratch(file, bytes);
    if (!written) goto cleanup;
    *file->save_size += extension_size + 8;
  }
  SaveCityFileHeader(file, stream, *file->save_size);
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  saved = 1;
cleanup:
  FreeCityPathStorage(path);
  return saved;
}
