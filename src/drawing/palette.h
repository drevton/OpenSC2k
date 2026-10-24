#ifndef SC2K_DRAWING_PALETTE_H
#define SC2K_DRAWING_PALETTE_H

#include "drawing/types.h"

typedef struct Drawing Drawing;

// RGB channels and palette metadata; flags is not an alpha channel.
typedef struct __attribute__((packed)) {
  u8 red;
  u8 green;
  u8 blue;
  u8 flags;
} CityPaletteEntry;

typedef struct __attribute__((packed)) {
  CityPaletteEntry entry[256];
} CityPaletteEntries;

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

// Fill Drawing's native 256-color base table; artwork may also supply independent palettes.
void InitializeNativeCityPalette(Drawing *drawing);
// colors is a borrowed mutable 256-entry 0xAARRGGBB table.
// NULL disables display writes without changing animation state.
void SetCityDisplayPalette(Drawing *drawing, u32 *colors);
// Initialization requires a non-NULL display table, snapshots it and resets the two animation counters.
void InitializeCityDisplayPalette(Drawing *drawing, u32 *colors);
void InitializeCityPaletteAnimation(Drawing *drawing);
// Snapshot RGB channels from the current display without permuting either animation block.
void CaptureCityPalette(CityPaletteState *palette, const u32 *colors);
// Write only the fast 49-entry and slow 16-entry ranges into a 256-entry output table.
void ApplyAnimatedCityPalette(const CityPaletteState *palette, u32 *colors);
// Nonzero selects the fast block; returns the flags of the final permuted entry.
u8 ReorderCityPaletteEntries(CityPaletteState *palette, int fast_palette);
// Per-call dependency masks, not retained state. Global and water cycles have different repaint rules.
typedef struct {
  u32 global_buckets, water_buckets;
} DrawingPaletteChanges;

// Advance integer palette phases and compare animation colors. water_colors borrows SPRITE_WATER_COLORS entries.
// Consume alpha-composite dependencies before applying colors to the prepared framebuffer.
DrawingPaletteChanges AdvanceDrawingAnimationPalette(Drawing *drawing, const u32 *water_colors, u32 intervals);
// Consume global-palette dependencies of blended interface pixels in the prepared framebuffer.
// Mark Drawing screen damage, then synchronously report each physical rectangle through mark.
// mark is required. The callback must not change Drawing's buffers/dependencies;
// neither callback nor context is retained.
// Water-only changes leave these rows untouched. Opaque indexed recoloring follows in ApplyDrawingAnimationPalette.
void ConsumeDrawingPaletteDamage(Drawing *drawing, DrawingPaletteChanges changes,
    CityCanvasVisiblePaint mark, void *context);
// Save resolved colors and recolor opaque indexed pixels in place.
// Water changes never repaint blended map pixels. Return whether indexed pixels were changed.
bool ApplyDrawingAnimationPalette(Drawing *drawing, DrawingPaletteChanges changes);

#endif
