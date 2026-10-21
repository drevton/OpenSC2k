#ifndef SC2K_FILE_SUMMARY_H
#define SC2K_FILE_SUMMARY_H

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
// Import only terrain and trees into the existing generator output or bounded preview.
bool ReadCityFileTerrain(const char *path, const CityTerrainSettings *settings, CityTerrainMap *map, bool preview);

#endif
