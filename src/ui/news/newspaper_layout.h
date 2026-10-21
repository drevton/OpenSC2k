#ifndef SC2K_UI_NEWS_NEWSPAPER_LAYOUT_H
#define SC2K_UI_NEWS_NEWSPAPER_LAYOUT_H

#include "city/types.h"

typedef struct __attribute__((packed)) {
  i8 choice[250];
} CityNewspaperSharedChoices;

typedef struct __attribute__((packed)) {
  CityRect panel[11];
} CityNewspaperLayout;

typedef struct __attribute__((packed)) {
  CityNewspaperLayout style[3];
} CityNewspaperLayouts;

typedef struct __attribute__((packed)) {
  i16 size[11];
} CityNewspaperFonts;

typedef struct __attribute__((packed)) {
  CityNewspaperFonts style[3];
} CityNewspaperFontTable;

typedef struct __attribute__((packed)) {
  i16 alignment[11];
} CityNewspaperAlignments;

typedef struct __attribute__((packed)) {
  CityNewspaperAlignments style[3];
} CityNewspaperAlignmentTable;

#endif
