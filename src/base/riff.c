#include "base/riff.h"
#include "base/byte_order.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zstd.h>

// RIFF/SC2K chunks contain decoded size, CRC32 and one Zstandard frame.
// Decoding and terrain preview visits remain bounded to one chunk at a time.
enum { kMaxArchiveBytes = 512 * 1024 * 1024, kMaxDataBytes = 768 * 1024 * 1024, kMaxChunks = 4096 };

static bool Contains(u32 size, u32 offset, u32 count) {
  return offset <= size && count <= size - offset;
}

static bool ReadChunkHeader(RiffEntry *entry, const u8 *header, u32 offset, u32 size) {
  u32 length = ReadLE32(header + 4);
  if (length < 8 || length > kMaxArchiveBytes || !Contains(size, offset, 8) ||
      !Contains(size, offset + 8, length + (length & 1)))
    return false;
  for (int i = 0; i < 4; ++i) if (header[i] < 32 || header[i] > 126) return false;
  memcpy(entry->name, header, 4); entry->name[4] = 0;
  entry->size = ReadLE32(header + 8);
  entry->crc = ReadLE32(header + 12);
  entry->compressed_size = length - 8;
  entry->data_offset = offset + 16;
  return entry->size <= kMaxDataBytes;
}

void ReleaseRiffArchive(RiffArchive *archive) {
  free(archive->bytes); free(archive->entries);
  *archive = (RiffArchive){0};
}

bool DecodeRiffArchive(RiffArchive *archive, u8 *owned_bytes, u32 size) {
  *archive = (RiffArchive){.bytes = owned_bytes, .size = size};
  if (size < 12 || size > kMaxArchiveBytes || memcmp(owned_bytes, "RIFF", 4) ||
      ReadLE32(owned_bytes + 4) != size - 8 || memcmp(owned_bytes + 8, "SC2K", 4)) return false;
  u32 offset = 12, count = 0;
  while (offset < size) {
    RiffEntry entry;
    if (!Contains(size, offset, 16) || !ReadChunkHeader(&entry, owned_bytes + offset, offset, size) ||
        ++count > kMaxChunks) return false;
    offset += 16 + entry.compressed_size + (entry.compressed_size & 1);
  }
  if (!count || offset != size) return false;
  archive->entries = calloc(count, sizeof(*archive->entries));
  if (!archive->entries) return false;
  archive->count = count;
  u64 decoded_size = 0;
  offset = 12;
  for (u32 i = 0; i < count; ++i) {
    RiffEntry *entry = &archive->entries[i];
    if (!ReadChunkHeader(entry, owned_bytes + offset, offset, size)) return false;
    for (u32 prior = 0; prior < i; ++prior)
      if (!strcmp(entry->name, archive->entries[prior].name)) return false;
    decoded_size += entry->size;
    if (decoded_size > kMaxDataBytes) return false;
    offset += 16 + entry->compressed_size + (entry->compressed_size & 1);
  }
  return true;
}

const RiffEntry *FindRiffEntry(const RiffArchive *archive, const char *name) {
  for (u32 i = 0; i < archive->count; ++i)
    if (!strcmp(archive->entries[i].name, name)) return &archive->entries[i];
  return NULL;
}

u8 *DecodeRiffEntry(const RiffArchive *archive, const RiffEntry *entry) {
  if (!entry || entry->size > kMaxDataBytes ||
      !Contains(archive->size, entry->data_offset, entry->compressed_size)) return NULL;
  u8 *bytes = malloc(entry->size ? entry->size : 1);
  if (!bytes) return NULL;
  size_t result = ZSTD_decompress(bytes, entry->size, archive->bytes + entry->data_offset, entry->compressed_size);
  if (ZSTD_isError(result) || result != entry->size || crc32(0, bytes, entry->size) != entry->crc) {
    free(bytes); return NULL;
  }
  return bytes;
}

static bool FindFileMember(FILE *stream, const char *name, u32 limit, RiffEntry *entry) {
  u8 header[16];
  if (fseek(stream, 0, SEEK_END)) return false;
  long length = ftell(stream);
  if (length < 12 || length > kMaxArchiveBytes || fseek(stream, 0, SEEK_SET) ||
      fread(header, 1, 12, stream) != 12 || memcmp(header, "RIFF", 4) ||
      ReadLE32(header + 4) != (u32)length - 8 || memcmp(header + 8, "SC2K", 4)) return false;
  u32 offset = 12, count = 0;
  while (offset < (u32)length) {
    if (!Contains(length, offset, 16) || fseek(stream, offset, SEEK_SET) ||
        fread(header, 1, 16, stream) != 16 || !ReadChunkHeader(entry, header, offset, length) ||
        ++count > kMaxChunks) return false;
    if (!strcmp(entry->name, name)) return entry->size <= limit;
    offset += 16 + entry->compressed_size + (entry->compressed_size & 1);
  }
  return false;
}

bool VisitRiffMember(FILE *stream, const char *name, u32 limit, RiffMemberVisitor *visit, void *context, u32 *size) {
  *size = 0;
  RiffEntry entry;
  if (!FindFileMember(stream, name, limit, &entry) || fseek(stream, entry.data_offset, SEEK_SET)) return false;
  ZSTD_DStream *decoder = ZSTD_createDStream();
  if (!decoder) return false;
  bool valid = !ZSTD_isError(ZSTD_initDStream(decoder));
  valid = valid && !ZSTD_isError(ZSTD_DCtx_setParameter(decoder, ZSTD_d_windowLogMax, 23));
  u8 input[8192], output[8192];
  ZSTD_inBuffer in = {input, 0, 0};
  u32 remaining = entry.compressed_size, produced = 0, crc = 0;
  size_t pending = 1;
  while (valid && pending) {
    if (in.pos == in.size && remaining) {
      u32 count = remaining < sizeof(input) ? remaining : sizeof(input);
      if (fread(input, 1, count, stream) != count) { valid = false; break; }
      remaining -= count; in = (ZSTD_inBuffer){input, count, 0};
    }
    ZSTD_outBuffer out = {output, sizeof(output), 0};
    size_t before = in.pos;
    pending = ZSTD_decompressStream(decoder, &out, &in);
    if (ZSTD_isError(pending) || out.pos > entry.size - produced) { valid = false; break; }
    if (pending && !out.pos && in.pos == before) { valid = false; break; }
    if (out.pos) {
      crc = crc32(crc, output, out.pos);
      valid = visit(context, output, produced, out.pos);
      produced += out.pos;
    }
  }
  valid = valid && !pending && !remaining && in.pos == in.size && produced == entry.size && crc == entry.crc;
  ZSTD_freeDStream(decoder);
  if (valid) *size = produced;
  return valid;
}

typedef struct { u8 *bytes; } MemberCopy;
static bool CopyMemberBytes(void *context, const u8 *bytes, u32 offset, u32 size) {
  MemberCopy *copy = context;
  memcpy(copy->bytes + offset, bytes, size);
  return true;
}

u8 *ReadRiffMember(FILE *stream, const char *name, u32 limit, u32 *size) {
  *size = 0;
  RiffEntry entry;
  if (!FindFileMember(stream, name, limit, &entry)) return NULL;
  MemberCopy copy = {malloc(entry.size ? entry.size : 1)};
  if (!copy.bytes) return NULL;
  if (!VisitRiffMember(stream, name, limit, CopyMemberBytes, &copy, size)) { free(copy.bytes); return NULL; }
  return copy.bytes;
}

static bool WritePreparedChunk(RiffWriter *writer, const RiffEntry *entry, const u8 *compressed) {
  u32 position = writer->position ? writer->position : 12;
  if (writer->count >= kMaxChunks || entry->size > kMaxDataBytes - writer->decoded_size ||
      (u64)position + 16 + entry->compressed_size + (entry->compressed_size & 1) > kMaxArchiveBytes) return false;
  if (writer->count == writer->capacity) {
    u32 capacity = writer->capacity ? writer->capacity * 2 : 32;
    RiffEntry *entries = realloc(writer->entries, capacity * sizeof(*entries));
    if (!entries) return false;
    writer->entries = entries; writer->capacity = capacity;
  }
  if (!writer->position) {
    const u8 header[12] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'S', 'C', '2', 'K'};
    if (!writer->write(writer->stream, header, sizeof(header))) return false;
    writer->position = 12;
  }
  for (u32 i = 0; i < writer->count; ++i) if (!strcmp(writer->entries[i].name, entry->name)) return false;
  u8 header[16], padding = 0;
  memcpy(header, entry->name, 4); WriteLE32(header + 4, entry->compressed_size + 8);
  WriteLE32(header + 8, entry->size); WriteLE32(header + 12, entry->crc);
  if (!writer->write(writer->stream, header, sizeof(header)) ||
      !writer->write(writer->stream, compressed, entry->compressed_size) ||
      ((entry->compressed_size & 1) && !writer->write(writer->stream, &padding, 1))) return false;
  writer->entries[writer->count++] = *entry;
  writer->decoded_size += entry->size;
  writer->position += 16 + entry->compressed_size + (entry->compressed_size & 1);
  return true;
}

bool WriteRiffEntry(RiffWriter *writer, const char *name, const u8 *bytes, u32 size) {
  if (strlen(name) != 4 || size > kMaxDataBytes) return false;
  size_t capacity = ZSTD_compressBound(size);
  u8 *compressed = malloc(capacity);
  if (!compressed) return false;
  size_t length = ZSTD_compress(compressed, capacity, bytes, size, 3);
  RiffEntry entry = {.size = size, .crc = crc32(0, bytes, size)};
  memcpy(entry.name, name, 4);
  bool valid = !ZSTD_isError(length) && length <= kMaxArchiveBytes;
  if (valid) { entry.compressed_size = length; valid = WritePreparedChunk(writer, &entry, compressed); }
  free(compressed);
  return valid;
}

bool CopyRiffEntry(RiffWriter *writer, const RiffArchive *archive, const RiffEntry *entry) {
  return Contains(archive->size, entry->data_offset, entry->compressed_size) &&
      WritePreparedChunk(writer, entry, archive->bytes + entry->data_offset);
}

bool FinishRiffArchive(RiffWriter *writer) {
  if (!writer->count || !writer->rewind(writer->stream)) return false;
  u8 header[12];
  memcpy(header, "RIFF", 4); WriteLE32(header + 4, writer->position - 8); memcpy(header + 8, "SC2K", 4);
  return writer->write(writer->stream, header, sizeof(header));
}

void ReleaseRiffWriter(RiffWriter *writer) {
  free(writer->entries);
  *writer = (RiffWriter){0};
}
