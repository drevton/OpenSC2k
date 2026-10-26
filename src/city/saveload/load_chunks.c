#include "city/saveload/state.h"
#include "city/saveload/runtime.h"
#include "city/saveload/services.h"
#include "city/engine/city_name.h"
#include "city/maps/labels.h"
#include "city/engine/simulation_state.h"
#include "city/saveload/events_format.h"
#include "city/saveload/map_format.h"
#include "city/saveload/map_native.h"
#include "city/maps/ground.h"
#include "city/events/events.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include "city/saveload/legacy_text.h"
#include "city/economy/facilities.h"
#include "city/economy/history.h"
#include "city/state.h"
#include "city/maps/map.h"
#include "city/tools/tools.h"
#include "city/saveload/sc2k.h"
#include "city/saveload/city_file_internal.h"
#include "city/saveload/input.h"
#include "game/errors.h"
#include "game/state.h"
#include "base/byte_order.h"
#include "base/text.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  City *city;
  const CityFileChunkSpec *spec;
  const u8 *bytes;
} CityTileLoadBuffer;

// Throwing reads retain the partial raw tile or stock name-slot writes. Cleanup context must identify its original
// staging buffer and target; it must remain alive through the throwing-read unwind lifetime.
static void ApplyCityTileLoadBuffer(void *context) {
  CityTileLoadBuffer *buffer = context;
  WriteCityFileChunkBytes(buffer->city, buffer->spec, buffer->bytes, buffer->spec->size);
}

// Stream dispatch uses the same format table as round-trip I/O. Requires an active file operation and initialized
// storage; failure preserves prior writes, and only complete success may run conversion/rebuilding.
bool LoadCityChunksFromInput(City *city, CityFileInput *input,
    const char *path, bool allocate_misc) {
  city->load_sloped_zone_tiles = 0;
  city->load_cleared_building_tiles = 0;
  city->load_submerged_occupant_tiles = 0;
  CityFileRuntime *file = city->game->file;
  u32 signature = 0;
  if (!CityFileInputRead(input, &signature, sizeof(signature))) {
    ReportCityFileError(city->game->file, CITY_NOTICE_ERROR_READING_FILE, CITY_FILE_ERROR_WITH_CAUSE);
    return false;
  }
  if (!memcmp(&signature, "\x28\xb5\x2f\xfd", 4)) return LoadCitySc2k(city, input);
  input->first_tag = signature;
  input->has_first_tag = true;
  u32 container_length = 0, position = 4, tag = 0, payload_size = 0;
  if (!CityFileValidateCityFileContainer(city, input, path, &container_length)) return false;
  if (!ResizeCityMap(city, CITY_MAP_SIZE, CITY_MAP_SIZE)) return false;
  for (int slot = CITY_STOCK_EVENT_SLOT_COUNT; slot < city->events.capacity; ++slot)
    ClearCityMapEventSlot(city, slot);
  memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(u16));
  for (int slot = 0; slot < CITY_STOCK_EVENT_SLOT_COUNT; ++slot) {
    city->events.event[slot].hash_next = 0;
    city->events.event[slot].coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
  }
  city->native_map_storage = false;
  city->simulation_work = (CitySimulationWork){0};
  ReleaseCityFileExtensions(city);
  memset(&city->cheats, 0, sizeof(city->cheats));
  EmptyCityName(city);
  bool has_labels = false, saw_name = false, saw_misc = false;
  bool saw_extension = false, saw_ground = false;
  do {
    if (CityFileInputRead(input, &tag, 4) == 0) {
      ShowCityFileException(city, CITY_NOTICE_ERROR_READING_FILE, (&file->error), path);
      return false;
    }
    tag = ReadBE32((const u8 *)&tag);
    if (CityFileInputRead(input, &payload_size, 4) == 0) {
      ShowCityFileException(city, CITY_NOTICE_ERROR_READING_FILE, (&file->error), path);
      return false;
    }
    payload_size = ReadBE32((const u8 *)&payload_size);
    char tag_bytes[4];
    WriteBE32((u8 *)tag_bytes, tag);
    const CityFileChunkSpec *spec = FindCityFileChunkSpec(tag_bytes);
    if (spec != NULL && spec->conversion == CITY_FILE_NAME) {
      if (payload_size == 0) {
        EmptyCityName(city);
      } else {
        u8 *text = AllocateCityFileScratch(file, (size_t)payload_size + 1);
        if (text == NULL) return false;
        CityFileScratchCleanup buffer = {file, text};
        CityFileCleanup cleanup = {ReleaseCityFileScratchBuffer, &buffer, NULL};
        PushCityFileCleanup(file, &cleanup);
        u32 bytes_read = CityFileInputRead(input, text, payload_size);
        if (bytes_read == 0) {
          PopCityFileCleanup(file, &cleanup);
          FreeCityFileScratch(file, text);
          return false;
        }
        u32 length = 0;
        // Ignore the prefix byte and bound the original strlen to bytes read.
        // Neither an unread tail nor the spare allocation byte is name text.
        while (length + 1 < bytes_read && text[length + 1] != 0) ++length;
        char *name = AllocateText(3 * (size_t)length + 1);
        size_t name_length = DecodeLegacyCityText(name, 3 * (size_t)length + 1, text + 1, length,
            CITY_TEXT_WINDOWS_1252);
        AssignCityName(city, name_length, (const u8 *)name);
        free(name);
        PopCityFileCleanup(file, &cleanup);
        FreeCityFileScratch(file, text);
        if (GetGameError(city->game) == SC2K_ERROR_GUEST_ALLOCATION_FAILURE) return false;
        saw_name = true;
      }
    } else if (spec != NULL && memcmp(spec->tag, "MISC", 4) == 0) {
      if (saw_extension) return false;
      // The opaque-file adapter supplies the caller-owned serialization buffer.
      // The original public boundary requires its caller to allocate it.
      if (allocate_misc && !saw_misc && !file->misc_active && !AllocateCityMiscBuffer(file)) return false;
      saw_misc = true;
      if (!LoadCityMiscFromInput(city, input, payload_size)) return false;
    } else if (!memcmp(tag_bytes, "XNAT", 4) || !memcmp(tag_bytes, "XGND", 4)) {
      bool ground = tag_bytes[1] == 'G';
      if (ground ? !saw_extension || saw_ground || payload_size < CityGroundExtensionSize(city) :
          !saw_misc || saw_extension || payload_size < 8) return false;
      if (payload_size > CITY_SAVE_EXTENSION_LIMIT) return false;
      u8 *bytes = AllocateCityFileScratch(file, payload_size);
      if (!bytes) return false;
      CityFileScratchCleanup buffer = {file, bytes};
      CityFileCleanup cleanup = {ReleaseCityFileScratchBuffer, &buffer, NULL};
      PushCityFileCleanup(file, &cleanup);
      bool loaded = CityFileInputRead(input, bytes, payload_size) == payload_size &&
          (ground ? ApplyCityGroundExtension(city, bytes, payload_size) :
              ApplyCitySaveExtension(city, bytes, payload_size));
      PopCityFileCleanup(file, &cleanup);
      FreeCityFileScratch(file, bytes);
      if (!loaded) {
        if (!ground) ReportCityFileError(file, CITY_NOTICE_INVALID_CITY_CONTENTS, CITY_FILE_ERROR_RESOURCE_ONLY);
        return false;
      }
      if (ground) saw_ground = true;
      else saw_extension = true;
    } else if (spec == NULL || spec->storage == CITY_FILE_ONLY) {
      if (!CityFileSkipCityChunk(file, input, payload_size)) return false;
    } else {
      u32 target_size = memcmp(spec->tag, "ALTM", 4) == 0 ? payload_size : spec->size;
      // Original map buffers are contiguous behind the first row pointer.
      // Oversized raw writes have a defined bounded failure instead of overflow.
      if (target_size > spec->size) return false;
      u8 *target = CityFileMutableChunkData(city, spec);
      if (target == NULL) return false;
      if (spec->conversion == CITY_FILE_EVENTS) {
        u8 bytes[CITY_STOCK_EVENT_BYTES * CITY_STOCK_EVENT_SLOT_COUNT];
        EncodeStockCityMapEvents(city, bytes);
        bool loaded = CityFileReadCompressedCityChunk(file, input, payload_size, bytes, sizeof(bytes));
        DecodeStockCityMapEvents(city, bytes);
        if (!loaded) return false;
      } else if (memcmp(spec->tag, "ALTM", 4) == 0) {
        u8 bytes[CITY_MAP_CELL_COUNT * sizeof(u16)];
        ReadCityFileChunkBytes(city, spec, bytes, spec->size);
        CityTileLoadBuffer buffer = {city, spec, bytes};
        CityFileCleanup cleanup = {ApplyCityTileLoadBuffer, &buffer, NULL};
        PushCityFileCleanup(file, &cleanup);
        u32 received = CityFileInputRead(input, bytes, payload_size);
        PopCityFileCleanup(file, &cleanup);
        if (received) CityFileConvertWords(bytes, payload_size & ~1u, 2);
        ApplyCityTileLoadBuffer(&buffer);
        if (!received) return false;
      } else if (spec->conversion == CITY_FILE_LABELS) {
        has_labels = true;
        u8 bytes[CITY_OVERLAY_NAME_COUNT * CITY_OVERLAY_LEGACY_NAME_BYTES];
        ReadCityFileChunkBytes(city, spec, bytes, spec->size);
        CityTileLoadBuffer buffer = {city, spec, bytes};
        CityFileCleanup cleanup = {ApplyCityTileLoadBuffer, &buffer, NULL};
        PushCityFileCleanup(file, &cleanup);
        bool loaded = ReadCityCompressedChunkStrided(file, input, payload_size, bytes, spec->size, 1);
        PopCityFileCleanup(file, &cleanup);
        ApplyCityTileLoadBuffer(&buffer);
        if (!loaded) return false;
      } else {
        int stride = CityFileChunkByteOffset(spec, 1);
        if (!ReadCityCompressedChunkStrided(file, input, payload_size, target, spec->size, stride)) return false;
        if (spec->conversion != CITY_FILE_LABELS) CityFileConvertChunk(target, spec, CITY_FILE_IMPORT);
      }
    }
    position += payload_size + 8;
  } while ((int)position < (int)container_length);
  if (city->native_map_storage != saw_ground) return false;
  if (!saw_name || city->name.length == 0) AssignCityNameFromHostFilename(city, path);
  if (!ConvertCityMapToNative(city)) {
    ReportCityFileError(file, CITY_NOTICE_INVALID_CITY_CONTENTS, CITY_FILE_ERROR_RESOURCE_ONLY);
    return false;
  }
  ImportStockCityEventLinks(city);
  bool individual = city->individual_facilities;
  city->individual_facilities = false;
  if (!ImportStockCityAssociations(city)) return false;
  RebuildLoadedCityMapState(city);
  RecountCityTileStatistics(city);
  RebuildCityHistoryMaxima(city);
  if (has_labels)
    DecodeLoadedCityOverlayNames(city);
  else
    ClearCityOverlayNames(city);
  if (individual && !SetCityIndividualFacilities(city, true)) return false;
  if (!ImportCityMapFormat(city)) {
    ReportCityFileError(file, CITY_NOTICE_INVALID_CITY_CONTENTS, CITY_FILE_ERROR_RESOURCE_ONLY);
    return false;
  }
  if (!saw_ground) InitializeCityTransportPaving(city);
  // CNAM can follow MISC; derive labels only after the final city name is known.
  RefreshCityNeighborNames(city);
  return true;
}
