#include <stdio.h>
#include <string.h>

#include "ui/input.h"
#include "ui/state.h"

typedef struct { CityHotkey key; const char *name; } CityNamedKey;
static const CityNamedKey kNamedKeys[] = {
  {CITY_KEY_BACKSPACE, "Backspace"}, {CITY_KEY_TAB, "Tab"}, {CITY_KEY_RETURN, "Return"},
  {CITY_KEY_ESCAPE, "Escape"}, {CITY_KEY_SPACE, "Space"}, {CITY_KEY_DELETE, "Delete"},
  {CITY_KEY_LEFT, "Left"}, {CITY_KEY_RIGHT, "Right"}, {CITY_KEY_UP, "Up"}, {CITY_KEY_DOWN, "Down"},
  {CITY_KEY_HOME, "Home"}, {CITY_KEY_END, "End"}, {CITY_KEY_PAGE_UP, "PageUp"},
  {CITY_KEY_PAGE_DOWN, "PageDown"}, {CITY_KEY_INSERT, "Insert"},
  {CITY_KEY_F1, "F1"}, {CITY_KEY_F2, "F2"}, {CITY_KEY_F3, "F3"}, {CITY_KEY_F4, "F4"},
  {CITY_KEY_F5, "F5"}, {CITY_KEY_F6, "F6"}, {CITY_KEY_F7, "F7"}, {CITY_KEY_F8, "F8"},
  {CITY_KEY_F9, "F9"}, {CITY_KEY_F10, "F10"}, {CITY_KEY_F11, "F11"}, {CITY_KEY_F12, "F12"},
  {CITY_KEY_KEYPAD_PLUS, "Keypad +"}, {CITY_KEY_KEYPAD_MINUS, "Keypad -"},
  {CITY_KEY_LEFT_ALT, "Left Alt"}, {CITY_KEY_RIGHT_ALT, "Right Alt"},
  {CITY_KEY_CONSOLE, "Console"},
};

char *CityHotkeyToString(CityHotkey key, char *text, size_t capacity) {
  const char *name = "";
  char printable[2] = {0};
  CityHotkey code = key & CITY_HOTKEY_KEY_MASK;
  if (code > CITY_KEY_SPACE && code < CITY_KEY_DELETE) {
    printable[0] = code;
    name = printable;
  } else for (size_t i = 0; i < sizeof(kNamedKeys) / sizeof(*kNamedKeys); ++i)
    if (kNamedKeys[i].key == code) { name = kNamedKeys[i].name; break; }
  snprintf(text, capacity, "%s%s%s%s", key & CITY_HOTKEY_CTRL ? "Ctrl+" : "",
      key & CITY_HOTKEY_ALT ? "Alt+" : "", key & CITY_HOTKEY_SHIFT ? "Shift+" : "", name);
  return text;
}

CityHotkey CityHotkeyFromString(const char *text) {
  CityHotkey modifiers = 0;
  for (;;) {
    if (!strncmp(text, "Ctrl+", 5)) { modifiers |= CITY_HOTKEY_CTRL; text += 5; }
    else if (!strncmp(text, "Alt+", 4)) { modifiers |= CITY_HOTKEY_ALT; text += 4; }
    else if (!strncmp(text, "Shift+", 6)) { modifiers |= CITY_HOTKEY_SHIFT; text += 6; }
    else break;
  }
  if (text[0] && !text[1]) {
    int key = (unsigned char)text[0];
    if (key >= 'a' && key <= 'z') key += 'A' - 'a';
    return modifiers | key;
  }
  for (size_t i = 0; i < sizeof(kNamedKeys) / sizeof(*kNamedKeys); ++i)
    if (!strcmp(kNamedKeys[i].name, text)) return modifiers | kNamedKeys[i].key;
  return CITY_KEY_NONE;
}

bool ParseCityKeyBinding(const char *text, CityKeyBinding *binding) {
  char first[48], second[48], extra;
  if (sscanf(text, "%47s %47s %c", first, second, &extra) != 2) return false;
  CityHotkey key = CityHotkeyFromString(first), action = CityHotkeyFromString(second);
  if (!key || !action || key == CITY_KEY_F12 || key == CITY_KEY_ESCAPE || key == CITY_KEY_CONSOLE) return false;
  *binding = (CityKeyBinding){key, action};
  return true;
}

bool BindCityKey(CityInterfaceState *ui, const char *text) {
  CityKeyBinding binding;
  if (!ParseCityKeyBinding(text, &binding)) return false;
  int index = 0;
  while (index < ui->console.binding_count && ui->console.bindings[index].key != binding.key) ++index;
  if (index == CITY_KEY_BINDING_CAPACITY) return false;
  if (index == ui->console.binding_count) ++ui->console.binding_count;
  ui->console.bindings[index] = binding;
  return true;
}

void UnbindCityKey(CityInterfaceState *ui, CityHotkey key) {
  for (int i = 0; i < ui->console.binding_count; ++i)
    if (ui->console.bindings[i].key == key) {
      memmove(&ui->console.bindings[i], &ui->console.bindings[i + 1],
          (ui->console.binding_count - i - 1) * sizeof(ui->console.bindings[0]));
      --ui->console.binding_count;
      break;
    }
}

CityHotkey RemapCityKey(const CityInterfaceState *ui, CityHotkey key) {
  for (int i = 0; i < ui->console.binding_count; ++i)
    if (ui->console.bindings[i].key == key) return ui->console.bindings[i].action;
  return key;
}
