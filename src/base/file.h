#ifndef SC2K_BASE_FILE_H
#define SC2K_BASE_FILE_H

#include <stdio.h>

#include "base/types.h"

// File Paths

// Host UTF-8 paths retain case and use the host's drive and separator rules.
#ifdef _WIN32
enum { kPathSeparator = '\\' };
#else
enum { kPathSeparator = '/' };
#endif
bool IsPathSeparator(int character);
bool IsPathAbsolute(const char *path);
const char *PathFilename(const char *path);
void NormalizeFilePath(char *path);
// Copy nonempty path text, allowing overlap, and normalize separators. Failure leaves output unchanged.
bool CopyFilePath(const char *path, char *output, size_t capacity);
// Replace caller-owned text on success; an empty path fails without changing it. directory must be non-NULL.
// Initialize *owned to NULL or an allocation released with free. These helpers do not require the path to exist.
bool ResolveFilePath(const char *path, const char *directory, char **owned);
// Configured paths can use ~/; a NULL directory resolves relative to the host working directory.
bool ResolveUserPath(const char *path, const char *directory, char **owned);
bool CreateDirectories(const char *path);
FILE *OpenHostFile(const char *path, const char *mode);
// The caller owns each returned path; NULL means host/allocation failure.
char *ReadWorkingDirectory(void);
// Executable directory when the host exposes it; the caller owns the result.
char *ReadExecutableDirectory(void);
bool SetWorkingDirectory(const char *path);
// The caller owns the result of the host's canonical/absolute path resolution.
char *ResolveExistingPath(const char *path);
bool RemoveFile(const char *path);
// Replace path.bak atomically. A missing source succeeds without changing an existing backup.
bool BackupFile(const char *path);
typedef struct {
  i64 modified;
  u64 size;
  bool directory, regular;
} HostFileInfo;
bool ReadHostFileInfo(const char *path, HostFileInfo *info);
// Entry names and metadata are borrowed only during the callback. False stops the listing.
bool VisitDirectory(const char *path,
    bool (*visit)(const char *name, const HostFileInfo *info, void *context), void *context);
bool ReadDirectoryFreeBytes(const char *path, u64 *bytes);
// Append stderr to path and return its saved descriptor, or -1 on failure.
int RedirectErrorLog(const char *path);
// Consume a valid descriptor returned by RedirectErrorLog after restoring stderr.
void RestoreErrorLog(int descriptor);
// On success leave the stream at its start. Failure returns -1 and may leave its position changed.
i64 FileByteLength(FILE *file);
// Create a sibling temporary file and call write_contents once if it opens. The callback borrows the stream;
// it must return normally without closing it. Replace path only after writing, flushing, syncing and closing succeed.
// Failure attempts to remove the temporary file. The parent directory is not synced.
bool WriteFileAtomically(const char *path, bool (*write_contents)(FILE *file, void *context), void *context);

// File Status

typedef struct {
  i32 year;
  i32 month;
  i32 day;
} FileLocalDate;
bool ReadFileCreationDate(const char *path, FileLocalDate *date);

// Reports whether a host path exists.
bool FileHasStatus(const char *path);

#endif
