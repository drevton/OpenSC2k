#include <string.h>

#include "assets/graphics.h"
#include "assets/state.h"
#include "city/infrastructure/bridges.h"
#include "city/infrastructure/highways.h"
#include "city/infrastructure/lines.h"
#include "city/infrastructure/power_lines.h"
#include "city/maps/buildings.h"
#include "city/maps/layers.h"
#include "city/maps/tables.h"
#include "city/maps/tile_storage.h"
#include "city/tools/types.h"
#include "drawing/maps/map_render.h"
#include "drawing/maps/tables.h"
#include "drawing/state.h"
#include "game/state.h"

// Drawing subsystem: map sprites.

// Select a rotated Drawing cache slot directly. Resource image indexes stay separate.
int CityMapCameraSprite(const CityResourceState *assets, int sprite, int rotation) {
  if (sprite >= assets->map_tile_sprite_count) return sprite;
  return assets->map_sprite_rotations[sprite][rotation & 3];
}

// Resolve tile appearance and the requested camera turn, independently of image resolution.
static int ResolveCityMapTileSprite(const City *city, TileIndex tile, int camera_rotation) {
  const TileInfo *tp = &city->tile[tile];
  int sprite = kCityMapKindSprites[tp->kind];
  switch ((ReadCityTileClasses(tp->kind) & CITY_TILE_LAYOUT_MASK) >> CITY_TILE_LAYOUT_SHIFT) {
  case CITY_TILE_LAYOUT_BUILDING: {
    // Only building kinds inspect construction state. The map retains the eventual
    // building kind while work is in progress; select its construction sprite until
    // completion, then use the finished sprite. Hydro plants additionally fold their
    // four physical facings into two layouts, including the requested camera turn.
    int construction = CityBuildingConstructionKind(tp);
    if (construction) sprite = kCityMapKindSprites[construction];
    if (tp->kind == TK_HYDRO_POWER)
      return sprite + (((DecodeBuildingOrientation(ReadTileZoneByte(tp)) - city->geography.rotation +
          camera_rotation) & 3) >> 1);
    break;
  }
  case CITY_TILE_LAYOUT_BRIDGE:
    // Native bridges keep their base sprite without generic camera remapping.
    // Highway deck pieces alternate from the head with the lowest tile index,
    // so every camera selects the same piece for a given world tile.
    if (IsTileKindHighwayBridge(tp->kind)) sprite += ReadCityHighwayBridgePiece(city, tile);
    return sprite;
  case CITY_TILE_LAYOUT_AXIS:
    // Ground crossings and ordinary highways select one of two layouts from
    // their stored primary axis, then use the common camera remapping below. Entrances need
    // their full directional variant, including mirror-dependent camera rotation;
    // that helper already chooses the viewed sprite, so return it directly.
    if (tp->kind == TK_HIGHWAY_ENTRANCE)
      return sprite + (CityHighwayRampView(city, tile, camera_rotation) & 3);
    sprite += IsTileKindGroundCrossing(tp->kind) ? ReadCityCrossingAxis(tp) : ReadCityHighwayAxis(tp);
    break;
  case CITY_TILE_LAYOUT_VARIANT:
    // Appearance variants and directional highway shapes select an offset from
    // the family's base sprite, followed by the common camera remapping.
    // Incline artwork starts on the Y axis; normalize that catalog convention
    // here while every stored highway orientation uses its low bit as the axis.
    sprite += tp->kind == TK_HIGHWAY_INCLINE ?
        (ReadCityHighwayOrientation(tp) + 3) & 3 : ReadTileVariant(tp);
    break;
  case CITY_TILE_LAYOUT_TRANSPORT:
    // Start with the connected road or rail shape. Foundation handling selects
    // the final incline or flat sprite from the resulting transport surface,
    // including surfaces raised above irregular ground.
    sprite += kLineTileOffsetByConnectionMask[ReadTransportConnections(tp)];
    break;
  case CITY_TILE_LAYOUT_POWER:
    // Power lines have their own connection and slope artwork. Combine the
    // stored connection mask with the tile's land shape, then rotate the resulting
    // sprite into the requested view. Water spans use their own straight artwork.
    if (IsCityWaterPowerLine(tp))
      return CityMapCameraSprite(city->game->resources, CITY_MAP_TILE_SPRITE_POWER_LINE_BRIDGE,
          camera_rotation + (camera_rotation + !(ReadTransportConnections(tp) & CONN_NS)) / 2);
    sprite = CityMapPowerLayout(ReadTransportConnections(tp), ReadTerrainCode(tp));
    break;
  case CITY_TILE_LAYOUT_FIXED:
    // The kind table already selects the complete base sprite.
    // No connections, variant or construction state affect it. Keep that sprite
    // and apply only the common camera remapping below.
    break;
  }
  return CityMapCameraSprite(city->game->resources, sprite, camera_rotation);
}

int CityMapTileSpriteIndex(const City *city, TileIndex tile) {
  return ResolveCityMapTileSprite(city, tile, 0);
}

int CityMapViewTileSpriteIndex(const City *city, TileIndex tile) {
  return ResolveCityMapTileSprite(city, tile, CityMapCameraRotation(city->game->drawing));
}

int CityMapViewTileFlip(const City *city, TileIndex tile) {
  const TileInfo *tp = &city->tile[tile];
  int rotation = CityMapCameraRotation(city->game->drawing), kind = tp->kind;
  if (kind == TK_HIGHWAY_ENTRANCE) return CityHighwayRampView(city, tile, rotation) >> 2;
  if (IsCityWaterPowerLine(tp))
    return ((ReadTransportConnections(tp) & CONN_NS) != 0) ^ (rotation & 1);
  if (IsTileKindBridge(kind))
    return ReadCityBridgeAxis(tp) ^ (rotation & 1);
  if (IsTileKindBuilding(kind))
    return (ReadBuildingOrientation(city, tile) - city->geography.rotation + rotation) & 1;
  if (IsTileKindHighwayIncline(kind)) {
    int axis = ReadCityHighwayAxis(tp) ^ (rotation & 1);
    return axis ^ CityMapHighwayLayoutAxis(CityMapViewTileSpriteIndex(city, tile));
  }
  return 0;
}

// Logical width and top-to-ground-anchor distance for an already zoomed map-cache slot.
CityPoint CityMapSpriteExtent(const Drawing *drawing, int id) {
  const MapSpriteMetrics *sprite = &drawing->map_state.sprite_metrics[id];
  return (CityPoint){sprite->width, sprite->baseline};
}

i32 CityMapBridgeArtworkDepth(const City *city, TileIndex tile, bool foreground) {
  CityPoint point = CityMapViewTilePoint(city, tile);
  // Both approach layers belong to the bank tile. Using the higher deck plane for
  // its front layer moves it ahead of objects on the next ground tile.
  int height = IsCityBridgeHead(&city->tile[tile]) ?
      ReadCityLandHeight(&city->tile[tile]) : ReadCityBridgeDeckHeight(city, tile);
  int edge = foreground ? 2048 : -2048;
  return CityMapObjectDepth(point.x * 4096 + edge, point.y * 4096 + edge, height * 4096);
}

void DrawCityBridgeTrafficForeground(City *city, TileIndex tile, int sprite, int x, int baseline, int palette_offset) {
  Drawing *drawing = city->game->drawing;
  if (!drawing->map_depth.preparing) return;
  i32 depth = drawing->map_depth.depth;
  drawing->map_depth.depth = CityMapBridgeArtworkDepth(city, tile, true);
  DrawCityMapPaletteSpriteAtBaseline(drawing, sprite, x, baseline, SPRITE_OPAQUE_PALETTE,
      MAP_PALETTE_BRIDGE_FIRST + palette_offset);
  drawing->map_depth.depth = depth;
}

// Palette IDs are resolved directly; no palette scans or copies occur per sprite.
static const u32 *MapSpritePalette(const Drawing *drawing, int palette) {
  const DrawingMapDepth *paint = &drawing->map_depth;
  if (palette >= MAP_PALETTE_CAR_FIRST) return paint->car_palettes[palette - MAP_PALETTE_CAR_FIRST];
  if (palette >= MAP_PALETTE_UNDERWORLD_FIRST)
    return paint->underworld_palettes[palette - MAP_PALETTE_UNDERWORLD_FIRST];
  return palette ? paint->bridge_palettes[palette - MAP_PALETTE_BRIDGE_FIRST] : NULL;
}

bool PrepareCityMapSprite(Drawing *drawing, int sprite, int x, int y, u32 flags, int palette) {
  _Static_assert(MAP_SPRITE_COUNT <= 65536, "Prepared sprite IDs fit in sixteen bits");
  DrawingMapDepth *paint = &drawing->map_depth;
  const RunSprite *image = drawing->sprite_cache.sprites[drawing->target_cache][sprite];
  if (!image || x >= paint->clip.right || y >= paint->clip.bottom ||
      (i64)x + image->width <= paint->clip.left || (i64)y + image->height <= paint->clip.top) return false;
  if (paint->count == CITY_MAP_DEPTH_CAPACITY ||
      x - paint->clip.left < -32768 || x - paint->clip.left > 32767 ||
      y - paint->clip.top < -32768 || y - paint->clip.top > 32767) {
    paint->overflow = true;
    return true;
  }
  // Quarter-tile precision covers a 4096-square map and its elevation in u16.
  // Equal depths retain the exact original submission order through the index key.
  int depth = IntClamp(paint->depth >> 10, 0, 65535);
  int index = paint->count++;
  drawing->map_sprite_work.sprites[index] = (PreparedMapSprite){
      .sprite = sprite, .depth = depth, .x = x - paint->clip.left, .y = y - paint->clip.top,
      .palette = palette, .flags = flags & 255, .opacity = 255 - (flags >> SPRITE_TRANSPARENCY_SHIFT)};
  drawing->map_sprite_work.order[index] = index;
  return true;
}

void DrawPreparedCityMapSprites(Drawing *drawing) {
  const DrawingMapDepth *paint = &drawing->map_depth;
  for (int i = 0; i < paint->count; ++i) {
    const PreparedMapSprite *command = &drawing->map_sprite_work.sprites[drawing->map_sprite_work.order[i]];
    SpriteSurface surface = drawing->map_target;
    int palette = command->palette;
    const u32 *colors = NULL;
    if (palette >= MAP_PALETTE_SELECTION_FIRST)
      ApplyCityMapSelectionPalette(&surface, palette - MAP_PALETTE_SELECTION_FIRST);
    else if (palette) {
      colors = MapSpritePalette(drawing, palette);
    }
    DrawRunSprite(&surface, drawing->sprite_cache.sprites[drawing->target_cache][command->sprite],
        paint->clip.left + command->x, paint->clip.top + command->y,
        command->flags | (u32)(255 - command->opacity) << SPRITE_TRANSPARENCY_SHIFT, colors);
  }
}

// Draw prescaled sprite streams into the native 32-bit target.
static bool DrawCachedMapSprite(Drawing *drawing, int id, i64 x_q8, i64 y_q8, u32 flags, bool at_baseline, int palette) {
  const RunSprite *sprite = drawing->sprite_cache.sprites[drawing->target_cache][id];
  if (!sprite) return false;
  int px = drawing->target_x + (int)((x_q8 + drawing->sprite_offset.x) * drawing->target_scale / 25600);
  int py = drawing->target_y + (int)((y_q8 + drawing->sprite_offset.y) * drawing->target_scale / 25600);
  if (at_baseline) py -= drawing->sprite_cache.baselines[drawing->target_cache][id];
  int offset_x = drawing->sprite_cache.offsets_x[drawing->target_cache][id];
  if (offset_x) {
    int tile_width = (8 << drawing->default_map_view->zoom) * drawing->target_scale / 100;
    px += (((flags & SPRITE_FLIPPED) != 0) ^ sprite->mirrored) ? tile_width - sprite->width - offset_x : offset_x;
  }
  if (drawing->sprite_damage_bounds) {
    int scale = drawing->target_scale;
    int left = (px - drawing->target_x) * 100;
    int right = (px - drawing->target_x + sprite->width) * 100;
    int top = (py - drawing->target_y) * 100;
    int bottom = (py - drawing->target_y + sprite->height) * 100;
    CityRect bounds = {left / scale - (left < 0 && left % scale != 0), top / scale - (top < 0 && top % scale != 0),
        right / scale + (right > 0 && right % scale != 0),
        bottom / scale + (bottom > 0 && bottom % scale != 0)};
    CityRect *damage = drawing->sprite_damage_bounds;
    if (damage->left >= damage->right || damage->top >= damage->bottom) *damage = bounds;
    else *damage = UnionCityRects(*damage, bounds);
    return true;
  }
  if (drawing->map_depth.preparing)
    return PrepareCityMapSprite(drawing, id, px, py, flags, palette);
  CityRect clip = drawing->map_target.canvas.clip;
  if ((i64)px + sprite->width <= clip.left || px >= clip.right ||
      (i64)py + sprite->height <= clip.top || py >= clip.bottom) return false;
  DrawRunSprite(&drawing->map_target, sprite, px, py, flags, MapSpritePalette(drawing, palette));
  return true;
}

// Cache metadata supplies every anchor; tile kinds have already been resolved.
bool DrawCityMapSpriteAtBaseline(Drawing *drawing, int id, int x, int baseline, int flipped) {
  return DrawCachedMapSprite(drawing, id, (i64)x * 256, (i64)baseline * 256, flipped ? SPRITE_FLIPPED : 0, true, 0);
}

void DrawCityMapTreeAtBaseline(Drawing *drawing, int id, int x, int baseline, CityPoint root) {
  i32 depth = drawing->map_depth.depth;
  if (drawing->map_depth.preparing) drawing->map_depth.depth += (root.x + root.y - 16) * 256;
  DrawCityMapSpriteAtBaseline(drawing, id, x, baseline, 0);
  drawing->map_depth.depth = depth;
}

bool DrawCityMapSpriteAtBaselineQ8(Drawing *drawing, int id, i64 x_q8, i64 baseline_q8, int flipped) {
  return DrawCachedMapSprite(drawing, id, x_q8, baseline_q8, flipped ? SPRITE_FLIPPED : 0, true, 0);
}

bool DrawCityMapPaletteSpriteAtBaseline(Drawing *drawing, int id, int x, int baseline, u32 flags, int palette) {
  return DrawCachedMapSprite(drawing, id, (i64)x * 256, (i64)baseline * 256, flags, true, palette);
}

bool DrawCityMapSprite(Drawing *drawing, int id, int x, int y, int flipped) {
  return DrawCachedMapSprite(drawing, id, (i64)x * 256, (i64)y * 256, flipped ? SPRITE_FLIPPED : 0, false, 0);
}

static bool DecodeNativeMapIcon(const RunSprite *sprite, const u32 palette[256],
    const u32 *animation_palette, u32 *icon, int size) {
  if (!sprite) return false;
  memset(icon, 0, size * size * sizeof(*icon));
  int divisor = IntMax(sprite->width, sprite->height);
  int width = IntMax(1, sprite->width * (size - 4) / divisor);
  int height = IntMax(1, sprite->height * (size - 4) / divisor);
  RunSprite *prepared = ResizeRunSprite(sprite, width, height);
  if (!prepared) return false;
  bool copied = CopyRunSpriteRgba(prepared, icon + (size - height) / 2 * size + (size - width) / 2,
      size, palette, animation_palette);
  FreeRunSprite(prepared);
  return copied;
}

// Decode already-loaded sprites for cursor and construction presentation.
bool DecodeCityMapIcon(const Drawing *drawing, int sprite_id, const u32 palette[256], u32 *icon, int size) {
  if ((unsigned)sprite_id >= MAP_SPRITE_COUNT || size <= 4) return false;
  return DecodeNativeMapIcon(drawing->sprite_cache.base[sprite_id], palette, drawing->animation_palette, icon, size);
}
