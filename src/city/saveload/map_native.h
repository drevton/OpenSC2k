#ifndef SC2K_CITY_MAP_NATIVE_H
#define SC2K_CITY_MAP_NATIVE_H

#include "city/types.h"

// Native tile validation is read-only: check local discriminators before neighboring geometry and footprint links.
// No stock projection, staging map, RNG, invalidation or load-error presentation is performed.
bool ValidateCityTileRecords(const City *city);

// The XGND payload stores six byte planes followed by a climate header and the existing snow/moisture lattice.
enum { CITY_SAVE_CLIMATE_HEADER_SIZE = 8 };
// Zero while stock records are still being decoded; otherwise includes the complete native planes and climate data.
u32 CityGroundExtensionSize(const City *city);
// Requires complete native records and a writable buffer of size bytes. Reject size/format admission before writes;
// on success write little-endian planes and climate data, leaving any trailing bytes unchanged.
bool EncodeCityGroundExtension(const City *city, u8 *bytes, u32 size);

#endif
