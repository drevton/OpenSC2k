// TileInfo signs, facility associations and stationary effects (zero-based bits):
// overlay[0] records sign presence; its identity/text is in City's sparse sign records, not in the byte.
// overlay[1..4] stores one effect: 0 none, 10 border connection, 11 pollution, 12 flood, 13/14 riots, 15 fire.
// Nonzero codes project to original overlay value 240 + code; 1..9 have no named native effect. Bits 5..7 are
// unassigned. Moving events and facility identities live in separate City records indexed by tile.
// ReadCityBaseOverlay resolves effect first, then sign, then facility; WriteOverlay(..., 0) clears only the effect.
// Sign removal clears bit 0 explicitly. Other TileInfo bytes keep their shared/family meanings from tile_storage.h.

#include "city/maps/labels.h"
#include "city/events/news.h"
#include "city/economy/facilities.h"
#include "audio/types.h"
#include "city/engine/command_types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "city/presentation.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "city/tools/tools.h"

#include "game/audio.h"
#include "city/state.h"
#include "city/engine/commands.h"
#include "city/economy/economy.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/saveload/file.h"
#include "game/state.h"
#include "assets/state.h"
#include "game/observation.h"
#include "base/text.h"

// Sparse map associations. Bucket and record storage grows with placed objects, bounded by map area.
static u32 AssociationBucket(TileIndex tile, int capacity) {
  u32 hash = tile ^ (tile >> 16);
  hash *= 0x7FEB352D;
  hash ^= hash >> 15;
  return hash & (capacity - 1);
}

CityAnnualFacilityRecord *CityFacilityRecordAt(City *city, int slot) {
  return slot < CITY_FACILITY_RECORD_COUNT ? &city->annual_facilities.record[slot] :
      &city->annual_facilities.extra[slot - CITY_FACILITY_RECORD_COUNT];
}
const CityAnnualFacilityRecord *ReadCityFacilityRecord(const City *city, int slot) {
  return slot < CITY_FACILITY_RECORD_COUNT ? &city->annual_facilities.record[slot] :
      &city->annual_facilities.extra[slot - CITY_FACILITY_RECORD_COUNT];
}
int CityFacilityRecordLimit(const City *city) {
  return city->individual_facilities ? IntMax(city->annual_facilities.used, CITY_FACILITY_RECORD_COUNT) :
      CITY_FACILITY_RECORD_COUNT;
}

bool CityHasExtendedAssociations(const City *city) {
  for (int slot = CITY_OVERLAY_SIGN_LAST + 1; slot < city->signs.used; ++slot)
    if (city->signs.record[slot].tile != INVALID_TILE) return true;
  for (int slot = CITY_FACILITY_RECORD_COUNT; slot < city->annual_facilities.used; ++slot)
    if (ReadCityFacilityRecord(city, slot)->type) return true;
  return false;
}

bool ReserveCitySigns(City *city, int requested) {
  CitySigns *signs = &city->signs;
  if (requested <= signs->capacity) return true;
  if (requested > city->map_cell_count + 1) return false;
  int capacity = IntMax(signs->capacity, 64);
  while (capacity < requested) capacity *= 2;
  CitySignRecord *records = calloc(capacity, sizeof(*records));
  u32 *buckets = calloc(capacity, sizeof(*buckets));
  if (!records || !buckets) { free(records); free(buckets); return false; }
  for (int slot = 0; slot < capacity; ++slot) records[slot].tile = INVALID_TILE;
  if (signs->used) memcpy(records, signs->record, signs->used * sizeof(*records));
  free(signs->record); free(signs->buckets);
  signs->record = records; signs->buckets = buckets; signs->capacity = capacity;
  if (!signs->used) signs->used = 1;
  for (int slot = signs->used - 1; slot > 0; --slot) {
    CitySignRecord *record = &records[slot];
    if (record->tile == INVALID_TILE) continue;
    u32 *head = &buckets[AssociationBucket(record->tile, capacity)];
    record->next = *head; *head = slot;
  }
  return true;
}

bool ReserveCityFacilities(City *city, int requested) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (requested <= facilities->capacity) return true;
  if (requested > city->map_cell_count + CITY_FACILITY_RECORD_RESERVED_COUNT) return false;
  int capacity = IntMax(facilities->capacity, 256);
  while (capacity < requested) capacity *= 2;
  CityAnnualFacilityRecord *extra = calloc(capacity - CITY_FACILITY_RECORD_COUNT, sizeof(*extra));
  CityFacilityDetails *details = calloc(capacity, sizeof(*details));
  u32 *buckets = calloc(capacity, sizeof(*buckets));
  if (!extra || !details || !buckets) { free(extra); free(details); free(buckets); return false; }
  for (int slot = 0; slot < capacity; ++slot) details[slot].tile = INVALID_TILE;
  if (facilities->capacity) {
    memcpy(details, facilities->details, facilities->capacity * sizeof(*details));
    memcpy(extra, facilities->extra, (facilities->capacity - CITY_FACILITY_RECORD_COUNT) * sizeof(*extra));
  }
  free(facilities->extra); free(facilities->details); free(facilities->buckets);
  facilities->extra = extra; facilities->details = details; facilities->buckets = buckets;
  facilities->capacity = capacity;
  if (!facilities->used) facilities->used = CITY_FACILITY_RECORD_RESERVED_COUNT;
  for (int slot = facilities->used - 1; slot >= CITY_FACILITY_RECORD_FIRST_DYNAMIC; --slot) {
    CityFacilityDetails *record = &details[slot];
    if (record->tile == INVALID_TILE || !ReadCityFacilityRecord(city, slot)->type) continue;
    u32 *head = &buckets[AssociationBucket(record->tile, capacity)];
    record->next = *head; *head = slot + 1;
  }
  return true;
}

// Release sparse storage without touching the fixed facility records. Shared-facility new-city resets preserve
// unused payload fields in those records; final release clears them as well.
static void ReleaseCityAssociationStorage(City *city) {
  free(city->signs.record); free(city->signs.buckets);
  city->signs = (CitySigns){0};
  CityAnnualFacilities *facilities = &city->annual_facilities;
  free(facilities->extra); free(facilities->details); free(facilities->buckets);
  facilities->extra = NULL;
  facilities->details = NULL;
  facilities->buckets = NULL;
  facilities->capacity = facilities->used = facilities->free_slot = 0;
}

void ReleaseCityAssociations(City *city) {
  ReleaseCityAssociationStorage(city);
  memset(city->annual_facilities.record, 0, sizeof(city->annual_facilities.record));
}

void ResetCityMapAssociations(City *city) {
  ReleaseCityAssociationStorage(city);
  for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile)
    city->tile[tile].overlay = CITY_OVERLAY_NONE;
  // Empty all fixed names except the mayor. Keep their unused bytes, as in stock new-city initialization.
  for (int overlay = CITY_OVERLAY_SIGN_FIRST; overlay < CITY_OVERLAY_NAME_COUNT; ++overlay)
    city->overlay_names.entry[overlay].character[0] = 0;
  CityOverlayName *mayor = &city->overlay_names.entry[0];
  CopyText((char *)mayor->character, sizeof(mayor->character), "Mayor McSim");
}

int ReadCitySignOverlay(const City *city, TileIndex tile) {
  if (!(city->tile[tile].overlay & CITY_TILE_HAS_SIGN) || !city->signs.capacity) return 0;
  for (u32 slot = city->signs.buckets[AssociationBucket(tile, city->signs.capacity)]; slot;
      slot = city->signs.record[slot].next)
    if (city->signs.record[slot].tile == tile) return CitySignOverlay(slot);
  return 0;
}

int ReadCityFacilityOverlay(const City *city, TileIndex tile) {
  int kind = ReadTile(city, tile);
  if (!IsTileKindFacility(kind)) return 0;
  int category = kFacilityClassByTile[kind];
  if (!city->individual_facilities && category >= CITY_FACILITY_INFO_SHARED_RECORD_FIRST)
    return CityFacilityOverlay(category - (CITY_FACILITY_INFO_SHARED_RECORD_FIRST - 1));
  const CityAnnualFacilities *facilities = &city->annual_facilities;
  if (!facilities->capacity) return 0;
  TileIndex anchor = ResolveBulldozeFootprint(city, tile).start;
  if (anchor == INVALID_TILE) return 0;
  for (u32 link = facilities->buckets[AssociationBucket(anchor, facilities->capacity)]; link;
      link = facilities->details[link - 1].next)
    if (facilities->details[link - 1].tile == anchor &&
        ReadCityFacilityRecord(city, link - 1)->type != TK_CLEAR)
      return CityFacilityOverlay(link - 1);
  return 0;
}

const CityOverlayName *ReadCityOverlayName(const City *city, int overlay) {
  if (overlay < CITY_SIGN_OVERLAY_EXTENDED) return &city->overlay_names.entry[overlay];
  if (IsCityFacilityOverlay(overlay))
    return &city->annual_facilities.details[CityFacilityOverlaySlot(overlay)].name;
  return &city->signs.record[CitySignOverlaySlot(overlay)].name;
}
CityOverlayName *CityOverlayNameAt(City *city, int overlay) {
  if (overlay < CITY_SIGN_OVERLAY_EXTENDED) return &city->overlay_names.entry[overlay];
  if (IsCityFacilityOverlay(overlay))
    return &city->annual_facilities.details[CityFacilityOverlaySlot(overlay)].name;
  return &city->signs.record[CitySignOverlaySlot(overlay)].name;
}

int ReadCityBaseOverlay(const City *city, TileIndex tile) {
  if (!city->native_map_storage) return city->tile[tile].overlay;
  int effect = (city->tile[tile].overlay & CITY_TILE_EFFECT_MASK) >> CITY_TILE_EFFECT_SHIFT;
  if (effect) return CITY_OVERLAY_EVENT_END + effect - 1;
  int sign = ReadCitySignOverlay(city, tile);
  if (sign) return sign;
  // New records for ordinary utilities must not change legacy event/disaster admission.
  if (city->individual_facilities && kFacilityClassByTile[ReadTile(city, tile)] == CITY_FACILITY_INFO_DEFAULT) return 0;
  return ReadCityFacilityOverlay(city, tile);
}

void RebuildCityAssociationHashes(City *city) {
  CitySigns *signs = &city->signs;
  ++signs->revision;
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (signs->capacity) memset(signs->buckets, 0, signs->capacity * sizeof(u32));
  if (facilities->capacity) memset(facilities->buckets, 0, facilities->capacity * sizeof(u32));
  for (int slot = signs->used - 1; slot > 0; --slot) {
    CitySignRecord *record = &signs->record[slot];
    if (record->tile == INVALID_TILE) continue;
    u32 *head = &signs->buckets[AssociationBucket(record->tile, signs->capacity)];
    record->next = *head; *head = slot;
  }
  for (int slot = facilities->used - 1; slot >= CITY_FACILITY_RECORD_FIRST_DYNAMIC; --slot) {
    CityFacilityDetails *record = &facilities->details[slot];
    if (record->tile == INVALID_TILE || !ReadCityFacilityRecord(city, slot)->type) continue;
    u32 *head = &facilities->buckets[AssociationBucket(record->tile, facilities->capacity)];
    record->next = *head; *head = slot + 1;
  }
}

static void UnlinkCitySign(City *city, int slot) {
  CitySigns *signs = &city->signs;
  TileIndex tile = signs->record[slot].tile;
  if (tile == INVALID_TILE) return;
  u32 *link = &signs->buckets[AssociationBucket(tile, signs->capacity)];
  while (*link && *link != (u32)slot) link = &signs->record[*link].next;
  if (*link) *link = signs->record[slot].next;
  signs->record[slot].next = 0;
}

void LinkCityFacility(City *city, int slot) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (slot < CITY_FACILITY_RECORD_FIRST_DYNAMIC) return;
  CityFacilityDetails *record = &facilities->details[slot];
  u32 *head = &facilities->buckets[AssociationBucket(record->tile, facilities->capacity)];
  record->next = *head;
  *head = slot + 1;
}

static void UnlinkCityFacility(City *city, int slot) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (!facilities->capacity) return;
  CityFacilityDetails *record = &facilities->details[slot];
  if (record->tile == INVALID_TILE) return;
  u32 *link = &facilities->buckets[AssociationBucket(record->tile, facilities->capacity)];
  while (*link && *link != (u32)slot + 1) link = &facilities->details[*link - 1].next;
  if (*link) *link = record->next;
  record->next = 0;
}

u8 ReadCityStockBaseOverlay(const City *city, TileIndex tile) {
  int overlay = ReadCityBaseOverlay(city, tile);
  return overlay <= UINT8_MAX ? overlay : CITY_OVERLAY_NONE;
}

// Zero clears only the stationary effect: signs and facility associations have independent lifetimes.
void WriteOverlay(City *city, TileIndex tile, int value) {
  TileInfo *tp = &city->tile[tile];
  if (!city->native_map_storage) { tp->overlay = value; return; }
  if (IsCitySignOverlay(value)) {
    int slot = CitySignOverlaySlot(value);
    assert(slot < city->signs.capacity);
    UnlinkCitySign(city, slot);
    city->signs.record[slot].tile = tile;
    city->signs.used = IntMax(city->signs.used, slot + 1);
    if (slot == IntMax(city->signs.free_slot, 1)) city->signs.free_slot = slot + 1;
    tp->overlay |= CITY_TILE_HAS_SIGN;
    ++city->signs.revision;
    u32 *head = &city->signs.buckets[AssociationBucket(tile, city->signs.capacity)];
    city->signs.record[slot].next = *head; *head = slot;
  } else if (!value || (value >= CITY_OVERLAY_EVENT_END && value <= CITY_OVERLAY_FIRE)) {
    tp->overlay = (tp->overlay & ~CITY_TILE_EFFECT_MASK) |
        (value ? (value - CITY_OVERLAY_EVENT_END + 1) << CITY_TILE_EFFECT_SHIFT : 0);
  }
}

// Stock association bytes are converted once after geometry and event stacks have been decoded.
bool ImportStockCityAssociations(City *city) {
  if (!ReserveCitySigns(city, CITY_OVERLAY_SIGN_COUNT + 1) || !ReserveCityFacilities(city, CITY_FACILITY_RECORD_COUNT))
    return false;
  for (int slot = 0; slot < city->signs.capacity; ++slot) city->signs.record[slot].tile = INVALID_TILE;
  for (int slot = 0; slot < city->annual_facilities.capacity; ++slot)
    city->annual_facilities.details[slot].tile = INVALID_TILE;
  city->signs.used = 1;
  city->signs.free_slot = 1;
  city->annual_facilities.free_slot = CITY_FACILITY_RECORD_FIRST_DYNAMIC;
  city->annual_facilities.used = CITY_FACILITY_RECORD_COUNT;
  for (TileIndex tile = 0; tile < (u32)city->map_cell_count; ++tile) {
    int overlay = city->tile[tile].overlay;
    city->tile[tile].overlay = 0;
    if (IsCitySignOverlay(overlay)) {
      city->tile[tile].overlay = CITY_TILE_HAS_SIGN;
      city->signs.record[overlay].tile = tile;
      city->signs.used = IntMax(city->signs.used, overlay + 1);
    } else if (IsCityFacilityOverlay(overlay)) {
      int slot = CityFacilityOverlaySlot(overlay);
      TileIndex anchor = ResolveBulldozeFootprint(city, tile).start;
      city->annual_facilities.details[slot].tile = anchor;
      int kind = ReadCityFacilityRecord(city, slot)->type;
      city->annual_facilities.details[slot].years =
          kind >= TK_GAS_POWER_PLANT && kind <= TK_COAL_POWER_PLANT ? ReadCityFacilityRecord(city, slot)->age : 0;
      city->annual_facilities.used = IntMax(city->annual_facilities.used, slot + 1);
    } else if (overlay >= CITY_OVERLAY_EVENT_END)
      city->tile[tile].overlay = (overlay - CITY_OVERLAY_EVENT_END + 1) << CITY_TILE_EFFECT_SHIFT;
  }
  RebuildCityAssociationHashes(city);
  return true;
}

// Clear every fixed UTF-8 overlay-name slot when the loaded file has no label table. Requires initialized City label
// storage; clears the bytes without freeing backing storage or editing host strings.
void ClearCityOverlayNames(City *city) { memset(city->overlay_names.entry, 0, sizeof(city->overlay_names.entry)); }

// Discard the record owned by an overlay identifier. Requires an admitted overlay identifier; clears its name and
// stale event associations without removing unrelated map overlays.
void ClearOverlayAssociation(City *city, int overlay) {
  if (IsCitySignOverlay(overlay)) {
    ++city->signs.revision;
    int slot = CitySignOverlaySlot(overlay);
    if (slot < city->signs.used) {
      TileIndex tile = city->signs.record[slot].tile;
      if (IsMapTileValid(city, tile)) city->tile[tile].overlay &= ~CITY_TILE_HAS_SIGN;
      UnlinkCitySign(city, slot);
      city->signs.record[slot].tile = INVALID_TILE;
    }
    city->signs.free_slot = IntMin(IntMax(city->signs.free_slot, 1), slot);
    CityOverlayName *name = CityOverlayNameAt(city, overlay);
    if (city->fixes & (1 << CITY_FIX_LABEL_CLEANUP)) memset(name, 0, sizeof(*name));
    else name->character[0] = 0;
  } else if (IsCityFacilityOverlay(overlay)) {
    int index = CityFacilityOverlaySlot(overlay);
    if (index >= CITY_FACILITY_RECORD_FIRST_DYNAMIC) {
      UnlinkCityFacility(city, index);
      CityFacilityRecordAt(city, index)->type = TK_CLEAR;
      city->annual_facilities.free_slot = IntMin(
          IntMax(city->annual_facilities.free_slot, CITY_FACILITY_RECORD_FIRST_DYNAMIC), index);
      CityOverlayName *name = CityOverlayNameAt(city, overlay);
      if (city->fixes & (1 << CITY_FIX_LABEL_CLEANUP)) memset(name, 0, sizeof(*name));
      else name->character[0] = 0;
      if (city->annual_facilities.capacity) city->annual_facilities.details[index].tile = INVALID_TILE;
    }
  } else if (IsCityEventOverlay(overlay)) {
    CityMapEvent *event = &city->events.event[CityEventOverlaySlot(overlay)];
    event->previous_overlay = 0;
    WriteOverlay(city, event->tile, CITY_OVERLAY_NONE);
  }
}

int FindFreeOverlayNameSlot(City *city) {
  int limit = city->individual_facilities ? city->map_cell_count : CITY_OVERLAY_SIGN_COUNT;
  for (int slot = city->individual_facilities ? IntMax(city->signs.free_slot, 1) : 1; slot <= limit; ++slot)
    if (slot >= city->signs.used || city->signs.record[slot].tile == INVALID_TILE)
      return CitySignOverlay(slot);
  return 0;
}

bool CityTileHasFacilityLabel(const City *city, TileIndex tile) {
  int overlay = ReadCityFacilityOverlay(city, tile);
  return overlay && ReadCityFacilityRecord(city, CityFacilityOverlaySlot(overlay))->type == ReadTile(city, tile);
}

bool CanEditCityMapSign(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile) || IsTileKindFacility(ReadTile(city, tile))) return false;
  if (city->individual_facilities) return true;
  int overlay = ReadOverlay(city, tile);
  return !overlay || IsCitySignOverlay(overlay);
}

// Decode an optional leading sign-color byte from terminated label text. Text must have at least a readable first
// byte; zero means no explicit supported color prefix.
int CitySignTextColor(const char *text) {
  int color = (u8)text[0];
  return color > 0 && color < CITY_SIGN_COLOR_COUNT ? color : 0;
}

// Return the display-name portion of a sign, skipping a supported color prefix. Result borrows the supplied terminated
// string and remains valid only for that string's lifetime.
const char *CitySignTextName(const char *text) {
  return text + (CitySignTextColor(text) != 0);
}

// Native sign actions resolve sparse records without a dialog continuation. CITY_COMMAND_TEST queries
// admission without applying effects; execution returns the action result or a negative error.
i64 CmdRenameCitySign(City *city, u32 flags, TileIndex tile, int slot, const u8 *text, int length) {
  if ((flags & ~CITY_COMMAND_TEST) || length < 0 || length > CITY_OVERLAY_NAME_MAX_LENGTH ||
      (length && (!text || memchr(text, 0, length)))) return CITY_COMMAND_COST_INVALID;
  if (IsMapTileValid(city, tile) && !CanEditCityMapSign(city, tile)) {
    return -CITY_NOTICE_CANNOT_PLACE_LABEL_HERE;
  }
  if (!IsMapTileValid(city, tile) || (slot && !IsCitySignOverlay(slot)) ||
      ReadCitySignOverlay(city, tile) != slot) return CITY_COMMAND_COST_INVALID;
  // An empty visible name deletes the sign, even if a caller supplied just its color.
  if (length == 1 && CitySignTextColor((const char *)text)) length = 0;
  if (!slot && !length) return flags & CITY_COMMAND_TEST ? 0 : true;
  if (!slot) slot = FindFreeOverlayNameSlot(city);
  if (!slot) {
    return -CITY_NOTICE_SIGN_LIMIT_REACHED;
  }
  if (flags & CITY_COMMAND_TEST) return 0;
  if (!ReserveCitySigns(city, CitySignOverlaySlot(slot) + 1)) return CITY_COMMAND_COST_BLOCKED;
  InvalidateTile(city, tile);
  CityOverlayName *name = CityOverlayNameAt(city, slot);
  memset(name->character, 0, sizeof(name->character));
  if (length) memcpy(name->character, text, length);
  if (length) WriteOverlay(city, tile, slot);
  else ClearOverlayAssociation(city, slot);
  InvalidateTile(city, tile);
  return true;
}

// Validate or move a captured sign slot from its source to the resolved destination. Rejects invalid flags, tiles or
// stale source overlays; TEST is read-only and execution transfers text association and damage together.
i64 CmdMoveCitySign(City *city, u32 flags, TileIndex tile, TileIndex destination, int slot) {
  if ((flags & ~CITY_COMMAND_TEST) || !IsMapTileValid(city, tile) || !IsMapTileValid(city, destination) ||
      !IsCitySignOverlay(slot) || ReadCitySignOverlay(city, tile) != slot)
    return CITY_COMMAND_COST_INVALID;
  if (tile == destination) return flags & CITY_COMMAND_TEST ? 0 : true;
  if (ReadCitySignOverlay(city, destination) || IsTileKindFacility(ReadTile(city, destination)) ||
      (!city->individual_facilities && ReadOverlay(city, destination))) {
    if (!(flags & CITY_COMMAND_TEST)) PlayCitySound(city, destination, CITY_SOUND_PLACEMENT_REJECTED);
    return CITY_COMMAND_COST_BLOCKED;
  }
  if (flags & CITY_COMMAND_TEST) return 0;
  InvalidateTile(city, tile);
  InvalidateTile(city, destination);
  city->tile[tile].overlay &= ~CITY_TILE_HAS_SIGN;
  WriteOverlay(city, destination, slot);
  InvalidateTile(city, tile);
  InvalidateTile(city, destination);
  return true;
}

// Resolve an existing sign slot or find a free one for a valid tile. Returns the slot or a negative label notice; this
// selection alone does not commit text or an overlay.
static int CityMapLabelSlot(City *city, TileIndex tile) {
  int slot = ReadCitySignOverlay(city, tile);
  if (!CanEditCityMapSign(city, tile)) return -CITY_NOTICE_CANNOT_PLACE_LABEL_HERE;
  if (slot == 0)
    slot = FindFreeOverlayNameSlot(city);
  if (slot && !ReserveCitySigns(city, CitySignOverlaySlot(slot) + 1)) return -CITY_NOTICE_SIGN_LIMIT_REACHED;
  return slot ? slot : -CITY_NOTICE_SIGN_LIMIT_REACHED;
}

// Commit a previously admitted label choice using the captured slot and bounded text state. Tile and slot must be
// valid; cancellation clears the overlay, and presentation damage is the caller's responsibility.
static void FinishCityMapLabelChoice(City *city, TileIndex tile, int slot,
    const CityNameCommandInput *state, int answer) {
  if (answer == CITY_DIALOG_CANCEL) {
    if (ReadCitySignOverlay(city, tile)) ClearOverlayAssociation(city, ReadCitySignOverlay(city, tile));
  } else if (state->length != 0) {
    WriteOverlay(city, tile, slot);
    memcpy(CityOverlayNameAt(city, slot)->character, state->name.character, state->length);
    CityOverlayNameAt(city, slot)->character[state->length] = 0;
  } else {
    ClearOverlayAssociation(city, slot);
    WriteOverlay(city, tile, CITY_OVERLAY_NONE);
  }
}

// Resolve the label slot and apply a captured text answer to a valid tile. Invalidates its drawing and returns a
// negative notice on slot failure; the text state is borrowed and copied into City storage.
i64 ApplyCityMapLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer) {
  InvalidateTile(city, tile);
  int slot = CityMapLabelSlot(city, tile);
  if (slot < 0) {
    return slot;
  }
  FinishCityMapLabelChoice(city, tile, slot, state, answer);
  return true;
}

// Commit a staged map-label text answer at a valid tile. Requires an active bounded text payload and OK/cancel choice;
// TEST validates only and execution returns label-slot or placement errors.
i64 CmdEditMapLabel(City *city, u32 flags, TileIndex tile, int answer) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, tile) ||
      (answer != CITY_DIALOG_OK && answer != CITY_DIALOG_CANCEL))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  const GameObservation *observer = city->game->observation;
  i64 applied = 1;
  if (observer && observer->edit_map_label) observer->edit_map_label(city, tile);
  else applied = ApplyCityMapLabelChoice(city, tile, &city->command_name, answer);
  RefreshCityMapViews(city->game);
  return applied;
}

// Retained comparison caller shares allocation and choice effects.
void EditCityOverlayNameAtTile(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  InvalidateTile(city, tile);
  int slot = CityMapLabelSlot(city, tile);
  if (slot < 0) {
    ShowCityMessage(city, -slot);
    return;
  }
  CityNameCommandInput state = {.name = (*CityOverlayNameAt(city, slot))};
  while (state.length < CITY_OVERLAY_NAME_MAX_LENGTH && state.name.character[state.length] != 0)
    ++state.length;
  int answer = QueryCityOverlayNameDialog(city, slot, &state);
  FinishCityMapLabelChoice(city, tile, slot, &state, answer);
}

// Apply a captured facility-name answer to the tile's live facility record. Requires a valid facility overlay and
// bounded text state; ranking callbacks may run before the live record is read again.
void ApplyCityFacilityLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer) {
  InvalidateTile(city, tile);
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!city->individual_facilities && slot == CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY)
    UpdateCityNewspaperRanksAndF3Records(city);
  if (CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type == TK_CLEAR)
    ReportCityTileInspection(city, tile);
  else if (answer == CITY_DIALOG_OK)
    memcpy(CityOverlayNameAt(city, slot)->character, state->name.character, state->length + 1);
}

// Commit a staged text answer for the selected facility's label. Requires the active bounded name payload and
// supported answer; TEST checks admission and execution reads the live facility state after callbacks.
i64 CmdEditFacilityLabel(City *city, u32 flags, TileIndex tile, int answer) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, tile) ||
      (answer != CITY_DIALOG_OK && answer != CITY_DIALOG_CANCEL))
    return invalid;
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!IsCityFacilityOverlay(slot))
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  const GameObservation *observer = city->game->observation;
  if (observer && observer->edit_facility_label) observer->edit_facility_label(city, tile);
  else ApplyCityFacilityLabelChoice(city, tile, &city->command_name, answer);
  RefreshCityMapViews(city->game);
  return 1;
}

// Retained diagnostic caller; overlay 111 refreshes F3 records first.
void EditCityOverlayLabelAtCell(City *city, TileIndex tile) {
  assert(IsMapTileValid(city, tile));
  InvalidateTile(city, tile);
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!city->individual_facilities && slot == CITY_OVERLAY_NEWSPAPER_RANKING_FACILITY)
    UpdateCityNewspaperRanksAndF3Records(city);
  if (CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type == TK_CLEAR) {
    ShowCityTileInformationDialog(city, tile);
  } else {
    CityNameCommandInput state;
    state.name = (*CityOverlayNameAt(city, slot));
    state.length = (int)strlen((const char *)state.name.character);
    QueryCityOverlayNameDialog(city, slot, &state);
    // The original copies the dialog text back regardless of its return code.
    memcpy(CityOverlayNameAt(city, slot)->character, state.name.character,
        strlen((const char *)state.name.character) + 1);
  }
}

// Copy the selected team's name and update the stadium's live record. Tile must still have a facility overlay and team
// must select the team-name table; the caller validates the captured choice before entry.
static void SetCityStadiumTeam(City *city, TileIndex tile, int team, const char *name) {
  CopyText((char *)CityOverlayNameAt(city, CITY_TEAM_NAME_FIRST + team)->character,
      sizeof(CityOverlayNameAt(city, 0)->character), name);
  int slot = ReadCityFacilityOverlay(city, tile);
  CityAnnualFacilityRecord *stadium = CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot));
  stadium->stadium_team = team;
  stadium->stadium_team_name_slot = CITY_TEAM_NAME_FIRST + team;
  city->infrastructure.stadium_team_mask |= 1u << team;
}

// Native commands report rejection; their UI caller presents it after command execution.
void ApplyCityStadiumTeamChoice(City *city, TileIndex tile, int team, const char *name) {
  assert(IsMapTileValid(city, tile));
  const GameObservation *observer = city->game->observation;
  if (observer && observer->accept_stadium_team) {
    observer->accept_stadium_team(city, tile, team, name);
    return;
  }
  if (team == -1) {
    PlayCitySound(city, tile, CITY_SOUND_PLACEMENT_REJECTED);
    return;
  }
  SetCityStadiumTeam(city, tile, team, name);
  return;
}

// Assign the requested team index to the live stadium using the staged bounded name. TEST validates the tile,
// stadium and index without effects; -1 preserves the rejection sound/error instead of assigning a team.
i64 CmdAssignStadiumTeam(City *city, u32 flags, TileIndex tile, int team) {
  int invalid = CITY_COMMAND_COST_INVALID;
  if (!city->command_name_active || !IsMapTileValid(city, tile) || team < -1 || team >= CITY_TEAM_NAME_COUNT)
    return invalid;
  int slot = ReadCityFacilityOverlay(city, tile);
  if (!IsCityFacilityOverlay(slot) ||
      CityFacilityRecordAt(city, CityFacilityOverlaySlot(slot))->type != TK_STADIUM)
    return invalid;
  if (flags & CITY_COMMAND_TEST) return 0;
  ApplyCityStadiumTeamChoice(city, tile, team, (const char *)city->command_name.name.character);
  return team == -1 ? -CITY_NOTICE_SELECT_VALID_STADIUM_SPORT : 1;
}

// Diagnostic adapter retains the original rejection caller until dismissal.
void AcceptCityStadiumTeamDialog(City *city, TileIndex tile, int team, const char *name) {
  assert(IsMapTileValid(city, tile));
  if (team == -1) {
    PlayCitySound(city, tile, CITY_SOUND_PLACEMENT_REJECTED);
    ShowCityMessage(city, CITY_NOTICE_SELECT_VALID_STADIUM_SPORT);
    return;
  }
  SetCityStadiumTeam(city, tile, team, name);
}
