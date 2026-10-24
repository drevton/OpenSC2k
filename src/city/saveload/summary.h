#ifndef SC2K_FILE_SUMMARY_H
#define SC2K_FILE_SUMMARY_H

#include <stdio.h>

#include "city/types.h"
#include "city/saveload/types.h"
#include "city/terrain/terrain.h"

typedef struct {
  char name[256];
  int width, height, year, month;
  u32 format_version;
  i64 cash;
  CityFileVersion writer_version;
  bool extended, has_economy, has_writer_version;
} CityFileSummary;

// Presentation-only, bounded metadata reads. Never loads or changes a City.
bool ReadCityFileSummary(const char *native_path, CityFileSummary *summary);
// Borrow the stdio stream and write bounded picture/description buffers. Returns picture availability;
// tagged reads seek back to their initial position, and no live City state is changed.
bool ReadCityPreview(FILE *stream, u8 *picture, size_t picture_capacity, char *description, size_t text_capacity);
// Read the host creation date's stock thumbnail convention; an unavailable date returns false.
bool CityFileUsesTransparentThumbnail(const char *path);
// Read and decode the versioned scenario instructions into the caller-owned buffer, including its terminator.
bool ReadCityScenarioInstructions(const char *path, char text[CITY_SCENARIO_INSTRUCTION_BYTES + 1]);

// Import only terrain and trees into the existing generator output or bounded preview.
bool ReadCityFileTerrain(const char *path, const CityTerrainSettings *settings, CityTerrainMap *map, bool preview);

#endif
