#include "drawing/palette.h"

#include <string.h>

#include "drawing/bitmaps.h"
#include "drawing/state.h"

enum { CITY_PALETTE_ENTRY_RESERVED = 1 };

// Snapshot the active palette and initialize its two animation ranges.
void InitializeCityPaletteAnimation(Drawing *drawing) {
  CityPaletteState *state = &drawing->palette;
  for (int index = 0; index < 256; ++index) {
    CityPaletteEntry *entry = &state->source.entry[index];
    u32 color = drawing->display_palette[index];
    entry->red = (u8)(color >> 16);
    entry->green = (u8)(color >> 8);
    entry->blue = (u8)color;
    entry->flags = 0;
  }
  state->fast_tick = 0;
  state->slow_tick = 0;
  for (int index = 0; index < 49; ++index) {
    state->fast[index] = state->source.entry[0xAB + index];
    state->fast[index].flags = CITY_PALETTE_ENTRY_RESERVED;
  }
  for (int index = 0; index < 16; ++index) {
    state->slow[index] = state->source.entry[0xE0 + index];
    state->slow[index].flags = CITY_PALETTE_ENTRY_RESERVED;
  }
}

// A project-authored compatibility palette. Most native artwork carries its own RGB or palette.
void InitializeNativeCityPalette(Drawing *drawing) {
  static const u32 kBase[] = {0x101418, 0x304838, 0x283C60, 0x706028,
      0x703830, 0x68486C, 0x387878, 0x787C78, 0xB0B4AC, 0x70A060,
      0x6498D0, 0xD8BC58, 0xC85C48, 0xAC80B4, 0x78B8B0, 0xF4F0DC};
  for (int index = 0; index < 256; ++index) {
    u32 base = kBase[index / 16];
    int shade = 64 + (index % 16) * 12;
    int red = IntMin(255, (int)(base >> 16) * shade / 160);
    int green = IntMin(255, (int)((base >> 8) & 255) * shade / 160);
    int blue = IntMin(255, (int)(base & 255) * shade / 160);
    drawing->colors[index] = 0xFF000000 | red << 16 | green << 8 | blue;
  }
  drawing->colors[0] = 0xFF000000;
  drawing->colors[255] = 0xFFFFFFFF;
}

// Apply the stock entry-index permutation to one palette block.
u8 ReorderCityPaletteEntries(CityPaletteState *palette, int fast_palette) {
  static const u8 kFastOrder[49] = {
    1, 2, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 13, 14, 15, 8,
    17, 18, 19, 20, 21, 22, 23, 16, 25, 26, 27, 24, 28, 36, 29, 30,
    31, 32, 33, 34, 35, 40, 37, 38, 39, 48, 41, 42, 43, 44, 45, 46,
    47
  };
  static const u8 kSlowOrder[16] = {
    1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 14, 0
  };
  CityPaletteEntry temporary[49];
  CityPaletteEntry *entries = fast_palette != 0 ? palette->fast :
      palette->slow;
  const u8 *order = fast_palette != 0 ? kFastOrder : kSlowOrder;
  int count = fast_palette != 0 ? 49 : 16;
  for (int index = 0; index < count; ++index)
    temporary[index] = entries[index];
  for (int index = 0; index < count; ++index)
    entries[index] = temporary[order[index]];
  return entries[count - 1].flags;
}

// Select the borrowed software display palette without changing animation state.
void SetCityDisplayPalette(Drawing *drawing, u32 *colors) {
  drawing->display_palette = colors;
}

// Initialize the active palette and its two animation blocks.
void InitializeCityDisplayPalette(Drawing *drawing, u32 *colors) {
  drawing->display_palette = colors;
  CaptureCityPalette(&drawing->palette, colors);
  InitializeCityPaletteAnimation(drawing);
}

// Snapshot display colors before an animation step changes its active block.
void CaptureCityPalette(CityPaletteState *palette, const u32 *colors) {
  for (int index = 0; index < 256; ++index) {
    CityPaletteEntry *entry = &palette->current[index];
    u32 color = colors[index];
    entry->red = (u8)(color >> 16);
    entry->green = (u8)(color >> 8);
    entry->blue = (u8)color;
    entry->flags = 0;
  }
}

// Apply both animation ranges to the display color table.
void ApplyAnimatedCityPalette(const CityPaletteState *palette, u32 *colors) {
  for (int index = 0; index < 49; ++index) {
    const CityPaletteEntry *entry = &palette->fast[index];
    colors[0xAB + index] = 0xFF000000u | (u32)entry->red << 16 |
        (u32)entry->green << 8 | entry->blue;
  }
  for (int index = 0; index < 16; ++index) {
    const CityPaletteEntry *entry = &palette->slow[index];
    colors[0xE0 + index] = 0xFF000000u | (u32)entry->red << 16 |
        (u32)entry->green << 8 | entry->blue;
  }
}

// Color cycling uses integer phases; Game supplies the number of elapsed presentation intervals.
DrawingPaletteChanges AdvanceDrawingAnimationPalette(Drawing *drawing, const u32 *water_colors, u32 intervals) {
  drawing->water_palette_counter += 8 * intervals;
  u32 water_changed = 0;
  int water_phase = ((u16)(drawing->water_palette_counter * 320) * SPRITE_WATER_COLORS) >> 16;
  for (int color = 0; color < SPRITE_WATER_COLORS; ++color) {
    u32 value = water_colors[(color + water_phase) % SPRITE_WATER_COLORS];
    int id = SPRITE_ANIMATION_WATER_FIRST + color;
    if (drawing->animation_palette[id] != value) water_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = value;
  }
  // OpenTTD palette.cpp: fifteen glitter colors, five entries spaced three
  // steps apart, advancing at EXTR(128, 15), separately from EXTR(320, 5).
  static const u32 kWaterGlitterCycle[] = {
    0xD8F4FC, 0xACD0E0, 0x84ACC4, 0x6484A8,
    0x486490, 0x486490, 0x486490, 0x486490,
    0x486490, 0x486490, 0x486490, 0x486490,
    0x6484A8, 0x84ACC4, 0xACD0E0
  };
  int glitter_phase = ((u16)(drawing->water_palette_counter * 128) * 15) >> 16;
  for (int color = 0; color < SPRITE_WATER_GLITTER_COLORS; ++color) {
    u32 value = kWaterGlitterCycle[(glitter_phase + color * 3) % 15];
    int id = SPRITE_ANIMATION_WATER_FIRST + SPRITE_WATER_COLORS + color;
    if (drawing->animation_palette[id] != value) water_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = value;
  }
  u32 palette_changed = 0;
  for (int id = 1; id < SPRITE_ANIMATION_WATER_FIRST; ++id) {
    u32 color = drawing->colors[SpriteAnimationSource(id)] & 0xFFFFFF;
    if (drawing->animation_palette[id] != color) palette_changed |= CanvasPaletteIndexMask(id);
    drawing->animation_palette[id] = color;
  }
  return (DrawingPaletteChanges){palette_changed, water_changed};
}

bool ApplyDrawingAnimationPalette(Drawing *drawing, DrawingPaletteChanges changes) {
  u32 palette_changed = changes.global_buckets, water_changed = changes.water_buckets;
  u32 changed_buckets = palette_changed | water_changed;
  if (palette_changed) InvalidateCityWindowPaletteBitmaps(drawing, palette_changed);
  memcpy(drawing->resolved_palette, drawing->colors, sizeof(drawing->colors));
  if (palette_changed) return AnimateSpriteSurfaceChanges(&drawing->screen_surface, changed_buckets);
  return water_changed && AnimateSpriteSurfaceWater(&drawing->screen_surface, water_changed);
}
