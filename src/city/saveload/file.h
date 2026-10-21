#ifndef SC2K_DOMAIN_SRC_FILE_FILE_H
#define SC2K_DOMAIN_SRC_FILE_FILE_H

#include <stdio.h>

#include "city/saveload/path.h"
#include "city/saveload/state.h"
#include "city/saveload/services.h"
#include "city/types.h"

// File

// Host adaptation of CRT unwinding. Nodes release native operation-local
// resources; they are never City state or alternative game return values.

void ReleaseCityFileExtensions(City *city);
bool CityUsesSc2k(const City *city);

typedef struct CityFileCleanup {
  void (*destroy)(void *context);
  void *context;
  struct CityFileCleanup *previous;
} CityFileCleanup;
// Native temporary buffers are released before an escaping file exception.
typedef struct {
  CityFileRuntime *file;
  void *bytes;
} CityFileScratchCleanup;
void ReleaseCityFileScratchBuffer(void *context);
void BeginCityFileOperation(Game *game, CityFileRuntime *file);
void EndCityFileOperation(CityFileRuntime *file);
// Window-owned operations may retain streams, but never stack cleanup/exception frames.
void SuspendCityFileOperation(CityFileRuntime *file);
void ResumeCityFileOperation(CityFileRuntime *file);
void PushCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup);
void PopCityFileCleanup(CityFileRuntime *file, CityFileCleanup *cleanup);
CityFileCleanup *CityFileCleanupTop(const CityFileRuntime *file);
void UnwindCityFileCleanups(CityFileRuntime *file, CityFileCleanup *until);

u8 *ReadCityFileChunkByTag(FILE *input, const char *tag, int version, u8 *output);
bool ReadCityPreview(FILE *stream, u8 *picture, size_t picture_capacity, char *description, size_t text_capacity);
bool CityFileUsesTransparentThumbnail(const char *path);

typedef struct CityFile CityFile;

// Reading retains opened bytes even when the optional editing index is
// incomplete. LoadCityArchive determines the original stream's success; unchanged
// Write preserves those bytes. SaveCityArchive requires complete editing metadata.
CityFile *CityFileRead(const char *path);
bool CityFileHasChunk(const CityFile *city, const char tag[4]);
bool LoadCityArchive(City *city, const CityFile *archive);
// Apply a stream in declared container order, including partial
// writes before failure. Shares the payload readers' direct stream/error services.
bool LoadCityFileChunks(City *city, void *stream, const char *path);
// Selects the message from the resource and exception cause.
void ShowCityFileException(City *city, u32 resource, const CityFileExceptionState *exception, const char *path);
void DeriveCityNameFromFilename(City *city, const char *path);
bool LoadCityFile(City *city, const char *path);
bool AllocateCityMiscBuffer(CityFileRuntime *file);
bool SaveCityMiscToBuffer(City *city);
bool LoadCityScenarioMetadata(City *city, const char *path);
bool SaveCityArchive(City *city, CityFile *archive);
bool CityFileWrite(const CityFile *city, const char *path);
void CityFileFree(CityFile *city);

// Resource text is consumed synchronously and capped at the original255 bytes.
// The toolbar update retains its original whole helper and discarded return.
// Allocate the MISC workspace before loading. Decode or version failures retain that buffer.
bool LoadCityMisc(City *city, void *stream, u32 encoded_size);

bool EnsureCityFileExtension(Game *game, CityPath *path, char *extension);
bool SaveCityFileChunks(City *city, void *stream, CityPath *path);
bool SaveCityFile(City *city, CityPath *path);

// Commands

void HandleSaveCityCommand(City *city);
void HandleSaveCityAsCommand(City *city);
// An already accepted host path; this operation never opens a picker.
bool SaveCityToPath(City *city, const u8 *text, int length, bool rename);

bool ReadCityScenarioInstructions(const char *path, char text[769]);

#endif
