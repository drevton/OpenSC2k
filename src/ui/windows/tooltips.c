#include "ui/windows/tooltips.h"

#include <stdio.h>
#include <string.h>

#include "assets/state.h"
#include "base/text.h"
#include "city/maps/map_types.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/raster/canvas_raster_internal.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/charts/charts.h"
#include "drawing/cursors.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/game_options.h"
#include "ui/dialogs/map_data.h"
#include "ui/dialogs/messages.h"
#include "ui/dialogs/spans.h"
#include "ui/map_views.h"
#include "ui/menus/menu.h"
#include "ui/music/jukebox.h"
#include "ui/signs.h"
#include "ui/startup/startup.h"
#include "ui/state.h"
#include "ui/windows/window_internal.h"

// Shared hover help. Control providers supply text; this owner handles lifetime,
// wrapping and damage for every window without changing City state.

static const double kTooltipDelay = 0.6, kTooltipRepeatDelay = 0.15;

static const char *WindowTooltipAt(Game *game, const Window *window, int x, int y,
    char *text, size_t size) {
  CityRect b = window->bounds;
  if (window->kind == CITY_CANVAS_TOOL_STATUS)
    return "The active tool and its cost. The second line shows city notices or whether simulation is paused.";
  if (y < b.top + 14) {
    if (x < b.left + 11) return "Close this window.";
    if (window->kind == CITY_CANVAS_CONSTRUCTION) {
      CityConstructionTooltipAt(game, window, x, y, text, size);
      return text;
    }
    if (window->kind == CITY_CANVAS_MAP_VIEWPORT || window->kind == CITY_CANVAS_MAP_DATA) {
      int control = CityCaptionControlAt(window, x, y);
      switch (control) {
      case CITY_CAPTION_SHADE: return window->shaded ? "Expand this window." : "Shade this window to its title bar.";
      case CITY_CAPTION_DEFAULT_SIZE: return "Restore the default window size.";
      case CITY_CAPTION_STICKY: return window->sticky ?
          "Unpin this window so Escape can close it." : "Pin this window to keep it open when pressing Escape.";
      default: break;
      }
    }
    if (window->kind == CITY_CANVAS_SIGN_EDIT || window->kind == CITY_CANVAS_SIGN_LIST) {
      switch (CitySignControlAt(window, x, y)) {
      case CITY_SIGN_LOCATION: return "Center the main view on this sign.";
      case CITY_SIGN_SHADE: return window->shaded ? "Expand this window." : "Shade this window to its title bar.";
      case CITY_SIGN_DEFAULT_SIZE: return "Restore the default window size.";
      case CITY_SIGN_STICKY: return window->sticky ?
          "Unpin this window so Escape can close it." : "Pin this window to keep it open when pressing Escape.";
      default: break;
      }
    }
    if (window->kind == CITY_CANVAS_JUKEBOX || window->kind == CITY_CANVAS_MUSIC_PROGRAMME) {
      switch (Sc2kCanvasJukeboxActionAt(game->ui, window, x, y)) {
      case JUKEBOX_COLLAPSE: return "Collapse or expand the music player.";
      case JUKEBOX_PIN: return "Keep the music player visible.";
      default: break;
      }
    }
    if ((window->kind == CITY_CANVAS_GAME_OPTIONS ||
        (window->kind == CITY_CANVAS_SPAN_STYLE && window->span_styles)) && x >= b.right - 11)
      return "Restore the default window size.";
    return "Drag the title bar to move this window.";
  }
  if (CanvasPointInRect(CityWindowResizeGrip(window), x, y)) return "Drag this corner to resize the window.";
  switch (window->kind) {
  case CITY_CANVAS_ADVISOR: {
    if (!window->advisor_category) return NULL;
    int category = CityAdvisorButtonAt(window, x, y);
    return category >= 0 ? CityAdvisorName(category) : "Select an icon to consult another city advisor.";
  }
  case CITY_CANVAS_CONSTRUCTION:
    CityConstructionTooltipAt(game, window, x, y, text, size);
    return text;
  case CITY_CANVAS_GAME_OPTIONS:
    return CityOptionsTooltipAt(game, window, x, y);
  case CITY_CANVAS_STARTUP_MENU:
  case CITY_CANVAS_SCENARIO_LIST:
    return CityStartupTooltipAt(window, x, y, text, size);
  case CITY_CANVAS_MAP_DATA: return CityMapTooltipAt(window, x, y);
  case CITY_CANVAS_POLICIES:
  case CITY_CANVAS_BUDGET_RATE:
  case CITY_CANVAS_BUDGET:
  case CITY_CANVAS_BONDS:
    return CityBudgetTooltipAt(window, x, y);
  case CITY_CANVAS_JUKEBOX:
  case CITY_CANVAS_MUSIC_PROGRAMME:
    return CityMusicTooltipAt(game, window, x, y);
  case CITY_CANVAS_HISTORY:
  case CITY_CANVAS_DEMOGRAPHICS:
  case CITY_CANVAS_NEIGHBORS:
  case CITY_CANVAS_INDUSTRY:
    return CityChartTooltipAt(game, window, x, y);
  case CITY_CANVAS_FILE_SAVE:
  case CITY_CANVAS_FILE_OPEN:
    return "Choose a folder and city file. Type a filename, then confirm; Cancel leaves the current city open.";
  case CITY_CANVAS_SIGN_EDIT:
  case CITY_CANVAS_SIGN_LIST:
    return CitySignTooltipAt(game, window, x, y);
  case CITY_CANVAS_LABEL_EDIT: return "Enter the map sign text, then confirm or cancel the edit.";
  case CITY_CANVAS_STADIUM_TEAM: return "Choose the team for this stadium.";
  case CITY_CANVAS_SAVE_QUESTION: {
    int answer = CityMessageAnswerAt(window, x, y);
    if (answer == CITY_DIALOG_YES) return "Save the current city before continuing.";
    if (answer == CITY_DIALOG_NO) return "Continue without saving the current city.";
    if (answer == CITY_DIALOG_CANCEL) return "Cancel and return to the current city.";
    return "Choose whether to save your city before continuing.";
  }
  case CITY_CANVAS_FILE_QUESTION:
  case CITY_CANVAS_CONFIRM:
    return "Confirm this action with Yes, or cancel it with No.";
  case CITY_CANVAS_COMMAND_ERROR:
    return "Close this message with the close button or Escape. You can continue playing.";
  case CITY_CANVAS_EXIT:
    return "Quit the game with Yes, or keep playing with No.";
  case CITY_CANVAS_ABANDON:
    return "Return to the startup screen with Yes, or keep playing with No.";
  case CITY_CANVAS_SPAN_STYLE: {
    if (x >= b.right - 12 && y >= b.bottom - 12) return "Drag to resize the bridge chooser.";
    if (x >= b.right - 12) return "Scroll through the available bridges.";
    if (y < b.top + CITY_SPAN_LIST_TOP)
      return x < b.left + 65 ? "Reverse the bridge sort order." : "Sort bridges by cost or name.";
    return "Click a bridge to build it. Use Up and Down to select, Enter to build, or Escape to cancel.";
  }
  case CITY_CANVAS_MAP_VIEWPORT:
    switch (CityViewportControlAt(window, x, y)) {
    case CITY_VIEWPORT_ROTATE_CLOCKWISE: return "Rotate this viewport clockwise.";
    case CITY_VIEWPORT_ROTATE_COUNTERCLOCKWISE: return "Rotate this viewport counterclockwise.";
    case CITY_VIEWPORT_ZOOM_IN: return "Zoom in on this viewport.";
    case CITY_VIEWPORT_ZOOM_OUT: return "Zoom out of this viewport.";
    case CITY_VIEWPORT_MAIN_CENTER: return "Center the main view on this viewport's location.";
    case CITY_VIEWPORT_CENTER: return "Center this viewport on the main view's location.";
    default: return "Another view of the same city. Scroll to pan this view independently.";
    }
  case CITY_CANVAS_TILE_INFORMATION:
  case CITY_CANVAS_FACILITY_INFORMATION:
    return "Information about the selected tile or building and its services.";
  case CITY_CANVAS_LAND_USE: return "The area and percentage of city land used by each category.";
  case CITY_CANVAS_NEWSPAPER: return "Click an article to read it. Scroll the full story; Escape returns to the paper.";
  default: return NULL;
  }
}

static u32 ResolveCityTooltip(Game *game, char *text, size_t size) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CityTooltip *tip = &desktop->tooltip;
  int x, y;
  int scale = game->drawing->options.scale;
  int global_x = tip->pointer_x * 100 / scale, global_y = tip->pointer_y * 100 / scale;
  text[0] = 0;
  if (CityMenuContains(desktop, global_x, global_y)) {
    CityMenuTooltipAt(game, global_x, global_y, text, size);
    return 0;
  }
  for (int i = desktop->count - 1; i >= 0; --i) {
    const Window *window = &desktop->windows[i];
    if (!window->visible || !CanvasPointInRect(Sc2kCanvasDesktopPixelBounds(desktop, window->window_id),
        tip->pointer_x, tip->pointer_y)) continue;
    Sc2kCanvasDesktopPixelPointer(desktop, window->window_id, tip->pointer_x, tip->pointer_y, &x, &y);
    const char *help = WindowTooltipAt(game, window, x, y, text, size);
    if (help && help != text) CopyText(text, size, help);
    return window->window_id;
  }

  return 0;
}

void MoveCityTooltipPointer(Game *game, int x, int y, double now) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CityTooltip *tip = &desktop->tooltip;
  if (tip->pointer_valid && tip->pointer_x == x && tip->pointer_y == y) return;
  if (tip->visible) Sc2kDirtyMarkRect(&desktop->dirty, tip->bounds);
  tip->visible = false;
  tip->pointer_x = x; tip->pointer_y = y;
  tip->show_at = now + (tip->shown_for_target ? kTooltipRepeatDelay : kTooltipDelay);
  tip->pointer_valid = !tip->buttons;
  // Resolve every move, including moves batched before the next frame, so leaving
  // the target resets its shorter redisplay delay.
  UpdateCityTooltip(game, now);
}

static void LayoutCityTooltip(Game *game) {
  CityTooltip *tip = &game->ui->desktop.tooltip;
  const CityInterfaceFont *font = &game->resources->interface_font;
  int scale = game->drawing->options.scale;
  int screen_width = game->drawing->framebuffer.width * 100 / scale;
  int screen_height = game->drawing->framebuffer.height * 100 / scale;
  int limit = IntClamp(screen_width - 16, 16, 220), width = 0;
  tip->line_count = 1;
  memset(tip->lines, 0, sizeof(tip->lines));
  const char *at = tip->text;
  while (*at && tip->line_count <= 8) {
    while (*at == ' ') ++at;
    if (!*at) break;
    const char *end = at;
    while (*end && *end != ' ' && *end != '\n') ++end;
    char candidate[256];
    char *line = tip->lines[tip->line_count - 1];
    int length = IntMin((int)(end - at), 95);
    snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", length, at);
    if (line[0] && (CityInterfaceTextWidth(font, candidate) > limit || strlen(candidate) >= 96)) {
      if (tip->line_count == 8) break;
      line = tip->lines[tip->line_count++];
      snprintf(candidate, sizeof(candidate), "%.*s", length, at);
    }
    CopyText(line, 96, candidate);
    width = IntMax(width, CityInterfaceTextWidth(font, line));
    at = end;
    if (*at == '\n') {
      ++at;
      if (tip->line_count == 8) break;
      ++tip->line_count;
    }
  }
  width = IntMin(width + 12, screen_width - 4);
  int height = tip->line_count * 12 + 8;
  CityRect pixels = Sc2kCanvasCursorBounds(tip->pointer_x, tip->pointer_y, scale);
  CityRect cursor = {pixels.left * 100 / scale, pixels.top * 100 / scale,
      (pixels.right * 100 + scale - 1) / scale, (pixels.bottom * 100 + scale - 1) / scale};
  int centered = cursor.left - (width >> 1);
  const int positions[][2] = {{centered, cursor.bottom + 4}, {centered, cursor.top - height - 4},
      {cursor.right + 4, cursor.top}, {cursor.left - width - 4, cursor.top}};
  int best_overlap = width * height + 1;
  for (int i = 0; i < 4; ++i) {
    int left = IntClamp(positions[i][0], 2, screen_width - width - 2);
    int top = IntClamp(positions[i][1], 2, screen_height - height - 2);
    CityRect bounds = {left, top, left + width, top + height};
    int overlap = IntMax(0, IntMin(bounds.right, cursor.right) - IntMax(bounds.left, cursor.left)) *
        IntMax(0, IntMin(bounds.bottom, cursor.bottom) - IntMax(bounds.top, cursor.top));
    if (overlap < best_overlap) {
      tip->bounds = bounds;
      best_overlap = overlap;
    }
    if (!overlap) break;
  }
}

bool UpdateCityTooltip(Game *game, double now) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CityTooltip *tip = &desktop->tooltip;
  if (!tip->pointer_valid) return false;
  if (tip->buttons || desktop->dropdown.items || desktop->dragging != CITY_CANVAS_DRAG_NONE || desktop->resizing ||
      game->ui->pointer_stroke_count || game->ui->options_slider || game->ui->jukebox_slider) {
    bool visible = tip->visible;
    HideCityTooltip(game);
    return visible;
  }
  char text[256];
  u32 window_id = ResolveCityTooltip(game, text, sizeof(text));
  bool changed = window_id != tip->window_id || strcmp(text, tip->text) != 0;
  bool dirty = false;
  if (changed) {
    dirty = tip->visible;
    if (dirty) Sc2kDirtyMarkRect(&desktop->dirty, tip->bounds);
    tip->visible = false;
    tip->shown_for_target = false;
    tip->window_id = window_id;
    CopyText(tip->text, sizeof(tip->text), text);
    tip->show_at = now + kTooltipDelay;
  }
  if (tip->text[0] && !tip->visible && now >= tip->show_at) {
    LayoutCityTooltip(game);
    tip->visible = true;
    tip->shown_for_target = true;
    Sc2kDirtyMarkRect(&desktop->dirty, tip->bounds);
    return true;
  }
  return dirty;
}

void HideCityTooltip(Game *game) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  CityTooltip *tip = &desktop->tooltip;
  if (tip->visible) Sc2kDirtyMarkRect(&desktop->dirty, tip->bounds);
  tip->visible = tip->pointer_valid = false;
  tip->shown_for_target = false;
  tip->window_id = 0;
  tip->text[0] = 0;
}

void DrawCityTooltip(const CityCanvasDesktop *desktop, Canvas *canvas) {
  const CityTooltip *tip = &desktop->tooltip;
  if (!tip->visible) return;
  CityRect b = tip->bounds;
  Canvas local = *canvas;
  local.raster.origin_x = local.raster.origin_y = 0;
  local.raster.offset_x = local.raster.offset_y = 0;
  CanvasFill(&local, b.left, b.top, b.right, b.bottom, 0xFF101010);
  CanvasFill(&local, b.left + 1, b.top + 1, b.right - 1, b.bottom - 1, 0xFFFFFF80);
  local.clip = IntersectCityRects(local.clip, (CityRect){b.left + 2, b.top + 2, b.right - 2, b.bottom - 2});
  for (int i = 0; i < tip->line_count; ++i)
    DrawCityInterfaceTextCentered(local.interface_font, &local,
        (CityRect){b.left + 4, b.top + 4 + i * 12, b.right - 4, b.top + 16 + i * 12},
        tip->lines[i], 0xFF101010, false, 10);
}
