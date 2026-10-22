#include "city/maps/labels.h"
#include "city/saveload/events_format.h"
#include "city/saveload/map_format.h"
#include "city/events/events.h"
#include "city/maps/map_types.h"
#include "city/saveload/legacy_text.h"
#include "base/byte_order.h"
#include "city/state.h"
#include "city/saveload/city_file_internal.h"
#include "city/maps/layers.h"

// Object locations and train next tiles must be in bounds. Other target pairs are offsets; destination pairs can be
// off-map aircraft waypoints. Event must be writable with its type already decoded; positions/train targets are
// clamped while other targets retain offset semantics.
static void DecodeStockCityEventCoordinates(City *city, CityMapEvent *event, int x, int y, int target_x, int target_y) {
  event->tile = MapTileXY(city, IntClamp(x, 0, city->map_width - 1),
      IntClamp(y, 0, city->map_height - 1));
  if (IsCityMapEventTrain(event->type)) {
    target_x = IntClamp(target_x, 0, city->map_width - 1);
    target_y = IntClamp(target_y, 0, city->map_height - 1);
  } else {
    target_x = IntClamp(target_x, 0, 15) * CITY_EVENT_OFFSET_SCALE;
    target_y = IntClamp(target_y, 0, 15) * CITY_EVENT_OFFSET_SCALE;
  }
  SetCityMapEventTarget(city, event, target_x, target_y);
}

// Stock XTHG stores the offset approach waypoint in bytes. Native events store its runway reference point regardless
// of the selected gameplay fixes. Narrow native coordinates and indexes only here for the stock byte format. Output
// must hold one full stock record and event links be consistent; native coordinates/state are projected without
// editing the source.
void EncodeStockCityMapEvent(const City *city, u8 bytes[CITY_STOCK_EVENT_BYTES], const CityMapEvent *event) {
  CityPoint position = ReadCityMapEventPosition(city, event);
  CityPoint target = ReadCityMapEventTarget(city, event);
  if (!(event->coordinate_flags & CITY_EVENT_TARGET_TILE)) {
    target.x /= CITY_EVENT_OFFSET_SCALE;
    target.y /= CITY_EVENT_OFFSET_SCALE;
  }
  bytes[0] = event->type; bytes[1] = event->direction; bytes[2] = event->state;
  bytes[3] = position.y; bytes[4] = position.x; bytes[5] = event->age;
  bytes[6] = target.y; bytes[7] = target.x;
  bytes[10] = event->previous_overlay;
  if (event->type == CITY_MAP_EVENT_SHIP) {
    bytes[8] = event->ship.destination_y; bytes[9] = event->ship.destination_x;
    bytes[11] = 0; // Ships do not consume the stock goal byte.
  } else {
    bytes[8] = event->raw.destination_y; bytes[9] = event->raw.destination_x;
    bytes[11] = event->raw.goal;
  }
  if ((event->coordinate_flags & CITY_EVENT_HASH_LINKED) && !IsCityMapEventTrain(event->type)) {
    // Reconstruct the stock hidden-overlay chain from canonical tile membership.
    // Train followers can share their engine's tile without joining that chain;
    // their saved overlay belongs to the movement record copied along the train.
    bytes[10] = ReadCityStockBaseOverlay(city, event->tile);
    for (int link = event->hash_next; link; link = city->events.event[link - 1].hash_next) {
      if (city->events.event[link - 1].tile != event->tile) continue;
      bytes[10] = CityEventOverlay(link - 1);
      break;
    }
  }
  if (event->type == CITY_MAP_EVENT_MAXIS_MAN && IsCityEventOverlay(event->maxis_man.goal))
    bytes[11] = CityEventOverlaySlot(event->maxis_man.goal);
  if (event->type == CITY_MAP_EVENT_AIRPLANE &&
      (event->state & CITY_AIRPLANE_STAGE_MASK) == CITY_AIRPLANE_APPROACH) {
    int x, y;
    ReadCityAirplaneApproachOffset(event, &x, &y);
    bytes[8] = event->raw.destination_y + y;
    bytes[9] = event->raw.destination_x + x;
  }
}

// Decode one complete stock event record into native coordinates, links and type-specific state. Bytes must hold
// CITY_STOCK_EVENT_BYTES and City geometry must be initialized; output is reset first and hash membership is rebuilt
// separately.
void DecodeStockCityMapEvent(City *city, CityMapEvent *event, const u8 bytes[CITY_STOCK_EVENT_BYTES]) {
  memset(event, 0, sizeof(*event));
  event->type = bytes[0]; event->direction = bytes[1]; event->state = bytes[2]; event->age = bytes[5];
  event->previous_overlay = bytes[10];
  if (event->type == CITY_MAP_EVENT_SHIP) {
    event->ship.home_tile = MapTileXYOrInvalid(city, city->disasters.border_water_x, city->disasters.border_water_y);
    event->ship.destination_y = bytes[8]; event->ship.destination_x = bytes[9];
  } else {
    event->raw.destination_y = bytes[8]; event->raw.destination_x = bytes[9];
    event->raw.goal = bytes[11];
  }
  if (event->type == CITY_MAP_EVENT_MAXIS_MAN && bytes[11] < CITY_STOCK_EVENT_SLOT_COUNT)
    event->maxis_man.goal = CityEventOverlay(bytes[11]);
  DecodeStockCityEventCoordinates(city, event, bytes[4], bytes[3], bytes[7], bytes[6]);
  if (event->type == CITY_MAP_EVENT_AIRPLANE &&
      (event->state & CITY_AIRPLANE_STAGE_MASK) == CITY_AIRPLANE_APPROACH) {
    int x, y;
    ReadCityAirplaneApproachOffset(event, &x, &y);
    event->raw.destination_y = (u8)(bytes[8] - y);
    event->raw.destination_x = (u8)(bytes[9] - x);
  }
}

// Separate stock overlay stacks into native terrain overlays and per-tile event membership. Run after stock records
// are decoded; clears unused event records and rebuilds hash links from active event locations.
void ImportStockCityEventLinks(City *city) {
  // Stock tiles keep only the top overlay. Native tiles retain the non-event
  // association underneath, and every active record is indexed at its own tile.
  for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int overlay = tp->overlay;
    for (int depth = 0; IsCityEventOverlay(overlay) && depth < CITY_STOCK_EVENT_SLOT_COUNT; ++depth)
      overlay = city->events.event[CityEventOverlaySlot(overlay)].previous_overlay;
    tp->overlay = IsCityEventOverlay(overlay) ? CITY_OVERLAY_NONE : overlay;
  }
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    CityMapEvent *event = &city->events.event[slot];
    if (event->type == CITY_MAP_EVENT_UNUSED) memset(event, 0, sizeof(*event));
    else if (IsMapTileValid(city, event->tile)) event->previous_overlay = city->tile[event->tile].overlay;
  }
  RebuildCityEventHash(city);
}

// Look up a four-byte stock chunk tag in the immutable schema table. Tag must provide four readable bytes; returns a
// borrowed specification or NULL for an unknown chunk.
const CityFileChunkSpec *FindCityFileChunkSpec(const char tag[4]) {
  for (size_t index = 0; index < sizeof(kCityFileChunks) / sizeof(kCityFileChunks[0]); ++index)
    if (memcmp(kCityFileChunks[index].tag, tag, 4) == 0)
      return &kCityFileChunks[index];
  return NULL;
}

// Resolve a schema record to its native City field or map-plane backing. Spec must be an admitted schema entry;
// returns borrowed writable storage or NULL for unavailable/non-native storage.
u8 *CityFileChunkData(City *city, const CityFileChunkSpec *spec) {
  if (spec->storage == CITY_FILE_SIMULATION)
    return (u8 *)city + spec->native_offset;
  if (spec->storage == CITY_FILE_TILE)
    return city->tile ? (u8 *)city->tile + spec->native_offset : NULL;
  if (spec->storage == CITY_FILE_HALF_MAP)
    return city->half ? (u8 *)city->half + spec->native_offset : NULL;
  if (spec->storage == CITY_FILE_QUARTER_MAP)
    return city->quarter ? (u8 *)city->quarter + spec->native_offset : NULL;
  if (spec->storage == CITY_FILE_MAP) {
    u8 *layer;
    memcpy(&layer, (u8 *)city + spec->native_offset, sizeof(layer));
    return layer;
  }
  return NULL;
}

// Decode packets independently of the caller's completion policy. A failing packet leaves prior output writes intact
// for the stock streaming loader. Source must point to an available control byte and target within decoded_size; false
// retains earlier packet writes and caller owns stride-backed storage.
bool DecodeCityRlePacket(const u8 *encoded, u32 encoded_size, u32 *source,
    u8 *decoded, u32 decoded_size, u32 *target, int stride) {
  int control = encoded[(*source)++];
  u32 run = control < 128 ? control : (u32)control - 127;
  if (run > decoded_size - *target || (!decoded && run) ||
      (control < 128 ? run > encoded_size - *source : *source == encoded_size)) return false;
  if (control < 128) {
    if (stride == 1) {
      if (run) memcpy(decoded + *target, encoded + *source, run);
    } else {
      for (u32 byte = 0; byte < run; ++byte) decoded[(size_t)(*target + byte) * stride] = encoded[*source + byte];
    }
    *source += run;
  } else {
    int value = encoded[(*source)++];
    if (stride == 1) memset(decoded + *target, value, run);
    else for (u32 byte = 0; byte < run; ++byte) decoded[(size_t)(*target + byte) * stride] = value;
  }
  *target += run;
  return true;
}

// Decode a bounded RLE payload using the stock packet rules. Source and destination must provide their stated sizes;
// false leaves prior packet writes intact and must not be treated as a transactional rollback.
bool CityFileDecodeRLE(const u8 *encoded, u32 encoded_size, u8 *decoded, u32 decoded_size) {
  u32 source = 0, target = 0;
  while (source < encoded_size)
    if (!DecodeCityRlePacket(encoded, encoded_size, &source, decoded, decoded_size, &target, 1)) return false;
  return target == decoded_size;
}

// Stock packet selection, shared by both save paths. size * 3 / 2 + 2 output bytes suffice, including tiny inputs and
// lookahead. Input must contain size readable bytes and output the stated worst-case capacity; returns encoded bytes
// written without taking buffer ownership.
int EncodeCityFileRle(const u8 *input, int size, u8 *output) {
  int source = 0, target = 0;
  while (source < size - 1) {
    int character = input[source];
    int run;
    if (input[source + 1] < 128 && input[source + 1] == character) {
      run = 2;
      while (run < 128 && source + run < size &&
          input[source + run] < 128 && input[source + run] == character) ++run;
      output[target++] = (run - 1) | 128;
      output[target++] = character;
      source += run;
    } else {
      run = 1;
      output[target + 1] = character;
      while (run < 128 && source + run < size &&
          (input[source + run] >= 128 || input[source + run] != character)) {
        character = input[source + run];
        ++run;
        output[target + run] = character;
      }
      output[target] = run - 1;
      source += run - 1;
      target += run;
    }
  }
  if (source == size - 1) {
    output[target++] = 1;
    output[target++] = input[source];
  }
  return target;
}

// Reverse byte order within fixed-width words in a byte buffer. Size and width must describe complete supported words;
// transformation is in place and callers retain buffer ownership.
void CityFileConvertWords(u8 *data, u32 size, u32 width) {
  for (u32 index = 0; index < size; index += width)
    for (u32 left = 0; left < width / 2; ++left) {
      int saved = data[index + left];
      data[index + left] = data[index + width - 1 - left];
      data[index + width - 1 - left] = saved;
    }
}

// Convert the fixed stock label table's representation after reading it. Data must provide the complete table; this
// codec step does not allocate or change the active City.
static void ConvertLabelsFromFile(u8 *data) {
  for (int index = 0; index < 256; ++index) {
    u8 *slot = data + 25 * index;
    u8 bytes[25];
    memcpy(bytes, slot, sizeof(bytes));
    DecodeLegacyCityText((char *)slot, 25, bytes + 1, IntMin(bytes[0], 23), CITY_TEXT_WINDOWS_1252);
  }
}

// Convert the complete fixed label table to its stock file representation in place. Caller owns the staging buffer and
// must not confuse converted bytes with native UTF-8 labels.
static void ConvertLabelsToFile(u8 *data) {
  for (int index = 0; index < 256; ++index) {
    u8 *slot = data + 25 * index;
    char text[26];
    memcpy(text, slot, 25); text[25] = 0;
    int length = EncodeLegacyCityText(slot + 1, 23, text);
    slot[0] = length;
  }
}

// Each eight-byte XMIC record has three words after its first two bytes. Data must hold size bytes of complete
// eight-byte records; conversion is in place and applying it twice restores the numeric bytes.
void ByteSwapCityFacilityRecords(u8 *data, int size) {
  for (int remaining = (u32)size >> 3; remaining > 0; --remaining, data += 8)
    CityFileConvertWords(data + 2, 6, 2);
}

// Convert a complete staged stock chunk's byte order or label encoding in place. Data must hold spec->size bytes;
// from_file selects label direction and City state is not implicitly imported/exported.
void CityFileConvertChunk(u8 *data, const CityFileChunkSpec *spec, i32 from_file) {
  switch (spec->conversion) {
  case CITY_FILE_WORDS16:
    CityFileConvertWords(data, spec->size, 2);
    break;
  case CITY_FILE_WORDS32:
    CityFileConvertWords(data, spec->size, 4);
    break;
  case CITY_FILE_FACILITIES:
    ByteSwapCityFacilityRecords(data, spec->size);
    break;
  case CITY_FILE_LABELS:
    if (from_file)
      ConvertLabelsFromFile(data);
    else
      ConvertLabelsToFile(data);
    break;
  case CITY_FILE_EVENTS:
  case CITY_FILE_BYTES:
  case CITY_FILE_NAME:
    break;
  }
}

// Copy a bounded schema chunk between staging bytes and native strided fields in the selected direction. Size must fit
// both representations; export of overlays uses effective events, while load deliberately preserves direct partial
// writes.
void CopyCityFileChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size, bool load) {
  if (spec->conversion == CITY_FILE_LABELS) {
    for (u32 byte = 0; byte < size; ++byte) {
      u8 *value = city->overlay_names.entry[byte / CITY_OVERLAY_LEGACY_NAME_BYTES].character +
          byte % CITY_OVERLAY_LEGACY_NAME_BYTES;
      if (load) *value = bytes[byte];
      else bytes[byte] = *value;
    }
    return;
  }
  u8 *field = CityFileChunkData(city, spec);
  if (spec->storage != CITY_FILE_TILE && spec->storage != CITY_FILE_HALF_MAP &&
      spec->storage != CITY_FILE_QUARTER_MAP) {
    if (load) memcpy(field, bytes, size);
    else memcpy(bytes, field, size);
    return;
  }
  for (u32 byte = 0; byte < size; ++byte) {
    u8 *value = field + CityFileChunkByteOffset(spec, byte);
    if (load) *value = bytes[byte];
    else if (!memcmp(spec->tag, "XTXT", 4)) bytes[byte] = ReadOverlay(city, byte);
    else bytes[byte] = *value;
  }
}

// Import/export a complete schema chunk, including event/label boundary conversions. Data must hold the schema size
// and City storage must be bound; false means unsupported or unavailable state, without rolling back preceding
// effects.
bool CityFileStateChunkBytes(City *city, const CityFileChunkSpec *spec, u8 *data, i32 load) {
  if (spec->conversion == CITY_FILE_LABELS && !load) {
    ExportStockCityOverlayNames(city, data);
    return true;
  }
  if (spec->conversion == CITY_FILE_EVENTS) {
    if (load) memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(u16));
    for (int slot = 0; slot < CITY_STOCK_EVENT_SLOT_COUNT; ++slot) {
      if (load) {
        u8 record[CITY_STOCK_EVENT_BYTES];
        memcpy(record, data + slot * CITY_STOCK_EVENT_BYTES, sizeof(record));
        if (!IsCityMapEventTrain(record[0])) {
          record[6] = IntMax(record[6] - 1, 0);
          record[7] = IntMax(record[7] - 1, 0);
        }
        DecodeStockCityMapEvent(city, &city->events.event[slot], record);
        // SC2 files omit the stock process's shared home coordinate. Use the
        // imported ship's position; comparison imports use the live shared home.
        if (record[0] == CITY_MAP_EVENT_SHIP)
          city->events.event[slot].ship.home_tile = city->events.event[slot].tile;
      } else EncodeStockCityMapEvent(city, data + slot * CITY_STOCK_EVENT_BYTES, &city->events.event[slot]);
    }
    return true;
  }
  if (spec->storage != CITY_FILE_ONLY) {
    u8 *target = CityFileChunkData(city, spec);
    if (target == NULL)
      return false;
    if (load) CopyCityFileChunkBytes(city, spec, data, spec->size, true);
    else ExportCityMapChunk(city, spec, data, spec->size);
    return true;
  }
  return false;
}

// Project City-owned UTF-8 labels into the stock fixed-width label table. Destination must hold the full stock table;
// unrepresentable characters use codec substitutions and native labels are left intact.
void ExportStockCityOverlayNames(const City *city, u8 *bytes) {
  for (int index = 0; index < CITY_OVERLAY_NAME_COUNT; ++index) {
    u8 *slot = bytes + index * CITY_OVERLAY_LEGACY_NAME_BYTES;
    memset(slot, 0, CITY_OVERLAY_LEGACY_NAME_BYTES);
    slot[0] = EncodeLegacyCityText(slot + 1, CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH,
        (const char *)city->overlay_names.entry[index].character);
  }
}

// Decode the just-loaded stock label slots into native terminated UTF-8 slots. Call at the import boundary before
// native label consumers; conversion respects each destination's fixed byte capacity.
void DecodeLoadedCityOverlayNames(City *city) {
  for (int index = 0; index < CITY_OVERLAY_NAME_COUNT; ++index) {
    u8 *name = city->overlay_names.entry[index].character;
    u8 bytes[CITY_OVERLAY_LEGACY_NAME_BYTES];
    memcpy(bytes, name, sizeof(bytes));
    DecodeLegacyCityText((char *)name, CITY_OVERLAY_NAME_BYTES, bytes + 1,
        IntMin(bytes[0], CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH), CITY_TEXT_WINDOWS_1252);
  }
}
