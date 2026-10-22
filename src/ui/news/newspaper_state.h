#ifndef SC2K_NEWSPAPER_STATE_H
#define SC2K_NEWSPAPER_STATE_H

#include "city/types.h"

typedef struct RunSprite RunSprite;

// The dialog borrows an immutable native sprite from Assets.
typedef struct CityNewspaperDialog {
  const RunSprite *picture;
  u16 picture_id;
  u8 edition_index;
  u8 picture_attempted;
} CityNewspaperDialog;

// A page borrows text, picture and filler arrays from its capture or immutable resources.
typedef struct {
  CityRect bounds;
  i16 font_size;
  const char *text;
} CityCanvasNewspaperPanel;
typedef struct {
  CityRect bounds;
  i16 font_size;
  i16 alignment;
  const char *text;
} CityCanvasNewspaperTextRun;

typedef struct {
  CityCanvasNewspaperPanel panel[7];
  CityRect picture_bounds;
  const struct RunSprite *picture;
  u8 picture_visible;
  const CityRect *filler;
  i32 filler_count;
  const CityCanvasNewspaperTextRun *text_runs;
  i32 text_run_count;
  CityRect masthead_bounds;
} CityCanvasNewspaperPage;

// The captured page owns its text/geometry storage and optional picture reference.
typedef struct NativeNewspaperDialog {
  double close_at;
  double opening_started;
  int opening_frame;
  bool opening_skipped;
  CityCanvasNewspaperPage page;
  struct RunSprite *picture; // Owned reference; grayscale issues share rows with a private palette.
  CityRect filler[2048];
  CityCanvasNewspaperTextRun text_runs[32];
  char text_run_buffers[32][2048];
  struct { char headline[2048], body[2048], heading[64]; } articles[7];
  int reading, scroll, hovered; // -1 selects the front page; reader scroll is in text rows.
  i64 wheel_remainder;
} NativeNewspaperDialog;

#endif
