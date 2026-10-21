#ifndef SC2K_BASE_RIFF_H
#define SC2K_BASE_RIFF_H

#include "base/types.h"
#include <stdio.h>

typedef struct {
  char name[5];
  u32 crc, size, compressed_size, data_offset;
} RiffEntry;
typedef struct {
  u8 *bytes;
  u32 size, count;
  RiffEntry *entries;
} RiffArchive;
typedef struct {
  void *stream;
  bool (*write)(void *stream, const void *bytes, u32 size);
  bool (*rewind)(void *stream);
  u32 position, count, capacity, decoded_size;
  RiffEntry *entries;
} RiffWriter;

bool DecodeRiffArchive(RiffArchive *archive, u8 *owned_bytes, u32 size);
void ReleaseRiffArchive(RiffArchive *archive);
const RiffEntry *FindRiffEntry(const RiffArchive *archive, const char *name);
u8 *DecodeRiffEntry(const RiffArchive *archive, const RiffEntry *entry);
u8 *ReadRiffMember(FILE *stream, const char *name, u32 limit, u32 *size);
typedef bool RiffMemberVisitor(void *context, const u8 *bytes, u32 offset, u32 size);
bool VisitRiffMember(FILE *stream, const char *name, u32 limit, RiffMemberVisitor *visit, void *context, u32 *size);
bool WriteRiffEntry(RiffWriter *writer, const char *name, const u8 *bytes, u32 size);
bool CopyRiffEntry(RiffWriter *writer, const RiffArchive *archive, const RiffEntry *entry);
bool FinishRiffArchive(RiffWriter *writer);
void ReleaseRiffWriter(RiffWriter *writer);

#endif
