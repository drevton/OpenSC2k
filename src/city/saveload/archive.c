#include "city/saveload/archive.h"
#include "base/byte_order.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zstd.h>

// RIFF chunks hold CRC32 followed by their unchanged record bytes. Only the surrounding stream is compressed.
enum {
  kMaxArchiveBytes = 512 * 1024 * 1024, kMaxDataBytes = 768 * 1024 * 1024, kMaxMembers = 4096,
  kArchiveHeaderSize = 12, kChunkHeaderSize = 12, kStreamBufferSize = 8192, kWindowLogMax = 23,
};

static bool Contains(u32 size, u32 offset, u32 count) {
  return offset <= size && count <= size - offset;
}

static bool ValidMemberName(const char name[4]) {
  for (int i = 0; i < 4; ++i) if ((u8)name[i] < 32 || (u8)name[i] > 126) return false;
  return true;
}

static bool ReadRiffHeader(const u8 *header, u32 *size) {
  u32 length = ReadLE32(header + 4);
  if (memcmp(header, "RIFF", 4) || memcmp(header + 8, "SC2K", 4) ||
      length < 4 || length > kMaxDataBytes - 8) return false;
  *size = length + 8;
  return true;
}

static bool ReadChunkHeader(CityArchiveEntry *entry, const u8 *header, u32 offset, u32 size) {
  u32 length = ReadLE32(header + 4);
  if (length < 4 || length > kMaxDataBytes || !ValidMemberName((const char *)header) ||
      !Contains(size, offset, kChunkHeaderSize) || !Contains(size, offset + 8, length + (length & 1))) return false;
  *entry = (CityArchiveEntry){.member = {.crc = ReadLE32(header + 8), .size = length - 4},
      .data_offset = offset + kChunkHeaderSize};
  memcpy(entry->member.name, header, 4);
  return true;
}

typedef struct CityArchiveCursor {
  ZSTD_DStream *decoder;
  ZSTD_inBuffer input;
  u32 position;
  bool finished;
} CityArchiveCursor;

void ReleaseCityArchiveCursor(CityArchive *archive) {
  if (!archive->cursor) return;
  ZSTD_freeDStream(archive->cursor->decoder);
  free(archive->cursor);
  archive->cursor = NULL;
}

void ReleaseCityArchive(CityArchive *archive) {
  ReleaseCityArchiveCursor(archive);
  free(archive->bytes); free(archive->entries);
  *archive = (CityArchive){0};
}

static bool StartArchiveCursor(CityArchive *archive) {
  ReleaseCityArchiveCursor(archive);
  CityArchiveCursor *cursor = calloc(1, sizeof(*cursor));
  if (!cursor) return false;
  cursor->decoder = ZSTD_createDStream();
  archive->cursor = cursor;
  cursor->input = (ZSTD_inBuffer){archive->bytes, archive->size, 0};
  return cursor->decoder && !ZSTD_isError(ZSTD_initDStream(cursor->decoder)) &&
      !ZSTD_isError(ZSTD_DCtx_setParameter(cursor->decoder, ZSTD_d_windowLogMax, kWindowLogMax));
}

static bool ReadArchiveBytes(CityArchive *archive, void *bytes, u32 size) {
  CityArchiveCursor *cursor = archive->cursor;
  if (size > archive->decoded_size - cursor->position) return false;
  ZSTD_outBuffer output = {bytes, size, 0};
  while (output.pos < output.size) {
    if (cursor->finished) return false;
    size_t before = output.pos, input = cursor->input.pos;
    size_t pending = ZSTD_decompressStream(cursor->decoder, &output, &cursor->input);
    if (ZSTD_isError(pending) || (pending && before == output.pos && input == cursor->input.pos)) return false;
    cursor->finished = pending == 0;
  }
  cursor->position += size;
  return true;
}

static bool SeekArchiveChunk(CityArchive *archive, u32 offset) {
  if ((!archive->cursor || archive->cursor->position > offset) && !StartArchiveCursor(archive)) return false;
  u8 discard[kStreamBufferSize];
  while (archive->cursor->position < offset) {
    u32 size = UintMin(sizeof(discard), offset - archive->cursor->position);
    if (!ReadArchiveBytes(archive, discard, size)) return false;
  }
  return true;
}

static bool FinishArchiveCursor(CityArchive *archive) {
  CityArchiveCursor *cursor = archive->cursor;
  u8 extra;
  while (!cursor->finished) {
    ZSTD_outBuffer output = {&extra, 1, 0};
    size_t before = cursor->input.pos;
    size_t pending = ZSTD_decompressStream(cursor->decoder, &output, &cursor->input);
    if (ZSTD_isError(pending) || output.pos || (pending && before == cursor->input.pos)) return false;
    cursor->finished = pending == 0;
  }
  return cursor->input.pos == cursor->input.size;
}

bool DecodeCityArchive(CityArchive *archive, u8 *owned_bytes, u32 size) {
  *archive = (CityArchive){.bytes = owned_bytes, .size = size};
  if (!size || size > kMaxArchiveBytes) return false;
  u64 decoded = ZSTD_getFrameContentSize(owned_bytes, size);
  if (decoded < kArchiveHeaderSize || decoded > kMaxDataBytes ||
      ZSTD_findFrameCompressedSize(owned_bytes, size) != size) return false;
  archive->decoded_size = decoded;
  if (!StartArchiveCursor(archive)) return false;
  u8 header[kArchiveHeaderSize], buffer[kStreamBufferSize];
  u32 riff_size, capacity = 0;
  if (!ReadArchiveBytes(archive, header, sizeof(header)) ||
      !ReadRiffHeader(header, &riff_size) || riff_size != decoded) return false;
  while (archive->cursor->position < riff_size) {
    CityArchiveEntry entry;
    u32 offset = archive->cursor->position;
    if (archive->count == kMaxMembers || !Contains(riff_size, offset, kChunkHeaderSize) ||
        !ReadArchiveBytes(archive, header, kChunkHeaderSize) ||
        !ReadChunkHeader(&entry, header, offset, riff_size)) return false;
    for (u32 prior = 0; prior < archive->count; ++prior)
      if (!memcmp(entry.member.name, archive->entries[prior].member.name, 4)) return false;
    u32 remaining = entry.member.size, crc = 0;
    while (remaining) {
      u32 bytes = UintMin(sizeof(buffer), remaining);
      if (!ReadArchiveBytes(archive, buffer, bytes)) return false;
      crc = crc32(crc, buffer, bytes); remaining -= bytes;
    }
    if (crc != entry.member.crc || ((entry.member.size & 1) && !ReadArchiveBytes(archive, header, 1))) return false;
    if (archive->count == capacity) {
      capacity = capacity ? capacity * 2 : 32;
      CityArchiveEntry *entries = realloc(archive->entries, capacity * sizeof(*entries));
      if (!entries) return false;
      archive->entries = entries;
    }
    archive->entries[archive->count++] = entry;
  }
  return archive->count && FinishArchiveCursor(archive);
}

const CityArchiveEntry *FindCityArchiveEntry(const CityArchive *archive, const char name[4]) {
  for (u32 i = 0; i < archive->count; ++i)
    if (!memcmp(archive->entries[i].member.name, name, 4)) return &archive->entries[i];
  return NULL;
}

u8 *DecodeCityArchiveEntry(CityArchive *archive, const CityArchiveEntry *entry) {
  if (!entry || !SeekArchiveChunk(archive, entry->data_offset)) return NULL;
  u8 *bytes = malloc(entry->member.size ? entry->member.size : 1);
  if (!bytes) return NULL;
  if (!ReadArchiveBytes(archive, bytes, entry->member.size) ||
      crc32(0, bytes, entry->member.size) != entry->member.crc) {
    free(bytes); return NULL;
  }
  return bytes;
}

// Summary and terrain preview keep one small input buffer and decode forward without allocating a City map.
typedef struct CityArchiveReader {
  FILE *file;
  ZSTD_DStream *decoder;
  u8 input[kStreamBufferSize];
  size_t input_size, input_offset;
  u32 position;
  bool finished;
} CityArchiveReader;

static bool ReadStreamBytes(CityArchiveReader *reader, void *bytes, u32 size) {
  if (size > kMaxDataBytes - reader->position) return false;
  ZSTD_outBuffer output = {bytes, size, 0};
  while (output.pos < output.size) {
    if (reader->finished) return false;
    if (reader->input_offset == reader->input_size) {
      reader->input_size = fread(reader->input, 1, sizeof(reader->input), reader->file);
      reader->input_offset = 0;
      if (ferror(reader->file)) return false;
    }
    ZSTD_inBuffer input = {reader->input, reader->input_size, reader->input_offset};
    size_t before = output.pos;
    size_t pending = ZSTD_decompressStream(reader->decoder, &output, &input);
    if (ZSTD_isError(pending) || (pending && before == output.pos && input.pos == reader->input_offset))
      return false;
    reader->input_offset = input.pos;
    reader->finished = pending == 0;
  }
  reader->position += size;
  return true;
}

static bool VisitStreamMember(CityArchiveReader *reader, const char name[4], u32 limit,
    CityArchiveMemberVisitor *visit, void *context, u32 *size) {
  u8 header[kArchiveHeaderSize], output[kStreamBufferSize];
  u32 length;
  if (!ReadStreamBytes(reader, header, sizeof(header)) || !ReadRiffHeader(header, &length)) return false;
  for (u32 count = 0; reader->position < length && count < kMaxMembers; ++count) {
    CityArchiveEntry entry;
    u32 offset = reader->position;
    if (!Contains(length, offset, kChunkHeaderSize) || !ReadStreamBytes(reader, header, kChunkHeaderSize) ||
        !ReadChunkHeader(&entry, header, offset, length)) return false;
    bool selected = !memcmp(entry.member.name, name, 4);
    if (selected && entry.member.size > limit) return false;
    u32 produced = 0, crc = 0;
    while (produced < entry.member.size) {
      u32 bytes = UintMin(sizeof(output), entry.member.size - produced);
      if (!ReadStreamBytes(reader, output, bytes)) return false;
      crc = crc32(crc, output, bytes);
      if (selected && !visit(context, output, produced, bytes)) return false;
      produced += bytes;
    }
    if (crc != entry.member.crc || ((entry.member.size & 1) && !ReadStreamBytes(reader, header, 1))) return false;
    if (selected) { *size = produced; return true; }
  }
  return false;
}

bool VisitCityArchiveMember(FILE *stream, const char name[4], u32 limit,
    CityArchiveMemberVisitor *visit, void *context, u32 *size) {
  *size = 0;
  if (fseek(stream, 0, SEEK_END)) return false;
  long length = ftell(stream);
  if (length < 1 || length > kMaxArchiveBytes || fseek(stream, 0, SEEK_SET)) return false;
  CityArchiveReader reader = {.file = stream, .decoder = ZSTD_createDStream()};
  if (!reader.decoder) return false;
  bool valid = !ZSTD_isError(ZSTD_initDStream(reader.decoder)) &&
      !ZSTD_isError(ZSTD_DCtx_setParameter(reader.decoder, ZSTD_d_windowLogMax, kWindowLogMax)) &&
      VisitStreamMember(&reader, name, limit, visit, context, size);
  ZSTD_freeDStream(reader.decoder);
  return valid;
}

typedef struct MemberCopy { u8 *bytes; u32 limit; } MemberCopy;
static bool CopyMemberBytes(void *context, const u8 *bytes, u32 offset, u32 size) {
  MemberCopy *copy = context;
  if (size > copy->limit - offset) return false;
  memcpy(copy->bytes + offset, bytes, size);
  return true;
}

u8 *ReadCityArchiveMember(FILE *stream, const char name[4], u32 limit, u32 *size) {
  MemberCopy copy = {malloc(limit ? limit : 1), limit};
  *size = 0;
  if (!copy.bytes) return NULL;
  if (!VisitCityArchiveMember(stream, name, limit, CopyMemberBytes, &copy, size)) { free(copy.bytes); return NULL; }
  return copy.bytes;
}

// Queued sources borrow either immutable record bytes or a retained compressed archive. Only the writer
// interprets this union; loaded indexes have a separate offset-only type.
typedef struct CityArchiveWriteEntry {
  CityArchiveMember member;
  u8 from_archive;
  union { const u8 *bytes; CityArchive *archive; };
} CityArchiveWriteEntry;
_Static_assert(sizeof(CityArchiveWriteEntry) <= 24, "queued archive sources fit the existing entry budget");

static bool AppendArchiveEntry(CityArchiveWriter *writer, const CityArchiveWriteEntry *entry) {
  u32 position = writer->decoded_size ? writer->decoded_size : kArchiveHeaderSize;
  u64 end = (u64)position + kChunkHeaderSize + entry->member.size + (entry->member.size & 1);
  if (!ValidMemberName(entry->member.name) || writer->count >= kMaxMembers || end > kMaxDataBytes) return false;
  for (u32 i = 0; i < writer->count; ++i)
    if (!memcmp(writer->entries[i].member.name, entry->member.name, 4)) return false;
  if (writer->count == writer->capacity) {
    u32 capacity = writer->capacity ? writer->capacity * 2 : 32;
    CityArchiveWriteEntry *entries = realloc(writer->entries, capacity * sizeof(*entries));
    if (!entries) return false;
    writer->entries = entries; writer->capacity = capacity;
  }
  writer->entries[writer->count++] = *entry;
  writer->decoded_size = end;
  return true;
}

bool WriteCityArchiveEntry(CityArchiveWriter *writer, const char name[4], const u8 *bytes, u32 size) {
  if (size > kMaxDataBytes) return false;
  CityArchiveWriteEntry entry = {.member = {.size = size, .crc = crc32(0, bytes, size)}, .bytes = bytes};
  memcpy(entry.member.name, name, 4);
  return AppendArchiveEntry(writer, &entry);
}

bool CopyCityArchiveEntry(CityArchiveWriter *writer, CityArchive *archive, const CityArchiveEntry *entry) {
  CityArchiveWriteEntry saved = {.member = entry->member, .from_archive = true, .archive = archive};
  return AppendArchiveEntry(writer, &saved);
}

const CityArchiveMember *FindCityArchiveWriterMember(const CityArchiveWriter *writer, const char name[4]) {
  for (u32 i = 0; i < writer->count; ++i)
    if (!memcmp(writer->entries[i].member.name, name, 4)) return &writer->entries[i].member;
  return NULL;
}

static bool CompressArchiveBytes(CityArchiveWriter *writer, ZSTD_CCtx *encoder,
    const void *bytes, u32 size, ZSTD_EndDirective end) {
  ZSTD_inBuffer input = {bytes, size, 0};
  u8 buffer[kStreamBufferSize];
  size_t pending;
  do {
    ZSTD_outBuffer output = {buffer, sizeof(buffer), 0};
    pending = ZSTD_compressStream2(encoder, &output, &input, end);
    if (ZSTD_isError(pending) || output.pos > kMaxArchiveBytes - writer->position) return false;
    if (output.pos && !writer->write(writer->stream, buffer, output.pos)) return false;
    writer->position += output.pos;
  } while (input.pos < input.size || (end == ZSTD_e_end && pending));
  return true;
}

static bool CompressStoredChunk(CityArchiveWriter *writer, ZSTD_CCtx *encoder, const CityArchiveWriteEntry *entry) {
  CityArchive *archive = entry->archive;
  const CityArchiveEntry *source = FindCityArchiveEntry(archive, entry->member.name);
  if (!source || !SeekArchiveChunk(archive, source->data_offset)) return false;
  u8 buffer[kStreamBufferSize];
  u32 remaining = entry->member.size, crc = 0;
  while (remaining) {
    u32 count = UintMin(sizeof(buffer), remaining);
    if (!ReadArchiveBytes(archive, buffer, count)) return false;
    crc = crc32(crc, buffer, count);
    if (!CompressArchiveBytes(writer, encoder, buffer, count, ZSTD_e_continue)) return false;
    remaining -= count;
  }
  return crc == entry->member.crc;
}

bool FinishCityArchive(CityArchiveWriter *writer) {
  if (!writer->count) return false;
  ZSTD_CCtx *encoder = ZSTD_createCCtx();
  if (!encoder) return false;
  bool valid = !ZSTD_isError(ZSTD_CCtx_setParameter(encoder, ZSTD_c_compressionLevel, 3)) &&
      !ZSTD_isError(ZSTD_CCtx_setParameter(encoder, ZSTD_c_checksumFlag, 1)) &&
      !ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(encoder, writer->decoded_size));
  u8 header[kArchiveHeaderSize], padding = 0;
  memcpy(header, "RIFF", 4); WriteLE32(header + 4, writer->decoded_size - 8); memcpy(header + 8, "SC2K", 4);
  valid = valid && CompressArchiveBytes(writer, encoder, header, sizeof(header), ZSTD_e_continue);
  for (u32 i = 0; valid && i < writer->count; ++i) {
    const CityArchiveWriteEntry *entry = &writer->entries[i];
    memcpy(header, entry->member.name, 4);
    WriteLE32(header + 4, entry->member.size + 4); WriteLE32(header + 8, entry->member.crc);
    valid = CompressArchiveBytes(writer, encoder, header, sizeof(header), ZSTD_e_continue) &&
        (entry->from_archive ? CompressStoredChunk(writer, encoder, entry) :
            CompressArchiveBytes(writer, encoder, entry->bytes, entry->member.size, ZSTD_e_continue)) &&
        (!(entry->member.size & 1) || CompressArchiveBytes(writer, encoder, &padding, 1, ZSTD_e_continue));
  }
  valid = valid && CompressArchiveBytes(writer, encoder, NULL, 0, ZSTD_e_end);
  ZSTD_freeCCtx(encoder);
  return valid;
}

void ReleaseCityArchiveWriter(CityArchiveWriter *writer) {
  free(writer->entries);
  *writer = (CityArchiveWriter){0};
}
