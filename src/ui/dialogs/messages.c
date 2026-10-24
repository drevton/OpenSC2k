#include "ui/windows/window_internal.h"
#include "ui/dialogs/messages.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/resource_types.h"
#include "assets/catalog.h"
#include "ui/dialogs/alerts.h"
#include "city/presentation.h"
#include "city/state.h"
#include "base/text.h"
#include "drawing/bitmaps.h"
#include "drawing/maps/placement.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "drawing/state.h"
#include "game/state.h"
#include "ui/commands.h"
#include "ui/dialogs/budget.h"
#include "ui/dialogs/dialogs.h"
#include "ui/map.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/windows/windows.h"

// Message variants share the same borrowed-content and opening-failure contract.
i32 Sc2kCanvasDesktopOpenNotice(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_NOTICE, .bounds = bounds, .title = title, .message = message}, flags);
}

bool OpenCityNoticeWithCallback(City *city, const char *title, const char *text, u32 flags,
    CityWindowCallback *callback, void *context) {
  int slot = Sc2kCanvasDesktopOpenNotice(&city->game->ui->desktop, (CityRect){70, 65, 530, 395},
      title, text, flags);
  if (slot < 0) return false;
  Window *window = &city->game->ui->desktop.windows[slot];
  if (!AttachCityWindowCallback(window, callback, context)) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window->window_id);
    return false;
  }
  window->owned_message = DuplicateText(text);
  window->message = window->owned_message;
  ApplicationUpdateDialogPause(city);
  return true;
}

i32 Sc2kCanvasDesktopOpenConfirm(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_CONFIRM, .bounds = bounds, .title = title, .message = message}, flags);
}

static i32 Sc2kCanvasDesktopOpenAdvisor(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, const RunSprite *portrait, u32 flags) {
  return OpenCityMessageBox(desktop,
      (Window){.kind = CITY_CANVAS_ADVISOR, .bounds = bounds, .title = title, .message = message,
          .portrait = portrait}, flags);
}

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

void DrawCityMessageButton(Canvas *canvas, CityRect b, const char *label, CityMessageStyle style) {
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

static CityRect CityCommandErrorBounds(Game *game, const char *summary, const char *detail) {
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
  if (game->ui->display && Sc2kSDLUIPointer(game->ui->display, &x, &y) &&
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
    DrawCityMessageButton(canvas, MessageButtonBounds(window, index), label, style);
  }
}

void ResizeCityMessageBox(CityCanvasDesktop *desktop, Window *window) {
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
  if (window->advisor_category) return;
  CityRect bounds = CityMessageBounds(desktop, window);
  window->bounds = bounds;
  Sc2kDirtyMarkRect(&desktop->dirty, CanvasWindowPaintRect(window));
}

int OpenCityMessageBox(CityCanvasDesktop *desktop, Window window, u32 flags) {
  int style = flags & 0x0F, buttons = flags & 0xF0;
  assert(!(flags & ~0xFF) && style >= CITY_MESSAGE_INFORMATION && style <= CITY_MESSAGE_ERROR);
  assert(buttons >= CITY_MESSAGE_NO_BUTTONS && buttons <= CITY_MESSAGE_YES_NO_CANCEL);
  window.message_style = style;
  window.message_buttons = buttons;
  if (!window.command_error) window.bounds = CityMessageBounds(desktop, &window);
  return Sc2kCanvasDesktopOpen(desktop, window);
}

void CloseCityCommandErrorWindows(CityCanvasDesktop *desktop) {
  WindowIds windows = SnapshotCityWindowIds(desktop);
  desktop->interface->command_error_close_at = 0;
  for (int i = windows.count - 1; i >= 0; --i) {
    Window *window = FindCityCanvasWindow(desktop, windows.ids[i]);
    if (window && window->kind == CITY_CANVAS_COMMAND_ERROR)
      Sc2kCanvasDesktopDismissWindow(desktop, window->window_id);
  }
}

// Keep the blocker with its error window. Damage both marker footprints before replacing UI's tile.
void SetCityErrorTile(Game *game, TileIndex tile) {
  CityInterfaceState *ui = game->ui;
  if (!IsMapTileValid(game->city, tile)) tile = INVALID_TILE;
  if (ui->error_tile == tile) return;
  InvalidateCityMapPlacement(game->city, ui->error_tile);
  InvalidateCityMapPlacement(game->city, tile);
  ui->error_tile = tile;
}

int OpenCityCommandErrorWindow(Game *game, const char *summary, const char *detail, bool retained, TileIndex tile) {
  CityCanvasDesktop *desktop = &game->ui->desktop;
  // Retained notices keep their individual lifetime; ordinary failures share one replaceable window.
  if (!retained) CloseCityCommandErrorWindows(desktop);
  char *message = FormatCityErrorReason(detail);
  Window window = {.kind = retained ? CITY_CANVAS_NOTICE : CITY_CANVAS_COMMAND_ERROR,
      .bounds = CityCommandErrorBounds(game, summary, message), .title = summary, .message = message,
      .owned_message = message,
      .command_error = true, .city = game->city};
  int index = OpenCityMessageBox(desktop, window, CITY_MESSAGE_ERROR | CITY_MESSAGE_NO_BUTTONS);
  if (index >= 0) {
    SetCityErrorTile(game, tile);
    game->ui->error_tile_window = IsMapTileValid(game->city, tile) ? desktop->windows[index].window_id : 0;
  }
  if (index < 0) free(message);
  return index;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool MessageWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    DrawCityMessageBox(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  if (input->kind == CANVAS_SDL_KEY_DOWN && window->kind == CITY_CANVAS_CONFIRM) {
    if (input->key == CITY_KEY_RETURN || input->key == CITY_KEY_ESCAPE)
      AnswerCityConfirmation(game->city, window->window_id, input->key == CITY_KEY_RETURN);
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int answer = CityMessageAnswerAt(window, event->x, event->y);
    if (answer == CITY_DIALOG_YES || answer == CITY_DIALOG_NO) {
      AnswerCityConfirmation(game->city, window->window_id, answer == CITY_DIALOG_YES);
      return true;
    }
    if (answer == CITY_DIALOG_OK) {
      Sc2kCanvasDesktopDismissWindow(&game->ui->desktop, window->window_id);
      ApplicationUpdateDialogPause(game->city);
      return true;
    }
  }
  return false;
}

int CreateCityResourceAlert(City *city, int bitmap_id, int text_id) {
  const RunSprite *bitmap = ReadCityPicture(city->game->resources, bitmap_id);
  const char *text = CityNoticeText(text_id);
  int slot;
  if (bitmap != NULL)
    slot = Sc2kCanvasDesktopOpenAdvisor(&city->game->ui->desktop, (CityRect){110, 100, 490, 300},
        "City notice", text, bitmap, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  else
    slot = Sc2kCanvasDesktopOpenNotice(&city->game->ui->desktop, (CityRect){110, 100, 490, 300},
        "City notice", text, CITY_MESSAGE_INFORMATION | CITY_MESSAGE_OK);
  return slot;
}

u32 OpenCityResourceAlert(City *city, int bitmap_id, int text_id) {
  int slot = CreateCityResourceAlert(city, bitmap_id, text_id);
  u32 window_id = slot >= 0 ? city->game->ui->desktop.windows[slot].window_id : 0;
  ApplicationUpdateDialogPause(city);
  return window_id;
}
