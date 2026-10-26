// TileInfo signs, facility associations and stationary effects (zero-based bits):
// overlay[0] records sign presence; its identity/text is in City's sparse sign records, not in the byte.
// overlay[1..4] stores one effect: 0 none, 10 border connection, 11 pollution, 12 flood, 13/14 riots, 15 fire.
// Nonzero codes project to original overlay value 240 + code; 1..9 have no named native effect. Bits 5..7 are
// unassigned. Moving events and facility identities live in separate City records indexed by tile.
// ReadCityBaseOverlay resolves effect first, then sign, then facility; WriteOverlay(..., 0) clears only the effect.
// Sign removal clears bit 0 explicitly. Other TileInfo bytes keep their shared/family meanings from tile_storage.h.

#include "city/maps/labels.h"

#include "city/events/events.h"
#include "city/economy/facilities.h"
#include "audio/types.h"
#include "city/engine/command_types.h"
#include "city/maps/map_types.h"
#include "city/maps/kinds.h"
#include "city/maps/tile_storage.h"
#include "city/presentation.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "city/state.h"
#include "city/engine/commands.h"
#include "city/maps/buildings.h"
#include "city/maps/map.h"
#include "game/state.h"
#include "game/observation.h"
#include "base/text.h"

// Sparse map associations. Growth requests are bounded by map area plus reserved slots; allocated capacity
// rounds up to a power of two and can exceed that limit. Existing capacity remains reusable without validation.
// Hashing requires a nonzero power-of-two capacity, as allocated by the reservation functions.
// Hash links terminate at zero. Sign links store the slot directly; facility links store slot + 1.
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

// Index live sign slots into empty buckets. Snapshot the separate arrays and capacity before writing links;
// descending insertion keeps each bucket's traversal in ascending slot order.
static void IndexCitySignRecords(CitySigns *signs) {
  CitySignRecord *records = signs->record;
  u32 *buckets = signs->buckets;
  int capacity = signs->capacity;
  for (int slot = signs->used - 1; slot > 0; --slot) {
    CitySignRecord *record = &records[slot];
    if (record->tile == INVALID_TILE) continue;
    u32 *head = &buckets[AssociationBucket(record->tile, capacity)];
    record->next = *head;
    *head = slot;
  }
}

// Index live dynamic facilities into empty buckets using the same descending order. Reserved shared slots
// are selected by class and remain outside the hash; cleared records retain their unused link fields.
static void IndexCityFacilityRecords(City *city) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  CityFacilityDetails *details = facilities->details;
  u32 *buckets = facilities->buckets;
  int capacity = facilities->capacity;
  for (int slot = facilities->used - 1; slot >= CITY_FACILITY_RECORD_FIRST_DYNAMIC; --slot) {
    CityFacilityDetails *detail = &details[slot];
    if (detail->tile == INVALID_TILE || !ReadCityFacilityRecord(city, slot)->type) continue;
    u32 *head = &buckets[AssociationBucket(detail->tile, capacity)];
    detail->next = *head;
    *head = slot + 1;
  }
}

bool ReserveCitySigns(City *city, u32 requested) {
  CitySigns *signs = &city->signs;
  if (requested <= (u32)signs->capacity) return true;
  if (requested > city->map_cell_count + 1) return false;
  // The admitted count fits int at every supported map size, including the reserved slot.
  int capacity = IntMax(signs->capacity, 64);
  while (capacity < (int)requested) capacity *= 2;
  CitySignRecord *records = calloc(capacity, sizeof(*records));
  u32 *buckets = calloc(capacity, sizeof(*buckets));
  if (!records || !buckets) {
    free(records);
    free(buckets);
    return false;
  }
  for (int slot = 0; slot < capacity; ++slot) records[slot].tile = INVALID_TILE;
  if (signs->used) memcpy(records, signs->record, signs->used * sizeof(*records));
  // Publish only after all replacement storage is ready. The old sparse borrows end here.
  free(signs->record);
  free(signs->buckets);
  signs->record = records;
  signs->buckets = buckets;
  signs->capacity = capacity;
  if (!signs->used) signs->used = 1;
  IndexCitySignRecords(signs);
  return true;
}

bool ReserveCityFacilities(City *city, u32 requested) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (requested <= (u32)facilities->capacity) return true;
  if (requested > city->map_cell_count + CITY_FACILITY_RECORD_RESERVED_COUNT) return false;
  // Keep the fixed record prefix in City; only the admitted extra records need replacement storage.
  int capacity = IntMax(facilities->capacity, 256);
  while (capacity < (int)requested) capacity *= 2;
  CityAnnualFacilityRecord *extra = calloc(capacity - CITY_FACILITY_RECORD_COUNT, sizeof(*extra));
  CityFacilityDetails *details = calloc(capacity, sizeof(*details));
  u32 *buckets = calloc(capacity, sizeof(*buckets));
  if (!extra || !details || !buckets) {
    free(extra);
    free(details);
    free(buckets);
    return false;
  }
  for (int slot = 0; slot < capacity; ++slot) details[slot].tile = INVALID_TILE;
  if (facilities->capacity) {
    memcpy(details, facilities->details, facilities->capacity * sizeof(*details));
    memcpy(extra, facilities->extra, (facilities->capacity - CITY_FACILITY_RECORD_COUNT) * sizeof(*extra));
  }
  // Commit the sparse arrays after all allocations and copies succeed; fixed records stay in City.
  free(facilities->extra);
  free(facilities->details);
  free(facilities->buckets);
  facilities->extra = extra;
  facilities->details = details;
  facilities->buckets = buckets;
  facilities->capacity = capacity;
  if (!facilities->used) facilities->used = CITY_FACILITY_RECORD_RESERVED_COUNT;
  IndexCityFacilityRecords(city);
  return true;
}

// Release sparse storage without touching the fixed facility records. Shared-facility new-city resets preserve
// unused payload fields in those records; final release clears them as well.
static void ReleaseCityAssociationStorage(City *city) {
  free(city->signs.record);
  free(city->signs.buckets);
  city->signs = (CitySigns){0};
  CityAnnualFacilities *facilities = &city->annual_facilities;
  free(facilities->extra);
  free(facilities->details);
  free(facilities->buckets);
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
  const CitySigns *signs = &city->signs;
  if (!(city->tile[tile].overlay & CITY_TILE_HAS_SIGN) || !signs->capacity) return 0;
  for (u32 slot = signs->buckets[AssociationBucket(tile, signs->capacity)]; slot;) {
    const CitySignRecord *record = &signs->record[slot];
    if (record->tile == tile) return CitySignOverlay(slot);
    slot = record->next;
  }
  return 0;
}

int ReadCityFacilityOverlay(const City *city, TileIndex tile) {
  int kind = city->tile[tile].kind;
  if (!IsTileKindFacility(kind)) return 0;
  int facility_class = kFacilityClassByTile[kind];
  if (!city->individual_facilities && facility_class >= CITY_FACILITY_INFO_SHARED_RECORD_FIRST)
    return CityFacilityOverlay(facility_class - (CITY_FACILITY_INFO_SHARED_RECORD_FIRST - 1));
  const CityAnnualFacilities *facilities = &city->annual_facilities;
  if (!facilities->capacity) return 0;
  TileIndex anchor = GetBuildingStartTile(city, tile);
  if (anchor == INVALID_TILE) return 0;
  for (u32 link = facilities->buckets[AssociationBucket(anchor, facilities->capacity)]; link;) {
    u32 slot = link - 1;
    const CityFacilityDetails *detail = &facilities->details[slot];
    if (detail->tile == anchor && ReadCityFacilityRecord(city, slot)->type != TK_CLEAR)
      return CityFacilityOverlay(slot);
    link = detail->next;
  }
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
  const TileInfo *tp = &city->tile[tile];
  if (!city->native_map_storage) return tp->overlay;
  int effect = (tp->overlay & CITY_TILE_EFFECT_MASK) >> CITY_TILE_EFFECT_SHIFT;
  if (effect) return CITY_OVERLAY_EVENT_END + effect - 1;
  int sign = ReadCitySignOverlay(city, tile);
  if (sign) return sign;
  // New records for ordinary utilities must not change legacy event/disaster admission.
  if (city->individual_facilities && kFacilityClassByTile[tp->kind] == CITY_FACILITY_INFO_DEFAULT) return 0;
  return ReadCityFacilityOverlay(city, tile);
}

// Descending insertion makes each rebuilt bucket traverse slots in ascending order.
// Reserved shared facility slots are selected by class rather than indexed in the hash.
void RebuildCityAssociationHashes(City *city) {
  CitySigns *signs = &city->signs;
  ++signs->revision;
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (signs->capacity) memset(signs->buckets, 0, signs->capacity * sizeof(u32));
  if (facilities->capacity) memset(facilities->buckets, 0, facilities->capacity * sizeof(u32));
  IndexCitySignRecords(signs);
  IndexCityFacilityRecords(city);
}

static void UnlinkCitySign(City *city, int slot) {
  CitySigns *signs = &city->signs;
  CitySignRecord *record = &signs->record[slot];
  TileIndex tile = record->tile;
  if (tile == INVALID_TILE) return;
  u32 *link = &signs->buckets[AssociationBucket(tile, signs->capacity)];
  while (*link && *link != (u32)slot) link = &signs->record[*link].next;
  if (*link) *link = record->next;
  record->next = 0;
}

void LinkCityFacility(City *city, int slot) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (slot < CITY_FACILITY_RECORD_FIRST_DYNAMIC) return;
  CityFacilityDetails *detail = &facilities->details[slot];
  u32 *head = &facilities->buckets[AssociationBucket(detail->tile, facilities->capacity)];
  detail->next = *head;
  *head = slot + 1;
}

static void UnlinkCityFacility(City *city, int slot) {
  CityAnnualFacilities *facilities = &city->annual_facilities;
  if (!facilities->capacity) return;
  CityFacilityDetails *detail = &facilities->details[slot];
  if (detail->tile == INVALID_TILE) return;
  u32 *link = &facilities->buckets[AssociationBucket(detail->tile, facilities->capacity)];
  while (*link && *link != (u32)slot + 1) link = &facilities->details[*link - 1].next;
  if (*link) *link = detail->next;
  detail->next = 0;
}

u8 ReadCityStockBaseOverlay(const City *city, TileIndex tile) {
  int overlay = ReadCityBaseOverlay(city, tile);
  return overlay <= UINT8_MAX ? overlay : CITY_OVERLAY_NONE;
}

// Zero clears only the stationary effect: signs and facility associations have independent lifetimes.
void WriteOverlay(City *city, TileIndex tile, int value) {
  TileInfo *tp = &city->tile[tile];
  if (!city->native_map_storage) {
    tp->overlay = value;
    return;
  }
  if (IsCitySignOverlay(value)) {
    CitySigns *signs = &city->signs;
    int slot = CitySignOverlaySlot(value);
    assert(slot < signs->capacity);
    UnlinkCitySign(city, slot);
    CitySignRecord *record = &signs->record[slot];
    record->tile = tile;
    signs->used = IntMax(signs->used, slot + 1);
    if (slot == IntMax(signs->free_slot, 1)) signs->free_slot = slot + 1;
    tp->overlay |= CITY_TILE_HAS_SIGN;
    ++signs->revision;
    u32 *head = &signs->buckets[AssociationBucket(tile, signs->capacity)];
    record->next = *head;
    *head = slot;
  } else if (!value || (value >= CITY_OVERLAY_EVENT_END && value <= CITY_OVERLAY_FIRE)) {
    tp->overlay = (tp->overlay & ~CITY_TILE_EFFECT_MASK) |
        (value ? (value - CITY_OVERLAY_EVENT_END + 1) << CITY_TILE_EFFECT_SHIFT : 0);
  }
}

// Clear every fixed UTF-8 overlay-name slot when the loaded file has no label table. Requires initialized City label
// storage; clears the bytes without freeing backing storage or editing host strings.
void ClearCityOverlayNames(City *city) { memset(city->overlay_names.entry, 0, sizeof(city->overlay_names.entry)); }

// Discard the record owned by an overlay identifier. Requires an admitted overlay identifier; clears its name and
// stale event associations without removing unrelated map overlays.
void ClearOverlayAssociation(City *city, int overlay) {
  if (IsCitySignOverlay(overlay)) {
    CitySigns *signs = &city->signs;
    ++signs->revision;
    int slot = CitySignOverlaySlot(overlay);
    if (slot < signs->used) {
      CitySignRecord *record = &signs->record[slot];
      TileIndex tile = record->tile;
      if (IsMapTileValid(city, tile)) city->tile[tile].overlay &= ~CITY_TILE_HAS_SIGN;
      UnlinkCitySign(city, slot);
      record->tile = INVALID_TILE;
    }
    signs->free_slot = IntMin(IntMax(signs->free_slot, 1), slot);
    CityOverlayName *name = CityOverlayNameAt(city, overlay);
    if (city->fixes & (1ull << CITY_FIX_LABEL_CLEANUP)) memset(name, 0, sizeof(*name));
    else name->character[0] = 0;
  } else if (IsCityFacilityOverlay(overlay)) {
    int slot = CityFacilityOverlaySlot(overlay);
    if (slot >= CITY_FACILITY_RECORD_FIRST_DYNAMIC) {
      CityAnnualFacilities *facilities = &city->annual_facilities;
      UnlinkCityFacility(city, slot);
      CityFacilityRecordAt(city, slot)->type = TK_CLEAR;
      facilities->free_slot = IntMin(IntMax(facilities->free_slot, CITY_FACILITY_RECORD_FIRST_DYNAMIC), slot);
      CityOverlayName *name = CityOverlayNameAt(city, overlay);
      if (city->fixes & (1ull << CITY_FIX_LABEL_CLEANUP)) memset(name, 0, sizeof(*name));
      else name->character[0] = 0;
      if (facilities->capacity) facilities->details[slot].tile = INVALID_TILE;
    }
  } else if (IsCityEventOverlay(overlay)) {
    CityMapEvent *event = &city->events.event[CityEventOverlaySlot(overlay)];
    event->previous_overlay = 0;
    WriteOverlay(city, event->tile, CITY_OVERLAY_NONE);
  }
}

int FindFreeOverlayNameSlot(const City *city) {
  const CitySigns *signs = &city->signs;
  int limit = city->individual_facilities ? city->map_cell_count : CITY_OVERLAY_SIGN_COUNT;
  for (int slot = city->individual_facilities ? IntMax(signs->free_slot, 1) : 1; slot <= limit; ++slot)
    if (slot >= signs->used || signs->record[slot].tile == INVALID_TILE)
      return CitySignOverlay(slot);
  return 0;
}

bool CityTileHasFacilityLabel(const City *city, TileIndex tile) {
  int overlay = ReadCityFacilityOverlay(city, tile);
  return overlay && ReadCityFacilityRecord(city, CityFacilityOverlaySlot(overlay))->type == city->tile[tile].kind;
}

bool CanEditCityMapSign(const City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile) || IsTileKindFacility(city->tile[tile].kind)) return false;
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
  if (ReadCitySignOverlay(city, destination) || IsTileKindFacility(city->tile[destination].kind) ||
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
int PrepareCityMapLabel(City *city, TileIndex tile) {
  int slot = ReadCitySignOverlay(city, tile);
  if (!CanEditCityMapSign(city, tile)) return -CITY_NOTICE_CANNOT_PLACE_LABEL_HERE;
  if (slot == 0)
    slot = FindFreeOverlayNameSlot(city);
  if (slot && !ReserveCitySigns(city, CitySignOverlaySlot(slot) + 1)) return -CITY_NOTICE_SIGN_LIMIT_REACHED;
  return slot ? slot : -CITY_NOTICE_SIGN_LIMIT_REACHED;
}

// Commit a previously admitted label choice using the captured slot and bounded text state. Tile and slot must be
// valid; cancellation clears the overlay, and presentation damage is the caller's responsibility.
void FinishCityMapLabelChoice(City *city, TileIndex tile, int slot,
    const CityNameCommandInput *state, int answer) {
  if (answer == CITY_DIALOG_CANCEL) {
    int current = ReadCitySignOverlay(city, tile);
    if (current) ClearOverlayAssociation(city, current);
  } else if (state->length != 0) {
    WriteOverlay(city, tile, slot);
    CityOverlayName *name = CityOverlayNameAt(city, slot);
    memcpy(name->character, state->name.character, state->length);
    name->character[state->length] = 0;
  } else {
    ClearOverlayAssociation(city, slot);
    WriteOverlay(city, tile, CITY_OVERLAY_NONE);
  }
}

// Resolve the label slot and apply a captured text answer to a valid tile. Invalidates its drawing and returns a
// negative notice on slot failure; the text state is borrowed and copied into City storage.
static i64 ApplyCityMapLabelChoice(City *city, TileIndex tile, const CityNameCommandInput *state, int answer) {
  InvalidateTile(city, tile);
  int slot = PrepareCityMapLabel(city, tile);
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
