#ifndef SC2K_BYTE_ORDER_H
#define SC2K_BYTE_ORDER_H

#include <string.h>
#include "base/types.h"

enum { kNativeLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ };

static inline u16 ReadLE16(const void *data) {
  u16 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? value : __builtin_bswap16(value);
}

static inline u32 ReadLE32(const void *data) {
  u32 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? value : __builtin_bswap32(value);
}

static inline u64 ReadLE64(const void *data) {
  u64 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? value : __builtin_bswap64(value);
}

static inline void WriteLE16(void *data, u16 value) {
  value = kNativeLittleEndian ? value : __builtin_bswap16(value);
  memcpy(data, &value, sizeof(value));
}

static inline void WriteLE32(void *data, u32 value) {
  value = kNativeLittleEndian ? value : __builtin_bswap32(value);
  memcpy(data, &value, sizeof(value));
}

static inline void WriteLE64(void *data, u64 value) {
  value = kNativeLittleEndian ? value : __builtin_bswap64(value);
  memcpy(data, &value, sizeof(value));
}

static inline u16 ReadBE16(const void *data) {
  u16 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? __builtin_bswap16(value) : value;
}

static inline u32 ReadBE32(const void *data) {
  u32 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? __builtin_bswap32(value) : value;
}

static inline u64 ReadBE64(const void *data) {
  u64 value;
  memcpy(&value, data, sizeof(value));
  return kNativeLittleEndian ? __builtin_bswap64(value) : value;
}

static inline void WriteBE16(void *data, u16 value) {
  value = kNativeLittleEndian ? __builtin_bswap16(value) : value;
  memcpy(data, &value, sizeof(value));
}

static inline void WriteBE32(void *data, u32 value) {
  value = kNativeLittleEndian ? __builtin_bswap32(value) : value;
  memcpy(data, &value, sizeof(value));
}

static inline void WriteBE64(void *data, u64 value) {
  value = kNativeLittleEndian ? __builtin_bswap64(value) : value;
  memcpy(data, &value, sizeof(value));
}

static inline void ByteSwapWordArray(u16 *words, i32 size) {
  for (int index = 0; index < size / 2; ++index)
    words[index] = __builtin_bswap16(words[index]);
}

static inline void ByteSwapDwordArray(u32 *dwords, i32 size) {
  if (size <= 0) return;
  int count = size / 4 + (size % 4 != 0);
  for (int index = 0; index < count; ++index)
    dwords[index] = __builtin_bswap32(dwords[index]);
}

#endif
