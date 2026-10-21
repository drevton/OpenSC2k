#ifndef SC2K_DRAWING_STATE_H
#define SC2K_DRAWING_STATE_H

#include "city/maps/tile_storage.h"
#include "city/engine/city_types.h"
#include "ui/sdl.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_render.h"
#include "drawing/raster/screen_damage.h"
#include "drawing/bitmaps.h"
#include "drawing/sprites/sprites.h"
#include "ui/cursor/cursor.h"
#include "ui/menus/menu.h"
#include "ui/dialogs/game_options.h"
#include "ui/news/newspaper_layout.h"

enum { kDefaultCityCanvasWidth = 600, kDefaultCityCanvasHeight = 490 };

typedef struct {
  CityPoint anchor;
  const char *text;
  int diagonal, row;
} MapSignPaint;

// Short-lived construction feedback is presentation state, never saved City state.
enum { CITY_COST_TEXT_COUNT = 16, CITY_COST_TEXT_CAPACITY = 41 };
typedef struct {
  TileIndex tile;
  char text[CITY_COST_TEXT_CAPACITY];
  double started;
  u16 age_ms;
  bool active;
} CityCostText;

enum { CITY_DEMOLITION_EFFECT_COUNT = 64 };
typedef struct {
  TileIndex tile;
  double started;
  u8 frame, elevation, flipped;
  bool large, active;
} CityDemolitionEffect;

typedef struct {
  CityRect *event_bounds;
  CityMapEvent *events;
  int capacity;
  int animation_phase;
  int origin_x, origin_y, zoom;
  u8 helicopter_frame;
  bool initialized;
} MapSpriteFootprints;

// Positions are large-zoom pixels with eight fractional bits. This history is
// presentation-only: XTHG records keep their original deterministic cadence.
typedef struct { int x, y, z; } VehiclePosition3D;

typedef struct {
  VehiclePosition3D from, target, offset;
  double started;
  u8 type, train_heading;
} VehicleMotion;

typedef struct {
  VehicleMotion *slots;
  int capacity;
  double time;
  int rotation;
  u8 helicopter_frame;
  bool enabled, initialized, animating;
} DrawingVehicles;

enum { CITY_ROAD_CAR_COUNT = 1024, CITY_ROAD_CAR_BUCKETS = 1024, CITY_ROAD_CAR_AGE_PHASES = 16 };
typedef struct RoadCar {
  TileIndex tile : 24; // Owns the front-axle navigation point.
  u32 path : 5, rotation : 2; // Path zero is inactive; real paths are one-based.
  u32 progress : 11, next : 11, palette : 5, model : 3, reserved : 1, fading : 1;
  u32 age : 15, base_height : 6, surface : 5, ahead : 2, behind : 2, junction_ahead : 1, straight : 1;
  u16 heading; // Body yaw in 1/65536 turns, retained across tile boundaries.
  u8 speed; // Cached base speed in 1/128 tile lengths per second.
  u8 steering; // Straight, left or right front-wheel artwork; reuses record padding.
} RoadCar;
_Static_assert(sizeof(RoadCar) == 16, "Compact car records use sixteen bytes");
_Static_assert(CITY_ROAD_CAR_COUNT < (1 << 11), "Car links include the zero terminator");
_Static_assert(CITY_ROAD_VEHICLE_PALETTES <= 32 && CITY_ROAD_VEHICLE_TYPES <= 8, "Car appearances fit packed fields");
typedef struct {
  RoadCar cars[CITY_ROAD_CAR_COUNT];
  u16 buckets[CITY_ROAD_CAR_BUCKETS];
  u32 random;
  u16 age_clock, age_updated[CITY_ROAD_CAR_AGE_PHASES];
  u8 age_phase;
  u32 time, next_spawn; // Presentation seconds in 1/256 units, wrapping together.
  const TileInfo *map;
  bool enabled;
} DrawingRoadTraffic;

// Final blit coordinates are relative to the current subdivision's physical clip.
typedef struct {
  u16 sprite, depth;
  i16 x, y;
  u8 palette, flags, opacity, reserved;
} PreparedMapSprite;
enum { CITY_MAP_DEPTH_CAPACITY = 16384, CITY_MAP_DEPTH_PIXEL_LIMIT = 1000000 };
typedef enum MapDepthPhase {
  MAP_DEPTH_INACTIVE, MAP_DEPTH_COLLECT, MAP_DEPTH_GROUND, MAP_DEPTH_OBJECTS
} MapDepthPhase;
typedef struct {
  const u32 (*bridge_palettes)[256], (*underworld_palettes)[256], (*car_palettes)[256];
  CityRect clip;
  i32 depth;
  int count;
  MapDepthPhase phase;
  bool preparing, overflow;
} DrawingMapDepth;
typedef struct {
  PreparedMapSprite sprites[CITY_MAP_DEPTH_CAPACITY];
  u16 order[CITY_MAP_DEPTH_CAPACITY];
} MapSpriteWork;
_Static_assert(sizeof(PreparedMapSprite) == 12, "Prepared map sprites use twelve bytes");
_Static_assert(sizeof(MapSpriteWork) == 224 * 1024, "Shared map sprite workspace uses 224 KiB");

typedef struct {
  CanvasDrawStats calls;
  u64 tile_visits, tile_visible_visits, repaint_regions;
  double seconds;
  bool pending;
} DrawingFrameStats;

typedef struct CityExtraMapView {
  u32 window_id;
  int zoom, origin_x, origin_y, width, height;
  u8 rotation;
  i64 scroll_x, scroll_y;
  CityPoint scroll_pixels;
  NativeMapCache cache;
  MapSpriteFootprints sprites;
  CityMapAnimationFootprints animations;
} CityExtraMapView;

typedef struct {
  TileIndex tile;
  SimulationRandomState random;
  CityRect bounds;
} MapAnimation;

typedef enum MapAnimationMode { ANIMATION_INACTIVE, ANIMATION_CAPTURE, ANIMATION_REPLAY } MapAnimationMode;

// Cached immutable sprite extents and reusable event anchors for clip-derived tile traversal.
typedef struct {
  int width, height, left_extent;
  CityPoint sprite_extents[3];
  const CityMapSprite *extent_catalog;
  u32 extent_revision;
  bool extent_native;
  u8 extent_valid;
  TileIndex *event_cells;
  int capacity;
  int event_count;
} MapTileTraversal;

typedef struct {
  MapAnimation *records;
  int count, capacity;
  SimulationRandomState random_after, saved_random;
  CityRect *saved_bounds;
  MapAnimationMode mode;
} DrawingAnimations;

// Map paint and pointer scratch. No executable addresses or OS objects.
typedef struct {
  int origin_x, origin_y;
  int save_x, save_y;
  u16 zone_display_mask;
  u16 tile_scan_mask;
  u16 selection_redraw_state;
  i16 preview_x;
  i32 pointer_packed_tile;
  i16 pointer_tile_y;
  i16 pointer_tile_x;
  i16 pointer_screen_x;
  i16 pointer_screen_y;
  i16 pointer_previous_y;
  i16 pointer_previous_x;
  i32 scroll_width;
  i32 scroll_height;
  u32 horizontal_arrow_extent;
  u32 vertical_arrow_extent;
  i32 scroll_redraw;
  union {
    struct {
      u32 events_suppressed, zone_ground_mode, tile_details_enabled, label_display, buildings_enabled;
    };
    u32 display_flags[5];
  };
  u8 networks_enabled, water_enabled, trees_enabled, vehicles_enabled;
  i16 label_heights[3];
  CityRect scan_clip;
  CityPoint pan_release_point;
  CityMapSelectionBounds selection_bounds;
  // Retained label content and position, used to erase its previous pixels.
  CityRect zone_cost_label_rect;
  int zone_cost_label_cost;
  char zone_cost_label_text[CITY_COST_TEXT_CAPACITY];
  CityPoint zone_cost_label_position;
  CityRect industry_plot_rect;
  u32 label_text_color;
  u32 border_light_color;
  u32 border_dark_color;
  u32 border_corner_color;
  u32 zone_cost_outline_color;
  u32 zone_cost_affordable_color;
  u32 zone_cost_unaffordable_color;
  i16 zone_cost_font_heights[3];
  CityMapOverlaySettings overlay_settings;
  CityRect paint_clip;
  u32 paint_clip_active;
  u32 surface_failure_notice_shown;
  u16 redraw_state;
  u16 data_mode;
  u32 data_overlay_enabled;
  u32 event_structure_background;
  CitySavedViewState saved_view;
  i32 tile_information_line_height;
  u32 tile_information_warning_color;
  CityMapPaintParameters paint_parameters;
  CitySpriteCatalog sprite_catalog;
  CityNewspaperSharedChoices newspaper_shared_choices;
  CityNewspaperLayouts newspaper_layouts;
  CityNewspaperFontTable newspaper_fonts;
  CityNewspaperAlignmentTable newspaper_alignments;
} CityMapDrawingState;

typedef struct CityPaletteState {
  CityPaletteEntry current[256];
  CityPaletteEntries source;
  CityPaletteEntry fast[49];
  CityPaletteEntry slow[16];
  u32 fast_due, slow_due, direct_animation;
  u32 background_enabled;
  u32 refresh_deferred, refresh_active;
  u32 fast_tick;
  u32 slow_tick;
} CityPaletteState;

// Native presentation ownership. This state is neither serialized nor compared
// with the original simulation. Screen storage follows drawable dimensions.
typedef struct Drawing {
  CityMapDrawingState map_state;
  CityRect client_rect;
  CityOverlayFont overlay_font;
  CityCursorCache cursors;
  CityMapImmediatePaintState line_preview_surface;
  bool line_preview_surface_bound;
  CityPaletteState palette;
  u32 *display_palette;
  u32 data_map_pixels[CITY_MAP_SIZE * CITY_MAP_SIZE];
  Canvas framebuffer;
  MapSpriteCache sprite_cache;
  RunSprite *toolbar_icons[CITY_TOOLBAR_ICON_COUNT][CITY_TOOLBAR_ICON_CACHE_VARIANTS];
  u8 toolbar_icon_next[CITY_TOOLBAR_ICON_COUNT];
  const CityInterfaceIcons *toolbar_icon_source;
  const u8 *toolbar_icon_data;
  int toolbar_icon_scale;
  CityWindowDrawing *window_drawing;
  u32 sprite_revision;
  SpriteSurface screen_surface, map_target;
  u32 *scroll_colors;
  u8 *screen_indices, *scroll_indices;
  u32 *animation_rows;
  u32 *animation_composite_rows; // Tail of the row allocation, one conservative bucket mask per row.
  u32 *animation_composite_bounds;
  int target_scale, target_x, target_y, target_cache;
  int map_width, map_height;
  bool target_physical_clip;
  CityPoint sprite_offset;
  CityRect target_clip;
  bool map_navigation_enabled, map_navigation_interactive, map_capture_active;
  GameOptions options;
  u32 colors[256], resolved_palette[256];
  u32 animation_palette[SPRITE_ANIMATION_COLORS];
  double next_water_palette_tick; // GAME_TIME_UNPAUSED seconds.
  u16 water_palette_counter;
  double power_smoke_time, power_smoke_phase;
  u8 power_smoke_frame, power_smoke_speed, wind_power_frame;
  bool power_smoke_visible, wind_power_visible;
  CanvasSDL *ui_display, *status_display, *shake_display;
  CityMapViewConfig map_view_storage, drag_view_storage;
  CityMapViewConfig *default_map_view;
  CityMapViewConfig *drag_map_view;
  bool (*present_map)(Game *game);
  bool tile_damage_enabled;
  bool shake_frame_presented;
  bool screen_shake_active;
  double screen_shake_started;
  int screen_shake_offset;
  CityRect *sprite_damage_bounds;
  bool paint_placement;
  u8 label_style;
  int sign_count, sign_capacity;
  MapSignPaint *signs;
  int map_pixel_scale, ui_scale_applied, ui_map_scale_applied;
  int benchmark_redraw, profile_redraw, disable_map_cache;
  u32 profiled_redraws;
  u64 repair_pixels;
  double snapshot_seconds, map_seconds, canvas_seconds;
  bool show_redraw_rects, show_render_stats;
  bool disable_repaint_coalescing;
  DrawingFrameStats frame_stats;
  CityRect redraw_rects_bounds;
  u32 redraw_tint_sequence;
  CityPoint scroll_pixels; // Physical pixels below the logical camera resolution.
  // Keep large retained buffers after frequently accessed presentation fields.
  u8 map_framebuffer[600 * 490];
  u8 scroll_source[600 * 490];
  MapTileTraversal tile_traversal;
  DrawingAnimations animation;
  NativeMapCache main_cache;
  MapSpriteFootprints main_sprites;
  CityMapAnimationFootprints main_animations;
  DrawingVehicles vehicles;
  DrawingRoadTraffic road_traffic;
  DrawingMapDepth map_depth;
  MapSpriteWork map_sprite_work;
  Canvas newspaper_opening; // Temporary full-resolution image, released after the entrance.
  u32 newspaper_opening_window;
  CityRect newspaper_opening_bounds;
  CityExtraMapView views[CITY_EXTRA_MAP_VIEW_COUNT];
  ScreenDamage screen_damage, world_damage, upload_damage;
  CityRect placement_painted_bounds;
  CityCostText cost_texts[CITY_COST_TEXT_COUNT];
  CityRect cost_text_bounds[CITY_EXTRA_MAP_VIEW_COUNT + 1][CITY_COST_TEXT_COUNT];
  double cost_text_time;
  bool cost_texts_active;
  CityDemolitionEffect demolition_effects[CITY_DEMOLITION_EFFECT_COUNT];
  CityRect demolition_bounds[CITY_EXTRA_MAP_VIEW_COUNT + 1][CITY_DEMOLITION_EFFECT_COUNT];
  u8 demolition_draw_states[CITY_EXTRA_MAP_VIEW_COUNT + 1][CITY_DEMOLITION_EFFECT_COUNT];
  bool demolition_enabled, demolition_active, demolition_suppressed;
  u32 demolition_random;
  CityMapDrawingSnapshot map_snapshot;
} Drawing;

void InitializeDrawingDefaults(Drawing *drawing);
void InitializeDrawing(Drawing *drawing);

#endif
