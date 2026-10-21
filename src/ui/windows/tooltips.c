#include "city/engine/command_types.h"
#include "city/maps/map_types.h"
#include "ui/windows/state.h"
#include "drawing/drawing_internal.h"
#include "ui/cursor/cursor.h"

// Shared hover help. Control providers supply text; this owner handles lifetime,
// wrapping and damage for every window without changing City state.

static const double kTooltipDelay = 0.6, kTooltipRepeatDelay = 0.15;

static const char *StartupTooltipAt(const Window *window, int x, int y, char *text, size_t size) {
  const CityCanvasStartupWindow *layout = window->startup_layout;
  if (!layout) return NULL;
  for (int i = layout->control_count - 1; i >= 0; --i) {
    const CityCanvasStartupControl *control = &layout->controls[i];
    CityRect b = control->bounds;
    b.left += window->bounds.left + 2; b.right += window->bounds.left + 2;
    b.top += window->bounds.top + 16; b.bottom += window->bounds.top + 16;
    if (!control->visible || !CanvasPointInRect(b, x, y)) continue;
    const char *help = control->tooltip ? control->tooltip : control->text;
    if (control->style & CITY_RESOURCE_STYLE_DISABLED) {
      snprintf(text, size, "%s %s", help,
          control->id == CITY_SETUP_MAP_WIDTH || control->id == CITY_SETUP_MAP_HEIGHT ?
          "Turn off SimCity 2000 compatibility to unlock larger map sizes." :
          control->id == CITY_SETUP_SNOW_COVERAGE ? "Choose Snowy or Mixed terrain to enable this control." :
          control->id == CITY_SETUP_DESERT_COVERAGE ? "Choose Desert or Mixed terrain to enable this control." :
          "Unavailable with the current selections; clear the conflicting option first.");
      return text;
    }
    return help;
  }
  return layout->terrain_setup ?
      "Terrain preview. Large maps use a reduced-size preview; Start City creates the full-size terrain." : NULL;
}

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
      int control = CityViewportControlAt(window, x, y);
      switch (control) {
      case CITY_VIEWPORT_SHADE: return window->shaded ? "Expand this window." : "Shade this window to its title bar.";
      case CITY_VIEWPORT_DEFAULT_SIZE: return "Restore the default window size.";
      case CITY_VIEWPORT_STICKY: return window->sticky ?
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
    return StartupTooltipAt(window, x, y, text, size);
  case CITY_CANVAS_MAP_DATA: return CityMapTooltipAt(window, x, y);
  case CITY_CANVAS_POLICIES: {
    if (CanvasPointInRect(CityPolicyPanelBounds(b, CITY_POLICY_GROUP_COUNT), x, y))
      return "Total is the annual cost of current ordinances. YTD is the amount recorded so far this year. "
          "Year-end estimate includes the remaining months.";
    if (Sc2kCanvasPolicyAt(window, x, y) >= 0)
      return "Enable or disable this city ordinance. The amount shows its estimated annual income or cost.";
    return "Each category heading shows its estimated annual income or cost. Changes take effect immediately.";
  }
  case CITY_CANVAS_BUDGET_RATE:
    return "Enter a percentage, then press Enter or OK. Escape or Cancel keeps the previous value.";
  case CITY_CANVAS_BUDGET: {
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_BORROW, 0), x, y))
      return "Borrow $10,000. Review the offered annual interest rate before confirming.";
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_REPAY, 0), x, y))
      return "Repay the oldest $10,000 bond. Review its interest rate before confirming.";
    if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_SHOW_BONDS, 0), x, y))
      return "List every outstanding bond, oldest first, with its interest rate and annual payment.";
    if (y >= b.top + 229) {
      if (x < b.left + 232) return "Own funds plus loans equals the bank balance.";
      if (y >= b.top + 253) return "Year-end balance equals the current bank balance plus estimated annual profit.";
      return "The interest rate offered for a new bond and the city's debt-to-assets credit grade.";
    }
    for (int category = 0; category < CITY_BUDGET_ROW_COUNT; ++category) {
      CityRect row = CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_CATEGORY, category);
      if (y < row.top || y >= row.bottom) continue;
      if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_ADVISOR, category), x, y))
        return "Ask the advisor about this budget category.";
      if (x >= b.right - 100) return "Estimated income or expense for the full year.";
      if (x >= b.right - 214) return "Income or expense accumulated so far this year.";
      if (category == CITY_BUDGET_ROW_ORDINANCES) {
        if (CanvasPointInRect(CityBudgetControlBounds(b, CITY_BUDGET_CONTROL_OPEN_ORDINANCES, 0), x, y))
          return "Open city ordinances.";
        return "Ordinance income and expenses. Use Open to manage city ordinances.";
      }
      if (category == CITY_BUDGET_ROW_BONDS)
        return "Bond interest payments. Use Show bonds to review the bond list.";
      if (x >= b.left + 150) return category <= CITY_BUDGET_ROW_INDUSTRIAL ?
          "Set this property tax rate. Click the value to type; arrows change by 1." :
          "Set service funding. Click the value to type; arrows change by 5, or 1 with Shift.";
      return "City budget category. Use the question mark to ask its advisor.";
    }
    return "Manage city taxes, service funding and financial forecasts.";
  }
  case CITY_CANVAS_BONDS:
    if (CanvasPointInRect(CityBondResizeGripBounds(window), x, y))
      return "Drag this corner to resize the bond list.";
    return "Scroll all bond information using the bar, wheel or arrow keys. Bonds are listed oldest first.";
  case CITY_CANVAS_JUKEBOX:
  case CITY_CANVAS_MUSIC_PROGRAMME: {
    int action = Sc2kCanvasJukeboxActionAt(game->ui, window, x, y);
    switch (action) {
    case JUKEBOX_PREVIOUS: return "Play the previous music track.";
    case JUKEBOX_NEXT: return "Play the next music track.";
    case JUKEBOX_STOP: return game->ui->desktop.menu.show_shortcuts ? "Stop music playback (F9)." :
        "Stop music playback.";
    case JUKEBOX_PLAY: return game->ui->desktop.menu.show_shortcuts ? "Start music playback (F9)." :
        "Start music playback.";
    case JUKEBOX_SHUFFLE: return "Shuffle the selected music programme.";
    case JUKEBOX_PROGRAMME: return "Edit the music programme.";
    case JUKEBOX_CLEAR: return "Clear the custom music programme.";
    default:
      if (action >= JUKEBOX_PLAY_TRACK) return "Play this track.";
      if (action >= JUKEBOX_EFFECTS_VOLUME) return "Adjust sound effect volume.";
      if (action >= JUKEBOX_MUSIC_VOLUME) return "Adjust music volume.";
      if (action >= JUKEBOX_REMOVE) return "Remove this track from the custom programme.";
      if (action >= JUKEBOX_ADD) return "Add this track to the custom programme.";
      if (action >= JUKEBOX_SELECT) return "Select a music programme.";
      return NULL;
    }
  }
  case CITY_CANVAS_HISTORY:
  case CITY_CANVAS_DEMOGRAPHICS:
  case CITY_CANVAS_NEIGHBORS:
  case CITY_CANVAS_INDUSTRY: {
    int code, control, value;
    if (Sc2kCanvasChartActionAt(window, x, y, &code, &control, &value)) {
      const CityCanvasChartControl *item = Sc2kCanvasChartControl(&game->ui->charts, code, control);
      return item ? item->text : "Choose the data shown in this graph.";
    }
    return "City statistics. Use the controls to choose the data and time period.";
  }
  case CITY_CANVAS_FILE_SAVE:
  case CITY_CANVAS_FILE_OPEN:
    return "Choose a folder and city file. Type a filename, then confirm; Cancel leaves the current city open.";
  case CITY_CANVAS_SIGN_EDIT: {
    CitySignControl control = CitySignControlAt(window, x, y);
    if (control >= CITY_SIGN_COLOR_FIRST && control <= CITY_SIGN_COLOR_LAST) {
      if (control == CITY_SIGN_COLOR_FIRST) return "Default green. Leaves more room for the sign name.";
      if (strlen(game->ui->signs.name.text) >= CITY_OVERLAY_NAME_MAX_LENGTH)
        return "Shorten the sign to 22 characters before choosing a color.";
      return "Choose the sign's color. Press OK to apply it, or Cancel to keep the current color.";
    }
    if (control == CITY_SIGN_TEXT) return "Click to place the caret, or move it with Left and Right.";
    if (control == CITY_SIGN_MOVE) return "Choose a new map location for this sign.";
    if (control == CITY_SIGN_DELETE) return "Delete this sign.";
    return "Edit this sign's text and color, then confirm or cancel the changes.";
  }
  case CITY_CANVAS_SIGN_LIST: return "Find signs by name. Click a sign to center the map; Ctrl-click to edit it.";
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
