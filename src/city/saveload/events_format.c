#include "city/saveload/events_format.h"

#include <string.h>

#include "city/events/events.h"
#include "city/maps/labels.h"
#include "city/maps/map_types.h"
#include "city/state.h"

// Object locations and train next tiles must be in bounds. Other target pairs are offsets; destination pairs can be
// off-map aircraft waypoints. Event must be writable with its type already decoded; positions/train targets are
// clamped while other targets retain offset semantics.
static void DecodeStockCityEventCoordinates(City *city, CityMapEvent *event, int x, int y, int target_x, int target_y) {
  event->tile = MapTileXY(city, IntClamp(x, 0, city->map_width - 1),
      IntClamp(y, 0, city->map_height - 1));
  if (IsCityMapEventTrain(event->type)) {
    target_x = IntClamp(target_x, 0, city->map_width - 1);
    target_y = IntClamp(target_y, 0, city->map_height - 1);
  } else {
    target_x = IntClamp(target_x, 0, 15) * CITY_EVENT_OFFSET_SCALE;
    target_y = IntClamp(target_y, 0, 15) * CITY_EVENT_OFFSET_SCALE;
  }
  SetCityMapEventTarget(city, event, target_x, target_y);
}

// Stock XTHG stores the offset approach waypoint in bytes. Native events store its runway reference point regardless
// of the selected gameplay fixes. Narrow native coordinates and indexes only here for the stock byte format. Output
// must hold one full stock record and event links be consistent; native coordinates/state are projected without
// editing the source.
void EncodeStockCityMapEvent(const City *city, u8 bytes[CITY_STOCK_EVENT_BYTES], const CityMapEvent *event) {
  CityPoint position = ReadCityMapEventPosition(city, event);
  CityPoint target = ReadCityMapEventTarget(city, event);
  if (!(event->coordinate_flags & CITY_EVENT_TARGET_TILE)) {
    target.x /= CITY_EVENT_OFFSET_SCALE;
    target.y /= CITY_EVENT_OFFSET_SCALE;
  }
  bytes[0] = event->type; bytes[1] = event->direction; bytes[2] = event->state;
  bytes[3] = position.y; bytes[4] = position.x; bytes[5] = event->age;
  bytes[6] = target.y; bytes[7] = target.x;
  bytes[10] = event->previous_overlay;
  if (event->type == CITY_MAP_EVENT_SHIP) {
    bytes[8] = event->ship.destination_y; bytes[9] = event->ship.destination_x;
    bytes[11] = 0; // Ships do not consume the stock goal byte.
  } else {
    bytes[8] = event->raw.destination_y; bytes[9] = event->raw.destination_x;
    bytes[11] = event->raw.goal;
  }
  if ((event->coordinate_flags & CITY_EVENT_HASH_LINKED) && !IsCityMapEventTrain(event->type)) {
    // Reconstruct the stock hidden-overlay chain from canonical tile membership.
    // Train followers can share their engine's tile without joining that chain;
    // their saved overlay belongs to the movement record copied along the train.
    bytes[10] = ReadCityStockBaseOverlay(city, event->tile);
    for (int link = event->hash_next; link; link = city->events.event[link - 1].hash_next) {
      if (city->events.event[link - 1].tile != event->tile) continue;
      bytes[10] = CityEventOverlay(link - 1);
      break;
    }
  }
  if (event->type == CITY_MAP_EVENT_MAXIS_MAN && IsCityEventOverlay(event->maxis_man.goal))
    bytes[11] = CityEventOverlaySlot(event->maxis_man.goal);
  if (event->type == CITY_MAP_EVENT_AIRPLANE &&
      (event->state & CITY_AIRPLANE_STAGE_MASK) == CITY_AIRPLANE_APPROACH) {
    CityPoint offset = ReadCityAirplaneApproachOffset(event);
    bytes[8] = event->raw.destination_y + offset.y;
    bytes[9] = event->raw.destination_x + offset.x;
  }
}

// Decode one complete stock event record into native coordinates, links and type-specific state. Bytes must hold
// CITY_STOCK_EVENT_BYTES and City geometry must be initialized; output is reset first and hash membership is rebuilt
// separately.
void DecodeStockCityMapEvent(City *city, CityMapEvent *event, const u8 bytes[CITY_STOCK_EVENT_BYTES]) {
  memset(event, 0, sizeof(*event));
  event->type = bytes[0]; event->direction = bytes[1]; event->state = bytes[2]; event->age = bytes[5];
  event->previous_overlay = bytes[10];
  if (event->type == CITY_MAP_EVENT_SHIP) {
    event->ship.home_tile = MapTileXYOrInvalid(city, city->disasters.border_water_x, city->disasters.border_water_y);
    event->ship.destination_y = bytes[8]; event->ship.destination_x = bytes[9];
  } else {
    event->raw.destination_y = bytes[8]; event->raw.destination_x = bytes[9];
    event->raw.goal = bytes[11];
  }
  if (event->type == CITY_MAP_EVENT_MAXIS_MAN && bytes[11] < CITY_STOCK_EVENT_SLOT_COUNT)
    event->maxis_man.goal = CityEventOverlay(bytes[11]);
  DecodeStockCityEventCoordinates(city, event, bytes[4], bytes[3], bytes[7], bytes[6]);
  if (event->type == CITY_MAP_EVENT_AIRPLANE &&
      (event->state & CITY_AIRPLANE_STAGE_MASK) == CITY_AIRPLANE_APPROACH) {
    CityPoint offset = ReadCityAirplaneApproachOffset(event);
    event->raw.destination_y = (u8)(bytes[8] - offset.y);
    event->raw.destination_x = (u8)(bytes[9] - offset.x);
  }
}

// Decode the complete stock event table before overlay-link restoration. The file format offsets non-train targets
// by one; ships lack their shared home coordinate, so use each imported ship's own position. Keep these adjustments
// at the file boundary rather than in the single-record decoder also used by other stock adapters.
void DecodeStockCityMapEvents(City *city, const u8 *bytes) {
  memset(city->events.buckets, 0, CITY_EVENT_HASH_BUCKETS * sizeof(u16));
  for (int slot = 0; slot < CITY_STOCK_EVENT_SLOT_COUNT; ++slot) {
    u8 record[CITY_STOCK_EVENT_BYTES];
    memcpy(record, bytes + slot * CITY_STOCK_EVENT_BYTES, sizeof(record));
    if (!IsCityMapEventTrain(record[0])) {
      record[6] = IntMax(record[6] - 1, 0);
      record[7] = IntMax(record[7] - 1, 0);
    }
    DecodeStockCityMapEvent(city, &city->events.event[slot], record);
    if (record[0] == CITY_MAP_EVENT_SHIP)
      city->events.event[slot].ship.home_tile = city->events.event[slot].tile;
  }
}

// Write the complete fixed stock event table without changing native event state or rebuilding its hash.
void EncodeStockCityMapEvents(const City *city, u8 *bytes) {
  for (int slot = 0; slot < CITY_STOCK_EVENT_SLOT_COUNT; ++slot)
    EncodeStockCityMapEvent(city, bytes + slot * CITY_STOCK_EVENT_BYTES, &city->events.event[slot]);
}

// Separate stock overlay stacks into native terrain overlays and per-tile event membership. Run after stock records
// are decoded; clears unused event records and rebuilds hash links from active event locations.
void ImportStockCityEventLinks(City *city) {
  // Stock tiles keep only the top overlay. Native tiles retain the non-event
  // association underneath. Dispatch records left behind after recall are not active without a saved overlay link.
  _Static_assert(CITY_STOCK_EVENT_SLOT_COUNT <= 64, "stock event references fit in one word");
  u64 referenced = 0;
  for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
    TileInfo *tp = &city->tile[tile];
    int overlay = tp->overlay;
    for (int depth = 0; IsCityEventOverlay(overlay) && depth < CITY_STOCK_EVENT_SLOT_COUNT; ++depth) {
      int slot = CityEventOverlaySlot(overlay);
      referenced |= 1ull << slot;
      overlay = city->events.event[slot].previous_overlay;
    }
    tp->overlay = IsCityEventOverlay(overlay) ? CITY_OVERLAY_NONE : overlay;
  }
  for (int slot = 0; slot < city->events.capacity; ++slot) {
    CityMapEvent *event = &city->events.event[slot];
    bool dispatch = event->type == CITY_MAP_EVENT_POLICE || event->type == CITY_MAP_EVENT_FIRE ||
        event->type == CITY_MAP_EVENT_MILITARY;
    bool stale = dispatch && slot < CITY_STOCK_EVENT_SLOT_COUNT && !(referenced & (1ull << slot));
    if (event->type == CITY_MAP_EVENT_UNUSED || stale) memset(event, 0, sizeof(*event));
    else if (IsMapTileValid(city, event->tile)) event->previous_overlay = city->tile[event->tile].overlay;
  }
  RebuildCityEventHash(city);
}

