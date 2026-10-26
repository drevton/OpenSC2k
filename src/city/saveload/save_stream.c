#include "city/saveload/services.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "base/file.h"
#include "city/saveload/runtime.h"
#include "city/saveload/state.h"
#include "city/saveload/types.h"
#include "game/observation.h"

// Filesystem writes for the complete City Save caller.

struct NativeCitySaveAttempt {
  struct NativeCitySaveAttempt *parent;
  CityFileRuntime *owner;
  char *destination;
  i32 io_error;
  u32 open_streams;
  // Backup has been admitted and output opening has begun; the destination can no longer change.
  bool started;
  CityFileScratch scratch;
  CityFileScratch *previous_scratch;
  u32 *previous_save_size;
  CityMiscBuffer *previous_misc;
  bool previous_misc_active, previous_misc_owned;
};

struct CitySaveStream {
  FILE *file;
  NativeCitySaveAttempt *attempt;
};

// Retain the first error within a save attempt, or the last error when no attempt exists. File must be valid and any
// attempt must belong to it; later failures do not replace an attempt's original cause.
static void RememberSaveError(CityFileRuntime *file, NativeCitySaveAttempt *attempt, i32 error) {
  if (attempt == NULL) file->save_io.last_error = error;
  else if (attempt->io_error == 0) attempt->io_error = error;
}

// Record the current errno as the attempt's first failure, substituting EIO when errno is empty. Requires a live
// attempt; this does not clear errno or close its streams.
static void RememberSaveIOError(NativeCitySaveAttempt *attempt) {
  RememberSaveError(attempt->owner, attempt, errno != 0 ? errno : EIO);
}

// Open the active attempt's selected destination, backing it up once when configured. Returns an owned unbuffered
// stream wrapper or NULL with a retained error. caller_path is observation text, not the host destination.
CitySaveStream *OpenCitySaveStream(CityFileRuntime *file, const char *caller_path) {
  NativeCitySaveAttempt *attempt = file->save_io.attempt;
  if (attempt == NULL || attempt->destination == NULL) {
    RememberSaveError(file, attempt, EINVAL);
    ObserveCityFileIO(file, NATIVE_SAVE_OPEN, 0, 0, EINVAL, caller_path, (u32)strlen(caller_path));
    return NULL;
  }
  if (!attempt->started && ShouldBackupCitySave(file) && !BackupFile(attempt->destination)) {
    RememberSaveIOError(attempt);
    return NULL;
  }
  attempt->started = true;
  CitySaveStream *stream = malloc(sizeof(*stream));
  if (stream == NULL) {
    RememberSaveError(file, attempt, ENOMEM);
    ObserveCityFileIO(file, NATIVE_SAVE_OPEN, 0, 0, ENOMEM, caller_path, (u32)strlen(caller_path));
    return NULL;
  }
  errno = 0;
  FILE *output = fopen(attempt->destination, "wb");
  if (output == NULL) {
    RememberSaveIOError(attempt);
    ObserveCityFileIO(file, NATIVE_SAVE_OPEN, 0, 0, attempt->io_error, caller_path, (u32)strlen(caller_path));
    free(stream);
    return NULL;
  }
  // Original CFile::Write reports errors at each write, not at a later stdio
  // flush. Keep the adapter's four-byte/header and payload boundaries visible.
  if (setvbuf(output, NULL, _IONBF, 0) != 0) {
    RememberSaveIOError(attempt);
    fclose(output);
    ObserveCityFileIO(file, NATIVE_SAVE_OPEN, 0, 0, attempt->io_error, caller_path, (u32)strlen(caller_path));
    free(stream);
    return NULL;
  }
  *stream = (CitySaveStream){output, attempt};
  ++attempt->open_streams;
  ObserveCityFileIO(file, NATIVE_SAVE_OPEN, 0, 1, 0, caller_path, (u32)strlen(caller_path));
  return stream;
}

// Write a complete byte block and observe the actual write boundary. Requires an open attempt stream and size readable
// bytes; false retains the first error and any partial output instead of deferring failure until flush.
bool WriteCitySaveStream(CitySaveStream *output, const void *bytes, u32 size) {
  errno = 0;
  size_t written = fwrite(bytes, 1, size, output->file);
  i32 error = written == size ? 0 : errno != 0 ? errno : EIO;
  NativeCitySaveAttempt *attempt = output->attempt;
  if (error) RememberSaveError(attempt->owner, attempt, error);
  ObserveCityFileIO(attempt->owner, NATIVE_SAVE_WRITE, size, (u32)written, error, bytes, size);
  return written == size;
}

// Rewind an open attempt stream to rewrite its header. Returns whether the seek succeeded and retains its failure in
// the attempt; it does not reset prior errors.
bool SeekCitySaveStreamStart(CitySaveStream *output) {
  errno = 0;
  bool success = fseek(output->file, 0, SEEK_SET) == 0;
  i32 error = success ? 0 : errno != 0 ? errno : EIO;
  NativeCitySaveAttempt *attempt = output->attempt;
  if (error) RememberSaveError(attempt->owner, attempt, error);
  ObserveCityFileIO(attempt->owner, NATIVE_SAVE_SEEK, 0, success, error, NULL, 0);
  return success;
}

// Close and release a save-stream wrapper, preserving any close error on its attempt. The wrapper becomes invalid and
// the open-stream count decreases even on failure.
void CloseCitySaveStream(CitySaveStream *output) {
  errno = 0;
  bool success = fclose(output->file) == 0;
  i32 error = success ? 0 : errno != 0 ? errno : EIO;
  NativeCitySaveAttempt *attempt = output->attempt;
  if (error) RememberSaveError(attempt->owner, attempt, error);
  ObserveCityFileIO(attempt->owner, NATIVE_SAVE_CLOSE, 0, success, error, NULL, 0);
  // The observer may run nested work. Read the stream's live binding before releasing its open count.
  --output->attempt->open_streams;
  free(output);
}

// Remove the active attempt's selected host destination after a failed save. Caller path is retained only for
// observation; missing destination or remove failure is recorded on the file operation.
void RemoveCitySaveFile(CityFileRuntime *file, const char *caller_path) {
  if (file->save_io.attempt == NULL || file->save_io.attempt->destination == NULL) {
    RememberSaveError(file, file->save_io.attempt, EINVAL);
    ObserveCityFileIO(file, NATIVE_SAVE_REMOVE, 0, 0, EINVAL, caller_path, (u32)strlen(caller_path));
    return;
  }
  errno = 0;
  bool success = remove(file->save_io.attempt->destination) == 0;
  i32 error = success ? 0 : errno != 0 ? errno : EIO;
  if (error) RememberSaveError(file, file->save_io.attempt, error);
  ObserveCityFileIO(file, NATIVE_SAVE_REMOVE, 0, success, error, caller_path, (u32)strlen(caller_path));
}

// Push a nested save attempt with private destination, scratch and error state. Returns NULL on allocation failure; a
// successful attempt must be ended after all its streams close to restore its parent's buffers.
NativeCitySaveAttempt *BeginCitySaveAttempt(CityFileRuntime *file) {
  NativeCitySaveAttempt *attempt = calloc(1, sizeof(*attempt));
  if (attempt == NULL) {
    file->save_io.last_error = errno = ENOMEM;
    return NULL;
  }
  attempt->owner = file;
  attempt->parent = file->save_io.attempt;
  attempt->previous_scratch = file->scratch;
  attempt->previous_save_size = file->save_size;
  file->save_size = &attempt->scratch.save_size;
  attempt->previous_misc = file->misc;
  attempt->previous_misc_active = file->misc_active;
  attempt->previous_misc_owned = file->misc_owned;
  file->scratch = &attempt->scratch;
  file->misc = NULL;
  file->misc_active = file->misc_owned = false;
  file->save_io.attempt = attempt;
  return attempt;
}

// Copy a nonempty host destination into the current, not-yet-started save attempt. Returns false for stale attempts,
// started output, invalid text or allocation failure; the attempt owns the copied path.
bool SelectCitySaveDestination(CityFileRuntime *file, NativeCitySaveAttempt *attempt,
    const char *destination) {
  if (attempt == NULL || attempt != file->save_io.attempt) {
    errno = attempt == NULL ? EINVAL : EBUSY;
    return false;
  }
  if (attempt->started) {
    RememberSaveError(file, attempt, EBUSY);
    errno = EBUSY;
    return false;
  }
  if (destination == NULL || destination[0] == 0) {
    RememberSaveError(file, attempt, EINVAL);
    errno = EINVAL;
    return false;
  }
  size_t length = strlen(destination);
  char *selected = malloc(length + 1);
  if (selected == NULL) {
    RememberSaveError(file, attempt, ENOMEM);
    errno = ENOMEM;
    return false;
  }
  memcpy(selected, destination, length + 1);
  free(attempt->destination);
  attempt->destination = selected;
  return true;
}

// Read the error from an attempt still present in this operation's nested attempt stack. Returns EINVAL for missing or
// unrelated attempts; no error or ownership state is changed.
i32 GetCitySaveAttemptError(const CityFileRuntime *file, const NativeCitySaveAttempt *attempt) {
  if (attempt == NULL) return EINVAL;
  for (const NativeCitySaveAttempt *scope = file->save_io.attempt; scope != NULL; scope = scope->parent)
    if (scope == attempt) return scope->io_error;
  return EINVAL;
}

// Release the current attempt and restore its parent's scratch and MISC bindings. Requires all streams closed; invalid
// nesting returns EINVAL/EBUSY without popping, while a valid end returns and retains the attempt's first error.
i32 EndCitySaveAttempt(CityFileRuntime *file, NativeCitySaveAttempt *attempt) {
  if (attempt == NULL || attempt != file->save_io.attempt || attempt->open_streams != 0) {
    errno = attempt == NULL ? EINVAL : EBUSY;
    return errno;
  }
  i32 error = attempt->io_error;
  file->save_io.attempt = attempt->parent;
  if (file->misc_owned) free(file->misc);
  file->scratch = attempt->previous_scratch;
  file->save_size = attempt->previous_save_size;
  file->misc = attempt->previous_misc;
  file->misc_active = attempt->previous_misc_active;
  file->misc_owned = attempt->previous_misc_owned;
  free(attempt->destination);
  free(attempt);
  file->save_io.last_error = error;
  errno = error;
  return error;
}

// Read the active save attempt's error, or the last completed attempt's error. File must be initialized; this query
// neither clears the error nor changes attempt nesting.
i32 GetCitySaveIOError(const CityFileRuntime *file) {
  return file->save_io.attempt != NULL ? file->save_io.attempt->io_error :
      file->save_io.last_error;
}

// Fill caller-owned exception details from the current save I/O error. Requires a valid file and output record; does
// not perform I/O or display an error.
void FillCitySaveFileError(const CityFileRuntime *file, CityFileExceptionState *exception) {
  exception->cause = CITY_FILE_CAUSE_GENERIC;
  exception->os_error = GetCitySaveIOError(file);
}
