#ifndef SC2K_CITY_SAVELOAD_EVENTS_FORMAT_H
#define SC2K_CITY_SAVELOAD_EVENTS_FORMAT_H

#include "city/types.h"

typedef struct CityMapEvent CityMapEvent;

enum { CITY_STOCK_EVENT_BYTES = 12 };

// Stock XTHG translation owns the twelve-byte event representation; native movement never reads those bytes.
// Translate one complete record with City geometry initialized and the native event capacity available.
// Decode clears the destination record and clamps tile coordinates; encode leaves City unchanged.
// Non-train targets are offsets, while train targets are tiles. Aircraft destinations may remain off-map.
// Hash membership is reconstructed separately after all records have been decoded.
void EncodeStockCityMapEvent(const City *city, u8 bytes[CITY_STOCK_EVENT_BYTES], const CityMapEvent *event);
void DecodeStockCityMapEvent(City *city, CityMapEvent *event, const u8 bytes[CITY_STOCK_EVENT_BYTES]);

// Complete XTHG payloads hold CITY_STOCK_EVENT_SLOT_COUNT records; caller provides the full buffer and City capacity.
// File decoding also adjusts non-train target offsets, initializes imported ship homes and clears event hash buckets.
// Restore tile overlays and rebuild membership separately after the remaining stock map chunks have loaded.
void EncodeStockCityMapEvents(const City *city, u8 *bytes);
void DecodeStockCityMapEvents(City *city, const u8 *bytes);

// After decoding stock tiles and events, restore each tile's base overlay and rebuild native event membership.
// Clears unused records and dispatch markers absent from saved overlay stacks; other vehicle records stay intact.
// Bounded traversal leaves cyclic stock overlay chains without an event overlay.
void ImportStockCityEventLinks(City *city);

#endif
