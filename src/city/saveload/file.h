#ifndef SC2K_DOMAIN_SRC_FILE_FILE_H
#define SC2K_DOMAIN_SRC_FILE_FILE_H

#include "city/saveload/path.h"
#include "city/saveload/types.h"
#include "city/types.h"

typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityLoadStream CityLoadStream;

// File

void ReleaseCityFileExtensions(City *city);
// Read-only format selection from host preference, retained format and City data that stock saves cannot represent.
bool CityUsesSc2k(const City *city);

// CityFileOpenStream returns a caller-owned load stream or NULL. CityFileCloseStream consumes it even if close throws.
// CityFileReadTaggedStream borrows the stream and exactly four tag bytes, without requiring a terminator.
// It writes within capacity; throwing errors preserve file unwinding.
CityLoadStream *CityFileOpenStream(CityFileRuntime *file, const char *path);
void CityFileCloseStream(CityLoadStream *stream);
u8 *CityFileReadTaggedStream(CityLoadStream *stream, const char tag[4], int version, u8 *output, size_t capacity);

typedef struct CityFile CityFile;

// Return an owned archive or NULL on open/resource/I/O failure. Retain original bytes even when the optional editing
// index is incomplete. LoadCityArchive determines decode success; unchanged CityFileWrite preserves those bytes.
// SaveCityArchive requires complete editing metadata. Release every returned archive through CityFileFree.
CityFile *CityFileRead(const char *path);
bool CityFileHasChunk(const CityFile *archive, const char tag[4]);
bool LoadCityArchive(City *city, const CityFile *archive);
// Copy the native UTF-8 host basename, including its extension, without changing case.
void AssignCityNameFromHostFilename(City *city, const char *path);
// Copy the native UTF-8 basename after stripping a recognized .sc2 or .sc2k extension, ignoring extension case.
void AssignCityNameFromSavePath(City *city, const char *path);
// Requires an active file operation. Normal decode returns close the stream, restore the view and rebuild unlocks.
// False retains partial City writes; throwing read/close errors unwind through the operation's cleanup stack.
bool LoadCityFile(City *city, const char *path);
// Load the City, then its versioned SCEN metadata. False retains partial state, including the mission flag set by
// a successful City load. Throwing reads/close keep their unwind path; pending disasters update only after close.
bool LoadCityScenarioMetadata(City *city, const char *path);
bool SaveCityArchive(City *city, CityFile *archive);
// Write the editing index when complete, otherwise the retained source bytes. Failure may leave partial output.
bool CityFileWrite(const CityFile *archive, const char *path);
// Consume the archive, its retained source/path and every indexed payload. NULL is accepted.
void CityFileFree(CityFile *archive);

bool SaveCityFile(City *city, CityPath *path);

// Accepted save paths

// Save to an accepted host destination inside a private I/O attempt. The caller retains an active file operation
// and a mutable CityPath; serialization may normalize that path. The attempt copies destination before writing.
// The caller owns the chooser, name changes and final path commit. False includes serialization or close failure.
bool SaveCityToDestination(City *city, CityPath *path, const char *destination);
// An already accepted host path; this operation never opens a picker.
// A failed path copy or extension check returns false before renaming or opening the destination.
bool SaveCityToPath(City *city, const u8 *text, int length, bool rename);

#endif
