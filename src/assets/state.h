#ifndef SC2K_RESOURCE_STATE_H
#define SC2K_RESOURCE_STATE_H

#include "city/economy/types.h"
#include "assets/resource_types.h"
#include "city/engine/city_types.h"
#include "city/maps/tile_storage.h"
#include <stddef.h>

#include "assets/ids.h"
#include "assets/fonts.h"
#include "assets/graphics.h"
#include "city/types.h"
#include "assets/cursors.h"

// DATA resources contain 80 words plus the loader's two zero padding words.
enum { CITY_RANKING_RESOURCE_WORDS = 82 };

typedef struct {
  i16 event_severity[CITY_RANKING_RESOURCE_WORDS];
  u16 event_decay[CITY_RANKING_RESOURCE_WORDS];
} CityNewsRankingResources;

typedef struct { i16 value[256]; } CityNewspaperTemplateGroups;
typedef struct { u16 template_id[256]; } CityNewspaperTokenExpansions;
enum { CITY_NEWSPAPER_PICTURE_FIRST = 400, CITY_NEWSPAPER_PICTURE_COUNT = 12 };

enum { CITY_RESOURCE_NAME_CAPACITY = 32 };
typedef struct {
  char full[CITY_MONTH_COUNT][CITY_RESOURCE_NAME_CAPACITY];
  char abbreviated[CITY_MONTH_COUNT][CITY_RESOURCE_NAME_CAPACITY];
} CityCalendarNames;

typedef struct {
  const char *headline, *body;
  i16 severity;
  u16 decay;
} CityNewspaperArticle;

typedef struct CityNewspaperResources {
  const CityNewspaperArticle *articles;
  u8 *text;
  u32 *offsets;
  size_t text_capacity, offsets_capacity;
  CityNewspaperTemplateGroups group_starts, group_counts;
  CityNewspaperTokenExpansions token_expansions;
} CityNewspaperResources;

// Resource readiness belongs to the asset owner, not a simulated city.
typedef struct CityResourceState {
  ByteSpan asset_spans[CITY_ASSET_COUNT];
  u8 *asset_blocks[CITY_ASSET_COUNT + 2];
  u8 asset_block_count;
  bool asset_packs_loaded; // Required byte spans are ready; image/font decoding follows.
  struct RunSprite *advisors[CITY_ADVISOR_PORTRAIT_COUNT];
  struct RunSprite *illustrations[CITY_NEWSPAPER_PICTURE_COUNT + 1];
  CityInterfaceIcons bridge_choice_icons, main_toolbar_icons, map_category_icons;
  CityCursorAssets cursors;
  CityMusicAssets music_buttons;
  CityInterfaceFont interface_font;
  CityOverlayFont overlay_font;
  CityIntroArt intro;
  u32 news_resources_unavailable; // Nonzero blocks news ranking and page composition after loading failure.
  CityNewsRankingResources rankings;
  CityNewspaperResources newspaper;
  CityCalendarNames calendar_names;
  const char *facility_information[CITY_FACILITY_INFO_COUNT][CITY_FACILITY_INFO_LINE_COUNT];
  struct RunSprite *neighbors[CITY_NEIGHBOR_ART_COUNT];
  MapSpriteMetrics map_sprite_metrics[CITY_MAP_SPRITE_COUNT];
  CityLandscapeSprite *landscape;
  u16 landscape_count;
  // Borrowed little-endian table bytes, retained for the Assets lifetime.
  // Pack members need not align these tables.
  const u8 *map_sprite_sources;
  const u8 *map_sprite_rotations;
  const u8 *sprite_index_lookup;
  SpriteVariants sprite_variants;
  u16 map_tile_sprite_count, map_group_sprite_first;
  u32 map_group_sprite_count;
  u32 map_sprite_index_count;
  CityPowerSmoke power_smoke[CITY_POWER_SMOKE_KINDS][CITY_CIVIC_VIEWS];
  CityBuildingArtwork buildings[CITY_BUILDING_ART_COUNT];
  // Maximum above any footprint tile, across all views and construction frames.
  u8 kind_sprite_heights[TK_COUNT];
  u8 civic_ground[CITY_CIVIC_ART_COUNT];
  CityLandscapeSprite demolition[CITY_DEMOLITION_FRAME_COUNT];
  // Shared visible mature-tree extents for temperate, arctic and tropical previews.
  CityRect tree_preview_bounds[3];
  u32 landscape_water_colors[5];
  u32 bridge_palettes[CITY_LANDSCAPE_BRIDGE_STYLES][256];
  u32 road_vehicle_palettes[CITY_ROAD_VEHICLE_PALETTES][256];
  u32 underworld_palettes[CITY_UNDERWORLD_GROUND_STYLES][256];
  u32 sprite_revision;
} CityResourceState;

#endif
