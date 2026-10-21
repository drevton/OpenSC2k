#ifndef SC2K_DOMAIN_SRC_DRAWING_MAPS_TABLES_H
#define SC2K_DOMAIN_SRC_DRAWING_MAPS_TABLES_H

#include "assets/graphics.h"
#include "city/maps/tile_storage.h"
#include "city/types.h"

// Native kind to base Drawing cache slot; resource image bindings remain replaceable.
extern const u16 kCityMapKindSprites[TK_COUNT];

// These predicates describe geometric layouts, independently of resource sprite indexes.
static inline bool IsCityMapLineLayout(int sprite, int first) {
  return sprite >= first && sprite < first + 15;
}
static inline bool IsCityMapSurfaceLayout(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_ROAD_FIRST && sprite <= CITY_MAP_TILE_SPRITE_RAIL_TRANSITION_LAST;
}
static inline bool IsCityMapHighwayLayout(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST && sprite <= CITY_MAP_TILE_SPRITE_HIGHWAY_LAST;
}
static inline bool IsCityMapMultiHighwayLayout(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_HIGHWAY_MULTI_FIRST && sprite <= CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_LAST;
}
static inline bool IsCityMapRailSlopeLayout(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_RAIL_SLOPE_FIRST && sprite <= CITY_MAP_TILE_SPRITE_RAIL_SLOPE_LAST;
}
static inline bool IsCityMapRailTransitionLayout(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_RAIL_TRANSITION_FIRST && sprite <= CITY_MAP_TILE_SPRITE_RAIL_TRANSITION_LAST;
}
static inline int CityMapLineSlopeLayout(int first, int shape) { return first + shape + 1; }
static inline int CityMapRailFlatLayout(int sprite) {
  if (IsCityMapRailSlopeLayout(sprite)) return CITY_MAP_TILE_SPRITE_RAIL_FIRST + !(sprite & 1);
  if (IsCityMapRailTransitionLayout(sprite)) return CITY_MAP_TILE_SPRITE_RAIL_FIRST + (sprite & 1);
  return sprite;
}
static inline int CityMapHighwayLayoutAxis(int sprite) {
  return sprite >= CITY_MAP_TILE_SPRITE_HIGHWAY_BRIDGE_FIRST ? 0 :
      sprite <= CITY_MAP_TILE_SPRITE_HIGHWAY_LAST ? (sprite - CITY_MAP_TILE_SPRITE_HIGHWAY_FIRST) & 1 : sprite & 1;
}

// Event Tables

// Native immutable values; no executable mapping is needed.
extern const u16 kCityMapEventBaseSprite[17];
extern const u8 kCityMapEventMinZoom[17];
extern const u8 kCityMapEventSpriteVerticalOffset[24];
extern const u8 kCityMapEventSpriteHeight[24];
extern const u8 kCityMapEventSpriteHalfWidth[24];
extern const u8 kCityMapEventDirectionSprite[8];
extern const u8 kCityMapEventDirectionFlip[8];
extern const u8 kCityMapEventDefaultSprite[4];
extern const u8 kCityMapEventDefaultFlip[4];
extern const u8 kCityMapEventXDivisor[4];
extern const u8 kCityMapEventYDivisor[4];

typedef struct {
  i8 upper_span_x[2];
  i8 upper_adjust_x[2];
  i8 upper_span_y[2];
  i8 upper_adjust_y[2];
  i8 lower_span_x[2];
  i8 lower_adjust_x[2];
  i8 lower_span_y[2];
  i8 lower_adjust_y[2];
} CityEventStructureOffsets;

extern const CityEventStructureOffsets kCityEventStructureOffsets;

// Sprite Tables

enum { CITY_TERRAIN_SPRITE_COUNT = 70 };
extern const u16 kTerrainSpriteByTerrainCode[CITY_TERRAIN_SPRITE_COUNT];

// Invalid saved terrain codes have no sprite; never read neighboring renderer state.
static inline int CityTerrainSprite(int terrain) {
  return (unsigned)terrain < CITY_TERRAIN_SPRITE_COUNT ? kTerrainSpriteByTerrainCode[terrain] : 0;
}

// Terrain Tables

// Offset in the zone-ground sprite bank, shared by all three zooms.
static inline int CityZoneGroundSpriteOffset(int zone) {
  static const u8 offsets[16] = {3, 0, 0, 1, 1, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4};
  return offsets[zone];
}

#endif
