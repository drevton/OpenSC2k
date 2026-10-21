#include "drawing/sprites/sprites.h"
#include "assets/resource_types.h"
#include "audio/types.h"
#include "city/economy/types.h"
#include "city/engine/city_types.h"
#include "city/engine/command_types.h"
#include "city/infrastructure/types.h"
#include "ui/windows/state.h"
#include "game/observation.h"
#include "city/presentation.h"
#include "drawing/drawing_internal.h"
#include "drawing/bitmaps.h"
#include "ui/dialogs/span_internal.h"
#include "ui/sdl.h"

// Drawing subsystem: dialogs.

const u32 canvas_text = 0xFF101010u;

typedef struct { u32 face, light, dark; } MessageColors;

static const MessageColors kMessageColors[] = {
  [CITY_MESSAGE_INFORMATION] = {0xFF74ACC0, 0xFFCCF0FC, 0xFF286078}, // Information: light blue.
  [CITY_MESSAGE_WARNING] = {0xFFE8B810, 0xFFFCF880, 0xFFB87818}, // Warning: yellow.
  [CITY_MESSAGE_ERROR] = {0xFFE00000, 0xFFFC6458, 0xFFA00000}, // Errors and confirmations: red.
};

static void DrawMessagePanel(Canvas *canvas, CityRect b, const char *title, CityMessageStyle style) {
  MessageColors colors = kMessageColors[style];
  DrawCanvasFrame(canvas, (CityRect){b.left, b.top + 14, b.right, b.bottom},
      colors.face, colors.light, colors.dark, true);
  DrawCityWindowCaption(canvas, b, title, colors.face, colors.light, colors.dark, 0);
}

static void DrawMessageButton(Canvas *canvas, CityRect b, const char *label, CityMessageStyle style) {
  bool warning = style == CITY_MESSAGE_WARNING;
  MessageColors colors = warning ? (MessageColors){0xFF244C78, 0xFF80A4CC, 0xFF102840} :
      kMessageColors[CITY_MESSAGE_WARNING];
  DrawCanvasButtonFrame(canvas, b, colors.face, colors.light, colors.dark, true);
  --b.top;
  --b.bottom;
  DrawCityInterfaceTextCentered(canvas->interface_font, canvas, b, label,
      warning ? 0xFFFCFCFC : canvas_text, warning, 10);
}

static int MessageButtonCount(CityMessageButtons buttons) {
  switch (buttons) {
  case CITY_MESSAGE_OK: return 1;
  case CITY_MESSAGE_YES_NO: return 2;
  case CITY_MESSAGE_YES_NO_CANCEL: return 3;
  default: return 0;
  }
}

static CityRect MessageButtonBounds(const Window *window, int index) {
  CityRect b = window->bounds;
  int count = MessageButtonCount(window->message_buttons);
  int width = IntMin(100, (b.right - b.left - 32 - (count - 1) * 8) / count);
  int left = (b.left + b.right - count * width - (count - 1) * 8) / 2 + index * (width + 8);
  return (CityRect){left, b.bottom - 22, left + width, b.bottom - 10};
}

static int MessageButtonAnswer(CityMessageButtons buttons, int index) {
  static const int kYesNo[] = {CITY_DIALOG_NO, CITY_DIALOG_YES};
  static const int kSave[] = {CITY_DIALOG_YES, CITY_DIALOG_NO, CITY_DIALOG_CANCEL};
  return buttons == CITY_MESSAGE_OK ? CITY_DIALOG_OK :
      buttons == CITY_MESSAGE_YES_NO ? kYesNo[index] : kSave[index];
}

int CityMessageAnswerAt(const Window *window, int x, int y) {
  if (CanvasCloseAt(window->bounds, x, y)) {
    if (window->message_buttons == CITY_MESSAGE_YES_NO_CANCEL) return CITY_DIALOG_CANCEL;
    return window->message_buttons == CITY_MESSAGE_YES_NO ? CITY_DIALOG_NO : CITY_DIALOG_OK;
  }
  for (int index = 0; index < MessageButtonCount(window->message_buttons); ++index)
    if (CanvasPointInRect(MessageButtonBounds(window, index), x, y))
      return MessageButtonAnswer(window->message_buttons, index);
  return CITY_DIALOG_NONE;
}

// Shared measuring/drawing pass: interface-font metrics and UTF-8 boundaries agree at every UI scale.
static int DialogText(Canvas *canvas, const CityInterfaceFont *font, CityRect b, const char *text,
    u32 color, bool centered) {
  CityRect clip = {0};
  if (canvas) {
    clip = canvas->clip;
    canvas->clip = IntersectCityRects(clip, b);
  }
  int y = b.top;
  while (*text) {
    const char *end = text, *word = NULL;
    while (*end && *end != '\n' && *end != '\r') {
      const char *next = end;
      ReadCityFontCharacter(&next);
      if (next - text >= 256 || CityInterfaceTextPrefixWidth(font, text, (int)(next - text)) > b.right - b.left) break;
      if (*end == ' ') word = end;
      end = next;
    }
    if (*end && *end != '\n' && *end != '\r' && word) end = word;
    if (end == text && *end != '\n' && *end != '\r') ReadCityFontCharacter(&end);
    if (canvas) {
      char line[256];
      size_t length = end - text;
      memcpy(line, text, length);
      line[length] = 0;
      int x = b.left + (centered ? (b.right - b.left - CityInterfaceTextWidth(font, line)) / 2 : 0);
      DrawCityInterfaceText(font, canvas, x, y, line, color, color != canvas_text);
    }
    y += 12;
    text = end;
    if (*text == '\r') {
      ++text;
      if (*text == '\n') ++text;
    } else if (*text == '\n') ++text;
    else while (*text == ' ') ++text;
  }
  if (canvas) canvas->clip = clip;
  return y - b.top;
}

CityRect CityMessageBounds(const CityCanvasDesktop *desktop, const Window *window) {
  int screen_width = desktop->canvas_width, screen_height = desktop->canvas_height;
  if (desktop->pixel_state.scale) {
    screen_width = (int)((i64)desktop->pixel_state.width * 100 / desktop->pixel_state.scale);
    screen_height = (int)((i64)desktop->pixel_state.height * 100 / desktop->pixel_state.scale);
  }
  if (window->advisor_category) {
    int width = IntMin(CITY_ADVISOR_WINDOW_WIDTH, IntMax(80, screen_width - 8));
    int text_width = width - CITY_ADVISOR_PORTRAIT_WIDTH / CITY_ADVISOR_PORTRAIT_SCALE -
        2 * CITY_ADVISOR_CONTENT_MARGIN - CITY_ADVISOR_PORTRAIT_GAP;
    int content_height = IntMax(CITY_ADVISOR_PORTRAIT_HEIGHT / CITY_ADVISOR_PORTRAIT_SCALE,
        DialogText(NULL, desktop->interface_font,
        (CityRect){0, 0, text_width, 0}, window->message, 0, false));
    int height = 14 + CITY_ADVISOR_BUTTON_SIZE + 2 * CITY_ADVISOR_CONTENT_MARGIN + content_height;
    int left = IntMax(0, (screen_width - width) / 2), top = IntMax(0, (screen_height - height) / 2);
    return (CityRect){left, top, left + width, top + height};
  }
  bool buttons = MessageButtonCount(window->message_buttons) != 0;
  int portrait = window->portrait ? window->portrait->width + 8 : 0;
  int width = CityInterfaceTextWidth(desktop->interface_font, window->title) + 28;
  for (const char *line = window->message; *line;) {
    const char *end = line;
    while (*end && *end != '\n' && *end != '\r') ++end;
    width = IntMax(width, CityInterfaceTextPrefixWidth(desktop->interface_font, line, end - line) + portrait + 24);
    line = end;
    if (*line == '\r') ++line;
    if (*line == '\n') ++line;
  }
  int maximum = IntMax(80, screen_width - 8);
  width = IntMin(maximum, IntMin(buttons ? 320 : 420, IntMax(220, width)));
  int padding = buttons ? 56 : 30;
  int height;
  do {
    height = padding + IntMax(window->portrait ? window->portrait->height : 0, DialogText(NULL, desktop->interface_font,
        (CityRect){0, 0, width - portrait - 24, 0}, window->message, 0, false));
    if (height <= screen_height - 8 || width == maximum) break;
    width = IntMin(maximum, width + 40);
  } while (true);
  int left = IntMax(0, (screen_width - width) / 2), top = IntMax(0, (screen_height - height) / 2);
  return (CityRect){left, top, left + width, top + height};
}

CityRect CityCommandErrorBounds(Game *game, const char *summary, const char *detail) {
  const CityCanvasDesktop *desktop = &game->ui->desktop;
  int width = IntMax(CityInterfaceTextWidth(desktop->interface_font, summary),
      CityInterfaceTextWidth(desktop->interface_font, detail)) + 24;
  width = IntMin(IntClamp(desktop->canvas_width - 8, 32, 300), IntMax(220, width));
  CityRect text = {0, 0, width - 24, 0};
  int height = 30 + DialogText(NULL, desktop->interface_font, text, summary, 0, true);
  if (*detail) height += 8 + DialogText(NULL, desktop->interface_font, text, detail, 0, true);
  int left = (desktop->canvas_width - width) / 2;
  int top = IntMax(CITY_MENU_BAR_HEIGHT + 2, (desktop->canvas_height - height) / 3);
  int x, y;
  if (game->drawing->ui_display && Sc2kSDLUIPointer(game->drawing->ui_display, &x, &y) &&
      x >= 0 && x < desktop->canvas_width && y >= 0 && y < desktop->canvas_height) {
    // OpenTTD ErrmsgWindow::OnInitialPosition keeps 200 screen pixels between
    // the cursor and the message. UI coordinates here are scaled canvas units.
    int gap = (20000 + game->drawing->options.scale - 1) / game->drawing->options.scale;
    int upper = CITY_MENU_BAR_HEIGHT + 2;
    int lower = desktop->canvas_height - 2;
    int above = y - upper, below = lower - y;
    left = IntClamp(x - width / 2, 2, desktop->canvas_width - width - 2);
    if (above >= height + gap || (below < height + gap && above >= below))
      top = y - height - IntMin(gap, IntMax(8, above - height));
    else
      top = y + IntMin(gap, IntMax(8, below - height));
    top = IntClamp(top, upper, lower - height);
    // A tall message may fit on neither side vertically. Keep it beside the
    // cursor when horizontal room is available instead of covering the click.
    if (y >= top - 8 && y < top + height + 8) {
      if (x >= width + 10) left = x - width - 8;
      else if (desktop->canvas_width - x >= width + 10) left = x + 8;
    }
  }
  return (CityRect){left, top, left + width, top + height};
}

static void DrawCityAdvisorMessage(Canvas *canvas, const Window *window) {
  const u32 face = 0xFF7E9696, light = 0xFFB4CCCA, dark = 0xFF4D6565;
  CityRect b = window->bounds;
  int top = b.top + 14 + CITY_ADVISOR_BUTTON_SIZE;
  DrawCanvasFrame(canvas, (CityRect){b.left, top, b.right, b.bottom},
      face, light, dark, true);
  DrawCityWindowCaption(canvas, b, window->title, face, light, dark, 0);
  int portrait_top = top + CITY_ADVISOR_CONTENT_MARGIN;
  int portrait_width = CITY_ADVISOR_PORTRAIT_WIDTH / CITY_ADVISOR_PORTRAIT_SCALE;
  int portrait_height = CITY_ADVISOR_PORTRAIT_HEIGHT / CITY_ADVISOR_PORTRAIT_SCALE;
  CityRect bounds = {b.left + CITY_ADVISOR_CONTENT_MARGIN, portrait_top,
      b.left + CITY_ADVISOR_CONTENT_MARGIN + portrait_width, portrait_top + portrait_height};
  DrawCanvasFrame(canvas, (CityRect){bounds.left - 1, bounds.top - 1, bounds.right + 1, bounds.bottom + 1},
      face, light, dark, false);
  DrawCanvasSprite(canvas, window->picture_drawing, window->portrait, bounds);
  DialogText(canvas, canvas->interface_font,
      (CityRect){b.left + CITY_ADVISOR_CONTENT_MARGIN + portrait_width + CITY_ADVISOR_PORTRAIT_GAP,
          portrait_top, b.right - CITY_ADVISOR_CONTENT_MARGIN, b.bottom - CITY_ADVISOR_CONTENT_MARGIN},
      window->message, canvas_text, false);
}

void DrawCityMessageBox(Canvas *canvas, const Window *window) {
  if (window->advisor_category) {
    DrawCityAdvisorMessage(canvas, window);
    return;
  }
  CityRect b = window->bounds;
  CityMessageStyle style = window->message_style;
  DrawMessagePanel(canvas, b, window->command_error ? "Message" : window->title, style);
  CityRect text = {b.left + 12, b.top + 23, b.right - 12,
      b.bottom - (MessageButtonCount(window->message_buttons) ? 32 : 7)};
  if (window->command_error) {
    int height = DialogText(canvas, canvas->interface_font, text, window->title, 0xFFFCFCFC, true);
    text.top += height + 8;
    DialogText(canvas, canvas->interface_font, text, window->message, 0xFFFCFCFC, true);
  } else {
    if (window->portrait) {
      int width = window->portrait->width, height = window->portrait->height;
      int top = text.top + IntMax(0, (text.bottom - text.top - height) / 2);
      DrawCanvasSprite(canvas, window->picture_drawing, window->portrait,
          (CityRect){text.left, top, text.left + width, top + height});
      text.left += width + 8;
    }
    int height = DialogText(NULL, canvas->interface_font, text, window->message, 0, false);
    text.top += IntMax(0, (text.bottom - text.top - height) / 2);
    DialogText(canvas, canvas->interface_font, text, window->message,
        style == CITY_MESSAGE_ERROR ? 0xFFFCFCFC : canvas_text, !window->portrait);
  }
  for (int index = 0; index < MessageButtonCount(window->message_buttons); ++index) {
    int answer = MessageButtonAnswer(window->message_buttons, index);
    const char *label = answer == CITY_DIALOG_OK ? "OK" : answer == CITY_DIALOG_YES ? "Yes" :
        answer == CITY_DIALOG_NO ? "No" : "Cancel";
    DrawMessageButton(canvas, MessageButtonBounds(window, index), label, style);
  }
}

// Drawing entry points retained for the standalone rendering diagnostics.
void Sc2kCanvasDrawNotice(Canvas *canvas, CityRect bounds, const char *title, const char *message) {
  DrawCityMessageBox(canvas, &(Window){.bounds = bounds, .title = title, .message = message,
      .message_style = CITY_MESSAGE_INFORMATION, .message_buttons = CITY_MESSAGE_NO_BUTTONS});
}

void Sc2kCanvasDrawSpanStyle(Canvas *canvas, CityRect bounds, const char *title, const char *message) {
  static const char *const styles[CITY_ROAD_SPAN_STYLE_COUNT] = {"Basic", "Arch", "Decorated"};
  CanvasPanelFrame(canvas, bounds, title);
  CanvasBeveledBox(canvas, bounds.left + 9, bounds.top + 28, bounds.right - 9, bounds.top + 58,
      canvas_light, 0);
  CanvasTextWrapped(canvas, bounds.left + 16, bounds.top + 38, bounds.right - 16,
      bounds.top + 54, message, canvas_text);
  for (i32 style = 0; style < CITY_ROAD_SPAN_STYLE_COUNT; ++style) {
    i32 top = bounds.top + 64 + style * 30;
    DrawCanvasButtonBox(canvas, bounds.left + 24, top, bounds.right - 24, top + 24, canvas_light, 1);
    CanvasTextAt(canvas, bounds.left + 42, top + 8, styles[style], canvas_text);
  }
  DrawCanvasButtonBox(canvas, bounds.right - 68, bounds.bottom - 29, bounds.right - 12, bounds.bottom - 10,
      canvas_light, 1);
  CanvasTextAt(canvas, bounds.right - 59, bounds.bottom - 24, "Cancel", canvas_text);
}

int ReadCitySpanList(const Window *window, u8 order[CITY_SPAN_STYLE_COUNT]) {
  int count = 0;
  for (int style = 0; style < CITY_SPAN_STYLE_COUNT; ++style) {
    if (!(window->span_styles & (1 << style))) continue;
    int position = count++;
    while (position > 0) {
      int previous = order[position - 1];
      int compare = window->span_sort == CITY_SPAN_SORT_NAME ? strcmp(kCitySpanNames[style], kCitySpanNames[previous]) :
          window->span_prices[style] < window->span_prices[previous] ? -1 :
          window->span_prices[style] > window->span_prices[previous];
      if (window->span_descending ? compare <= 0 : compare >= 0) break;
      order[position] = previous;
      --position;
    }
    order[position] = style;
  }
  return count;
}

int CitySpanVisibleRows(const Window *window) {
  return IntMax(1, (window->bounds.bottom - window->bounds.top - CITY_SPAN_LIST_TOP -
      CITY_SPAN_FOOTER_HEIGHT) / CITY_SPAN_ROW_HEIGHT);
}

CityRect CitySpanControlBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  if (control >= 0) {
    int top = b.top + CITY_SPAN_LIST_TOP + control * CITY_SPAN_ROW_HEIGHT;
    return (CityRect){b.left + 1, top, b.right - 12, top + CITY_SPAN_ROW_HEIGHT};
  }
  switch (control) {
  case CITY_CANVAS_SPAN_HIT_DIRECTION: return (CityRect){b.left + 1, b.top + 14, b.left + 65, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_SORT: return (CityRect){b.left + 65, b.top + 14, b.right - 12, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_UP: return (CityRect){b.right - 12, b.top + 14, b.right, b.top + 26};
  case CITY_CANVAS_SPAN_HIT_DOWN: return (CityRect){b.right - 12, b.bottom - 26, b.right, b.bottom - 14};
  case CITY_CANVAS_SPAN_HIT_TRACK: return (CityRect){b.right - 12, b.top + 26, b.right, b.bottom - 26};
  case CITY_CANVAS_SPAN_HIT_RESTORE: return (CityRect){b.right - 11, b.top, b.right, b.top + 14};
  default: return (CityRect){0};
  }
}

CityRect CitySpanScrollThumb(const Window *window) {
  CityRect track = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_TRACK);
  u8 order[CITY_SPAN_STYLE_COUNT];
  int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
  CityRect thumb = ReadCityScrollbar(track, count, visible, window->span_scroll, 8).thumb;
  if (count > visible) { ++thumb.left; --thumb.right; }
  return thumb;
}

static void DrawBridgeArrow(Canvas *canvas, CityRect b, bool up, u32 ink) {
  int x = (b.left + b.right) / 2, y = (b.top + b.bottom) / 2;
  for (int row = 0; row < 4; ++row) {
    int top = y + (up ? 1 - row : row - 2);
    CanvasFill(canvas, x - 4 + row, top, x + 4 - row, top + 1, ink);
  }
}

void CanvasDrawSpanChoices(Canvas *canvas, const Window *window) {
  _Static_assert(CITY_BRIDGE_CHOICE_ICON_COUNT == CITY_BRIDGE_BITMAP_COUNT, "Bridge cache covers every style");
  NativeSpanDialog *dialog = window->city->game->ui->span.active;
  while (dialog->window_id != window->window_id) dialog = dialog->parent;
  CityBridgeWindowDrawing *pixels = dialog->drawing;
  const u32 face = 0xFF687B48, light = 0xFF9BB56C, dark = 0xFF3D4F29, ink = 0xFF101010;
  CityRect b = window->bounds;
  DrawCanvasFrame(canvas, b, face, light, dark, true);
  DrawCityWindowCaption(canvas, b, window->title, face, light, dark, 11);
  CityRect restore = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_RESTORE);
  DrawCanvasButtonFrame(canvas, restore, face, light, dark, true);
  DrawCityWindowControlIcon(canvas, restore, CITY_WINDOW_ICON_DEFAULT_SIZE, false);
  CityRect direction = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_DIRECTION);
  CityRect sort = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_SORT);
  DrawCanvasButtonFrame(canvas, direction, face, light, dark, true);
  DrawCanvasButtonFrame(canvas, sort, face, light, dark, true);
  DrawCityInterfaceText(canvas->interface_font, canvas, direction.left + 3, direction.top + 1, "Sort by", ink, false);
  DrawBridgeArrow(canvas, (CityRect){direction.right - 13, direction.top, direction.right, direction.bottom},
      window->span_descending, ink);
  DrawCityInterfaceText(canvas->interface_font, canvas, sort.left + 3, sort.top + 1,
      window->span_sort == CITY_SPAN_SORT_NAME ? "Name" : "Cost", ink, false);
  DrawBridgeArrow(canvas, (CityRect){sort.right - 12, sort.top, sort.right, sort.bottom}, false, ink);
  u8 order[CITY_SPAN_STYLE_COUNT];
  int count = ReadCitySpanList(window, order), visible = CitySpanVisibleRows(window);
  int scroll = IntMin(window->span_scroll, IntMax(0, count - visible));
  CityRect clip = canvas->clip;
  canvas->clip = IntersectCityRects(clip, (CityRect){b.left + 1, b.top + CITY_SPAN_LIST_TOP,
      b.right - 12, b.bottom - CITY_SPAN_FOOTER_HEIGHT});
  for (int row = 0; row < IntMin(visible, count - scroll); ++row) {
    int style = order[scroll + row];
    CityRect rect = CitySpanControlBounds(window, row);
    DrawCanvasButtonFrame(canvas, rect, style == window->span_selected ? 0xFF7B8D53 : face, light, dark, true);
    const CityInterfaceIcons *icons = window->city ? &window->city->game->resources->bridge_choice_icons : NULL;
    int icon = style + (window->span_rail && style >= CITY_ROAD_BRIDGE_STYLE_FIRST ?
        CITY_ROAD_BRIDGE_STYLE_COUNT : 0);
    if (icons && !DrawCityInterfaceIcon(canvas,
        &pixels->icons[icon], icons, icon,
        (CityRect){rect.left + 2, rect.top, rect.left + 48, rect.bottom}))
      window->city->game->execution_failed = true;
    DrawCityInterfaceText(canvas->interface_font, canvas, rect.left + 51, rect.top + 1,
        kCitySpanNames[style], 0xFFFFD800, true);
    char price[CITY_CURRENCY_TEXT_CAPACITY];
    if (window->city) FormatCityCurrency(window->city->game, price, window->span_prices[style]);
    else snprintf(price, sizeof(price), "$%u", window->span_prices[style]);
    DrawCityInterfaceText(canvas->interface_font, canvas, rect.left + 51, rect.top + 12,
        price, 0xFFFCFCFC, true);
  }
  canvas->clip = clip;
  CityRect track = CitySpanControlBounds(window, CITY_CANVAS_SPAN_HIT_TRACK);
  DrawCanvasFrame(canvas, track, dark, light, dark, false);
  if (count > visible)
    DrawCanvasFrame(canvas, CitySpanScrollThumb(window), face, light, dark, true);
  else DrawCanvasFrame(canvas, track, face, light, dark, false);
  for (int control = CITY_CANVAS_SPAN_HIT_UP; control >= CITY_CANVAS_SPAN_HIT_DOWN; --control) {
    CityRect rect = CitySpanControlBounds(window, control);
    bool up = control == CITY_CANVAS_SPAN_HIT_UP;
    bool disabled = up ? scroll == 0 : scroll >= count - visible;
    DrawCanvasButtonFrame(canvas, rect, face, light, dark, true);
    DrawBridgeArrow(canvas, rect, up, disabled ? dark : ink);
  }
  CityRect grip = {b.right - 12, b.bottom - 12, b.right, b.bottom};
  DrawCanvasFrame(canvas, grip, face, light, dark, true);
  for (int line = 0; line < 3; ++line)
    for (int pixel = 0; pixel < 3 + line * 2; ++pixel)
      CanvasFill(canvas, b.right - 3 - pixel, b.bottom - 3 - line * 2 + pixel,
          b.right - 2 - pixel, b.bottom - 2 - line * 2 + pixel, dark);
}

void Sc2kCanvasDrawPolicies(Game *game, Canvas *canvas, const Window *window) {
  static const char *const headings[CITY_POLICY_GROUP_COUNT] = {
    "Finance", "Safety & Health", "Education", "Promotional", "Other"
  };
  static const char *const policies[CITY_ORDINANCE_COUNT] = {
    "1% Sales Tax", "1% Income Tax", "Legalized Gambling", "Parking Fines",
    "Volunteer Fire Dept.", "Public Smoking Ban", "Free Clinics", "Junior Sports",
    "Pro-Reading Campaign", "Anti-Drug Campaign", "CPR Training", "Neighborhood Watch",
    "Tourist Advertising", "Business Advertising", "City Beautification", "Annual Carnival",
    "Energy Conservation", "Nuclear Free Zone", "Homeless Shelters", "Pollution Controls"
  };
  CityRect bounds = window->bounds;
  CanvasBeveledBox(canvas, bounds.left, bounds.top + 14, bounds.right, bounds.bottom, canvas_face, 1);
  DrawCityWindowCaption(canvas, bounds, window->title, 0xFF247CC9, 0xFF78B4E4, 0xFF124C83, 0);
  char amount[CITY_CURRENCY_TEXT_CAPACITY];
  i64 annual_total = 0;

  for (i32 group = 0; group < CITY_POLICY_GROUP_COUNT; ++group) {
    CityRect panel = CityPolicyPanelBounds(bounds, group);
    i32 left = panel.left, top = panel.top, right = panel.right;
    CanvasBeveledBox(canvas, left, top, right, panel.bottom, canvas_face, 0);
    DrawCityInterfaceText(canvas->interface_font, canvas, left + 4, top + 3, headings[group], 0xFFFCFCFC, true);
    i64 annual_cost = window->policy_group_cost[group] / 75;
    annual_total += annual_cost;
    FormatCityCurrency(game, amount, annual_cost);
    CanvasTextRight(canvas, right - 4, top + 3, amount, canvas_text);
    for (i32 item = 0; item < 4; ++item) {
      i32 policy = group * CITY_POLICIES_PER_GROUP + item;
      i32 row_top = top + CITY_POLICY_ROWS_TOP + item * CITY_POLICY_ROW_PITCH;
      i32 row_height = CITY_POLICY_ROW_HEIGHT;
      i32 check_left = left + 4;
      i32 check_right = check_left + 16;
      bool checked = (window->policy_flags & (1 << policy)) != 0;
      DrawCityCheckbox(canvas, (CityRect){check_left, row_top + 1, check_right, row_top + 11},
          checked, canvas_face, canvas_light, canvas_dark);
      CanvasTextAt(canvas, check_right + 4, row_top + (row_height - 10) / 2,
          policies[policy], canvas_text);
      if (checked) {
        FormatCityCurrency(game, amount, window->policy_cost[policy] / 75);
        CanvasTextRight(canvas, right - 4, row_top + (row_height - 10) / 2, amount, canvas_text);
      }
    }
  }

  CityRect totals = CityPolicyPanelBounds(bounds, CITY_POLICY_GROUP_COUNT);
  CanvasBeveledBox(canvas, totals.left, totals.top, totals.right, totals.bottom, canvas_face, 0);
  DrawCityInterfaceText(canvas->interface_font, canvas, totals.left + 4, totals.top + 3,
      "Ordinance Totals", 0xFFFCFCFC, true);
  int cost_left = totals.left, cost_right = totals.right;
  int total_y = totals.top + CITY_POLICY_ROWS_TOP + 4;
  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y, "Total", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, annual_total);
  DrawCityInterfaceText(canvas->interface_font, canvas,
      cost_right - 4 - CityInterfaceTextWidth(canvas->interface_font, amount), total_y, amount, 0xFFFCFCFC, true);

  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y + 12, "YTD", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, window->policy_ytd_total / 900);
  CanvasTextRight(canvas, cost_right - 4, total_y + 12, amount, canvas_text);
  DrawCityInterfaceText(canvas->interface_font, canvas, cost_left + 8, total_y + 24,
      "Year-end estimate", 0xFFFCFCFC, true);
  FormatCityCurrency(game, amount, window->policy_estimated_total / 900);
  CanvasTextRight(canvas, cost_right - 4, total_y + 24, amount, canvas_text);
}

enum { kBondRowHeight = 12, kBondScrollbarWidth = 12, kBondScrollArrowHeight = 10, kBondHeaderRows = 4 };

CityRect CityBondResizeGripBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - kBondScrollbarWidth, b.bottom - kBondScrollbarWidth, b.right, b.bottom};
}

static CityRect CityBondContentBounds(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.left + 1, b.top + 15, b.right - kBondScrollbarWidth - 1, b.bottom - 1};
}

int CityBondContentRows(int count) {
  // Summary, headings, bonds (or the empty message), a gap and the yearly total share one scroll range.
  return kBondHeaderRows + IntMax(1, count) + 2;
}

int CityBondVisibleRows(const Window *window) {
  CityRect list = CityBondContentBounds(window);
  return (list.bottom - list.top) / kBondRowHeight;
}

static CityRect CityBondScrollTrack(const Window *window) {
  CityRect b = window->bounds;
  return (CityRect){b.right - kBondScrollbarWidth, b.top + 14 + kBondScrollArrowHeight,
      b.right, b.bottom - kBondScrollbarWidth - kBondScrollArrowHeight};
}

static CityRect CityBondScrollThumb(const Window *window, int count) {
  CityRect track = CityBondScrollTrack(window);
  return ReadCityScrollbar(track, CityBondContentRows(count), CityBondVisibleRows(window),
      window->bond_scroll, kBondScrollbarWidth).thumb;
}

static void ScrollCityBonds(Game *game, Window *window, int delta) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  int last = IntMax(0, CityBondContentRows(game->city->indicators.bond_count) - CityBondVisibleRows(window));
  int first = IntClamp(IntClamp(window->bond_scroll, 0, last) + delta, 0, last);
  if (first != window->bond_scroll) {
    window->bond_scroll = first;
    Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  }
}

static CityRect CityBondControlBounds(const Window *window, int first, CityBondAction action) {
  CityRect content = CityBondContentBounds(window);
  int y = content.top + 1 + (2 - first) * kBondRowHeight;
  switch (action) {
  case CITY_BOND_ACTION_DECREASE_RATE:
    return (CityRect){content.right - 90, y, content.right - 66, y + kBondRowHeight};
  case CITY_BOND_ACTION_INCREASE_RATE:
    return (CityRect){content.right - 32, y, content.right - 8, y + kBondRowHeight};
  default: return (CityRect){0};
  }
}

static void DrawCityBondScrollArrow(Canvas *canvas, CityRect b, bool up) {
  DrawCanvasButtonFrame(canvas, b, canvas_face, canvas_light, canvas_dark, true);
  int pressed = CityCanvasButtonPressed(canvas, b);
  int center = (b.left + b.right) / 2 + pressed, top = (b.top + b.bottom - 4) / 2 + pressed;
  for (int row = 0; row < 4; ++row) {
    int half = up ? row : 3 - row;
    CanvasFill(canvas, center - half, top + row, center + half + 1, top + row + 1, canvas_text);
  }
}

void DrawCityBonds(const City *city, Canvas *canvas, const Window *window) {
  CityRect b = window->bounds, content = CityBondContentBounds(window);
  int count = city->indicators.bond_count, rows = CityBondContentRows(count);
  int visible = CityBondVisibleRows(window);
  int first = IntClamp(window->bond_scroll, 0, IntMax(0, rows - visible));
  Canvas framed = *canvas;
  framed.unscaled_bevels = false;
  DrawCanvasFrame(&framed, (CityRect){b.left, b.top + 14, b.right - kBondScrollbarWidth, b.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, b, "City Bonds", canvas_title, 0xFFD3BB93, 0xFF674F2B, 0);

  Canvas text = framed;
  text.clip = IntersectCityRects(canvas->clip, content);
  bool rated = (city->fixes & (1 << CITY_FIX_BOND_RATING)) != 0;
  int right = content.right - 6;
  // Draw only visible content rows; the clip also handles a partially visible final row.
  for (int row = first; row < IntMin(rows, first + visible + 1); ++row) {
    int y = content.top + 1 + (row - first) * kBondRowHeight;
    char line[128], amount[CITY_CURRENCY_TEXT_CAPACITY];
    if (row == 0) {
      u64 city_value = city->indicators.asset_value;
      FormatCityCurrency(city->game, amount, city_value > INT64_MAX ? INT64_MAX : city_value);
      snprintf(line, sizeof(line), "Credit: %s    City value: %s", CityBondCreditRating(city), amount);
      CanvasTextAt(&text, content.left + 3, y, line, canvas_text);
    } else if (row == 1) {
      snprintf(line, sizeof(line), "Bonds: %d    Next bond: %d%%    Bank rate: %d%%", count,
          rated ? CityBondInterestRate(city) : window->bond_issue_rate, city->indicators.federal_interest_rate);
      CanvasTextAt(&text, content.left + 3, y, line, canvas_text);
    } else if (row == 2 && !rated) {
      CanvasTextAt(&text, content.left + 3, y, "Next bond rate:", canvas_text);
      snprintf(line, sizeof(line), "%d%%", window->bond_issue_rate);
      DrawCityInterfaceTextCentered(text.interface_font, &text,
          (CityRect){content.right - 66, y, content.right - 32, y + kBondRowHeight},
          line, canvas_text, false, 10);
      DrawMessageButton(&text, CityBondControlBounds(window, first, CITY_BOND_ACTION_DECREASE_RATE), "-",
          CITY_MESSAGE_INFORMATION);
      DrawMessageButton(&text, CityBondControlBounds(window, first, CITY_BOND_ACTION_INCREASE_RATE), "+",
          CITY_MESSAGE_INFORMATION);
    } else if (row == kBondHeaderRows - 1) {
      DrawCityInterfaceText(text.interface_font, &text, content.left + 4, y, "Bond", 0xFFFCFCFC, true);
      DrawCityInterfaceText(text.interface_font, &text, right - 122, y, "Interest", 0xFFFCFCFC, true);
      DrawCityInterfaceText(text.interface_font, &text, right - 47, y, "Per year", 0xFFFCFCFC, true);
    } else if (row >= kBondHeaderRows && row < kBondHeaderRows + IntMax(1, count)) {
      if (!count) {
        CanvasTextAt(&text, content.left + 4, y, "No outstanding bonds.", canvas_text);
        continue;
      }
      int index = row - kBondHeaderRows;
      snprintf(line, sizeof(line), index == 0 ? "Bond %d (oldest)" : "Bond %d", index + 1);
      CanvasTextAt(&text, content.left + 4, y, line, canvas_text);
      int rate = city->economic_tables.bond_rates.rate[index];
      snprintf(line, sizeof(line), "%d%%", rate);
      CanvasTextRight(&text, right - 75, y, line, canvas_text);
      FormatCityCurrency(city->game, line, (i64)rate * 100);
      CanvasTextRight(&text, right, y, line, canvas_text);
    } else if (row == rows - 1) {
      i64 yearly_total = 0;
      for (int index = 0; index < count; ++index)
        yearly_total += (i64)city->economic_tables.bond_rates.rate[index] * 100;
      FormatCityCurrency(city->game, amount, yearly_total);
      snprintf(line, sizeof(line), "Yearly total: %s", amount);
      CanvasTextAt(&text, content.left + 4, y, line, canvas_text);
    }
  }

  CityRect track = CityBondScrollTrack(window);
  DrawCanvasFrame(&framed, track, canvas_dark, canvas_light, canvas_dark, false);
  DrawCanvasFrame(&framed, CityBondScrollThumb(window, count), canvas_face, canvas_light, canvas_dark, true);
  DrawCityBondScrollArrow(&framed,
      (CityRect){track.left, b.top + 14, track.right, track.top}, true);
  DrawCityBondScrollArrow(&framed,
      (CityRect){track.left, track.bottom, track.right, b.bottom - kBondScrollbarWidth}, false);
  CityRect grip = CityBondResizeGripBounds(window);
  DrawCanvasFrame(&framed, grip, canvas_face, canvas_light, canvas_dark, true);
  for (int offset = 3; offset <= 9; offset += 3)
    DrawCanvasLine(canvas, grip.right - offset, grip.bottom - 2,
        grip.right - 2, grip.bottom - offset, canvas_text, 1);
}

// Native canvas requests share the original gate/pause ordering. Original framework-object dispatch remains in the
// comparison backend. Callback/context remain borrowed; without a retained blocking adapter the unanswered result is
// propagated unchanged.
i32 RunPausedCityModalDialog(City *city, void *dialog) {
  return RunPausedCityAction(city, ChooseCityCanvasDialog, dialog);
}

// Open the manual budget through the installed observation adapter or native UI. Requires live City/UI owners; this
// routes presentation and does not itself perform annual settlement.
void ShowBudgetDialogManually(City *city) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->show_manual_budget) observer->show_manual_budget(city);
  else OpenCityBudget(city);
}

// Simulation-visible state of the original mission/bankruptcy callback. Outcome must be supported; cleanup follows the
// actual notice return and requires the attached UI owner to retain the caller's pause lifetime.
void FailRadio(City *city, i32 outcome) {
  city->game->ui->outcome_dialog_active = 1;
  int result = outcome & 0xFFFF;
  if (result <= CITY_OUTCOME_BANKRUPT) {
    PlayCitySound(city, INVALID_TILE,
        result == CITY_OUTCOME_MISSION_WON ? CITY_SOUND_CIVIC_PLACEMENT : CITY_SOUND_DEMOLITION);
    ShowCityMissionOutcome(city, result);
    FinishCityMissionOutcome(city, result);
  }
  city->game->ui->outcome_dialog_active = 0;
}

// Bond scrolling, scrollbar capture and rate controls belong to this window.
// Geometry is shared with painting; input changes only this window's presentation.
bool BondsWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityBonds(game->city, event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  Window *target = FindCityCanvasWindow(desktop, window->window_id);
  switch (input->kind) {
  case CANVAS_SDL_POINTER_DOWN: {
    if (input->button != CANVAS_SDL_BUTTON_LEFT) return false;
    int x = event->x, y = event->y;
    CityRect b = window->bounds;
    CityRect scrollbar = {b.right - kBondScrollbarWidth, b.top + 14, b.right, b.bottom - kBondScrollbarWidth};
    if (CanvasPointInRect(scrollbar, x, y)) {
      CityRect track = CityBondScrollTrack(window);
      CityRect thumb = CityBondScrollThumb(window, game->city->indicators.bond_count);
      if (y < track.top) ScrollCityBonds(game, target, -1);
      else if (y >= track.bottom) ScrollCityBonds(game, target, 1);
      else if (y < thumb.top) ScrollCityBonds(game, target, -CityBondVisibleRows(window));
      else if (y >= thumb.bottom) ScrollCityBonds(game, target, CityBondVisibleRows(window));
      else {
        target->bond_scroll_dragging = true;
        target->bond_scroll_grab = y - thumb.top;
      }
      return true;
    }
    if ((game->city->fixes & (1 << CITY_FIX_BOND_RATING)) ||
        !CanvasPointInRect(CityBondContentBounds(window), x, y)) return false;
    int last = IntMax(0, CityBondContentRows(game->city->indicators.bond_count) - CityBondVisibleRows(window));
    int first = IntClamp(window->bond_scroll, 0, last);
    for (CityBondAction action = CITY_BOND_ACTION_DECREASE_RATE; action <= CITY_BOND_ACTION_INCREASE_RATE; ++action) {
      if (!CanvasPointInRect(CityBondControlBounds(window, first, action), x, y)) continue;
      BeginCityButtonRepeat(game, window, event);
      if (action == CITY_BOND_ACTION_DECREASE_RATE && target->bond_issue_rate > -100) --target->bond_issue_rate;
      if (action == CITY_BOND_ACTION_INCREASE_RATE && target->bond_issue_rate < 100) ++target->bond_issue_rate;
      Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(target));
      return true;
    }
    return false;
  }
  case CANVAS_SDL_POINTER_MOVE: {
    if (!window->bond_scroll_dragging) return false;
    CityScrollbar scrollbar = ReadCityScrollbar(CityBondScrollTrack(window),
        CityBondContentRows(game->city->indicators.bond_count), CityBondVisibleRows(window),
        window->bond_scroll, kBondScrollbarWidth);
    int first = CityScrollbarPosition(&scrollbar, event->y, window->bond_scroll_grab, CITY_SCROLLBAR_DRAG_NEAREST);
    ScrollCityBonds(game, target, first - IntClamp(window->bond_scroll, 0, scrollbar.maximum));
    return true;
  }
  case CANVAS_SDL_POINTER_UP:
  case CANVAS_SDL_FOCUS_LOST: {
    bool dragging = window->bond_scroll_dragging;
    target->bond_scroll_dragging = false;
    return dragging;
  }
  case CANVAS_SDL_SCROLL:
    ScrollCityBonds(game, target, input->y > 0 ? -3 : input->y < 0 ? 3 : 0);
    return true;
  case CANVAS_SDL_KEY_DOWN: {
    int page = CityBondVisibleRows(window);
    int delta = input->key == CITY_KEY_UP ? -1 : input->key == CITY_KEY_DOWN ? 1 :
        input->key == CITY_KEY_PAGE_UP ? -page : input->key == CITY_KEY_PAGE_DOWN ? page : 0;
    if (!delta) return false;
    ScrollCityBonds(game, target, delta);
    return true;
  }
  default: return false;
  }
}
