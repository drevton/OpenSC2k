#ifndef SC2K_DRAWING_MAPS_MAP_EFFECTS_H
#define SC2K_DRAWING_MAPS_MAP_EFFECTS_H

#include "city/types.h"

typedef struct Drawing Drawing;
typedef struct CityMapPaint CityMapPaint;

// Short-lived construction feedback is presentation state, never saved City state.
enum { CITY_COST_TEXT_COUNT = 16, CITY_COST_TEXT_CAPACITY = 41 };
typedef struct {
  TileIndex tile;
  char text[CITY_COST_TEXT_CAPACITY];
  double started;
  u16 age_ms;
  bool active;
} CityCostText;

enum { CITY_DEMOLITION_EFFECT_COUNT = 64 };
typedef struct {
  TileIndex tile;
  double started;
  u8 frame, elevation, flipped;
  bool large, active;
} CityDemolitionEffect;

typedef enum CityMapSpriteEffect {
  CITY_MAP_EFFECT_WIND, CITY_MAP_EFFECT_POWER_SMOKE
} CityMapSpriteEffect;
// Mark an effect as visible and return its current presentation frame; timing remains Drawing-owned.
int UseCityMapSpriteEffect(Drawing *drawing, CityMapSpriteEffect effect);

void StartCityScreenShake(Drawing *drawing);
// World effects use GAME_TIME_UNPAUSED. Speed is the current City simulation speed.
bool UpdateCityScreenShake(Drawing *drawing, double now);
bool UpdateCityPowerAnimation(Drawing *drawing, int speed, double now);

// The caller supplies a valid tile and its surface elevation in map height levels. Uses the existing fixed effect pool;
// repeated requests on an active tile and disabled/suppressed effects are ignored.
void StartCityDemolitionEffect(Drawing *drawing, TileIndex tile, int elevation, bool large);
// Demolition uses GAME_TIME_REALTIME and continues while simulation is paused.
bool UpdateCityDemolitionEffects(Drawing *drawing, double now);

// Currency is already formatted for the city. Copies it into the fixed feedback pool with a leading minus;
// the caller supplies a valid tile and a GAME_TIME_REALTIME sample. Replaces the oldest text when full.
void StartCityConstructionCost(Drawing *drawing, TileIndex tile, const char *currency, double now);
bool UpdateCityCostTexts(Drawing *drawing, double now);

// The matching map target and camera must already be bound. UI supplies the viewport in framebuffer pixels;
// histories retain unclipped bounds so scrolling and moved/removed effects damage both old and new footprints.
void PrepareCityMapEffectDamage(const CityMapPaint *paint, CityRect viewport);
void DrawCityCostTexts(City *city);
void DrawCityDemolitionEffects(City *city);

#endif
