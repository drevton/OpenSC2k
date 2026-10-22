#ifndef SC2K_DOMAIN_SRC_FILE_FILE_H
#define SC2K_DOMAIN_SRC_FILE_FILE_H

#include <stdio.h>

#include "city/saveload/path.h"
#include "city/saveload/types.h"
#include "city/types.h"

typedef struct CityFileRuntime CityFileRuntime;

// File

void ReleaseCityFileExtensions(City *city);
bool CityUsesSc2k(const City *city);

// CityFileOpenStream returns a caller-owned load stream or NULL. CityFileCloseStream consumes it even if close throws.
// CityFileReadTaggedStream borrows the stream and writes within capacity; throwing errors preserve file unwinding.
void *CityFileOpenStream(CityFileRuntime *file, const char *path);
void CityFileCloseStream(void *stream);
u8 *CityFileReadTaggedStream(void *stream, const char *tag, int version, u8 *output, size_t capacity);
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
// Copy the native UTF-8 host basename, including its extension, without changing case.
void AssignCityNameFromHostFilename(City *city, const char *path);
// Copy the native UTF-8 basename after stripping a recognized .sc2 or .sc2k extension, ignoring extension case.
void AssignCityNameFromSavePath(City *city, const char *path);
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

// Extension is borrowed terminated text; only path-owned storage may change.
bool EnsureCitySaveExtension(Game *game, CityPath *path, const char *extension);
bool SaveCityFileChunks(City *city, void *stream, CityPath *path);
bool SaveCityFile(City *city, CityPath *path);

// Accepted save paths

// An already accepted host path; this operation never opens a picker.
// A failed path copy or extension check returns false before renaming or opening the destination.
bool SaveCityToPath(City *city, const u8 *text, int length, bool rename);

bool ReadCityScenarioInstructions(const char *path, char text[769]);

#endif
