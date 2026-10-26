#ifndef SC2K_CITY_SAVELOAD_ARCHIVE_H
#define SC2K_CITY_SAVELOAD_ARCHIVE_H

#include "base/types.h"
#include <stdio.h>

// One RIFF/SC2K stream inside one checksummed Zstandard frame. Chunk names are exactly four ASCII bytes, without NUL.
// Member metadata describes record bytes, independent of their source. Names have no trailing NUL.
typedef struct CityArchiveMember {
  char name[4];
  u32 crc, size;
} CityArchiveMember;
// A loaded index stores decoded-stream offsets; CityArchive retains the compressed frame.
typedef struct CityArchiveEntry {
  CityArchiveMember member;
  u32 data_offset;
} CityArchiveEntry;
_Static_assert(sizeof(CityArchiveEntry) == 16, "loaded archive indexes use sixteen bytes per member");

typedef struct CityArchive {
  u8 *bytes;
  struct CityArchiveCursor *cursor;
  u32 size, decoded_size, count;
  CityArchiveEntry *entries;
} CityArchive;

typedef struct CityArchiveWriter {
  void *stream;
  bool (*write)(void *stream, const void *bytes, u32 size);
  u32 position, count, capacity, decoded_size;
  struct CityArchiveWriteEntry *entries;
} CityArchiveWriter;

// Takes ownership of bytes even on failure. Validates the complete frame, RIFF bounds, unique names and chunk CRCs.
// Records decode through a temporary forward cursor; release it after applying City, retaining compressed unknown data.
bool DecodeCityArchive(CityArchive *archive, u8 *owned_bytes, u32 size);
void ReleaseCityArchive(CityArchive *archive);
// Borrow an index entry until the loaded index is compacted or released; missing names return NULL.
const CityArchiveEntry *FindCityArchiveEntry(const CityArchive *archive, const char name[4]);
// Entry must belong to this archive, or be NULL. Return one owned decoded record, or NULL on failure.
// Forward reads reuse the cursor; an earlier offset restarts it.
u8 *DecodeCityArchiveEntry(CityArchive *archive, const CityArchiveEntry *entry);
void ReleaseCityArchiveCursor(CityArchive *archive);

// Prefix reads borrow FILE and leave its position unspecified. Each visited chunk is bounded and CRC-checked;
// validating the whole frame/trailer belongs to full loading. Size is zero on failure; earlier visitor writes remain.
typedef bool CityArchiveMemberVisitor(void *context, const u8 *bytes, u32 offset, u32 size);
u8 *ReadCityArchiveMember(FILE *stream, const char name[4], u32 limit, u32 *size);
bool VisitCityArchiveMember(FILE *stream, const char name[4], u32 limit,
    CityArchiveMemberVisitor *visit, void *context, u32 *size);

// Start with a zeroed writer bound to an empty stream. Queue record references without copying their data;
// every source must stay alive and unchanged until Finish returns. No output occurs before Finish.
bool WriteCityArchiveEntry(CityArchiveWriter *writer, const char name[4], const u8 *bytes, u32 size);
// Queue a member of this archive without buffering its decoded payload. Source bytes and indexes stay alive
// and unchanged through Finish; its decoder cursor may move.
bool CopyCityArchiveEntry(CityArchiveWriter *writer, CityArchive *archive, const CityArchiveEntry *entry);
// Borrow queued metadata until another entry is appended or the writer is released. Missing names return NULL.
// Read/write entry storage is private to its own path; callers cannot treat queued sources as decoded offsets.
const CityArchiveMember *FindCityArchiveWriterMember(const CityArchiveWriter *writer, const char name[4]);
// Compress the complete RIFF stream once. A failed write retains partial output; release on either outcome.
bool FinishCityArchive(CityArchiveWriter *writer);
void ReleaseCityArchiveWriter(CityArchiveWriter *writer);

#endif
