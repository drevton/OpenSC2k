#ifndef SC2K_RENDER_SCREEN_DAMAGE_H
#define SC2K_RENDER_SCREEN_DAMAGE_H

#include "drawing/drawing.h"

enum { SCREEN_DIRTY_BLOCK_WIDTH = 64, SCREEN_DIRTY_BLOCK_HEIGHT = 8,
  SCREEN_DAMAGE_MAX_WIDTH = 600 * 16, SCREEN_DAMAGE_MAX_HEIGHT = 490 * 16,
  SCREEN_DAMAGE_MAX_BLOCKS = (SCREEN_DAMAGE_MAX_WIDTH / SCREEN_DIRTY_BLOCK_WIDTH) *
      (SCREEN_DAMAGE_MAX_HEIGHT / SCREEN_DIRTY_BLOCK_HEIGHT) };

typedef struct {
  u8 left, top, right, bottom;
} ScreenBlockBounds;

// OpenTTD-style shared screen grid. All map views and windows schedule damage here.
// Column-major blocks permit contiguous marking/coalescing down each column.
typedef struct ScreenDamage {
  u8 blocks[SCREEN_DAMAGE_MAX_BLOCKS];
  ScreenBlockBounds touched[SCREEN_DAMAGE_MAX_BLOCKS];
  int width, height, columns, rows;
  CityRect bounds;
} ScreenDamage;

void ResizeScreenDamage(ScreenDamage *damage, int width, int height);
void MarkScreenDirty(ScreenDamage *damage, CityRect rect);
// Non-destructive visitation allows every viewport to update its own pixel cache
// before the same screen rectangles are composited through overlapping windows.
void VisitScreenDamage(const ScreenDamage *damage, CityCanvasVisiblePaint paint, void *context);
// Optional bounded joining of stacked repaint strips; texture uploads remain exact.
void VisitScreenRepaintDamage(const ScreenDamage *damage, CityCanvasVisiblePaint paint, void *context, bool coalesce);
void ClearScreenDamage(ScreenDamage *damage);

#endif
