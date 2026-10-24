#include "base/file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
// Keep the Win32 32-bit declaration separate from our 64-bit arithmetic helper.
#define MulDiv WindowsMulDiv
#include <windows.h>
#undef MulDiv
#else
#include <dirent.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include <string.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "base/text.h"

bool IsPathSeparator(int character) {
#ifdef _WIN32
  return character == '/' || character == '\\';
#else
  return character == '/';
#endif
}

bool IsPathAbsolute(const char *path) {
#ifdef _WIN32
  return IsPathSeparator(path[0]) || (path[0] && path[1] == ':');
#else
  return path[0] == '/';
#endif
}

const char *PathFilename(const char *path) {
  const char *name = path;
  for (; *path; ++path) if (IsPathSeparator(*path)) name = path + 1;
  return name;
}

void NormalizeFilePath(char *path) {
#ifdef _WIN32
  for (; *path; ++path) if (*path == '/') *path = kPathSeparator;
#else
  (void)path;
#endif
}

bool CopyFilePath(const char *path, char *output, size_t capacity) {
  size_t length = strlen(path);
  if (!length || length >= capacity) return false;
  memmove(output, path, length + 1);
  NormalizeFilePath(output);
  return true;
}

bool ResolveFilePath(const char *path, const char *directory, char **owned) {
  if (!path[0]) return false;
  if (IsPathAbsolute(path)) SetText(owned, path);
  else {
    if (!directory[0]) directory = ".";
    FormatText(owned, "%s%c%s", directory, kPathSeparator, path);
  }
  NormalizeFilePath(*owned);
  return true;
}

FILE *OpenHostFile(const char *path, const char *mode) {
  return path != NULL ? fopen(path, mode) : NULL;
}

char *ReadWorkingDirectory(void) {
#ifdef _WIN32
  return _getcwd(NULL, 0);
#else
  return getcwd(NULL, 0);
#endif
}

char *ReadExecutableDirectory(void) {
#ifdef __APPLE__
  u32 capacity = 0;
  _NSGetExecutablePath(NULL, &capacity);
  char *path = malloc(capacity);
  if (!path) return NULL;
  if (_NSGetExecutablePath(path, &capacity)) {
    free(path);
    return NULL;
  }
  char *name = (char *)PathFilename(path);
  if (name == path) {
    free(path);
    return NULL;
  }
  name[-1] = 0;
  return path;
#elif defined(_WIN32)
  for (u32 capacity = 256; capacity <= 32768; capacity *= 2) {
    char *path = malloc(capacity);
    if (!path) return NULL;
    DWORD length = GetModuleFileNameA(NULL, path, capacity);
    if (length && length < capacity) {
      char *name = (char *)PathFilename(path);
      if (name != path) {
        name[-1] = 0;
        return path;
      }
    }
    free(path);
    if (!length) return NULL;
  }
  return NULL;
#elif defined(__linux__)
  for (size_t capacity = 256; capacity <= 65536; capacity *= 2) {
    char *path = malloc(capacity);
    if (!path) return NULL;
    ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
    if (length > 0 && (size_t)length < capacity - 1) {
      path[length] = 0;
      char *name = (char *)PathFilename(path);
      if (name != path) {
        name[-1] = 0;
        return path;
      }
    }
    free(path);
    if (length < 0) return NULL;
  }
  return NULL;
#else
  return NULL;
#endif
}

bool SetWorkingDirectory(const char *path) {
#ifdef _WIN32
  return _chdir(path) == 0;
#else
  return chdir(path) == 0;
#endif
}

char *ResolveExistingPath(const char *path) {
#ifdef _WIN32
  return _fullpath(NULL, path, 0);
#else
  return realpath(path, NULL);
#endif
}

bool RemoveFile(const char *path) {
#ifdef _WIN32
  return _unlink(path) == 0;
#else
  return unlink(path) == 0;
#endif
}

bool ReadHostFileInfo(const char *path, HostFileInfo *info) {
  struct stat status;
  if (stat(path, &status)) return false;
  *info = (HostFileInfo){status.st_mtime, status.st_size,
      S_ISDIR(status.st_mode), S_ISREG(status.st_mode)};
  return true;
}

bool VisitDirectory(const char *path,
    bool (*visit)(const char *name, const HostFileInfo *info, void *context), void *context) {
  bool ok = true;
  char *filename = NULL;
#ifdef _WIN32
  char *pattern = NULL;
  FormatText(&pattern, "%s\\*", path);
  WIN32_FIND_DATAA entry;
  HANDLE directory = FindFirstFileA(pattern, &entry);
  free(pattern);
  if (directory == INVALID_HANDLE_VALUE) return false;
  do {
    FormatText(&filename, "%s%c%s", path, kPathSeparator, entry.cFileName);
    HostFileInfo info;
    if (ReadHostFileInfo(filename, &info)) ok = visit(entry.cFileName, &info, context);
  } while (ok && FindNextFileA(directory, &entry));
  FindClose(directory);
#else
  DIR *directory = opendir(path);
  if (!directory) return false;
  struct dirent *entry;
  while (ok) {
    errno = 0;
    entry = readdir(directory);
    if (!entry) {
      ok = errno == 0;
      break;
    }
    FormatText(&filename, "%s/%s", path, entry->d_name);
    HostFileInfo info;
    if (ReadHostFileInfo(filename, &info)) ok = visit(entry->d_name, &info, context);
  }
  if (closedir(directory)) ok = false;
#endif
  free(filename);
  return ok;
}

bool ReadDirectoryFreeBytes(const char *path, u64 *bytes) {
#ifdef _WIN32
  ULARGE_INTEGER available;
  if (!GetDiskFreeSpaceExA(path, &available, NULL, NULL)) return false;
  *bytes = available.QuadPart;
#else
  struct statvfs space;
  if (statvfs(path, &space)) return false;
  *bytes = (u64)space.f_bavail * space.f_frsize;
#endif
  return true;
}

int RedirectErrorLog(const char *path) {
  if (!path) return -1;
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) return -1;
  int saved = dup(fileno(stderr));
  fflush(stderr);
  if (saved >= 0 && dup2(fd, fileno(stderr)) < 0) {
    close(saved);
    saved = -1;
  }
  close(fd);
  return saved;
}

void RestoreErrorLog(int descriptor) {
  fflush(stderr);
  dup2(descriptor, fileno(stderr));
  close(descriptor);
}

bool ResolveUserPath(const char *path, const char *directory, char **owned) {
  if (path[0] == '~' && (!path[1] || IsPathSeparator(path[1]))) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home || !home[0]) home = getenv("USERPROFILE");
#endif
    if (!home || !home[0]) return false;
    FormatText(owned, "%s%s", home, path + 1);
    NormalizeFilePath(*owned);
    return true;
  }
  char *working = NULL;
  if (!directory) {
    working = ReadWorkingDirectory();
    if (!working) return false;
    directory = working;
  }
  bool resolved = ResolveFilePath(path, directory, owned);
  free(working);
  return resolved;
}

static bool CreateDirectoryComponent(const char *path) {
  struct stat status;
  if (!stat(path, &status)) return S_ISDIR(status.st_mode);
#ifdef _WIN32
  int result = _mkdir(path);
#else
  int result = mkdir(path, 0700);
#endif
  if (result && errno != EEXIST) return false;
  return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

bool CreateDirectories(const char *path) {
  if (!path[0]) return false;
  char *text = DuplicateText(path);
  bool success = true;
  char *first = text + 1;
#ifdef _WIN32
  if (text[0] && text[1] == ':') {
    first = text + 2;
    if (IsPathSeparator(*first)) ++first;
  }
#endif
  for (char *p = first; *p && success; ++p) {
    if (!IsPathSeparator(*p)) continue;
    char separator = *p;
    *p = 0;
    success = CreateDirectoryComponent(text);
    *p = separator;
  }
  if (success) success = CreateDirectoryComponent(text);
  free(text);
  return success;
}

bool FileHasStatus(const char *path) {
  struct stat status;
  return path != NULL && stat(path, &status) == 0;
}

bool ReadFileCreationDate(const char *path, FileLocalDate *date) {
  struct stat status;
  if (path == NULL || stat(path, &status) != 0)
    return false;
#ifdef __APPLE__
  time_t created = status.st_birthtimespec.tv_sec;
  if (created == 0)
    created = status.st_mtimespec.tv_sec;
#else
  time_t created = status.st_ctime;
#endif
  const struct tm *local = localtime(&created);
  if (local == NULL)
    return false;
  *date = (FileLocalDate){local->tm_year + 1900, local->tm_mon + 1, local->tm_mday};
  return true;
}

i64 FileByteLength(FILE *file) {
  if (fseek(file, 0, SEEK_END)) return -1;
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET)) return -1;
  return length;
}

bool WriteFileAtomically(const char *path, bool (*write_contents)(FILE *file, void *context), void *context) {
  errno = 0;
  char *temporary = NULL;
  FormatText(&temporary, "%s.XXXXXX", path);
#ifdef _WIN32
  if (_mktemp_s(temporary, strlen(temporary) + 1)) {
    free(temporary);
    return false;
  }
  FILE *file = fopen(temporary, "wx");
#else
  int descriptor = mkstemp(temporary);
  FILE *file = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
  if (!file && descriptor >= 0) close(descriptor);
#endif
  bool ok = file && write_contents(file, context);
  if (file && ok && fflush(file)) ok = false;
#ifdef _WIN32
  if (file && ok && _commit(_fileno(file))) ok = false;
#else
  if (file && ok && fsync(fileno(file))) ok = false;
#endif
  if (file && fclose(file)) ok = false;
  int error = errno;
#ifdef _WIN32
  if (ok && !MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = false;
#else
  if (ok && rename(temporary, path)) {
    ok = false;
    error = errno;
  }
#endif
  if (!ok) remove(temporary);
  free(temporary);
  errno = ok ? 0 : error ? error : EIO;
  return ok;
}

// Consume the source when invoked. BackupFile closes it if temporary-file creation fails before this callback.
static bool CopyAndCloseFileSource(FILE *output, void *context) {
  FILE **stream = context;
  FILE *source = *stream;
  u8 buffer[65536];
  size_t count;
  bool ok = true;
  while (ok && (count = fread(buffer, 1, sizeof(buffer), source)) != 0)
    ok = fwrite(buffer, 1, count, output) == count;
  if (ferror(source)) ok = false;
  if (fclose(source)) ok = false;
  *stream = NULL;
  return ok;
}

bool BackupFile(const char *path) {
  FILE *source = OpenHostFile(path, "rb");
  if (!source) return errno == ENOENT;
  char *backup = NULL;
  FormatText(&backup, "%s.bak", path);
  bool ok = WriteFileAtomically(backup, CopyAndCloseFileSource, &source);
  int error = errno;
  if (source) fclose(source);
  free(backup);
  errno = ok ? 0 : error ? error : EIO;
  return ok;
}
