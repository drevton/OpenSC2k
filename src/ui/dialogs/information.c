#include "city/maps/labels.h"
#include "ui/windows/window_internal.h"
#include "ui/dialogs/information.h"
#include "city/maps/terrain.h"

#include <stdio.h>
#include <string.h>

#include "assets/catalog.h"
#include "assets/graphics.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "base/text.h"
#include "city/economy/facilities.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/saveload/map_format.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/map_view.h"
#include "drawing/maps/tables.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/name_editor.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/information.h"
#include "ui/windows/windows.h"

// A snapshot belongs to its inspector window. Hidden windows still retain it; a closed window clears it.
bool ReconcileCityTileInformationWindow(CityInterfaceState *ui) {
  CityTileInformationSnapshot *pending = &ui->information_window;
  if (!pending->window_id) return false;
  if (FindCityCanvasWindow(&ui->desktop, pending->window_id)) return true;
  *pending = (CityTileInformationSnapshot){0};
  return false;
}

void PresentCityTileInformationDialog(City *city, TileIndex tile) {
  if (!IsMapTileValid(city, tile)) return;
  CityInterfaceState *ui = city->game->ui;
  ApplicationUpdateDialogPause(city);
  if (CityTileHasFacilityLabel(city, tile) &&
      kFacilityClassByTile[ReadTile(city, tile)] != CITY_FACILITY_INFO_DEFAULT &&
      !(ui->advanced_query && IsAltKeyPressed(ui))) {
    int opened = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_FACILITY_INFORMATION);
    int facility = CityFacilityOverlaySlot(ReadCityFacilityOverlay(city, tile));
    if (opened < 0)
      opened = Sc2kCanvasDesktopOpenFacilityInformation(&ui->desktop, (CityRect){85, 90, 365, 250}, facility);
    if (opened >= 0) {
      Window *window = &ui->desktop.windows[opened];
      window->data_map_selection = facility;
      window->confirmed_command = (CityCommand){CITY_COMMAND_EDIT_FACILITY_LABEL,
          MapTileX(city, tile), MapTileY(city, tile), 1};
      Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
    }
    return;
  }
  int opened = FocusCityCanvasWindowKind(&ui->desktop, CITY_CANVAS_TILE_INFORMATION);
  bool reused = opened >= 0;
  if (!reused)
    opened = Sc2kCanvasDesktopOpenTileInformation(city, (CityRect){85, 90, 495, 250}, tile);
  if (opened < 0) {
    SetGameError(city->game, SC2K_ERROR_GUEST_ALLOCATION_FAILURE);
    return;
  }
  Window *window = &ui->desktop.windows[opened];
  Window replacement = *window;
  replacement.data_map_selection = tile;
  replacement.policy_flags &= ~3;
  replacement.title = "Tile Information";
  replacement.bounds.right = replacement.bounds.left + CITY_TILE_INFORMATION_WIDTH;
  replacement.bounds.bottom = replacement.bounds.top + CITY_TILE_INFORMATION_HEIGHT;
  if (ui->advanced_query && IsAltKeyPressed(ui)) {
    replacement.policy_flags |= 2;
    replacement.title = "Advanced Tile Query";
    replacement.bounds.right = replacement.bounds.left + 350;
    replacement.bounds.bottom = replacement.bounds.top + 248;
  }
  CityTileInformationSnapshot captured = {.window_id = window->window_id, .data.tile = tile};
  CaptureCityTileInformation(city, &captured);
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  ui->information_window = captured;
  replacement.information = &ui->information_window;
  *window = replacement;
  Sc2kDirtyMarkRect(&ui->desktop.dirty, window->bounds);
  ApplicationUpdateDialogPause(city);
}

// Tile snapshots, live facility information and their window input.

static void DrawCityFacilityInformationWindow(City *city, Canvas *canvas, const Window *window) {
  CityRect bounds = window->bounds;
  DrawCanvasFrame(canvas, (CityRect){bounds.left, bounds.top + 14, bounds.right, bounds.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, bounds, window->title, canvas_face, canvas_light, canvas_dark, 0);
  CityRect old_clip = canvas->clip;
  canvas->clip = IntersectCityRects(old_clip,
      (CityRect){bounds.left + 8, bounds.top + 25, bounds.right - 8, bounds.bottom - 29});
  int facility = window->data_map_selection;
  bool valid = facility >= 0 && facility < CityFacilityRecordLimit(city) &&
      ReadCityFacilityRecord(city, facility)->type != TK_CLEAR;
  const char *name = valid ? (const char *)ReadCityOverlayName(city, CityFacilityOverlay(facility))->character :
      "Facility no longer exists";
  CanvasTextAt(canvas, bounds.left + 12, bounds.top + 30, name, canvas_text);
  bool show_age = valid && city->individual_facilities && city->annual_facilities.details;
  if (show_age) {
    char age[64];
    snprintf(age, sizeof(age), "Age: %u years", city->annual_facilities.details[facility].years);
    CanvasTextAt(canvas, bounds.left + 12, bounds.top + 48, age, canvas_text);
  }
  int first_line = show_age ? 64 : 48;
  CityCanvasInformationCursor cursor = {canvas, bounds.left + 12, bounds.top + first_line, 0, 0, 1, canvas_text, 0, 0};
  CityInformationText text = {.cursor = &cursor};
  text.city = city;
  for (int line = 0; valid && line < CITY_FACILITY_INFO_LINE_COUNT; ++line) {
    cursor.x = bounds.left + 12;
    cursor.y = bounds.top + first_line + line * 16;
    DrawCityFacilityInformationTemplate(&text, facility, line);
  }
  canvas->clip = old_clip;
  if (window->confirmed_command.id == CITY_COMMAND_EDIT_FACILITY_LABEL) {
    CityRect rename = {bounds.left + 10, bounds.bottom - 24, bounds.left + 72, bounds.bottom - 9};
    DrawCanvasButtonFrame(canvas, rename, canvas_face, canvas_light, canvas_dark, true);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, rename, "Rename", canvas_text, false, 10);
  }
  CityRect close = {bounds.right - 51, bounds.bottom - 24, bounds.right - 10, bounds.bottom - 9};
  DrawCanvasButtonFrame(canvas, close, canvas_face, canvas_light, canvas_dark, true);
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, close, "Close", canvas_text, false, 10);
}

i32 Sc2kCanvasDesktopOpenTileInformation(City *city, CityRect bounds, TileIndex tile) {
  bounds.right = bounds.left + CITY_TILE_INFORMATION_WIDTH;
  bounds.bottom = bounds.top + CITY_TILE_INFORMATION_HEIGHT;
  return Sc2kCanvasDesktopOpen(&city->game->ui->desktop,
      (Window){.kind = CITY_CANVAS_TILE_INFORMATION, .bounds = bounds,
          .title = "Tile Information", .data_map_selection = tile, .city = city});
}

i32 Sc2kCanvasDesktopOpenFacilityInformation(CityCanvasDesktop *desktop, CityRect bounds, i32 facility) {
  bounds.bottom = IntMax(bounds.bottom, bounds.top + 176);
  if (facility < 0)
    return 0;
  return Sc2kCanvasDesktopOpen(desktop,
      (Window){.kind = CITY_CANVAS_FACILITY_INFORMATION, .visible = 1, .bounds = bounds,
          .title = "Facility Information", .data_map_selection = facility});
}

// Retain facts and presentation choices at opening; painting does not requery City.
void CaptureCityTileInformation(City *city, CityTileInformationSnapshot *snapshot) {
  CityTileInformation *information = &snapshot->data;
  TileIndex tile = information->tile;
  *snapshot = (CityTileInformationSnapshot){.window_id = snapshot->window_id};
  *information = ReadCityTileInformation(city, tile);
  int kind = information->cell.kind;
  snapshot->sprite_flipped = LegacyCityTileFlip(city, tile);
  char locale[4];
  CopyText(locale, sizeof(locale), (const char *)city->country.character);
  for (int i = 0; locale[i]; ++i)
    if (locale[i] >= 'A' && locale[i] <= 'Z') locale[i] += 'a' - 'A';
  snapshot->us_currency = strcmp(locale, "usa") == 0;
  int overlay = ReadOverlay(city, tile);
  bool sailboat = IsCityEventOverlay(overlay) &&
      city->events.event[CityEventOverlaySlot(overlay)].type == CITY_MAP_EVENT_SAILBOAT;
  if (sailboat) snapshot->sprite = 1380;
  else if (IsTileKindExposedWater(kind) && (city->fixes & (1 << CITY_FIX_NATIVE_LANDSCAPE))) {
    CityTileGeometry geometry = ReadCityTileGeometry(city, tile);
    int rotation = CityMapCameraRotation(city->game->drawing);
    geometry.surface_shape = CityMapCameraShape(geometry.surface_shape, rotation);
    snapshot->river = ReadCityRiverSprites(city, tile, CITY_MAP_ZOOM_LARGE, &geometry);
    snapshot->sprite = CityWaterSprite(city, tile, CITY_MAP_ZOOM_LARGE, &geometry);
  } else {
    int terrain_sprite = ProjectCityTerrainToLegacy(city, tile, ReadTerrainCode(&information->cell));
    snapshot->sprite = kind == TK_CLEAR || IsTileKindExposedWater(kind) ?
        CityTerrainSprite(terrain_sprite) + CITY_SPRITE_CATALOG_LARGE_BASE :
        MapSpriteZoomIndex(CityMapTileSpriteIndex(city, tile), CITY_MAP_ZOOM_LARGE);
  }
  int name = CityTileInformationCategory(kCityMapKindSprites[kind]);
  if (name == CITY_TILE_CATEGORY_CLEAR) {
    name = CITY_TILE_NAME_CLEAR_TERRAIN;
    if (IsTileKindExposedWater(kind))
      name = sailboat ? CITY_TILE_NAME_FIRST_LIGHT_SAILBOAT : information->cell.flags & TF_SALTWATER ?
          CITY_TILE_NAME_SALT_WATER : CITY_TILE_NAME_FRESH_WATER;
  }
  CopyText(snapshot->name, sizeof(snapshot->name), ReadCityNameResource(CITY_NAME_CATEGORY_TILE, name));
}

static void DrawTileInformationRow(Canvas *canvas, CityRect content, int label_width, int row,
    const char *label, const char *value, u32 color) {
  int y = content.top + 16 + row * 11;
  CanvasTextRight(canvas, content.left + label_width, y, label, canvas_text);
  CanvasTextAt(canvas, content.left + label_width + 6, y, value, color);
}

// Cache families use different zoom strides; native river images do not use the ordinary map-catalog layout.
static int TileInformationPreviewSprite(const CityTileInformationSnapshot *snapshot, int piece, int zoom) {
  int sprite = snapshot->river.count ? snapshot->river.sprite[piece] : snapshot->sprite;
  if (sprite >= MAP_LANDSCAPE_SPRITE_FIRST)
    return MAP_LANDSCAPE_SPRITE_FIRST + (sprite - MAP_LANDSCAPE_SPRITE_FIRST) % MAP_LANDSCAPE_SPRITES_PER_ZOOM +
        zoom * MAP_LANDSCAPE_SPRITES_PER_ZOOM;
  if (sprite >= MAP_SLOPED_WATER_SPRITE_FIRST)
    return MAP_SLOPED_WATER_SPRITE_FIRST + (sprite - MAP_SLOPED_WATER_SPRITE_FIRST) % MAP_SELECTION_SPRITE_SHAPES +
        zoom * MAP_SELECTION_SPRITE_SHAPES;
  return MapSpriteZoomIndex(sprite % CITY_MAP_SPRITES_PER_ZOOM, zoom);
}

static CityRect TileInformationSpriteBounds(const MapSpriteCache *cache, int scale, int zoom, int id) {
  const RunSprite *sprite = cache->sprites[scale][id];
  int x = cache->offsets_x[scale][id];
  if (sprite->mirrored) x = (8 << zoom) * cache->scales[scale] / 100 - sprite->width - x;
  int y = -cache->baselines[scale][id];
  return (CityRect){x, y, x + sprite->width, y + sprite->height};
}

static bool TileInformationPreviewBounds(const CityTileInformationSnapshot *snapshot, const MapSpriteCache *cache,
    int scale, int zoom, CityRect *bounds) {
  for (int i = 0; i < IntMax(1, snapshot->river.count); ++i) {
    int id = TileInformationPreviewSprite(snapshot, i, zoom);
    if (!cache->sprites[scale][id]) return false;
    CityRect piece = TileInformationSpriteBounds(cache, scale, zoom, id);
    *bounds = i ? UnionCityRects(*bounds, piece) : piece;
  }
  return true;
}

static void DrawCityTileInformationWindow(Game *game, Canvas *canvas, const Window *window) {
  const CityTileInformationSnapshot *snapshot = window->information;
  const CityTileInformation *information = &snapshot->data;
  CityRect bounds = window->bounds;
  DrawCanvasFrame(canvas, (CityRect){bounds.left, bounds.top + 14, bounds.right, bounds.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, bounds, window->title, canvas_face, canvas_light, canvas_dark, 0);
  CityRect old_clip = canvas->clip;
  CityRect content = {bounds.left + 5, bounds.top + 17, bounds.right - 5, bounds.bottom - 4};
  canvas->clip = IntersectCityRects(old_clip, content);
  if (window->policy_flags & 2) {
    const TileInfo *tp = &information->cell;
    const int values[][3] = {
      {information->tile, information->x, information->y},
      {tp->kind, tp->terrain, tp->underground},
      {ReadCityLandHeight(tp), tp->altitude >> CITY_ALTITUDE_WATER_SHIFT & CITY_ALTITUDE_LAND_MASK,
          tp->altitude >> CITY_ALTITUDE_TUNNEL_SHIFT & CITY_ALTITUDE_LAND_MASK},
      {ReadTileZoneByte(tp), tp->flags, tp->overlay},
      {information->density, information->density_history, information->traffic_density},
      {information->land_value, information->crime, information->pollution},
      {information->police, information->fire, 0}
    };
    const char *const labels[] = {"Tile %d   X %d   Y %d", "Building %d   Terrain %d   Underground %d",
        information->building_stage ? "Elevation %d   Stage %d   Tunnel %d" :
            "Elevation %d   Water %d   Tunnel %d",
        "Zone 0x%02X   Flags 0x%02X   Overlay %d", "Density %d   Previous %d   Traffic %d",
        "Land value %d   Crime %d   Pollution %d", "Police coverage %d   Fire coverage %d"};
    for (int i = 0; i < 7; ++i) {
      char line[160];
      snprintf(line, sizeof(line), labels[i], values[i][0], values[i][1], values[i][2]);
      CanvasTextAt(canvas, bounds.left + 12, bounds.top + 30 + i * 24, line, canvas_text);
    }
    canvas->clip = old_clip;
    return;
  }
  Drawing *drawing = game->drawing;
  CityRect preview_bounds = {content.left + (content.right - content.left) / 2 + 8,
      content.top + 20, content.right - 4, content.bottom - 4};
  CityRect box = CanvasRasterImageBounds(canvas, preview_bounds);
  const MapSpriteCache *cache = &drawing->sprite_cache;
  int preview_scale = -1, preview_zoom = 0, smallest_scale = -1, smallest_zoom = 0;
  CityRect preview_bounds_pixels = {0}, smallest_bounds = {0};
  // Fit every river layer together at a prepared scale, retaining their shared tile origin and baseline.
  for (int scale = 0; scale < MAP_SPRITE_SCALE_COUNT; ++scale) {
    for (int zoom = 0; zoom < 3; ++zoom) {
      CityRect candidate;
      if (!TileInformationPreviewBounds(snapshot, cache, scale, zoom, &candidate)) continue;
      int width = candidate.right - candidate.left, height = candidate.bottom - candidate.top;
      int area = width * height;
      if (smallest_scale < 0 || area < (smallest_bounds.right - smallest_bounds.left) *
          (smallest_bounds.bottom - smallest_bounds.top)) {
        smallest_scale = scale;
        smallest_zoom = zoom;
        smallest_bounds = candidate;
      }
      if (width <= box.right - box.left && height <= box.bottom - box.top &&
          (preview_scale < 0 || area > (preview_bounds_pixels.right - preview_bounds_pixels.left) *
              (preview_bounds_pixels.bottom - preview_bounds_pixels.top))) {
        preview_scale = scale;
        preview_zoom = zoom;
        preview_bounds_pixels = candidate;
      }
    }
  }
  if (preview_scale < 0) {
    preview_scale = smallest_scale;
    preview_zoom = smallest_zoom;
    preview_bounds_pixels = smallest_bounds;
  }
  if (preview_scale >= 0) {
    CanvasRaster raster = CanvasRasterView(canvas);
    SpriteSurface surface = drawing->screen_surface;
    surface.canvas = *canvas;
    surface.canvas.width = raster.width;
    surface.canvas.height = raster.height;
    surface.canvas.clip = CanvasRasterRect(canvas, preview_bounds);
    surface.canvas.raster = (CanvasRaster){0};
    int x = box.left + (box.right - box.left - preview_bounds_pixels.right - preview_bounds_pixels.left) / 2;
    int y = box.top + (box.bottom - box.top - preview_bounds_pixels.bottom - preview_bounds_pixels.top) / 2;
    for (int i = 0; i < IntMax(1, snapshot->river.count); ++i) {
      int id = TileInformationPreviewSprite(snapshot, i, preview_zoom);
      CityRect piece = TileInformationSpriteBounds(cache, preview_scale, preview_zoom, id);
      DrawRunSprite(&surface, cache->sprites[preview_scale][id], x + piece.left, y + piece.top, SPRITE_COPY, NULL);
    }
  }
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas,
      (CityRect){content.left, content.top, content.right, content.top + 14},
      snapshot->name, canvas_text, false, 10);
  int zone = ReadCityZoneKind(&information->cell), label_width = 0, row = 0;
  static const char *const kLabels[] = {
    "Zone:",
    "Traffic:",
    "Elevation:",
    "Land value:",
    "Crime:",
    "Pollution:",
    "Electricity:",
    "Water service:",
  };
  for (int i = 0; i < (int)(sizeof(kLabels) / sizeof(kLabels[0])); ++i)
    label_width = IntMax(label_width, CityInterfaceTextWidth(canvas->interface_font, kLabels[i]));
  char value[512];
  if (zone != ZONE_NONE) {
    DrawTileInformationRow(canvas, content, label_width, row++,
        "Zone:", ReadCityNameResource(CITY_NAME_CATEGORY_ZONE, zone), canvas_text);
    DrawTileInformationRow(canvas, content, label_width, row++,
        "", ReadCityNameResource(CITY_NAME_CATEGORY_ZONE, zone + 16), canvas_text);
  } else if (information->show_traffic) {
    snprintf(value, sizeof(value), "%d%s", information->traffic,
        " vehicles/min");
    DrawTileInformationRow(canvas, content, label_width, row++,
        "Traffic:", value, canvas_text);
  }
  if (information->has_facility_age) {
    snprintf(value, sizeof(value), "%u years", information->facility_years);
    DrawTileInformationRow(canvas, content, label_width, row++, "Age:", value, canvas_text);
  }
  snprintf(value, sizeof(value), "%d %s", information->altitude_feet, (information->underwater ? "feet deep" : "feet"));
  DrawTileInformationRow(canvas, content, label_width, row++,
      "Elevation:", value, canvas_text);
  if (!information->rocky) {
    snprintf(value, sizeof(value), "%s%d%s", snapshot->us_currency ? "$" : "", information->land_value + 1,
        ",000 per acre");
    DrawTileInformationRow(canvas, content, label_width, row++,
        "Land value:", value, canvas_text);
  }
  for (int metric_row = 0; metric_row < 2; ++metric_row) {
    int rating = metric_row ? information->pollution_rating : information->crime_rating;
    DrawTileInformationRow(canvas, content, label_width, row++,
        (metric_row ? "Pollution:" : "Crime:"), ReadCityNameResource(CITY_NAME_CATEGORY_RATING, rating), canvas_text);
  }
  if (information->show_services) {
    u32 warning = drawing->map_state.tile_information_warning_color;
    warning = 0xFF000000 | ((warning & 255) << 16) | (warning & 0xFF00) | ((warning >> 16) & 255);
    bool powered = information->powered;
    DrawTileInformationRow(canvas, content, label_width, row++,
        "Electricity:",
        (powered ? "Yes" : "No"),
        powered ? canvas_text : warning);
    bool watered = information->watered;
    if (information->water_supply != CITY_TILE_WATER_SERVICE) {
      snprintf(value, sizeof(value), "%d %s", information->water,
          information->water_supply == CITY_TILE_WATER_PRODUCTION ? "gallons/month" : "gallons stored");
      watered = true;
    } else CopyText(value, sizeof(value),
        (watered ? "Yes" : "No"));
    DrawTileInformationRow(canvas, content, label_width, row,
        "Water service:", value, watered ? canvas_text : warning);
  }
  if (window->policy_flags & 1) {
    snprintf(value, sizeof(value), "X=%d  Y=%d%s  Txt=%d", information->y, information->x,
        snapshot->sprite_flipped ? "flip" : "", information->cell.overlay);
    CanvasTextAt(canvas, content.left + 10, content.bottom - 30, value, canvas_text);
  }
  canvas->clip = old_clip;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool InformationWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    if (window->kind == CITY_CANVAS_TILE_INFORMATION)
      DrawCityTileInformationWindow(game, canvas, window);
    else DrawCityFacilityInformationWindow(game->city, canvas, window);
    return true;
  }
  if (event->kind == WINDOW_INPUT && window->kind == CITY_CANVAS_FACILITY_INFORMATION &&
      window->confirmed_command.id == CITY_COMMAND_EDIT_FACILITY_LABEL &&
      event->input->kind == CANVAS_SDL_POINTER_DOWN && event->input->button == CANVAS_SDL_BUTTON_LEFT) {
    CityRect b = window->bounds;
    if (CanvasPointInRect((CityRect){b.left + 10, b.bottom - 24, b.left + 72, b.bottom - 9}, event->x, event->y)) {
      TileIndex tile = MapTileXYOrInvalid(game->city, window->confirmed_command.parameter1,
          window->confirmed_command.parameter2);
      if (tile != INVALID_TILE) PresentCityLabelEditor(game->city, tile, true);
      return true;
    }
  }
  return false;
}

// Open inspection through the installed presentation adapter or native UI.
void ShowCityTileInformationDialog(City *city, TileIndex tile) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->show_tile_information)
    observer->show_tile_information(city, tile);
  else
    PresentCityTileInformationDialog(city, tile);
}
