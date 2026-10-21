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
bool CopyFilePath(const char *path, char *output, size_t capacity);
bool ResolveFilePath(const char *path, const char *directory, char **owned);
// Configured paths can use ~/; a NULL directory resolves relative to the host working directory.
bool ResolveUserPath(const char *path, const char *directory, char **owned);
bool CreateDirectories(const char *path);
FILE *OpenHostFile(const char *path, const char *mode);
char *ReadWorkingDirectory(void);
// Executable directory when the host exposes it; the caller owns the result.
char *ReadExecutableDirectory(void);
bool SetWorkingDirectory(const char *path);
char *ResolveExistingPath(const char *path);
bool RemoveFile(const char *path);
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
int RedirectErrorLog(const char *path);
void RestoreErrorLog(int descriptor);
// Rewinds to the start; returns -1 on host I/O failure.
i64 FileByteLength(FILE *file);
bool WriteFileAtomically(const char *path, bool (*write)(FILE *file, void *context), void *context);

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
