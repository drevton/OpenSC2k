#ifndef SC2K_NEWSPAPER_STATE_H
#define SC2K_NEWSPAPER_STATE_H

#include "drawing/sprites/sprites.h"
#include "city/types.h"

// The dialog borrows an immutable native sprite from Assets.
typedef struct CityNewspaperDialog {
  const RunSprite *picture;
  u16 picture_id;
  u8 edition_index;
  u8 picture_attempted;
} CityNewspaperDialog;

#endif
