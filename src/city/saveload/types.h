#ifndef SC2K_CITY_SAVELOAD_TYPES_H
#define SC2K_CITY_SAVELOAD_TYPES_H

#include "base/types.h"
#include "base/version.h"

// Stock XLAB slots reserve a length byte and one spare byte beyond the text limit.
enum {
  CITY_OVERLAY_LEGACY_NAME_BYTES = 25,
  CITY_OVERLAY_LEGACY_NAME_MAX_LENGTH = CITY_OVERLAY_LEGACY_NAME_BYTES - 2,
};

// Shared admission limit for native scalar and ground extension packets carried by stock archives.
enum { CITY_SAVE_EXTENSION_LIMIT = 128 * 1024 * 1024 };

typedef enum CityFileReleaseStage {
  CITY_FILE_RELEASE_DEVELOPMENT, CITY_FILE_RELEASE_ALPHA, CITY_FILE_RELEASE_BETA, CITY_FILE_RELEASE_STABLE
} CityFileReleaseStage;

typedef struct {
  u32 major, minor, patch;
  u8 stage;
  char revision[SC2K_GIT_REVISION_CAPACITY];
} CityFileVersion;

// Stock scenario tagged records use these versions and bounded payload sizes.
enum {
  CITY_SCENARIO_METADATA_VERSION = 128,
  CITY_SCENARIO_PREVIEW_VERSION = 128,
  CITY_SCENARIO_INSTRUCTIONS_VERSION = 129,
  CITY_SCENARIO_INSTRUCTION_BYTES = 768,
  CITY_SCENARIO_PICTURE_BYTES = 4300,
};

typedef enum CityFileExceptionCause {
  CITY_FILE_CAUSE_NONE = 0,
  CITY_FILE_CAUSE_GENERIC = 1,
  CITY_FILE_CAUSE_FILE_NOT_FOUND = 2,
  CITY_FILE_CAUSE_BAD_PATH = 3,
  CITY_FILE_CAUSE_TOO_MANY_OPEN_FILES = 4,
  CITY_FILE_CAUSE_ACCESS_DENIED = 5,
  CITY_FILE_CAUSE_INVALID_FILE = 6,
  CITY_FILE_CAUSE_REMOVE_CURRENT_DIRECTORY = 7,
  CITY_FILE_CAUSE_DIRECTORY_FULL = 8,
  CITY_FILE_CAUSE_BAD_SEEK = 9,
  CITY_FILE_CAUSE_HARDWARE = 10,
  CITY_FILE_CAUSE_SHARING_VIOLATION = 11,
  CITY_FILE_CAUSE_LOCK_VIOLATION = 12,
  CITY_FILE_CAUSE_DISK_FULL = 13,
  CITY_FILE_CAUSE_END_OF_FILE = 14,
} CityFileExceptionCause;

typedef struct CityFileExceptionState {
  i32 cause;
  i32 os_error; // Native errno.
} CityFileExceptionState;

// MISC word positions are file-format IDs, independent of guest addresses.
// Word29's role remains unidentified; retain its number rather than guessing.
typedef enum CityMiscWordIndex {
  CITY_MISC_WORD_VERSION = 0,
  CITY_MISC_WORD_CASH = 5,
  CITY_MISC_WORD_ASSET_VALUE = 9,
  CITY_MISC_WORD_FIELD_29 = 29,
  CITY_MISC_WORD_SPECIAL_FACILITY_AVAILABILITY = 30,
  CITY_MISC_WORD_ACCOUNT_NAMES_FIRST = 438,
  CITY_MISC_WORD_ECONOMY_FIRST = 479,
  CITY_MISC_WORD_ECONOMY_END = 911,
  CITY_MISC_WORD_SOUND_ENABLED = 1022,
  CITY_MISC_WORD_MUSIC_ENABLED = 1023,
  CITY_MISC_WORD_NEWSPAPER_EDITION = 1027,
  CITY_MISC_WORD_SAVED_VIEW_POSITION = 1028,
  CITY_MISC_WORD_SAVED_VIEW_MODE = 1029,
  CITY_MISC_WORD_LAST_SAVED = 1043,
} CityMiscWordIndex;

enum {
  CITY_MISC_VERSION = 290,
  CITY_MISC_ECONOMY_WORDS_PER_SECTOR = 27,
  CITY_MISC_WORD_RESERVED_FIRST = CITY_MISC_WORD_LAST_SAVED + 1,
  CITY_MISC_WORD_COUNT = 1200,
};

// Host file I/O observations; these records are not saved City state.
typedef enum CityFileIOOperation {
  NATIVE_SAVE_OPEN = 1,
  NATIVE_SAVE_WRITE = 2,
  NATIVE_SAVE_SEEK = 3,
  NATIVE_SAVE_CLOSE = 4,
  NATIVE_SAVE_REMOVE = 5,
  NATIVE_LOAD_OPEN = 6,
  NATIVE_LOAD_READ = 7,
  NATIVE_LOAD_SEEK = 8,
  NATIVE_LOAD_CLOSE = 9,
  NATIVE_LOAD_DIRECTORY = 10
} CityFileIOOperation;

// Platform observations, not guest mutations. Write result is the byte count;
// other results are 0/1. Input is valid only for the duration of the call.
typedef struct CityFileIOOutcome {
  CityFileIOOperation operation;
  u32 requested;
  u32 result;
  i32 error;
  const u8 *input;
  u32 input_size;
} CityFileIOOutcome;

#endif
