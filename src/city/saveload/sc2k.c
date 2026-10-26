#include "city/saveload/sc2k.h"

#include "city/engine/city_name.h"
#include "city/maps/labels.h"
#include "city/engine/simulation.h"
#include "city/economy/facilities.h"
#include "city/events/events.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/maps/kinds.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/terrain/climate.h"
#include "game/state.h"
#include "base/utf8.h"
#include "base/byte_order.h"
#include "base/version.h"
#include "city/saveload/archive.h"
#include "city/saveload/input.h"
#include "city/saveload/runtime.h"
#include "city/saveload/services.h"
#include "city/saveload/file.h"
#include "city/saveload/map_native.h"
#include <zlib.h>
#include <stdlib.h>
#include "base/text.h"

// One Zstandard frame contains the complete RIFF stream and native little-endian records.
_Static_assert(kNativeLittleEndian, "native SC2K raw records require a little-endian host");
struct CityFileExtensions { CityArchive archive; };
typedef struct { u8 *data; u32 size, capacity; bool failed; } Buffer;
typedef struct {
  CityAnnualFacilityRecord simulation;
  TileIndex tile;
  u32 years;
  CityOverlayName name;
} SavedFacility;
typedef struct { u32 token; TileIndex tile; CityOverlayName name; } SavedSign;
_Static_assert(sizeof(SavedFacility) == 48 && offsetof(SavedFacility, tile) == 8 &&
    offsetof(SavedFacility, years) == 12 && offsetof(SavedFacility, name) == 16, "native facility record layout");
_Static_assert(sizeof(SavedSign) == 40 && offsetof(SavedSign, tile) == 4 &&
    offsetof(SavedSign, name) == 8, "native sign record layout");
static const char *const kRequiredEntries[] = {"TILE", "HALF", "QUAR", "LABL", "FACL", "SIGN", "EVNT", "HIST"};
// Reserve and append uninitialized bytes in a native-save staging buffer. Returns a borrowed span or NULL and latches
// failed on allocation/size failure; later growth may invalidate the span.
static u8 *AppendSpace(Buffer *b, u32 size) {
  if (b->failed || size > 768 * 1024 * 1024 - b->size) { b->failed = true; return NULL; }
  if (b->size + size > b->capacity) {
    u32 capacity = IntMax(b->size + size, b->capacity ? b->capacity * 2 : 256);
    u8 *bytes = realloc(b->data, capacity);
    if (!bytes) { b->failed = true; return NULL; }
    b->data = bytes; b->capacity = capacity;
  }
  u8 *space = b->data + b->size;
  b->size += size;
  return space;
}
// Copy a byte span to the native-save buffer, preserving its sticky failure state. Data must provide size readable
// bytes; zero size is accepted and the buffer owns its appended copy.
static void Append(Buffer *b, const void *data, u32 size) {
  if (!size) return;
  u8 *space = AppendSpace(b, size);
  if (space) memcpy(space, data, size);
}
// Append one numeric value to a native-save buffer in little-endian order. Buffer must be initialized; allocation
// failure is retained in its failed flag for the enclosing serializer.
static void Append32(Buffer *b, u32 value) { u8 bytes[4]; WriteLE32(bytes, value); Append(b, bytes, 4); }
// Release City's retained native archive, accepting NULL extension state.
// Clears the owner pointer and invalidates borrowed views.
void ReleaseCityFileExtensions(City *city) {
  CityFileExtensions *saved = city->file_extensions;
  if (saved) { ReleaseCityArchive(&saved->archive); free(saved); }
  city->file_extensions = NULL;
}
// Metadata views borrow the validated member. The name length follows from its payload size.
bool ReadCitySc2kMetadata(CitySc2kMetadata *metadata, const u8 *bytes, u32 size) {
  *metadata = (CitySc2kMetadata){0};
  if (size <= CITY_SC2K_METADATA_HEADER_SIZE || size > CITY_SC2K_METADATA_LIMIT ||
      ReadLE32(bytes) != CITY_SAVE_FORMAT_VERSION) return false;
  u32 width = ReadLE32(bytes + 4), height = ReadLE32(bytes + 8), next_sign = ReadLE32(bytes + 12);
  u32 length = size - CITY_SC2K_METADATA_HEADER_SIZE;
  int characters = CountUtf8Characters(bytes + CITY_SC2K_METADATA_HEADER_SIZE, length);
  i32 start_year = (i32)ReadLE32(bytes + 16);
  if (!IsSupportedMapAxis(width) || !IsSupportedMapAxis(height) || !next_sign ||
      next_sign > width * height + 1 || characters < 1 || characters > 64 ||
      memchr(bytes + CITY_SC2K_METADATA_HEADER_SIZE, 0, length) || start_year < INT16_MIN || start_year > INT16_MAX)
    return false;
  *metadata = (CitySc2kMetadata){.width = width, .height = height, .next_sign = next_sign,
      .format_version = ReadLE32(bytes), .start_year = start_year, .tick = ReadLE32(bytes + 20),
      .name = {bytes + CITY_SC2K_METADATA_HEADER_SIZE, length},
      .settings = {bytes + CITY_SC2K_METADATA_SETTINGS_OFFSET, CITY_SAVE_EXTENSION_SIZE}};
  return true;
}

bool ReadCitySc2kVersion(CityFileVersion *version, const u8 *bytes, u32 size) {
  *version = (CityFileVersion){0};
  if (size <= 16 || size >= 16 + SC2K_GIT_REVISION_CAPACITY ||
      ReadLE32(bytes + 12) > CITY_FILE_RELEASE_STABLE || memchr(bytes + 16, 0, size - 16) ||
      CountUtf8Characters(bytes + 16, size - 16) < 0) return false;
  *version = (CityFileVersion){.major = ReadLE32(bytes), .minor = ReadLE32(bytes + 4),
      .patch = ReadLE32(bytes + 8), .stage = ReadLE32(bytes + 12)};
  memcpy(version->revision, bytes + 16, size - 16);
  return true;
}

static bool ValidName(const CityOverlayName *name) {
  const u8 *end = memchr(name->character, 0, sizeof(name->character));
  return end && CountUtf8Characters(name->character, end - name->character) >= 0;
}

static bool SaveMetadata(const City *city, Buffer *output) {
  const CityName *stored = &city->name;
  if (stored->length < 1 || stored->length > 256 ||
      (!stored->overflow && (stored->overflow_capacity || stored->length >= CITY_NAME_CAPACITY)) ||
      (stored->overflow && (stored->length < CITY_NAME_CAPACITY || stored->length > stored->overflow_capacity)))
    return false;
  const char *name = CityNameText(stored);
  int characters = CountUtf8Characters((const u8 *)name, stored->length);
  if (memchr(name, 0, stored->length + 1) != name + stored->length || characters < 1 || characters > 64) return false;
  Append32(output, CITY_SAVE_FORMAT_VERSION); Append32(output, city->map_width); Append32(output, city->map_height);
  Append32(output, IntMax(city->signs.used, 1)); Append32(output, city->geography.start_year);
  Append32(output, city->clock.tick);
  u8 *settings = AppendSpace(output, CITY_SAVE_EXTENSION_SIZE);
  if (!settings) return false;
  memset(settings, 0, CITY_SAVE_EXTENSION_SIZE);
  if (!EncodeCitySaveExtension(city, settings, CITY_SAVE_EXTENSION_SIZE)) return false;
  Append(output, name, stored->length);
  return !output->failed;
}

static bool SaveFacilities(const City *city, Buffer *output) {
  int count = CityFacilityRecordLimit(city);
  for (int slot = 0; slot < count; ++slot) {
    const CityOverlayName *name = ReadCityOverlayName(city, CityFacilityOverlay(slot));
    if (!ValidName(name)) return false;
    SavedFacility record = {.simulation = *ReadCityFacilityRecord(city, slot), .tile = INVALID_TILE};
    if (city->annual_facilities.capacity) {
      record.tile = city->annual_facilities.details[slot].tile;
      record.years = city->annual_facilities.details[slot].years;
    }
    CopyText((char *)record.name.character, sizeof(record.name.character), (const char *)name->character);
    Append(output, &record, sizeof(record));
  }
  return !output->failed;
}

static bool LoadFacilities(City *city, const u8 *bytes, u32 size) {
  u32 count = size / sizeof(SavedFacility);
  if (size % sizeof(SavedFacility) || count < CITY_FACILITY_RECORD_RESERVED_COUNT ||
      count > city->map_cell_count + CITY_FACILITY_RECORD_RESERVED_COUNT || !ReserveCityFacilities(city, count))
    return false;
  city->annual_facilities.used = count;
  memset(city->scratch, 0, city->map_cell_count * sizeof(*city->scratch));
  const SavedFacility *records = (const SavedFacility *)bytes;
  for (u32 slot = 0; slot < count; ++slot) {
    const SavedFacility *record = &records[slot];
    if ((record->simulation.type && !IsTileKindFacility(record->simulation.type)) || !ValidName(&record->name) ||
        (record->tile != INVALID_TILE && !IsMapTileValid(city, record->tile))) return false;
    if (slot >= CITY_FACILITY_RECORD_FIRST_DYNAMIC && record->simulation.type && record->tile != INVALID_TILE) {
      if (city->scratch[record->tile]) return false;
      city->scratch[record->tile] = 1;
    }
    *CityFacilityRecordAt(city, slot) = record->simulation;
    city->annual_facilities.details[slot].tile = record->tile;
    city->annual_facilities.details[slot].years = record->years;
    *CityOverlayNameAt(city, CityFacilityOverlay(slot)) = record->name;
  }
  return true;
}

static bool SaveSigns(const City *city, Buffer *output) {
  for (int slot = 1; slot < city->signs.used; ++slot) {
    if (city->signs.record[slot].tile == INVALID_TILE) continue;
    const CityOverlayName *name = ReadCityOverlayName(city, CitySignOverlay(slot));
    if (!ValidName(name)) return false;
    SavedSign record = {.token = slot, .tile = city->signs.record[slot].tile};
    CopyText((char *)record.name.character, sizeof(record.name.character), (const char *)name->character);
    Append(output, &record, sizeof(record));
  }
  return !output->failed;
}

static bool LoadSigns(City *city, const u8 *bytes, u32 size, u32 next_sign) {
  if (size % sizeof(SavedSign) || !ReserveCitySigns(city, next_sign)) return false;
  memset(city->scratch, 0, city->map_cell_count * sizeof(*city->scratch));
  const SavedSign *records = (const SavedSign *)bytes;
  u32 count = size / sizeof(SavedSign), marked = 0;
  for (TileIndex tile = 0; tile < city->map_cell_count; ++tile) {
    int overlay = city->tile[tile].overlay;
    if (overlay & ~(CITY_TILE_HAS_SIGN | CITY_TILE_EFFECT_MASK)) return false;
    marked += (overlay & CITY_TILE_HAS_SIGN) != 0;
  }
  if (marked != count) return false;
  for (u32 i = 0; i < count; ++i) {
    const SavedSign *record = &records[i];
    if (!record->token || record->token >= next_sign || !IsMapTileValid(city, record->tile) ||
        city->scratch[record->tile] || !(city->tile[record->tile].overlay & CITY_TILE_HAS_SIGN) ||
        city->signs.record[record->token].tile != INVALID_TILE || !ValidName(&record->name)) return false;
    city->scratch[record->tile] = 1;
    city->signs.record[record->token].tile = record->tile;
    *CityOverlayNameAt(city, CitySignOverlay(record->token)) = record->name;
  }
  city->signs.used = next_sign;
  return true;
}

static bool SaveLabels(const City *city, Buffer *output) {
  CityOverlayNames *labels = (CityOverlayNames *)AppendSpace(output, sizeof(CityOverlayNames));
  if (!labels) return false;
  *labels = city->overlay_names;
  for (int slot = 0; slot < CITY_OVERLAY_NAME_COUNT; ++slot) {
    if (!ValidName(&labels->entry[slot])) return false;
    bool facility = IsCityFacilityOverlay(slot);
    bool sign = IsCitySignOverlay(slot) && slot < city->signs.used &&
        city->signs.record[slot].tile != INVALID_TILE;
    if (facility || sign) memset(&labels->entry[slot], 0, sizeof(CityOverlayName));
  }
  return true;
}
// Validate and copy native event records, reserving the City's supported event capacity first. Returns false for
// malformed coordinates, types or links with preceding writes retained; event hash reconstruction belongs to
// successful-load completion.
static bool LoadThings(City *city, const u8 *bytes, u32 size) {
  u32 count = size / CITY_NATIVE_EVENT_BYTES;
  if (size % CITY_NATIVE_EVENT_BYTES || count < CITY_STOCK_EVENT_SLOT_COUNT || count > CITY_EVENT_MAX_SLOTS)
    return false;
  if (!ReserveCityEventSlots(city, count)) return false;
  memset(city->events.event, 0, city->events.capacity * sizeof(CityMapEvent));
  for (u32 slot = 0; slot < count; ++slot)
    memcpy(&city->events.event[slot], bytes + slot * CITY_NATIVE_EVENT_BYTES, CITY_NATIVE_EVENT_BYTES);
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    const CityMapEvent *event = &city->events.event[slot];
    if (event->type >= CITY_MAP_EVENT_KIND_COUNT || !IsMapTileValid(city, event->tile) ||
        event->coordinate_flags & ~CITY_EVENT_TARGET_TILE ||
        ((event->coordinate_flags & CITY_EVENT_TARGET_TILE) && !IsMapTileValid(city, event->train.next_tile)))
      return false;
    if (IsCityMapEventTrain(event->type) && event->next_slot >= city->events.capacity) return false;
    if (event->type == CITY_MAP_EVENT_SHIP && event->ship.home_tile != INVALID_TILE &&
        !IsMapTileValid(city, event->ship.home_tile)) return false;
    if (event->type == CITY_MAP_EVENT_MAXIS_MAN && !IsCityMarkerOverlay(event->maxis_man.goal) &&
        (!IsCityEventOverlay(event->maxis_man.goal) ||
            CityEventOverlaySlot(event->maxis_man.goal) >= city->events.capacity)) return false;
  }
  return true;
}
typedef struct { u32 offset; u8 width; u32 count, stride; } NativeField;
static const NativeField kNativeFields[] = {
#include "city/saveload/sc2k_fields.inc"
};
// Bind resume state to these serialized members in a fixed order, independent of archive/write order.
static const char *const kResumeMembers[] = {
  "META", "TILE", "HALF", "QUAR", "LABL", "FACL", "SIGN", "EVNT", "HIST", "CLIM"
};

static u32 FingerprintMember(u32 crc, const CityArchiveMember *member) {
  if (!member) return crc;
  u8 values[8];
  WriteLE32(values, member->crc); WriteLE32(values + 4, member->size);
  crc = crc32(crc, (const u8 *)member->name, sizeof(member->name));
  return crc32(crc, values, sizeof(values));
}

static u32 Fingerprint(const CityArchive *archive) {
  u32 crc = 0;
  for (u32 i = 0; i < sizeof(kResumeMembers) / sizeof(kResumeMembers[0]); ++i) {
    const CityArchiveEntry *entry = FindCityArchiveEntry(archive, kResumeMembers[i]);
    crc = FingerprintMember(crc, entry ? &entry->member : NULL);
  }
  return crc;
}

static u32 WriterFingerprint(const CityArchiveWriter *writer) {
  u32 crc = 0;
  for (u32 i = 0; i < sizeof(kResumeMembers) / sizeof(kResumeMembers[0]); ++i)
    crc = FingerprintMember(crc, FindCityArchiveWriterMember(writer, kResumeMembers[i]));
  return crc;
}
// Serialize deterministic fields in format-version order at a supported save boundary.
// Staging failure leaves caller ownership intact.
static bool SaveNativeResume(const City *city, Buffer *result, u32 fingerprint) {
  Append32(result, fingerprint);
  for (size_t i = 0; i < sizeof(kNativeFields) / sizeof(kNativeFields[0]); ++i) {
    const NativeField *field = &kNativeFields[i];
    for (u32 item = 0; item < field->count; ++item) {
      Append(result, (const u8 *)city + field->offset + item * field->stride, field->width);
    }
  }
  u32 scratch_bytes = city->map_cell_count * sizeof(*city->scratch);
  u8 *scratch = AppendSpace(result, scratch_bytes);
  if (!scratch) return false;
  memcpy(scratch, city->scratch, scratch_bytes);
  const CityTileQueueCursor *queue = &city->route_queue_cursor;
  u32 read = queue->read, write = queue->write, mask = queue->mask;
  Append32(result, read); Append32(result, write);
  u32 length = (write - read) & mask;
  Append32(result, length);
  for (u32 i = 0; i < length; ++i) {
    PointU16 point = city->route_points[(read + i) & mask];
    Append32(result, point.x | (u32)point.y << 16);
  }
  return !result->failed;
}
// Validate the versioned payload bounds and fingerprint before applying deterministic fields. City must already
// contain the archive's base data; failure is a load error and does not promise rollback of preceding load work.
static bool LoadNativeResume(City *city, CityArchive *archive) {
  const CityArchiveEntry *entry = FindCityArchiveEntry(archive, "NRTS");
  if (!entry) return false;
  u8 *bytes = DecodeCityArchiveEntry(archive, entry);
  if (!bytes) return false;
  if (entry->member.size < 4 || ReadLE32(bytes) != Fingerprint(archive)) {
    free(bytes); return false;
  }
  u32 count = sizeof(kNativeFields) / sizeof(kNativeFields[0]);
  // Preserve default initialization before a partial or failed field read.
  memset(&city->cheats, 0, sizeof(city->cheats));
  u32 offset = 4;
  for (u32 i = 0; i < count; ++i) {
    const NativeField *field = &kNativeFields[i];
    u32 length = field->width * field->count;
    if (offset > entry->member.size || length > entry->member.size - offset) { free(bytes); return false; }
    if (field->offset == offsetof(City, individual_facilities) && bytes[offset] > 1) { free(bytes); return false; }
    for (u32 item = 0; item < field->count; ++item) {
      memcpy((u8 *)city + field->offset + item * field->stride, bytes + offset, field->width);
      offset += field->width;
    }
  }
  if (city->cheats.church > 2 || city->cheats.used > 1 || city->cheats.house > (u32)city->map_cell_count) {
    free(bytes); return false;
  }
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    const CityMapEvent *event = &city->events.event[slot];
    if (!IsMapTileValid(city, event->tile) ||
        ((event->coordinate_flags & CITY_EVENT_TARGET_TILE) && !IsMapTileValid(city, event->train.next_tile))) {
      free(bytes); return false;
    }
  }
  u32 cells = city->map_cell_count;
  u32 scratch_bytes = cells * sizeof(*city->scratch);
  if (entry->member.size - offset < scratch_bytes + 12) { free(bytes); return false; }
  memcpy(city->scratch, bytes + offset, scratch_bytes);
  offset += scratch_bytes;
  u32 read = ReadLE32(bytes + offset), write = ReadLE32(bytes + offset + 4), length = ReadLE32(bytes + offset + 8);
  offset += 12;
  u32 mask = city->route_queue_cursor.mask, capacity = mask + 1;
  if (read >= capacity || write >= capacity || length != ((write - read) & mask) ||
      length > (entry->member.size - offset) / 4) {
    free(bytes); return false;
  }
  memset(city->route_queued, 0, cells / 8);
  u8 *queued = city->fixes & (1ull << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION) ? city->route_queued : NULL;
  for (u32 i = 0; i < length; ++i) {
    PointU16 point = {ReadLE16(bytes + offset), ReadLE16(bytes + offset + 2)};
    offset += 4;
    if (point.x >= city->map_width || point.y >= city->map_height) { free(bytes); return false; }
    city->route_points[(read + i) & mask] = point;
    if (queued) {
      TileIndex tile = MapTileXY(city, point.x, point.y);
      queued[tile / 8] |= 1 << (tile & 7);
    }
  }
  // Restore cursors after the point loop. Trailing bytes can still reject the payload after these writes.
  city->route_queue_cursor.read = read;
  city->route_queue_cursor.write = write;
  bool valid = offset == entry->member.size;
  free(bytes); return valid;
}
static bool KnownEntry(const char *name) {
  if (!memcmp(name, "META", 4) || !memcmp(name, "VERS", 4) || !memcmp(name, "CLIM", 4) || !memcmp(name, "NRTS", 4))
    return true;
  for (u32 i = 0; i < sizeof(kRequiredEntries) / sizeof(kRequiredEntries[0]); ++i)
    if (!memcmp(name, kRequiredEntries[i], 4)) return true;
  return false;
}

static void RetainUnknownChunks(CityArchive *archive) {
  u32 count = 0;
  for (u32 i = 0; i < archive->count; ++i)
    if (!KnownEntry(archive->entries[i].member.name)) archive->entries[count++] = archive->entries[i];
  archive->count = count;
  if (!count) ReleaseCityArchive(archive);
  else ReleaseCityArchiveCursor(archive);
}

// The generic archive sink borrows the observed save stream for this synchronous write.
static bool WriteNativeArchiveBytes(void *stream, const void *bytes, u32 size) {
  return WriteCitySaveStream(stream, bytes, size);
}

bool SaveCitySc2k(City *city, CitySaveStream *stream) {
  FinishCitySimulationWork(city);
  if (city->simulation_work.phase || city->question_count || city->game->zone_drag_input) return false;
  CityArchiveWriter writer = {.stream = stream, .write = WriteNativeArchiveBytes};
  Buffer metadata = {0}, labels = {0}, facilities = {0}, signs = {0}, events = {0}, resume = {0};
  bool valid = SaveMetadata(city, &metadata) && WriteCityArchiveEntry(&writer, "META", metadata.data, metadata.size);
  u8 version[16 + SC2K_GIT_REVISION_CAPACITY];
  WriteLE32(version, SC2K_VERSION_MAJOR); WriteLE32(version + 4, SC2K_VERSION_MINOR);
  WriteLE32(version + 8, SC2K_VERSION_PATCH); WriteLE32(version + 12, SC2K_VERSION_STAGE);
  ReadBuildRevision((char *)version + 16, SC2K_GIT_REVISION_CAPACITY);
  valid = valid && WriteCityArchiveEntry(&writer, "VERS", version, 16 + strlen((const char *)version + 16));
  valid = valid && WriteCityArchiveEntry(&writer, "TILE", (const u8 *)city->tile,
      city->map_cell_count * sizeof(TileInfo)) &&
      WriteCityArchiveEntry(&writer, "HALF", (const u8 *)city->half, city->map_cell_count / 4 * sizeof(*city->half)) &&
      WriteCityArchiveEntry(&writer, "QUAR", (const u8 *)city->quarter,
          city->map_cell_count / 16 * sizeof(*city->quarter)) &&
      WriteCityArchiveEntry(&writer, "HIST", (const u8 *)city->history,
          CITY_HISTORY_SERIES_COUNT * sizeof(*city->history));
  valid = valid && SaveLabels(city, &labels) && WriteCityArchiveEntry(&writer, "LABL", labels.data, labels.size) &&
      SaveFacilities(city, &facilities) && SaveSigns(city, &signs);
  for (int slot = 0; valid && slot < city->events.capacity; ++slot) {
    CityMapEvent event = city->events.event[slot];
    event.coordinate_flags &= ~CITY_EVENT_HASH_LINKED;
    if (event.type == CITY_MAP_EVENT_UNUSED) memset(&event, 0, sizeof(event));
    Append(&events, &event, CITY_NATIVE_EVENT_BYTES);
  }
  valid = valid && !events.failed && WriteCityArchiveEntry(&writer, "EVNT", events.data, events.size) &&
      WriteCityArchiveEntry(&writer, "FACL", facilities.data, facilities.size) &&
      WriteCityArchiveEntry(&writer, "SIGN", signs.data, signs.size);
  if (valid && city->native_map_storage)
    valid = WriteCityArchiveEntry(&writer, "CLIM", city->snow.points,
        TerrainSnowNoiseSize(city->map_width, city->map_height));
  valid = valid && SaveNativeResume(city, &resume, WriterFingerprint(&writer)) &&
      WriteCityArchiveEntry(&writer, "NRTS", resume.data, resume.size);
  const CityFileExtensions *saved = city->file_extensions;
  CityArchive retained = saved ? saved->archive : (CityArchive){0};
  retained.cursor = NULL;
  for (u32 i = 0; valid && i < retained.count; ++i)
    valid = CopyCityArchiveEntry(&writer, &retained, &retained.entries[i]);
  valid = valid && FinishCityArchive(&writer);
  ReleaseCityArchiveCursor(&retained);
  free(metadata.data); free(labels.data); free(facilities.data); free(signs.data); free(events.data); free(resume.data);
  ReleaseCityArchiveWriter(&writer);
  if (!valid) ReportCitySaveError(city->game->file, CITY_NOTICE_ERROR_WRITING_FILE, NULL);
  return valid;
}

static bool LoadRecordMember(CityArchive *archive, const char *name, void *target, u32 size) {
  const CityArchiveEntry *entry = FindCityArchiveEntry(archive, name);
  if (!entry || entry->member.size != size) return false;
  u8 *bytes = DecodeCityArchiveEntry(archive, entry);
  if (!bytes) return false;
  memcpy(target, bytes, size); free(bytes);
  return true;
}

// Until successful attachment, the load operation owns both the archive and its temporary metadata bytes.
typedef struct { CityFileExtensions *saved; u8 *metadata; } CitySc2kLoad;

static void ReleaseCitySc2kLoad(void *context) {
  CitySc2kLoad *load = context;
  free(load->metadata);
  if (load->saved) {
    ReleaseCityArchive(&load->saved->archive);
    free(load->saved);
  }
}

bool LoadCitySc2k(City *city, CityFileInput *input) {
  city->load_sloped_zone_tiles = city->load_cleared_building_tiles = city->load_submerged_occupant_tiles = 0;
  CityFileExtensions *saved = calloc(1, sizeof(*saved));
  if (!saved) return false;
  CityFileRuntime *file = city->game->file;
  CitySc2kLoad load = {.saved = saved};
  CityFileCleanup cleanup = {ReleaseCitySc2kLoad, &load, NULL};
  PushCityFileCleanup(file, &cleanup);
  bool valid = CityFileInputSeek(input, 0, SEEK_END) == 0;
  int length = valid ? CityFileInputPosition(input) : -1;
  valid = valid && length >= 6 && length <= 512 * 1024 * 1024 && CityFileInputSeek(input, 0, SEEK_SET) == 0;
  u8 *archive = valid ? malloc(length) : NULL;
  // Attach before the observed read, which can unwind rather than return a short count.
  saved->archive.bytes = archive;
  valid = archive && CityFileInputRead(input, archive, length) == (u32)length;
  if (valid) valid = DecodeCityArchive(&saved->archive, archive, length);
  const CityArchiveEntry *metadata = valid ? FindCityArchiveEntry(&saved->archive, "META") : NULL;
  valid = metadata && metadata->member.size <= CITY_SC2K_METADATA_LIMIT;
  if (valid) {
    load.metadata = DecodeCityArchiveEntry(&saved->archive, metadata);
    valid = load.metadata != NULL;
  }
  CitySc2kMetadata info;
  valid = valid && ReadCitySc2kMetadata(&info, load.metadata, metadata->member.size);
  for (u32 i = 0; valid && i < sizeof(kRequiredEntries) / sizeof(kRequiredEntries[0]); ++i)
    valid = FindCityArchiveEntry(&saved->archive, kRequiredEntries[i]) != NULL;
  valid = valid && FindCityArchiveEntry(&saved->archive, "NRTS") != NULL;
  const CityArchiveEntry *version_entry = valid ? FindCityArchiveEntry(&saved->archive, "VERS") : NULL;
  if (version_entry) {
    u8 *bytes = DecodeCityArchiveEntry(&saved->archive, version_entry);
    CityFileVersion version;
    valid = bytes && ReadCitySc2kVersion(&version, bytes, version_entry->member.size);
    free(bytes);
  }
  if (valid) valid = ResizeCityMap(city, info.width, info.height);
  if (valid) {
    InitializeCityStorage(city);
    AssignCityName(city, info.name.size, info.name.data);
    city->fixes = ReadLE64(info.settings.data + 48);
    memcpy(&city->indicators.cash, info.settings.data, sizeof(city->indicators.cash));
    city->indicators.asset_value = ReadLE64(info.settings.data + 40);
    city->geography.start_year = info.start_year; city->clock.tick = info.tick;
  }
  valid = valid && LoadRecordMember(&saved->archive, "TILE", city->tile, city->map_cell_count * sizeof(TileInfo)) &&
      LoadRecordMember(&saved->archive, "HALF", city->half, city->map_cell_count / 4 * sizeof(*city->half)) &&
      LoadRecordMember(&saved->archive, "QUAR", city->quarter, city->map_cell_count / 16 * sizeof(*city->quarter)) &&
      LoadRecordMember(&saved->archive, "HIST", city->history, CITY_HISTORY_SERIES_COUNT * sizeof(*city->history));
  if (valid) {
    const CityArchiveEntry *entry = FindCityArchiveEntry(&saved->archive, "LABL");
    u8 *bytes = entry->member.size == sizeof(CityOverlayNames) ? DecodeCityArchiveEntry(&saved->archive, entry) : NULL;
    valid = bytes != NULL;
    if (valid) for (int slot = 0; slot < CITY_OVERLAY_NAME_COUNT; ++slot)
      if (!ValidName(&((const CityOverlayNames *)bytes)->entry[slot])) { valid = false; break; }
    if (valid) memcpy(&city->overlay_names, bytes, sizeof(city->overlay_names));
    free(bytes);
  }
  const char *const records[] = {"EVNT", "FACL", "SIGN"};
  for (int i = 0; valid && i < 3; ++i) {
    const CityArchiveEntry *entry = FindCityArchiveEntry(&saved->archive, records[i]);
    u8 *bytes = DecodeCityArchiveEntry(&saved->archive, entry);
    valid = bytes && (i == 0 ? LoadThings(city, bytes, entry->member.size) :
        i == 1 ? LoadFacilities(city, bytes, entry->member.size) :
            LoadSigns(city, bytes, entry->member.size, info.next_sign));
    free(bytes);
  }
  const CityArchiveEntry *climate = valid ? FindCityArchiveEntry(&saved->archive, "CLIM") : NULL;
  if (valid && (info.settings.data[39] != 0) != (climate != NULL)) valid = false;
  if (valid && climate) valid = LoadRecordMember(&saved->archive, "CLIM", city->snow.points,
      TerrainSnowNoiseSize(city->map_width, city->map_height));
  if (valid) valid = LoadNativeResume(city, &saved->archive);
  if (valid) valid = ApplyCitySaveExtension(city, info.settings.data, info.settings.size);
  if (valid) valid = ValidateCityTileRecords(city);
  if (valid) {
    RefreshCityNeighborNames(city); RebuildCityEventHash(city); RebuildCityAssociationHashes(city);
    RetainUnknownChunks(&saved->archive);
    city->file_extensions = saved;
    load.saved = NULL;
    int x = city->geography.center_x, y = city->geography.center_y;
    if (!IsMapCoordinateValid(city, x, y)) { x = city->map_width / 2; y = city->map_height / 2; }
    SetCityLoadedViewCenter(city, x, y); InvalidateMap(city);
  }
  PopCityFileCleanup(file, &cleanup);
  ReleaseCitySc2kLoad(&load);
  if (!valid) ReportCityFileError(city->game->file, CITY_NOTICE_INVALID_CITY_CONTENTS, CITY_FILE_ERROR_RESOURCE_ONLY);
  return valid;
}
