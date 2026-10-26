#include "city/saveload/city_file_internal.h"

#include <string.h>

#include "city/events/events.h"
#include "city/maps/labels.h"
#include "city/maps/map_types.h"
#include "city/saveload/events_format.h"
#include "city/saveload/legacy_text.h"
#include "city/saveload/map_format.h"
#include "city/state.h"

// Look up a four-byte stock chunk tag in the immutable schema table. Tag must provide four readable bytes; returns a
// borrowed specification or NULL for an unknown chunk.
const CityFileChunkSpec *FindCityFileChunkSpec(const char tag[4]) {
  for (size_t index = 0; index < sizeof(kCityFileChunks) / sizeof(kCityFileChunks[0]); ++index)
    if (memcmp(kCityFileChunks[index].tag, tag, 4) == 0)
      return &kCityFileChunks[index];
  return NULL;
}

// Resolve a schema record to its native City field or map-plane backing. Spec must be an admitted schema entry;
// returns borrowed read-only storage or NULL for unavailable/non-native storage.
const u8 *CityFileChunkData(const City *city, const CityFileChunkSpec *spec) {
  switch (spec->storage) {
  case CITY_FILE_SIMULATION:
    return (const u8 *)city + spec->native_offset;
  case CITY_FILE_TILE:
    return city->tile ? (const u8 *)city->tile + spec->native_offset : NULL;
  case CITY_FILE_HALF_MAP:
    return city->half ? (const u8 *)city->half + spec->native_offset : NULL;
  case CITY_FILE_QUARTER_MAP:
    return city->quarter ? (const u8 *)city->quarter + spec->native_offset : NULL;
  case CITY_FILE_MAP: {
    u8 *layer;
    memcpy(&layer, (const u8 *)city + spec->native_offset, sizeof(layer));
    return layer;
  }
  case CITY_FILE_ONLY:
  default:
    return NULL;
  }
}

// The resolved storage belongs to the supplied mutable City. Keep the qualification change at this access boundary;
// the shared resolver itself returns a read-only view.
u8 *CityFileMutableChunkData(City *city, const CityFileChunkSpec *spec) {
  return (u8 *)CityFileChunkData(city, spec);
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

// Decode the complete stock label table into terminated UTF-8 staging slots in place. The slots keep their stock
// byte stride; this does not expand them to City's wider names or change the active City.
static void DecodeStockLabelTable(u8 *data) {
  for (int index = 0; index < CITY_OVERLAY_NAME_COUNT; ++index) {
    u8 *slot = data + CITY_OVERLAY_LEGACY_NAME_BYTES * index;
    u8 bytes[CITY_OVERLAY_LEGACY_NAME_BYTES];
    memcpy(bytes, slot, sizeof(bytes));
    DecodeLegacyCityText((char *)slot, CITY_OVERLAY_LEGACY_NAME_BYTES, bytes + 1,
        IntMin(bytes[0], CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH), CITY_TEXT_WINDOWS_1252);
  }
}

// Encode the complete fixed-stride UTF-8 staging table into stock length-prefixed slots in place. The temporary
// terminator bounds the source read; bytes beyond each written name retain their prior values.
static void EncodeStockLabelTable(u8 *data) {
  for (int index = 0; index < CITY_OVERLAY_NAME_COUNT; ++index) {
    u8 *slot = data + CITY_OVERLAY_LEGACY_NAME_BYTES * index;
    char text[CITY_OVERLAY_LEGACY_NAME_BYTES + 1];
    memcpy(text, slot, CITY_OVERLAY_LEGACY_NAME_BYTES);
    text[CITY_OVERLAY_LEGACY_NAME_BYTES] = 0;
    int length = EncodeLegacyCityText(slot + 1, CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH, text);
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
// direction selects label conversion and City state is not implicitly imported/exported.
void CityFileConvertChunk(u8 *data, const CityFileChunkSpec *spec, CityFileDirection direction) {
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
    if (direction != CITY_FILE_EXPORT)
      DecodeStockLabelTable(data);
    else
      EncodeStockLabelTable(data);
    break;
  case CITY_FILE_EVENTS:
  case CITY_FILE_BYTES:
  case CITY_FILE_NAME:
    break;
  }
}

// Read City backing into bounded staging bytes without changing City. Overlay reads use effective event overlays;
// kind projection and byte-order conversion remain separate codec steps.
void ReadCityFileChunkBytes(const City *city, const CityFileChunkSpec *spec, u8 *bytes, u32 size) {
  if (spec->conversion == CITY_FILE_LABELS) {
    for (u32 byte = 0; byte < size; ++byte)
      bytes[byte] = city->overlay_names.entry[byte / CITY_OVERLAY_LEGACY_NAME_BYTES].character[
          byte % CITY_OVERLAY_LEGACY_NAME_BYTES];
    return;
  }
  const u8 *field = CityFileChunkData(city, spec);
  if (spec->storage != CITY_FILE_TILE && spec->storage != CITY_FILE_HALF_MAP &&
      spec->storage != CITY_FILE_QUARTER_MAP) {
    memcpy(bytes, field, size);
    return;
  }
  for (u32 byte = 0; byte < size; ++byte) {
    if (!memcmp(spec->tag, "XTXT", 4)) bytes[byte] = ReadOverlay(city, byte);
    else bytes[byte] = field[CityFileChunkByteOffset(spec, byte)];
  }
}

// Write admitted staging bytes directly without changing the source, preserving partial load writes.
// Event decoding, kind translation and byte-order conversion remain separate import steps.
void WriteCityFileChunkBytes(City *city, const CityFileChunkSpec *spec, const u8 *bytes, u32 size) {
  if (spec->conversion == CITY_FILE_LABELS) {
    for (u32 byte = 0; byte < size; ++byte)
      city->overlay_names.entry[byte / CITY_OVERLAY_LEGACY_NAME_BYTES].character[
          byte % CITY_OVERLAY_LEGACY_NAME_BYTES] = bytes[byte];
    return;
  }
  u8 *field = CityFileMutableChunkData(city, spec);
  if (spec->storage != CITY_FILE_TILE && spec->storage != CITY_FILE_HALF_MAP &&
      spec->storage != CITY_FILE_QUARTER_MAP) {
    memcpy(field, bytes, size);
    return;
  }
  for (u32 byte = 0; byte < size; ++byte)
    field[CityFileChunkByteOffset(spec, byte)] = bytes[byte];
}

// Export one complete schema-sized chunk, projecting map kinds, labels and events at the stock boundary. Numeric
// byte-order conversion remains a separate step; unavailable backing returns false without changing City.
bool ExportCityFileStateChunk(const City *city, const CityFileChunkSpec *spec, u8 *data) {
  if (spec->conversion == CITY_FILE_LABELS) {
    ExportStockCityOverlayNames(city, data);
    return true;
  }
  if (spec->conversion == CITY_FILE_EVENTS) {
    EncodeStockCityMapEvents(city, data);
    return true;
  }
  if (spec->storage == CITY_FILE_ONLY || CityFileChunkData(city, spec) == NULL) return false;
  ExportCityMapChunk(city, spec, data, spec->size);
  return true;
}

// Import one complete schema-sized staging chunk after byte-order conversion. Event records decode at this boundary;
// labels retain their stock slot bytes until their later text conversion. Source bytes remain caller-owned and intact.
bool ImportCityFileStateChunk(City *city, const CityFileChunkSpec *spec, const u8 *data) {
  if (spec->conversion == CITY_FILE_EVENTS) {
    DecodeStockCityMapEvents(city, data);
    return true;
  }
  if (spec->storage == CITY_FILE_ONLY || CityFileChunkData(city, spec) == NULL) return false;
  WriteCityFileChunkBytes(city, spec, data, spec->size);
  return true;
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
