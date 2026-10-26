#ifndef SC2K_CITY_SAVELOAD_INPUT_H
#define SC2K_CITY_SAVELOAD_INPUT_H

#include "base/types.h"
#include <stdio.h>

typedef struct CityLoadStream CityLoadStream;

typedef enum CityFileInputKind {
  CITY_FILE_INPUT_PLATFORM,
  CITY_FILE_INPUT_STDIO,
  CITY_FILE_INPUT_MEMORY
} CityFileInputKind;

// Initialize the selected backend before use. PLATFORM borrows an observed load-stream wrapper; STDIO borrows FILE*.
// Select load_stream for PLATFORM and stdio_stream for STDIO. MEMORY borrows size readable bytes with position <= size.
// Keep that backing storage alive until reads finish.
// None of these operations opens or closes the backend. Zero-initialize unused fields.
typedef struct CityFileInput {
  CityFileInputKind kind;
  union { CityLoadStream *load_stream; FILE *stdio_stream; };
  const u8 *bytes;
  u32 size;
  u32 position;
  // Chunk parsing may retain an already consumed signature here; input operations leave this lookahead alone.
  u32 first_tag;
  bool has_first_tag;
} CityFileInput;

// Destination must provide size writable bytes. Read at most size bytes and advance the selected backend. Short reads
// return their actual count; observed filesystem errors throw, while stdio retains fread's error convention.
u32 CityFileInputRead(CityFileInput *input, void *destination, u32 size);
// Seek using SEEK_SET/CUR/END. Memory rejects positions outside [0, size] without moving; stdio returns fseek status.
// Observed streams return zero after success and throw on failure.
i32 CityFileInputSeek(CityFileInput *input, i32 offset, i32 origin);
// Query without consuming bytes. Preserve the backend's signed 32-bit position projection and error convention;
// an observed stream performs its ordinary observed seek and can throw.
i32 CityFileInputPosition(CityFileInput *input);

#endif
