#include "city/engine/command_types.h"
#include "ui/input.h"
#include "ui/state.h"

bool IsAltKeyPressed(const CityInterfaceState *ui) {
  return (ui->key_modifiers & CITY_KEY_MODIFIER_ALT) != 0;
}

void SetCityAltKeyPressed(CityInterfaceState *ui, bool pressed) {
  if (pressed)
    ui->key_modifiers |= CITY_KEY_MODIFIER_ALT;
  else
    ui->key_modifiers &= ~CITY_KEY_MODIFIER_ALT;
}

bool IsShiftKeyPressed(const CityInterfaceState *ui) {
  return (ui->key_modifiers & CITY_KEY_MODIFIER_SHIFT) != 0;
}

void SetCityShiftKeyPressed(CityInterfaceState *ui, bool pressed) {
  if (pressed)
    ui->key_modifiers |= CITY_KEY_MODIFIER_SHIFT;
  else
    ui->key_modifiers &= ~CITY_KEY_MODIFIER_SHIFT;
}

bool IsCtrlKeyPressed(const CityInterfaceState *ui) {
  return (ui->key_modifiers & CITY_KEY_MODIFIER_CONTROL) != 0;
}

void SetCityCtrlKeyPressed(CityInterfaceState *ui, bool pressed) {
  if (pressed)
    ui->key_modifiers |= CITY_KEY_MODIFIER_CONTROL;
  else
    ui->key_modifiers &= ~CITY_KEY_MODIFIER_CONTROL;
}

int ReadCityBorderConnectionAnswer(const CityInterfaceState *ui) {
  return ui->border_connection_answer ? CITY_DIALOG_YES : CITY_DIALOG_NO;
}

void SetCityBorderConnectionAnswer(CityInterfaceState *ui, bool accept) {
  ui->border_connection_answer = accept != 0;
}

// Explicit command answers belong to UI, independently of modal dialog transport.
int ReadCitySpanChoice(const CityInterfaceState *ui) { return ui->span_choice; }
void SetCitySpanChoice(CityInterfaceState *ui, i16 choice) { ui->span_choice = choice; }
