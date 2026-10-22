#include "drawing/state.h"

#include <stdlib.h>
#include <string.h>

#include "drawing/bitmaps.h"
#include "drawing/display.h"
#include "drawing/maps/map_render.h"
#include "drawing/newspaper.h"

// Native drawing preferences and presentation defaults.
void InitializeDrawingDefaults(Drawing *drawing) {
  drawing->demolition_random = 1;
  CityMapDrawingState *map = &drawing->map_state;
  map->tile_information_line_height = 20;
  map->tile_information_warning_color = 255;
  map->pointer_previous_x = -1;
  map->pointer_previous_y = -1;
  map->events_suppressed = 0;
  map->zone_ground_mode = 1;
  map->tile_details_enabled = 1;
  map->label_display = 1;
  map->buildings_enabled = 1;
  map->networks_enabled = 1;
  map->water_enabled = 1;
  map->trees_enabled = 1;
  map->vehicles_enabled = 1;
  map->label_heights[0] = 12;
  map->label_heights[1] = 14;
  map->label_heights[2] = 16;
  map->label_text_color = 0x300000;
  map->border_light_color = 0x838383;
  map->border_dark_color = 0xE3E3E3;
  map->border_corner_color = 0x575757;
  map->zone_cost_font_heights[0] = 18;
  map->zone_cost_font_heights[1] = 22;
  map->zone_cost_font_heights[2] = 24;
  map->overlay_settings = (CityMapOverlaySettings){
    .surface = {0, 1, 1, 1, 1},
    .underground = {0, 0, 1, 0, 0}
  };
}

void InitializeDrawing(Drawing *drawing) {
  memset(drawing, 0, sizeof(*drawing));
  InitializeDrawingDefaults(drawing);
  InitializeCityOverlayFont(&drawing->overlay_font);
  drawing->map_pixel_scale = 4;
  drawing->ui_scale_applied = 400;
  drawing->ui_map_scale_applied = 4;
  drawing->options = (DrawingOptions){.scale = 400, .automatic = 1, .snapping = 1,
      .scale_bevels = true, .sprite_font = true, .font_antialias = true, .black_background = true,
      .fixes = DRAWING_FIX_DEFAULT_MASK, .demolition_elevation = true, .save_view_position = true,
      .window_width = 2400, .window_height = 1960, .driver = "headless"};
}

void ReleaseCityNewspaperOpening(Drawing *drawing) {
  free(drawing->newspaper_opening.pixels);
  drawing->newspaper_opening = (Canvas){0};
  drawing->newspaper_opening_window = 0;
  drawing->newspaper_opening_bounds = (CityRect){0};
}

void ReleaseDrawing(Drawing *drawing) {
  ReleaseCityNewspaperOpening(drawing);
  ReleaseDrawingFramebuffer(drawing);
  FreeMapSpriteCache(&drawing->sprite_cache);
  ReleaseCityToolbarIcons(drawing);
  while (drawing->window_drawing) ReleaseCityWindowDrawing(drawing->window_drawing);
  ReleaseCityMapDrawingStorage(drawing);
  free(drawing->signs);
  drawing->signs = NULL;
  drawing->sign_count = drawing->sign_capacity = 0;
}
