#include "city/saveload/services.h"
#include "city/saveload/city_file_internal.h"
#include <string.h>

// Read up to size bytes through the stdio, bounded-memory or observed-load input backend. Destination must provide
// size writable bytes; returns actual bytes read and preserves the backend's throwing-error behavior.
u32 CityFileInputRead(CityFileInput *input, void *destination, u32 size) {
  switch (input->kind) {
    case CITY_FILE_INPUT_PLATFORM:
      return ReadCityLoadStream(input->stream, destination, size);
    case CITY_FILE_INPUT_STDIO:
      return (u32)fread(destination, 1, size, input->stream);
    case CITY_FILE_INPUT_MEMORY: {
      u32 available = input->size - input->position;
      u32 count = size < available ? size : available;
      if (count != 0) memcpy(destination, input->bytes + input->position, count);
      input->position += count;
      return count;
    }
  }
  return 0;
}

// Seek the selected stdio, memory or observed-load backend. Memory seeks reject positions outside its span; returns
// the backend's zero-success convention and propagates throwing load-stream failures.
int CityFileInputSeek(CityFileInput *input, int offset, int origin) {
  if (input->kind == CITY_FILE_INPUT_STDIO) return fseek(input->stream, offset, origin);
  if (input->kind == CITY_FILE_INPUT_MEMORY) {
    i64 base = origin == SEEK_CUR ? input->position : origin == SEEK_END ? input->size : 0;
    i64 position = base + offset;
    if ((origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) || position < 0 || position > input->size)
      return -1;
    input->position = (u32)position;
    return 0;
  }
  return SeekCityPreviewStream(input->stream, offset, origin);
}

// Query the current selected input position without consuming bytes. Input must be initialized; preserves stdio/load
// position and error conventions rather than turning failures into EOF.
int CityFileInputPosition(CityFileInput *input) {
  if (input->kind == CITY_FILE_INPUT_STDIO) return (int)ftell(input->stream);
  if (input->kind == CITY_FILE_INPUT_MEMORY) return (int)input->position;
  return GetCityPreviewStreamPosition(input->stream);
}
