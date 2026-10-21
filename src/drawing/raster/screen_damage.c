#include "drawing/raster/screen_damage.h"

#include <string.h>

void ClearScreenDamage(ScreenDamage *damage) {
  memset(damage->blocks, 0, (size_t)damage->columns * damage->rows);
  damage->bounds = (CityRect){damage->width, damage->height, 0, 0};
}

void ResizeScreenDamage(ScreenDamage *damage, int width, int height) {
  if (width < 1 || height < 1 || width > SCREEN_DAMAGE_MAX_WIDTH || height > SCREEN_DAMAGE_MAX_HEIGHT)
    return;
  if (damage->width == width && damage->height == height)
    return;
  damage->width = width;
  damage->height = height;
  damage->columns = (width + SCREEN_DIRTY_BLOCK_WIDTH - 1) / SCREEN_DIRTY_BLOCK_WIDTH;
  damage->rows = (height + SCREEN_DIRTY_BLOCK_HEIGHT - 1) / SCREEN_DIRTY_BLOCK_HEIGHT;
  ClearScreenDamage(damage);
  MarkScreenDirty(damage, (CityRect){0, 0, width, height});
}

void MarkScreenDirty(ScreenDamage *damage, CityRect rect) {
  rect = IntersectCityRects(rect, (CityRect){0, 0, damage->width, damage->height});
  if (rect.left >= rect.right || rect.top >= rect.bottom)
    return;
  damage->bounds = UnionCityRects(damage->bounds, rect);
  int top = rect.top / SCREEN_DIRTY_BLOCK_HEIGHT;
  int bottom = (rect.bottom + SCREEN_DIRTY_BLOCK_HEIGHT - 1) / SCREEN_DIRTY_BLOCK_HEIGHT;
  int left = rect.left / SCREEN_DIRTY_BLOCK_WIDTH;
  int right = (rect.right + SCREEN_DIRTY_BLOCK_WIDTH - 1) / SCREEN_DIRTY_BLOCK_WIDTH;
  for (int x = left; x < right; ++x)
    for (int y = top; y < bottom; ++y) {
      int index = x * damage->rows + y;
      int origin_x = x * SCREEN_DIRTY_BLOCK_WIDTH, origin_y = y * SCREEN_DIRTY_BLOCK_HEIGHT;
      ScreenBlockBounds bounds = {
        rect.left > origin_x ? rect.left - origin_x : 0,
        rect.top > origin_y ? rect.top - origin_y : 0,
        rect.right < origin_x + SCREEN_DIRTY_BLOCK_WIDTH ? rect.right - origin_x : SCREEN_DIRTY_BLOCK_WIDTH,
        rect.bottom < origin_y + SCREEN_DIRTY_BLOCK_HEIGHT ? rect.bottom - origin_y : SCREEN_DIRTY_BLOCK_HEIGHT
      };
      ScreenBlockBounds *old = &damage->touched[index];
      if (damage->blocks[index]) {
        old->left = IntMin(old->left, bounds.left);
        old->top = IntMin(old->top, bounds.top);
        old->right = IntMax(old->right, bounds.right);
        old->bottom = IntMax(old->bottom, bounds.bottom);
      } else *old = bounds;
      damage->blocks[index] = 1;
    }
}

static void VisitScreenDamageRects(const ScreenDamage *damage, CityCanvasVisiblePaint paint, void *context,
    bool row_order) {
  if (damage->bounds.left >= damage->bounds.right || damage->bounds.top >= damage->bounds.bottom)
    return;
  u8 blocks[SCREEN_DAMAGE_MAX_BLOCKS];
  ScreenBlockBounds touched[SCREEN_DAMAGE_MAX_BLOCKS];
  int left = damage->bounds.left / SCREEN_DIRTY_BLOCK_WIDTH;
  int right = (damage->bounds.right + SCREEN_DIRTY_BLOCK_WIDTH - 1) / SCREEN_DIRTY_BLOCK_WIDTH;
  int top = damage->bounds.top / SCREEN_DIRTY_BLOCK_HEIGHT;
  int last = (damage->bounds.bottom + SCREEN_DIRTY_BLOCK_HEIGHT - 1) / SCREEN_DIRTY_BLOCK_HEIGHT;
  // Keep the visitation snapshot, including callback/reentrant marking, but
  // copy only columns and rows that can contain damage in this visit.
  if (!top && last == damage->rows) {
    size_t first = (size_t)left * damage->rows, count = (size_t)(right - left) * damage->rows;
    memcpy(blocks + first, damage->blocks + first, count);
    memcpy(touched + first, damage->touched + first, count * sizeof(*touched));
  } else for (int x = left; x < right; ++x) {
    int first = x * damage->rows + top, count = last - top;
    memcpy(blocks + first, damage->blocks + first, count);
    memcpy(touched + first, damage->touched + first, count * sizeof(*touched));
  }
  int major_first = row_order ? top : left, major_last = row_order ? last : right;
  int minor_first = row_order ? left : top, minor_last = row_order ? right : last;
  for (int major = major_first; major < major_last; ++major)
    for (int minor = minor_first; minor < minor_last; ++minor) {
      int x = row_order ? minor : major, y = row_order ? major : minor;
      u8 *column = blocks + x * damage->rows;
      if (column[y] == 0) continue;
      ScreenBlockBounds first = touched[x * damage->rows + y];
      int last_bottom = first.bottom;
      int bottom = y + 1;
      // Merge only adjoining rectangles: no block rounding on thin strips,
      // and no extra pixels introduced by coalescing unequal edge bounds.
      while (bottom < last && column[bottom] && last_bottom == SCREEN_DIRTY_BLOCK_HEIGHT) {
        ScreenBlockBounds next = touched[x * damage->rows + bottom];
        if (next.left != first.left || next.right != first.right || next.top != 0) break;
        last_bottom = next.bottom;
        ++bottom;
      }
      memset(column + y, 0, (size_t)(bottom - y));
      int end_x = x + 1;
      int last_right = first.right;
      while (end_x < right && last_right == SCREEN_DIRTY_BLOCK_WIDTH) {
        u8 *next = blocks + end_x * damage->rows;
        ScreenBlockBounds next_first = touched[end_x * damage->rows + y];
        int row = y;
        while (row < bottom && next[row]) {
          ScreenBlockBounds bounds = touched[end_x * damage->rows + row];
          if (bounds.left != 0 || bounds.right != next_first.right ||
              bounds.top != (row == y ? first.top : 0) ||
              bounds.bottom != (row == bottom - 1 ? last_bottom : SCREEN_DIRTY_BLOCK_HEIGHT)) break;
          ++row;
        }
        if (row != bottom) break;
        memset(next + y, 0, (size_t)(bottom - y));
        last_right = next_first.right;
        ++end_x;
      }
      CityRect rect = {x * SCREEN_DIRTY_BLOCK_WIDTH + first.left, y * SCREEN_DIRTY_BLOCK_HEIGHT + first.top,
          (end_x - 1) * SCREEN_DIRTY_BLOCK_WIDTH + last_right,
          (bottom - 1) * SCREEN_DIRTY_BLOCK_HEIGHT + last_bottom};
      rect = IntersectCityRects(rect, damage->bounds);
      paint(rect, context);
    }
}

void VisitScreenDamage(const ScreenDamage *damage, CityCanvasVisiblePaint paint, void *context) {
  VisitScreenDamageRects(damage, paint, context, false);
}

typedef struct {
  CityCanvasVisiblePaint paint;
  void *context;
  CityRect pending;
  i64 covered;
} ScreenRepaintJoin;

static void JoinScreenRepaintStrip(CityRect rect, void *context) {
  ScreenRepaintJoin *join = context;
  i64 pixels = (i64)(rect.right - rect.left) * (rect.bottom - rect.top);
  if (join->covered) {
    CityRect previous = join->pending, combined = UnionCityRects(previous, rect);
    i64 covered = join->covered + pixels;
    i64 total = (i64)(combined.right - combined.left) * (combined.bottom - combined.top);
    // Join vertically stacked bands, including slightly sloped diamond edges.
    // Side-by-side regions retain exact joins; cap both relative and absolute
    // extra work rather than widening a repaint to the whole diamond.
    if (rect.top >= previous.bottom && rect.left < previous.right && previous.left < rect.right &&
        total * 4 <= covered * 5 && total - covered <= 4096) {
      join->pending = combined;
      join->covered = covered;
      return;
    }
    join->paint(previous, join->context);
  }
  join->pending = rect;
  join->covered = pixels;
}

void VisitScreenRepaintDamage(const ScreenDamage *damage, CityCanvasVisiblePaint paint, void *context, bool coalesce) {
  if (!coalesce) { VisitScreenDamage(damage, paint, context); return; }
  ScreenRepaintJoin join = {.paint = paint, .context = context};
  VisitScreenDamageRects(damage, JoinScreenRepaintStrip, &join, true);
  if (join.covered) paint(join.pending, context);
}
