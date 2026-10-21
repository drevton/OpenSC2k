#include "ui/windows/state.h"
#include "base/time.h"
#include "game/application_internal.h"
#include <stdio.h>
#include <string.h>

#include "game/state.h"
#include "drawing/drawing_internal.h"
#include "drawing/bitmaps.h"
#include "ui/music/jukebox_state.h"
#include "ui/state.h"

// Jukebox
// Drawing subsystem: jukebox.

enum { MUSIC_TITLE_WIDTH = 103 };

static void MusicTitleText(const Jukebox *player, char *text, size_t capacity) {
  int current = player->playing ?
      (player->interrupt_track >= 0 ? player->interrupt_track : JukeboxCurrent(player)) : -1;
  CopyText(text, capacity, player->count ? "-----" : "Empty playlist");
  if (current >= 0)
    snprintf(text, capacity, player->interrupt_track >= 0 ? "Interrupt: %s" : "\"%s\"",
        jukebox_tracks[current].title);
  if (player->error[0]) CopyText(text, capacity, player->error);
}

bool UpdateCityJukeboxTitle(CityInterfaceState *ui, u32 milliseconds) {
  const Jukebox *player = ui->jukebox_display;
  Window *window = NULL;
  for (int i = 0; player && !player->collapsed && i < ui->desktop.count; ++i)
    if (ui->desktop.windows[i].kind == CITY_CANVAS_JUKEBOX && !ui->desktop.windows[i].shaded) {
      window = &ui->desktop.windows[i];
      break;
    }
  CityMusicTitle *title = &ui->music_title;
  if (!window) { title->active = false; return false; }
  char text[100];
  MusicTitleText(player, text, sizeof(text));
  bool changed = !title->active || strcmp(title->text, text) != 0;
  if (changed) {
    CopyText(title->text, sizeof(title->text), text);
    title->started_at = milliseconds;
    title->active = true;
  }
  int width = (CityInterfaceTextWidth(ui->desktop.interface_font, text) * 8 + 9) / 10;
  int overflow = width - MUSIC_TITLE_WIDTH, offset = 0;
  if (overflow > 0) {
    enum { PAUSE_MS = 1000, PIXEL_MS = 50 };
    int travel = overflow * PIXEL_MS;
    u32 phase = (milliseconds - title->started_at) % (2 * (PAUSE_MS + travel));
    if (phase > PAUSE_MS && phase <= PAUSE_MS + travel) offset = (phase - PAUSE_MS) / PIXEL_MS;
    else if (phase > PAUSE_MS + travel && phase <= 2 * PAUSE_MS + travel) offset = overflow;
    else if (phase > 2 * PAUSE_MS + travel) offset = overflow - (phase - 2 * PAUSE_MS - travel) / PIXEL_MS;
  }
  changed |= title->offset != offset;
  title->offset = offset;
  if (changed)
    Sc2kDirtyMarkRect(&ui->desktop.dirty, (CityRect){window->bounds.left + 82, window->bounds.top + 51,
        window->bounds.left + 82 + MUSIC_TITLE_WIDTH, window->bounds.top + 60});
  return changed;
}

void DirtyCityMusicWindows(Game *game) {
  CityInterfaceState *ui = game->ui;
  for (int index = 0; index < ui->desktop.count; ++index)
    if (ui->desktop.windows[index].kind == CITY_CANVAS_JUKEBOX ||
        ui->desktop.windows[index].kind == CITY_CANVAS_MUSIC_PROGRAMME ||
        ui->desktop.windows[index].kind == CITY_CANVAS_GAME_OPTIONS)
      Sc2kDirtyMarkRect(&ui->desktop.dirty, ui->desktop.windows[index].bounds);
}

void Sc2kCanvasSetJukeboxState(CityInterfaceState *ui, const Jukebox *player) {
  ui->jukebox_display = player;
  ui->music_press.active = ui->music_press.released = 0;
}

// Coordinates stay relative to the pressed window even when windows overlap.
i32 Sc2kCanvasJukeboxPress(CityInterfaceState *ui, const Window *window, i32 x, i32 y) {
  i32 active = window != NULL;
  u32 id = window ? window->window_id : 0;
  if (window) { x -= window->bounds.left; y -= window->bounds.top; }
  else x = y = 0;
  i32 changed = ui->music_press.active != active || ui->music_press.window_id != id ||
      ui->music_press.x != x || ui->music_press.y != y;
  if (active && (!ui->music_press.active || ui->music_press.released || ui->music_press.window_id != id)) {
    ui->music_press.start_x = x; ui->music_press.start_y = y;
  }
  ui->music_press.window_id = id; ui->music_press.x = x; ui->music_press.y = y;
  ui->music_press.active = active;
  ui->music_press.released = 0;
  return changed;
}

// Keep quick down/up events visible even when delivered in one event batch.
void Sc2kCanvasJukeboxRelease(CityInterfaceState *ui, u32 milliseconds) {
  if (ui->music_press.active) {
    ui->music_press.released = 1;
    ui->music_press.release_time = milliseconds;
  }
}

bool Sc2kCanvasJukeboxTick(CityInterfaceState *ui, u32 milliseconds) {
  if (!ui->music_press.released || (u32)(milliseconds - ui->music_press.release_time) < 150)
    return false;
  Sc2kCanvasJukeboxPress(ui, NULL, 0, 0);
  return true;
}

static bool MusicHeld(const CityInterfaceState *ui, const Window *w,
    i32 x, i32 y, i32 width, i32 height) {
  if (!w || !ui->music_press.active || w->window_id != ui->music_press.window_id) return false;
  i32 px = w->bounds.left + ui->music_press.x, py = w->bounds.top + ui->music_press.y;
  i32 sx = w->bounds.left + ui->music_press.start_x, sy = w->bounds.top + ui->music_press.start_y;
  return px >= x && px < x + width && py >= y && py < y + height &&
      sx >= x && sx < x + width && sy >= y && sy < y + height;
}

enum { MUSIC_TRACK_DISPLAY = 40, MUSIC_TITLE_DISPLAY };

#define MW(k, ident, width, height)                                                                \
  {.kind = k, .id = ident, .min_width = width, .min_height = height}
#define MW_ROW(height, ...)                                                                        \
  {.kind = CITY_WIDGET_ROW,                                                                        \
   .min_height = height,                                                                           \
   .children = (const CityWidget[]){__VA_ARGS__},                                                  \
   .count = sizeof((const CityWidget[]){__VA_ARGS__}) / sizeof(CityWidget)}
static const CityWidget music_player = {
    .kind = CITY_WIDGET_COLUMN,
    .min_width = JUKEBOX_PLAYER_WIDTH,
    .children = (const CityWidget[]){MW_ROW(JUKEBOX_TITLE_HEIGHT,
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_CLOSE, 11, JUKEBOX_TITLE_HEIGHT),
                                            {.kind = CITY_WIDGET_SPACER, .fill = 1},
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_COLLAPSE, 12, JUKEBOX_TITLE_HEIGHT),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_PIN, 12, JUKEBOX_TITLE_HEIGHT)),
                                     MW(CITY_WIDGET_SPACER, 0, 0, 2),
                                     MW_ROW(22, MW(CITY_WIDGET_BUTTON, JUKEBOX_PREVIOUS, 21, 22),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_NEXT, 21, 22),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_STOP, 21, 22),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_PLAY, 21, 22),
                                            MW(CITY_WIDGET_SPACER, 0, 1, 22),
                                            MW(CITY_WIDGET_SPACER, 0, 8, 22),
                                            MW(CITY_WIDGET_SLIDER, JUKEBOX_MUSIC_VOLUME, 70, 22),
                                            MW(CITY_WIDGET_SPACER, 0, 8, 22),
                                            MW(CITY_WIDGET_SLIDER, JUKEBOX_EFFECTS_VOLUME, 70, 22),
                                            MW(CITY_WIDGET_SPACER, 0, 8, 22)),
                                     MW(CITY_WIDGET_SPACER, 0, 0, 3),
                                     MW_ROW(19, MW(CITY_WIDGET_SPACER, 0, 4, 19),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SHUFFLE, 50, 19),
                                            MW(CITY_WIDGET_SPACER, 0, 6, 19),
                                            MW(CITY_WIDGET_LABEL, MUSIC_TRACK_DISPLAY, 20, 19),
                                            MW(CITY_WIDGET_LABEL, MUSIC_TITLE_DISPLAY, 107, 19),
                                            MW(CITY_WIDGET_SPACER, 0, 6, 19),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_PROGRAMME, 50, 19),
                                            MW(CITY_WIDGET_SPACER, 0, 7, 19)),
                                     MW(CITY_WIDGET_SPACER, 0, 0, 2),
                                     MW_ROW(10,
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_ALL, 38, 10),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_OLD_STYLE,
                                               41, 10),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_NEW_STYLE,
                                               41, 10),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_EZY_STREET,
                                               44, 10),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_CUSTOM_1,
                                               41, 10),
                                            MW(CITY_WIDGET_BUTTON, JUKEBOX_SELECT + JUKEBOX_PROGRAMME_CUSTOM_2,
                                               45, 10))},
    .count = 7};

#undef MW
#undef MW_ROW
static i32 MusicPlayerLayout(const Window *w, CityWidgetLayout *out) {
  CityRect b = w->bounds;
  b.bottom = b.top + JUKEBOX_PLAYER_HEIGHT;
  i32 count = Sc2kCanvasWidgetLayout(&music_player, b, out, 64);
  if (count < 0)
    return 0;
  for (i32 i = 0; i < count; ++i) {
    i32 id = out[i].widget->id;
    if (id == JUKEBOX_SHUFFLE || id == JUKEBOX_PROGRAMME) {
      out[i].bounds.top += 5;
      out[i].bounds.bottom = out[i].bounds.top + 9;
    } else if (id == JUKEBOX_MUSIC_VOLUME || id == JUKEBOX_EFFECTS_VOLUME) {
      out[i].bounds.top += 9;
      out[i].bounds.right += 1;
    }
  }
  return count;
}

static CityRect MusicWidgetBounds(const CityWidgetLayout *layout, i32 count, i32 id) {
  for (i32 i = 0; i < count; ++i)
    if (layout[i].widget->id == id)
      return layout[i].bounds;
  return (CityRect){0, 0, 0, 0};
}

static i32 MusicFontWidth(const char *text, i32 size) {
  return (CanvasTextWidth(text) * size + 9) / 10;
}

static void MusicFontText(Canvas *c, i32 x, i32 y, const char *text, u32 color,
    i32 size) {
  if (c->interface_font && c->interface_font->ready) {
    DrawCityInterfaceTextSized(c->interface_font, c, x, y, text, color, false, size);
    return;
  }
  Canvas glyph_canvas = *c;
  glyph_canvas.raster = CanvasRasterView(c);
  glyph_canvas.raster.origin_x *= 10; glyph_canvas.raster.origin_y *= 10;
  glyph_canvas.raster.denominator *= 10;
  glyph_canvas.width *= 10; glyph_canvas.height *= 10;
  glyph_canvas.clip = (CityRect){c->clip.left * 10, c->clip.top * 10,
      c->clip.right * 10, c->clip.bottom * 10};
  x *= 10; y *= 10;
  while (*text) {
    int character = ReadCityFontCharacter(&text);
    const CityCanvasGlyph *glyph = &kCanvasFont[character];
    for (i32 row = 0; row < glyph->height; ++row)
      for (i32 bit = 0; bit < glyph->width; ++bit)
        if (glyph->rows[row] & (1ull << bit))
          CanvasFill(&glyph_canvas, x + (glyph->offset_x + bit) * size, y + (glyph->offset_y + row) * size,
              x + (glyph->offset_x + bit + 1) * size, y + (glyph->offset_y + row + 1) * size, color);
    x += glyph->advance * size;
  }
}

static i32 MusicTextWidth(const char *text) {
  return MusicFontWidth(text, 8);
}

static void MusicText(Canvas *c, i32 x, i32 y, const char *text, u32 color) {
  MusicFontText(c, x, y, text, color, 8);
}

static void MusicBox(Canvas *c, i32 x, i32 y, i32 w, i32 h, u32 fill, i32 raised) {
  CanvasBeveledBox(c, x, y, x + w, y + h, fill, raised);
}

static void MusicLabel(Canvas *c, i32 x, i32 y, i32 w, const char *text, u32 color) {
  MusicText(c, x + (w - MusicTextWidth(text)) / 2, y, text, color);
}

static void MusicButton(Canvas *c,
    i32 x, i32 y, i32 w, i32 h, const char *text, i32 pressed, i32 disabled) {
  if (!disabled) pressed |= RegisterCityCanvasButton(c, (CityRect){x, y, x + w, y + h});
  MusicBox(c, x, y, w, h, canvas_face, !pressed);
  i32 text_width = MusicFontWidth(text, 7);
  CityRect clip = c->clip;
  c->clip.left = IntMax(c->clip.left, x + 1);
  c->clip.right = IntMin(c->clip.right, x + w - 1);
  MusicFontText(c, x + pressed + (w - text_width) / 2, y + pressed + (h - 7) / 2, text,
                   disabled ? canvas_dark : 0xFF101010u, 7);
  c->clip = clip;
}

static void MusicSlider(Canvas *c, i32 x, i32 y, i32 value) {
  // The rising trough and narrow thumb follow the original volume widget.
  for (i32 col = 0; col < JUKEBOX_SLIDER_WIDTH; ++col) {
    i32 top = y + 11 - col / 10;
    CanvasFill(c, x + col, top, x + col + 1, top + 1, canvas_dark);
  }
  CanvasFill(c, x, y + 12, x + JUKEBOX_SLIDER_WIDTH, y + 13, canvas_light);
  CanvasFill(c, x + JUKEBOX_SLIDER_WIDTH - 1, y + 4, x + JUKEBOX_SLIDER_WIDTH, y + 13,
              canvas_light);
  MusicBox(c, x + value * (JUKEBOX_SLIDER_WIDTH - 3) / 100, y + 2, 3, 12, canvas_face, 1);
}

static void MusicTransport(Game *game, Canvas *c, const Window *window, int x, int y, int kind, int pressed) {
  pressed |= RegisterCityCanvasButton(c, (CityRect){x, y, x + 21, y + 22});
  MusicBox(c, x, y, 21, 22, canvas_face, !pressed);
  if (!DrawCanvasSprite(c, window->picture_drawing, game->resources->music_buttons.images[kind],
      (CityRect){x + pressed, y + 1 + pressed, x + 20 + pressed, y + 21 + pressed}))
    game->execution_failed = true;
}

void CanvasDrawJukebox(Game *game, Canvas *c, const Window *w) {
  CityInterfaceState *ui = game->ui;
  const Jukebox *p = ui->jukebox_display;
  if (!p)
    return;
  CityRect b = w->bounds;
  i32 x = b.left, y = b.top;
  MusicBox(c, x, y, b.right - x, b.bottom - y, canvas_face, 1);
  if (w->kind == CITY_CANVAS_JUKEBOX) {
    DrawCityWindowCaption(c, b, "Jazz Jukebox", canvas_face, canvas_light, canvas_dark, 24);
    MusicButton(c, b.right - 24, y, 12, JUKEBOX_TITLE_HEIGHT, "", p->collapsed, 0);
    DrawCityWindowControlIcon(c, (CityRect){b.right - 24, y, b.right - 12, y + JUKEBOX_TITLE_HEIGHT},
        CITY_WINDOW_ICON_SHADE, p->collapsed || MusicHeld(ui, w, b.right - 24, y, 12, JUKEBOX_TITLE_HEIGHT));
    MusicButton(c, b.right - 12, y, 12, JUKEBOX_TITLE_HEIGHT, "", p->pinned, 0);
    DrawCityWindowControlIcon(c, (CityRect){b.right - 12, y, b.right, y + JUKEBOX_TITLE_HEIGHT},
        CITY_WINDOW_ICON_STICKY, p->pinned || MusicHeld(ui, w, b.right - 12, y, 12, JUKEBOX_TITLE_HEIGHT));
    if (p->collapsed)
      return;
    CityWidgetLayout layout[64];
    i32 count = MusicPlayerLayout(w, layout);
    for (i32 button = 0; button < CITY_MUSIC_SPRITE_COUNT; ++button) {
      CityRect control = MusicWidgetBounds(layout, count, JUKEBOX_PREVIOUS + button);
      MusicTransport(game, c, w, control.left, control.top, button,
          MusicHeld(ui, w, control.left, control.top, 21, 22));
    }
    MusicBox(c, x + 85, y + 16, 164, 22, canvas_face, 1);
    for (i32 i = 0; i < 2; ++i) {
      CityRect control =
          MusicWidgetBounds(layout, count, i ? JUKEBOX_EFFECTS_VOLUME : JUKEBOX_MUSIC_VOLUME);
      MusicLabel(c, control.left, control.top - 8, control.right - control.left,
                  i ? "Effects Volume" : "Music Volume", 0xFF101010u);
      MusicSlider(c, control.left, control.top - 2, i ? p->effects_volume : p->volume);
    }
    for (i32 i = 0; i < 2; ++i) {
      CityRect control =
          MusicWidgetBounds(layout, count, i ? JUKEBOX_PROGRAMME : JUKEBOX_SHUFFLE);
      MusicButton(c, control.left, control.top, control.right - control.left,
                   control.bottom - control.top, i ? "Programme" : "Shuffle", !i && p->shuffle, 0);
    }
    MusicLabel(c, x + 60, y + 41, 20, "Track", 0xFF101010u);
    MusicLabel(c, x + 80, y + 41, 107, "Title", 0xFF101010u);
    MusicBox(c, x + 60, y + 50, 20, 10, 0xFF101010u, 0);
    MusicBox(c, x + 80, y + 50, 107, 10, 0xFF101010u, 0);
    i32 current = p->playing ? (p->interrupt_track >= 0 ? p->interrupt_track : JukeboxCurrent(p)) : -1;
    char number[8] = "--", title[100];
    MusicTitleText(p, title, sizeof(title));
    if (current >= 0) {
      snprintf(number, sizeof(number), "%02d", current);
    }
    MusicLabel(c, x + 61, y + 51, 18, number, 0xFFB6CF7Fu);
    CityRect clip = c->clip;
    c->clip.left = IntMax(c->clip.left, x + 82);
    c->clip.right = IntMin(c->clip.right, x + 185);
    int title_width = (CityInterfaceTextWidth(c->interface_font, title) * 8 + 9) / 10;
    int offset = ui->music_title.active && !strcmp(ui->music_title.text, title) ? ui->music_title.offset : 0;
    int title_x = title_width <= MUSIC_TITLE_WIDTH ? (MUSIC_TITLE_WIDTH - title_width) / 2 : -offset;
    MusicText(c, x + 82 + title_x, y + 51, title, 0xFFB6CF7Fu);
    c->clip = clip;
    for (i32 tab = 0; tab < 6; ++tab) {
      CityRect control = MusicWidgetBounds(layout, count, JUKEBOX_SELECT + tab);
      MusicButton(c, control.left, control.top, control.right - control.left,
                   control.bottom - control.top, jukebox_programmes[tab], p->programme == tab, 0);
    }
  } else {
    DrawCityWindowCaption(c, b, "Music Programme - OpenMSX", canvas_face, canvas_light, canvas_dark, 0);
    MusicLabel(c, x + 2, y + 15, 180, "Track Index", 0xFF101010u);
    char label[48];
    snprintf(label, sizeof(label), "Programme - '%s'", jukebox_programmes[p->programme]);
    MusicLabel(c, x + 251, y + 15, 179, label, 0xFF101010u);
    MusicBox(c, x + 2, y + 23, 180, 226, 0xFF101010u, 0);
    MusicBox(c, x + 251, y + 23, 179, 226, 0xFF101010u, 0);
    CityRect list_clip = c->clip;
    c->clip.top = IntMax(c->clip.top, y + 24);
    c->clip.bottom = IntMin(c->clip.bottom, y + 248);
    for (i32 list = 0; list < 2; ++list) {
      int count = list ? p->count : JUKEBOX_TRACKS;
      for (i32 row = 0; row < count; ++row) {
        int track = list ? p->order[row] : row;
        char text[96];
        snprintf(text, sizeof(text), "%02d\"%s\"", track, jukebox_tracks[track].title);
        MusicText(c, x + (list ? 253 : 4), y + 25 + row * 7, text, 0xFF80C4FCu);
      }
    }
    c->clip = list_clip;
    for (i32 tab = 0; tab < 6; ++tab)
      MusicButton(c, x + 187, y + 46 + tab * 9, 60, 9, jukebox_programmes[tab],
                   p->programme == tab, 0);
    MusicButton(c, x + 187, y + 115, 60, 9, "Clear", 0, p->programme < JUKEBOX_PROGRAMME_CUSTOM_1);
    if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1)
      for (i32 row = 1; row < 8; row += 2)
        for (i32 col = row % 4; col < 58; col += 2)
          CanvasFill(c, x + 188 + col, y + 115 + row, x + 189 + col, y + 116 + row, canvas_dark);

  }
}

i32 Sc2kCanvasJukeboxActionAt(const CityInterfaceState *ui, const Window *w, i32 px, i32 py) {
  if (!ui->jukebox_display || !w->visible ||
      (w->kind != CITY_CANVAS_JUKEBOX && w->kind != CITY_CANVAS_MUSIC_PROGRAMME))
    return JUKEBOX_NONE;
  i32 x = px - w->bounds.left, y = py - w->bounds.top;
  i32 width = w->bounds.right - w->bounds.left;
  if (x < 0 || y < 0 || x >= width || py >= w->bounds.bottom)
    return JUKEBOX_NONE;
  if (CanvasCloseAt(w->bounds, px, py))
    return JUKEBOX_CLOSE;
  if (w->kind == CITY_CANVAS_JUKEBOX) {
    if (y < JUKEBOX_TITLE_HEIGHT && x >= width - 12)
      return JUKEBOX_PIN;
    if (y < JUKEBOX_TITLE_HEIGHT && x >= width - 24)
      return JUKEBOX_COLLAPSE;
    if (ui->jukebox_display->collapsed)
      return JUKEBOX_NONE;
    CityWidgetLayout layout[64];
    i32 count = MusicPlayerLayout(w, layout);
    i32 id = Sc2kCanvasWidgetHit(layout, count, px, py);
    if (id == JUKEBOX_MUSIC_VOLUME || id == JUKEBOX_EFFECTS_VOLUME) {
      CityRect control = MusicWidgetBounds(layout, count, id);
      return id + (px - control.left) * 100 / (control.right - control.left - 1);
    }
    return id;
  } else {
    if (x >= 187 && x < 247 && y >= 46 && y < 100)
      return JUKEBOX_SELECT + (y - 46) / 9;
    if (x >= 187 && x < 247 && y >= 115 && y < 124)
      return JUKEBOX_CLEAR;
    if (y >= 25 && y < 248) {
      int row = (y - 25) / 7;
      bool custom = ui->jukebox_display->programme >= JUKEBOX_PROGRAMME_CUSTOM_1;
      if (x >= 3 && x < 181 && row < JUKEBOX_TRACKS)
        return (custom ? JUKEBOX_ADD : JUKEBOX_PLAY_TRACK) + row;
      if (x >= 252 && x < 429 && row < ui->jukebox_display->count)
        return custom ? JUKEBOX_REMOVE + row : JUKEBOX_PLAY_TRACK + ui->jukebox_display->order[row];
    }
  }
  return JUKEBOX_NONE;
}

// Jukebox State

const JukeboxTrack jukebox_tracks[JUKEBOX_TRACKS] = {
  {"OpenTTD journey", "tttheme2.mid"},
  {"Keep on Rolling", "keep_on_rolling.mid"},
  {"Downtown cab ride", "ttsong_iv_imuh3.mid"},
  {"Modern Motion", "modern_motion.mid"},
  {"Busy Schedule", "busy_schedule.mid"},
  {"The Fast Route", "the_fast_route.mid"},
  {"Skydive", "ttsong_iii_imuh3.mid"},
  {"Train filled with cash", "train_filled_with_cash.mid"},
  {"Unidentifed Flying Scotsman", "flying_scotsman.mid"},
  {"Chugga Chugga Cha-ching", "chuggachugga.mid"},
  {"The Hobo", "the_hobo_redfarn.mid"},
  {"Ultimate run", "ultimate_run.mid"},
  {"Midnight snow run", "midnight_snow_run.mid"},
  {"Excitement! Run for your life", "run_for_your_life.mid"},
  {"Coconut run 2", "coconut_run2.mid"},
  {"Harp harmony 'n humor", "harp_harmony.mid"},
  {"Mighty giant run", "mighty_giant_run.mid"},
  {"Wood whistles", "wood_whistles.mid"},
  {"Linn's basket of treats", "linns_basket.mid"},
  {"Plain simple relax song", "relax_song.mid"},
  {"Deep down chemistry lab", "chemistry_lab.mid"},
  {"5 4 3 2 .. Gone", "5432gone_redfarn.mid"},
  {"Boogie Marabi", "boogi_marabi_redfarn.mid"},
  {"Moo Moo Boogie", "moo_redfarn.mid"},
  {"Say What", "say_what_redfarn.mid"},
  {"Be Sharp Boogie", "be_sharp_bw_redfarn.mid"},
  {"Careless Love", "careless_perc_redfarn.mid"},
  {"Mosey Along", "mosey_along_redfarn.mid"},
  {"Slow and Easy", "slow_neasy_redfarn.mid"},
  {"City Blues", "city_blues_redfarn.mid"},
  {"No Work Song", "no_work_song_redfarn.mid"},
};
const char *const jukebox_programmes[JUKEBOX_PROGRAMMES] = {
  "All", "Old Style", "New Style", "Ezy Street", "Custom 1", "Custom 2"
};

static u32 RandomNext(Jukebox *p) {
  u32 x = p->random;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return p->random = x;
}

i32 JukeboxCurrent(const Jukebox *p) {
  return p->count && p->position >= 0 && p->position < p->count ? p->order[p->position] : -1;
}

static void RebuildJukeboxProgramme(Jukebox *p, i32 preserve) {
  p->count = 0;
  if (p->programme >= JUKEBOX_PROGRAMME_CUSTOM_1) {
    p->count = p->custom_count[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1];
    memcpy(p->order, p->custom[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1], (size_t)p->count);
  } else {
    i32 first = p->programme == JUKEBOX_PROGRAMME_ALL ? 1 :
        (p->programme - JUKEBOX_PROGRAMME_OLD_STYLE) * 10 + 1;
    i32 last = p->programme == JUKEBOX_PROGRAMME_ALL ? 30 : first + 9;
    for (int track = first; track <= last; ++track)
      p->order[p->count++] = (u8)track;
  }
  if (p->shuffle)
    for (i32 index = p->count - 1; index > 0; --index) {
      i32 other = (i32)(RandomNext(p) % (u32)(index + 1));
      int track = p->order[index]; p->order[index] = p->order[other]; p->order[other] = track;
    }
  p->position = 0;
  for (i32 index = 0; index < p->count; ++index)
    if (p->order[index] == preserve) { p->position = index; break; }
}

void JukeboxInit(Jukebox *p, u32 seed) {
  memset(p, 0, sizeof(*p));
  p->interrupt_track = -1;
  p->random = seed ? seed : 1;
  p->volume = 50;
  p->effects_volume = 80;
  RebuildJukeboxProgramme(p, -1);
}

void JukeboxSelect(Jukebox *p, i32 programme) {
  if (programme < JUKEBOX_PROGRAMME_ALL || programme >= JUKEBOX_PROGRAMMES) return;
  p->programme = programme;
  RebuildJukeboxProgramme(p, -1);
}

void SelectJukeboxTrack(Jukebox *p, int track) {
  if (track < 0 || track >= JUKEBOX_TRACKS) return;
  for (int row = 0; row < p->count; ++row)
    if (p->order[row] == track) {
      p->position = row;
      p->playing = 1;
      return;
    }
  JukeboxSelect(p, JUKEBOX_PROGRAMME_ALL);
  for (int row = 0; row < p->count; ++row)
    if (p->order[row] == track) { p->position = row; p->playing = 1; return; }
  // The OpenMSX title theme is listed, but excluded from automatic programmes.
  if (track == 0) {
    p->position = p->count;
    p->order[p->count++] = 0;
    p->playing = 1;
  }
}

void JukeboxShuffle(Jukebox *p) {
  i32 current = JukeboxCurrent(p);
  p->shuffle = !p->shuffle;
  RebuildJukeboxProgramme(p, current);
}
void JukeboxStep(Jukebox *p, i32 direction) {
  if (p->count) p->position = (p->position + direction % p->count + p->count) % p->count;
}
void JukeboxAdd(Jukebox *p, i32 track) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1 || track < 0 || track >= JUKEBOX_TRACKS) return;
  i32 custom = p->programme - JUKEBOX_PROGRAMME_CUSTOM_1, count = p->custom_count[custom];
  if (count == JUKEBOX_CAPACITY) return;
  p->custom[custom][count] = (u8)track;
  ++p->custom_count[custom];
  // Insert into the shuffled order without changing the current song.
  i32 insert = p->shuffle && p->count ? (i32)(RandomNext(p) % (u32)(p->count + 1)) : p->count;
  memmove(p->order + insert + 1, p->order + insert, (size_t)(p->count - insert));
  p->order[insert] = (u8)track;
  if (p->count && insert <= p->position) ++p->position;
  ++p->count;
}
void JukeboxRemove(Jukebox *p, i32 row) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1 || row < 0 || row >= p->count) return;
  i32 custom = p->programme - JUKEBOX_PROGRAMME_CUSTOM_1, index = 0;
  if (p->shuffle) {
    while (index < p->custom_count[custom] && p->custom[custom][index] != p->order[row]) ++index;
  } else index = row;
  if (index == p->custom_count[custom]) return;
  memmove(p->custom[custom] + index, p->custom[custom] + index + 1,
      (size_t)(--p->custom_count[custom] - index));
  memmove(p->order + row, p->order + row + 1, (size_t)(--p->count - row));
  if (row < p->position) --p->position;
  if (p->position >= p->count) p->position = 0;
}
void JukeboxClear(Jukebox *p) {
  if (p->programme < JUKEBOX_PROGRAMME_CUSTOM_1) return;
  p->custom_count[p->programme - JUKEBOX_PROGRAMME_CUSTOM_1] = p->count = p->position = 0;
}

// Handles this window's painting and input; paint coordinates are local to the window.
bool MusicWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    Canvas *canvas = event->canvas;
    CanvasDrawJukebox(game, canvas, window);
    return true;
  }
  if (event->kind == WINDOW_DESTROY) {
    CityInterfaceState *ui = game->ui;
    if (ui->jukebox_pressed_window == window->window_id) {
      Sc2kCanvasJukeboxPress(ui, NULL, 0, 0);
      ui->jukebox_pressed_window = 0;
    }
    if (ui->jukebox_slider_window == window->window_id) ui->jukebox_slider = CITY_CANVAS_VOLUME_SLIDER_NONE;
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  const CanvasSDLEvent *input = event->input;
  CityInterfaceState *ui = game->ui;
  Window *target = FindCityCanvasWindow(&ui->desktop, window->window_id);
  if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    int action = Sc2kCanvasJukeboxActionAt(ui, target, event->x, event->y);
    if (action) {
      ui->jukebox_pressed_window = target->window_id;
      Sc2kCanvasJukeboxPress(ui, target, event->x, event->y);
      PlayCityButtonBeep(game);
    }
    if ((action >= JUKEBOX_MUSIC_VOLUME && action <= JUKEBOX_MUSIC_VOLUME + 100) ||
        (action >= JUKEBOX_EFFECTS_VOLUME && action <= JUKEBOX_EFFECTS_VOLUME + 100)) {
      ui->jukebox_slider = action >= JUKEBOX_EFFECTS_VOLUME ? CITY_CANVAS_VOLUME_SLIDER_EFFECTS :
          CITY_CANVAS_VOLUME_SLIDER_MUSIC;
      ui->jukebox_slider_window = target->window_id;
    }
    if (action) ApplyCityMusicAction(game, action, target);
    else if (event->y < target->bounds.top + JUKEBOX_TITLE_HEIGHT) {
      ui->desktop.dragging = ui->desktop.count - 1;
      ui->desktop.drag_x = event->x - target->bounds.left;
      ui->desktop.drag_y = event->y - target->bounds.top;
    }
    return true;
  }
  if (input->kind == CANVAS_SDL_POINTER_MOVE) {
    if (ui->jukebox_pressed_window == window->window_id && Sc2kCanvasJukeboxPress(ui, target, event->x, event->y))
      DirtyCityMusicWindows(game);
    if (ui->jukebox_slider && ui->jukebox_slider_window == window->window_id) {
      int value = (event->x - window->bounds.left -
          (ui->jukebox_slider == CITY_CANVAS_VOLUME_SLIDER_MUSIC ? JUKEBOX_MUSIC_SLIDER_X :
              JUKEBOX_EFFECTS_SLIDER_X)) * 100 / JUKEBOX_SLIDER_WIDTH;
      ApplyCityMusicAction(game, (ui->jukebox_slider == CITY_CANVAS_VOLUME_SLIDER_MUSIC ?
          JUKEBOX_MUSIC_VOLUME : JUKEBOX_EFFECTS_VOLUME) + IntClamp(value, 0, 100), target);
      return true;
    }
  }
  if (input->kind == CANVAS_SDL_POINTER_UP || input->kind == CANVAS_SDL_FOCUS_LOST) {
    if (input->kind == CANVAS_SDL_FOCUS_LOST) Sc2kCanvasJukeboxPress(ui, NULL, 0, 0);
    else Sc2kCanvasJukeboxRelease(ui, (u32)(u64)(MonotonicSeconds() * 1000));
    ui->jukebox_pressed_window = 0;
    ui->jukebox_slider = CITY_CANVAS_VOLUME_SLIDER_NONE;
    DirtyCityMusicWindows(game);
    return true;
  }
  if (input->kind == CANVAS_SDL_KEY_DOWN && input->key == CITY_KEY_ESCAPE &&
      window->kind == CITY_CANVAS_JUKEBOX && ui->jukebox.pinned) return true;
  return false;
}
