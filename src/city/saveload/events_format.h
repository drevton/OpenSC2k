#ifndef SC2K_CITY_SAVELOAD_EVENTS_FORMAT_H
#define SC2K_CITY_SAVELOAD_EVENTS_FORMAT_H

#include "city/types.h"

typedef struct CityMapEvent CityMapEvent;

enum { CITY_STOCK_EVENT_BYTES = 12 };

// Translate one complete stock XTHG record. City geometry must be initialized.
// Decode clears the destination record and clamps tile coordinates; encode leaves City unchanged.
// Hash membership is reconstructed separately after all records have been decoded.
void EncodeStockCityMapEvent(const City *city, u8 bytes[CITY_STOCK_EVENT_BYTES], const CityMapEvent *event);
void DecodeStockCityMapEvent(City *city, CityMapEvent *event, const u8 bytes[CITY_STOCK_EVENT_BYTES]);
// After decoding stock tiles and events, restore each tile's base overlay and rebuild native event membership.
// Clears unused event records; bounded traversal leaves cyclic stock overlay chains without an event overlay.
void ImportStockCityEventLinks(City *city);

#endif
