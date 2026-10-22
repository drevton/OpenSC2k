#ifndef SC2K_UI_WINDOW_CONTROLS_H
#define SC2K_UI_WINDOW_CONTROLS_H

#include "city/types.h"
#include "ui/input.h"

typedef struct Canvas Canvas;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityInterfaceFont CityInterfaceFont;

// Shared scrollbar geometry in logical UI coordinates; callers own capture and damage.
typedef struct {
  CityRect track, thumb;
  int maximum;
} CityScrollbar;
CityScrollbar ReadCityScrollbar(CityRect track, int count, int visible, int first, int minimum_thumb);
typedef enum CityScrollbarMapping {
  CITY_SCROLLBAR_DRAG, CITY_SCROLLBAR_DRAG_NEAREST, CITY_SCROLLBAR_JUMP,
} CityScrollbarMapping;
int CityScrollbarPosition(const CityScrollbar *scrollbar, int pointer, int grab, CityScrollbarMapping mapping);

// Custom window descriptions use logical units. Containers measure their children,
// distribute extra space by fill weight, and produce shared paint/input bounds.
typedef enum CityWidgetKind {
  CITY_WIDGET_ROW,
  CITY_WIDGET_COLUMN,
  CITY_WIDGET_GROUP,
  CITY_WIDGET_LABEL,
  CITY_WIDGET_BUTTON,
  CITY_WIDGET_SLIDER,
  CITY_WIDGET_TOGGLE,
  CITY_WIDGET_SPACER
} CityWidgetKind;
typedef struct CityWidget CityWidget;
struct CityWidget {
  CityWidgetKind kind;
  i32 id, min_width, min_height, padding, gap, fill;
  const char *text;
  const char *tooltip; // Borrowed immutable help; NULL uses the visible label.
  const CityWidget *children;
  i32 count;
};
typedef struct {
  i32 width, height;
} CityWidgetSize;
typedef struct {
  const CityWidget *widget;
  CityRect bounds;
} CityWidgetLayout;
CityWidgetSize Sc2kCanvasWidgetMinimum(const CityWidget *widget);
// Returns the entry count, or -1 for insufficient space/capacity; discard any partial output on failure.
i32 Sc2kCanvasWidgetLayout(const CityWidget *widget, CityRect bounds, CityWidgetLayout *layout,
    i32 capacity);
i32 Sc2kCanvasWidgetHit(const CityWidgetLayout *layout, i32 count, i32 x, i32 y);
const char *CityWidgetTooltipAt(const CityWidgetLayout *layout, int count, int x, int y);

// Text boxes use UTF-8 byte offsets for carets and logical pixels for horizontal scrolling.
int CityTextBoxScroll(const CityInterfaceFont *font, CityRect bounds, const char *text, int caret, int scroll);
int CityTextBoxCaretAt(const CityInterfaceFont *font, CityRect bounds, const char *text, int caret, int *scroll, int x);
void DrawCityTextBox(Canvas *canvas, CityRect bounds, const char *text, int caret, int scroll,
    bool focused, bool selected, bool caret_visible);

typedef enum CityDropdownItemKind {
  CITY_DROPDOWN_PLAIN, CITY_DROPDOWN_RADIO, CITY_DROPDOWN_CHECKBOX, CITY_DROPDOWN_SEPARATOR
} CityDropdownItemKind;
typedef struct CityDropdownItem {
  const char *text; // Borrowed for the lifetime of an open dropdown.
  int value;
  u8 kind;
} CityDropdownItem;
typedef struct {
  u32 face, light, dark, pressed_face, text;
  u32 disabled_face, disabled_light, disabled_dark, disabled_text, disabled_item_text;
  u32 selected_face, selected_text;
} CityDropdownColors;
typedef struct {
  const CityDropdownItem *items;
  const CityDropdownColors *colors; // Borrowed for the lifetime of an open dropdown.
  CityRect anchor; // Relative to the owning window, not the screen.
  u32 window_id;
  int control, count, selected, hovered;
  u32 checked, disabled; // Item-index bitmasks for the first 32 rows; disabled also applies to ordinary dropdowns.
  const char *summary; // Borrowed live caption; non-NULL enables multiple selection.
  bool button_menu; // Size the popup to its labels and retain the separately painted trigger button.
} CityDropdown;
typedef struct CityDropdownSelection { u32 window_id; int control, value; } CityDropdownSelection;
typedef enum CityDropdownPointer { CITY_DROPDOWN_MOVE, CITY_DROPDOWN_DOWN, CITY_DROPDOWN_UP } CityDropdownPointer;

// Opening requires nonempty choices and a valid selected index. Choices, colors and captions are borrowed until closure.
// Anchors are window-local logical rectangles.
// Bounds are logical unless named PixelBounds. Pointer input uses physical pixels; keyboard input is UI-local.
// Input handlers report capture, writing selection only for an enabled choice; callers initialize it before dispatch.
extern const CityDropdownColors kCityDropdownGoldColors;
void DrawCityDropdownControl(Canvas *canvas, const CityDropdownColors *colors, CityRect bounds,
    const char *text, bool pressed, bool disabled);
void OpenCityDropdown(CityCanvasDesktop *desktop, u32 window_id, int control, CityRect anchor,
    const CityDropdownItem *items, int count, int selected, const CityDropdownColors *colors);
void SetCityDropdownChecks(CityCanvasDesktop *desktop, u32 checked, u32 disabled, const char *summary);
// Turn an open dropdown into a button menu, sizing its popup independently of the trigger.
void SetCityDropdownMenu(CityCanvasDesktop *desktop, u32 disabled);
void CloseCityDropdown(CityCanvasDesktop *desktop);
CityRect CityDropdownBounds(const CityCanvasDesktop *desktop);
CityRect CityDropdownPixelBounds(const CityCanvasDesktop *desktop);
void DrawCityDropdown(const CityCanvasDesktop *desktop, Canvas *canvas);
bool HandleCityDropdownPointer(CityCanvasDesktop *desktop, CityDropdownPointer kind, int x, int y,
    CityDropdownSelection *selection);
bool HandleCityDropdownKey(CityCanvasDesktop *desktop, CityHotkey key, CityDropdownSelection *selection);

#endif
