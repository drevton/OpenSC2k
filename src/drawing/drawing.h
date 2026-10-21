#ifndef SC2K_DRAWING_H
#define SC2K_DRAWING_H

#include "city/economy/facilities.h"
#include "city/infrastructure/types.h"
#include "city/presentation.h"

#include "ui/input.h"
#include "audio/audio.h"
#include "city/engine/simulation.h"
#include "ui/dialogs/alerts.h"
#include "ui/news/newspaper.h"
#include "drawing/canvas.h"
#include "drawing/maps/map_render.h"
#include "drawing/sprites/sprites.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "ui/dialogs/dialogs.h"
#include "ui/dialogs/game_options.h"
#include "ui/music/jukebox_state.h"
#include "ui/windows/state.h"

#include "city/state.h"
#include "drawing/maps/map_view.h"
#include "ui/news/newspaper_state.h"

typedef struct Drawing Drawing;
typedef struct CityPaletteState CityPaletteState;

// A logical coordinate view backed by the final framebuffer. The callback must
// use canvas drawing functions; stride and pixel storage remain physical.
typedef void (*CityCanvasScaledPaint)(Canvas *canvas, void *context);
void Sc2kCanvasPaintScaled(Canvas *framebuffer, i32 numerator, i32 denominator,
    CityCanvasScaledPaint paint, void *context);

// Typed software text state; drawing services are linked directly.
typedef struct CityCanvasInformationCursor CityCanvasInformationCursor;
typedef struct CityInformationText {
  CityCanvasInformationCursor *cursor;
  City *city;
} CityInformationText;

typedef struct {
  CityInformationText text;
  CityRect bounds;
  i32 alt_pressed;
} CityMapInformationPainter;
void CaptureCityTileInformation(City *city, CityTileInformationSnapshot *snapshot);

bool DrawCityTextLiteral(CityInformationText *text, const char *value);
bool DrawCityTextBytes(CityInformationText *text, const char *value, int length);
bool DrawCityTextCharacter(CityInformationText *text, char value);
bool DrawCityTextUnsignedWord(CityInformationText *text, u16 value);
bool DrawCityTextInteger(CityInformationText *text, i32 value);
void DrawCityFacilityInformationTemplate(CityInformationText *text, int facility, int line);

// Host pixel overlay is painted below windows, clipped to each dirty region.

// Software canvas font adapter; caller supplies the localized currency string.

// Physical window origins are independent of scaled logical widget geometry.
void Sc2kCanvasDesktopPixelMetrics(CityCanvasDesktop *desktop, i32 scale, i32 map_scale,
    i32 width, i32 height);
CityRect Sc2kCanvasDesktopPixelBounds(const CityCanvasDesktop *desktop, u32 id);
void PositionCityWindowPixels(CityCanvasDesktop *desktop, u32 id, CityPoint position);
void Sc2kCanvasDesktopPixelPointer(const CityCanvasDesktop *desktop, u32 capture,
    i32 x, i32 y, i32 *logical_x, i32 *logical_y);
u32 Sc2kCanvasDesktopFocusPixels(CityCanvasDesktop *desktop, i32 x, i32 y);
void Sc2kCanvasDesktopBeginPixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopMovePixelDrag(CityCanvasDesktop *desktop, i32 x, i32 y);

void Sc2kRefreshCityStatusPanel(City *city);
void ShowCityNewspaper(City *city, i32 edition_index);

void SetCityCanvasPaletteDialogState(City *city, bool paused);
void InitializeCityPaletteAnimation(Drawing *drawing);
void InitializeCityDisplayPalette(Drawing *drawing, u32 *colors);
// Borrow the mutable display color table; this installs no callback.
void SetCityDisplayPalette(Drawing *drawing, u32 *colors);
void CaptureCityPalette(CityPaletteState *palette, const u32 *colors);
void ApplyAnimatedCityPalette(const CityPaletteState *palette, u32 *colors);
void ConstructCityRect(CityRect *rectangle, i32 left, i32 top, i32 right, i32 bottom);
void InitializeCityNewspaperLayouts(Drawing *drawing);

void InitializeCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
CityNewspaperInitialization BeginCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);

void DestroyCityNewspaperWindow(City *city, CityNewspaperDialog *dialog);
typedef struct NativeNewspaperDialog NativeNewspaperDialog;
void PaintCityNewspaperDialog(City *city, NativeNewspaperDialog *capture, CityNewspaperDialog *dialog);

bool DrawNewspaperTextBlock(Game *game, NativeNewspaperDialog *capture, char *text, const CityRect *bounds, int font_size,
    int alignment);
void DrawNewspaperArticleText(Game *game, NativeNewspaperDialog *capture, char *text, CityRect *bounds, int font_size,
    int alignment);
void DrawNewspaperArticleWithFiller(City *city, NativeNewspaperDialog *capture, char *text, CityRect *bounds,
    int font_size, int alignment);
int FindNewspaperTextWrapPoint(const char *text);
int ExpandCityNewspaperTemplate(City *city, const CityNewspaperResources *resources, char *output, int template_id,
    int article_index, int half);
bool ComposeCityNewspaperArticle(City *city, const CityNewspaperResources *resources, int article_index, char *output);
int ComposeCityNewspaperArticleBody(City *city, const CityNewspaperResources *resources, int article_index,
    char *output);
void DrawNewspaperFillerLines(City *city, NativeNewspaperDialog *capture, const CityRect *bounds);
void FormatCityNewspaperWeather(City *city, char *output);
// Scoped canvas metric input; no callback registration.
void TrimNewspaperHeadlineToWidth(char *text, i32 width, void *drawing_context);
void Sc2kCanvasTrimNewspaperHeadline(char *text, i32 width, i32 font_size, i32 page_scale);
u8 ReorderCityPaletteEntries(CityPaletteState *palette, int palette_49_entries);
int AnimateCityPalette(City *city, int slow_palette);

bool CanvasPointInRect(CityRect rect, i32 x, i32 y);
void DrawCanvasLine(Canvas *canvas, int x0, int y0, int x1, int y1, u32 color, int width);
void Sc2kCanvasBlitIndexed(Canvas *canvas, const u8 *indices, i32 source_stride,
    const u32 *palette, CityRect area);
void Sc2kCanvasBudgetFromCity(City *city, CityBudgetPanel *budget);
enum { CITY_BUDGET_WINDOW_WIDTH = 432, CITY_BUDGET_WINDOW_HEIGHT = 276 };
typedef enum CityBudgetControl {
  CITY_BUDGET_CONTROL_CATEGORY, CITY_BUDGET_CONTROL_RATE,
  CITY_BUDGET_CONTROL_DECREASE, CITY_BUDGET_CONTROL_INCREASE,
  CITY_BUDGET_CONTROL_ADVISOR, CITY_BUDGET_CONTROL_TO_DATE, CITY_BUDGET_CONTROL_ESTIMATE,
  CITY_BUDGET_CONTROL_BORROW, CITY_BUDGET_CONTROL_REPAY, CITY_BUDGET_CONTROL_SHOW_BONDS,
  CITY_BUDGET_CONTROL_TIMER, CITY_BUDGET_CONTROL_OPEN_ORDINANCES
} CityBudgetControl;
CityRect CityBudgetControlBounds(CityRect bounds, CityBudgetControl control, int category);
int CityBudgetRateKindForRow(int row);
int CityBudgetRateMaximum(int row);
int Sc2kCanvasBudgetRateAt(const Window *window, int x, int y);
typedef enum CityBudgetQueryControl {
  CITY_BUDGET_QUERY_INPUT, CITY_BUDGET_QUERY_CLEAR, CITY_BUDGET_QUERY_DEFAULT,
  CITY_BUDGET_QUERY_CANCEL, CITY_BUDGET_QUERY_OK
} CityBudgetQueryControl;
CityRect CityBudgetQueryBounds(CityRect bounds, CityBudgetQueryControl control);
void DrawCityBudgetRateEditor(Canvas *canvas, const Window *window);
// Return -1 outside an arrow, otherwise budget row * 2 plus one for increase.
i32 Sc2kCanvasBudgetStepAt(const Window *window, i32 x, i32 y);
i32 Sc2kCanvasBudgetAdviceAt(const Window *window, i32 x, i32 y);
// Returns a policy index or a CityCanvasPolicyHit sentinel.
enum {
  CITY_POLICY_WINDOW_WIDTH = 460, CITY_POLICY_WINDOW_HEIGHT = 236,
  CITY_POLICY_PANEL_MARGIN = 4, CITY_POLICY_PANEL_GAP = 4, CITY_POLICY_PANEL_HEIGHT = 68,
  CITY_POLICY_ROWS_TOP = 14, CITY_POLICY_ROW_PITCH = 12, CITY_POLICY_ROW_HEIGHT = 12
};
CityRect CityPolicyPanelBounds(CityRect bounds, int panel);
i32 Sc2kCanvasPolicyAt(const Window *window, i32 x, i32 y);
enum { CITY_MAP_DATA_MIN_WIDTH = 250, CITY_MAP_DATA_MIN_HEIGHT = 160,
    CITY_MAP_DATA_DEFAULT_WIDTH = 356, CITY_MAP_DATA_DEFAULT_HEIGHT = 270 };

CityRect CityMapResizeGripBounds(const Window *window);
void ToggleCityMapDataOverlay(City *city);
bool ToggleCityMapDataDialog(City *city);
void CityMapDataDialogOnOverlaySelectionChanged(City *city, int selection);
void CityMapDataDialogOnListSelectionChanged(City *city, int selection);
bool IsCityTransitUnderfunded(const City *city);
void Sc2kCanvasDrawBudgetPanel(Canvas *canvas, CityRect bounds, const CityBudgetPanel *budget);
int OpenCityCommandErrorWindow(Game *game, const char *summary, const char *detail, bool retained, TileIndex tile);
void CloseCityCommandErrorWindows(CityCanvasDesktop *desktop);
void Sc2kCanvasDrawNotice(Canvas *canvas, CityRect bounds, const char *title, const char *message);
void Sc2kCanvasDrawSpanStyle(Canvas *canvas, CityRect bounds, const char *title, const char *message);
void Sc2kCanvasDrawNewspaper(Canvas *canvas, CityRect bounds,
    const char *headline, const char *article);
i32 Sc2kCanvasNewspaperLayoutStyle(City *city, int edition_index);
void Sc2kCanvasNewspaperPageLayout(const Drawing *drawing, CityCanvasNewspaperPage *page, i32 layout_style,
    const char *const *text);
void DrawCityMessageBox(Canvas *canvas, const Window *window);
// Required flags combine one CityMessageStyle and one CityMessageButtons value. Text and portrait are borrowed.
int OpenCityMessageBox(CityCanvasDesktop *desktop, Window window, u32 flags);
// Call after changing message text to resize and invalidate both the old and new bounds.
CityRect CityMessageBounds(const CityCanvasDesktop *desktop, const Window *window);
void ResizeCityMessageBox(CityCanvasDesktop *desktop, Window *window);
void Sc2kCanvasDrawPolicies(Game *game, Canvas *canvas, const Window *window);
void ResetCityWindowSize(Window *window);

enum { CITY_BOND_WINDOW_WIDTH = 360, CITY_BOND_WINDOW_HEIGHT = 204, CITY_BOND_WINDOW_MIN_HEIGHT = 132 };
void DrawCityBonds(const City *city, Canvas *canvas, const Window *window);
CityRect CityBondResizeGripBounds(const Window *window);
int CityBondVisibleRows(const Window *window);
int CityBondContentRows(int count);
void DrawCityMapDataWindow(Drawing *drawing, Canvas *canvas, const Window *window);
void DrawCityCanvasWindow(Game *game, Canvas *canvas, const Window *window);
void PlayCityButtonBeep(Game *game);
void BeginCityButtonFeedback(Game *game, int pixel_x, int pixel_y, double now);
bool PollCityButtonFeedback(Game *game, double now);
// Borrow application-owned UI state. The application retains ownership.
void Sc2kCanvasSetJukeboxState(CityInterfaceState *ui, const Jukebox *player);
i32 Sc2kCanvasJukeboxActionAt(const CityInterfaceState *ui, const Window *window, i32 x, i32 y);
// Pointer position for held button bevels; NULL releases the capture.
i32 Sc2kCanvasJukeboxPress(CityInterfaceState *ui, const Window *window, i32 x, i32 y);
// Momentary feedback lasts 150ms after release; tick returns whether repaint is needed.
void Sc2kCanvasJukeboxRelease(CityInterfaceState *ui, u32 milliseconds);
bool Sc2kCanvasJukeboxTick(CityInterfaceState *ui, u32 milliseconds);
bool UpdateCityJukeboxTitle(CityInterfaceState *ui, u32 milliseconds);
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
i32 Sc2kCanvasWidgetLayout(const CityWidget *widget, CityRect bounds, CityWidgetLayout *layout,
                              i32 capacity);
i32 Sc2kCanvasWidgetHit(const CityWidgetLayout *layout, i32 count, i32 x, i32 y);
const char *CityWidgetTooltipAt(const CityWidgetLayout *layout, int count, int x, int y);
i32 Sc2kCanvasGameOptionsActionAt(const Game *game, const Window *window,
    i32 x, i32 y, i32 continuous);
i32 Sc2kCanvasGameOptionsSliderAt(const GameOptions *options, const Window *window,
    i32 slider, i32 x, i32 continuous);
void Sc2kCanvasSetUIScale(CityCanvasDesktop *desktop, i32 numerator, i32 denominator);
void Sc2kCanvasSetMapZoom(CityCanvasDesktop *desktop, int level);
CityRect Sc2kCanvasUIBounds(const CityCanvasDesktop *desktop, CityRect bounds);
void Sc2kCanvasSetWindowSnapRadius(CityCanvasDesktop *desktop, i32 radius);
void DrawCityDesktopMap(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette);
void DrawCityDesktopFramebuffer(Game *game, const CityCanvasDesktop *desktop, Canvas *framebuffer,
    const u8 *map_indices, i32 map_stride, const u32 *palette, i32 map_scale, i32 ui_scale);
void DrawCityDesktopUI(Game *game, const CityCanvasDesktop *desktop, Canvas *canvas);
bool Sc2kCanvasClickWindow(Window *window, i32 x, i32 y);
// Window damage uses physical pixels; id 0 names the main map. Other ids use window-local pixels.
bool InvalidateCityCanvasWindow(Game *game, u32 window, const CityRect *rect);
void Sc2kDirtyClear(CityDirtyRegions *dirty);
void Sc2kDirtyMarkRect(CityDirtyRegions *dirty, CityRect rect);
void Sc2kCanvasDesktopInit(CityCanvasDesktop *desktop);
i32 Sc2kCanvasDesktopOpen(CityCanvasDesktop *desktop, Window window);
// Returns CITY_DIALOG_NONE outside a button; close chooses Cancel, No or OK for the configured button set.
int CityMessageAnswerAt(const Window *window, int x, int y);
i32 Sc2kCanvasDesktopOpenBudget(CityCanvasDesktop *desktop, CityRect bounds,
    const CityBudgetPanel *budget);
i32 Sc2kCanvasDesktopOpenNotice(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags);
i32 Sc2kCanvasDesktopOpenConfirm(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 flags);
u8 ReadCityFontCharacter(const char **text);
i32 Sc2kCanvasStadiumTeamAt(const Window *window, i32 x, i32 y);
i32 Sc2kCanvasDesktopOpenSpanStyle(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message);
u32 Sc2kInfrastructureSpanPrice(int distance, int style);
i32 Sc2kCanvasDesktopOpenSpanChoices(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, u32 styles, const u32 *prices);
enum { CITY_SPAN_CHOICE_WIDTH = 220, CITY_SPAN_LIST_TOP = 27, CITY_SPAN_ROW_HEIGHT = 24,
       CITY_SPAN_FOOTER_HEIGHT = 14 };
enum { CITY_SPAN_SORT_COST, CITY_SPAN_SORT_NAME };
// Nonnegative controls are visible row numbers; negative values are header/scroll controls.
CityRect CitySpanControlBounds(const Window *window, int control);
int ReadCitySpanList(const Window *window, u8 order[CITY_SPAN_STYLE_COUNT]);
int CitySpanVisibleRows(const Window *window);
CityRect CitySpanScrollThumb(const Window *window);
// Returns a style index or a CityCanvasSpanHit sentinel.
i32 CitySpanChoiceAt(const Window *window, i32 x, i32 y);
i32 Sc2kCanvasDesktopOpenNewspaper(CityCanvasDesktop *desktop, CityRect bounds,
    const char *headline, const char *article);
i32 Sc2kCanvasDesktopOpenNewspaperPage(CityCanvasDesktop *desktop, CityRect bounds,
    const CityCanvasNewspaperPage *page);
i32 Sc2kCanvasDesktopOpenAdvisor(CityCanvasDesktop *desktop, CityRect bounds,
    const char *title, const char *message, const struct RunSprite *portrait, u32 flags);
i32 Sc2kCanvasDesktopOpenPolicies(CityCanvasDesktop *desktop, CityRect bounds, u32 policy_flags);
i32 Sc2kCanvasDesktopOpenBonds(CityCanvasDesktop *desktop, CityRect bounds, int issue_rate);
CityRect Sc2kCanvasMapDataPreviewRect(const Window *window);
i32 Sc2kCanvasDesktopOpenMapData(CityCanvasDesktop *desktop, CityRect bounds, i32 selection);
i32 Sc2kCanvasDesktopOpenLandUse(CityCanvasDesktop *desktop, CityRect bounds,
    const i16 *tile_counts);
i32 Sc2kCanvasDesktopOpenFacilityInformation(CityCanvasDesktop *desktop, CityRect bounds, i32 facility);
i32 Sc2kCanvasDesktopOpenTileInformation(City *city, CityRect bounds, TileIndex tile);
void Sc2kCanvasDesktopUpdateLandUse(CityCanvasDesktop *desktop, const i16 *tile_counts);
void Sc2kCanvasDesktopUpdateMapData(CityCanvasDesktop *desktop, i32 selection,
    const u32 *map_preview);
void UpdateCityPolicyWindows(City *city, CityCanvasDesktop *desktop, u32 policy_flags);
void Sc2kCanvasDesktopUpdateBudget(CityCanvasDesktop *desktop, const CityBudgetPanel *budget);
bool Sc2kCanvasDesktopDismissTop(CityCanvasDesktop *desktop);
bool Sc2kCanvasDesktopDismissWindow(CityCanvasDesktop *desktop, u32 window_id);
Window *FindCityCanvasWindow(CityCanvasDesktop *desktop, u32 id);
// Bring an existing visible window forward; return its index, or -1 when absent.
int FocusCityCanvasWindow(CityCanvasDesktop *desktop, u32 id);
int FocusCityCanvasWindowKind(CityCanvasDesktop *desktop, WindowKind kind);
bool PollCityWindowHighlights(CityCanvasDesktop *desktop, u32 milliseconds);
u32 Sc2kCanvasDesktopFocusAt(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopPointerDown(CityCanvasDesktop *desktop, i32 x, i32 y);
bool Sc2kCanvasDesktopPointerMove(CityCanvasDesktop *desktop, i32 x, i32 y);
void Sc2kCanvasDesktopPointerUp(CityCanvasDesktop *desktop);
void FormatCityToolStatusText(const CityResourceState *assets, char *text, u32 capacity, int tool);
extern const CityDropdownColors kCityDropdownGoldColors;
void DrawCityDropdownControl(Canvas *canvas, const CityDropdownColors *colors, CityRect bounds,
    const char *text, bool pressed, bool disabled);
int CityTextBoxScroll(const CityInterfaceFont *font, CityRect bounds, const char *text, int caret, int scroll);
void DrawCityDeveloperConsole(Game *game, Canvas *canvas, const Window *window);
int CityTextBoxCaretAt(const CityInterfaceFont *font, CityRect bounds, const char *text, int caret, int *scroll, int x);
void DrawCityTextBox(Canvas *canvas, CityRect bounds, const char *text, int caret, int scroll,
    bool focused, bool selected, bool caret_visible);
void OpenCityDropdown(CityCanvasDesktop *desktop, u32 window_id, int control, CityRect anchor,
    const CityDropdownItem *items, int count, int selected, const CityDropdownColors *colors);
void SetCityDropdownChecks(CityCanvasDesktop *desktop, u32 checked, u32 disabled, const char *summary);
void CloseCityDropdown(CityCanvasDesktop *desktop);
CityRect CityDropdownBounds(const CityCanvasDesktop *desktop);
CityRect CityDropdownPixelBounds(const CityCanvasDesktop *desktop);
void DrawCityDropdown(const CityCanvasDesktop *desktop, Canvas *canvas);
bool HandleCityDropdownPointer(CityCanvasDesktop *desktop, CityDropdownPointer kind, int x, int y,
    CityDropdownSelection *selection);
bool HandleCityDropdownKey(CityCanvasDesktop *desktop, CityHotkey key, CityDropdownSelection *selection);
void HideCityTooltip(Game *game);
void MoveCityTooltipPointer(Game *game, int x, int y, double now);
bool UpdateCityTooltip(Game *game, double now);
const char *CityOptionsTooltipAt(const Game *game, const Window *window, int x, int y);
const char *CityMapTooltipAt(const Window *window, int x, int y);
void VisitCanvasMapPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
// Repaints run beneath toolbar/status controls; the compositor restores them afterward.
void VisitCanvasMapRepaintPixels(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
void Sc2kCanvasDesktopVisitMap(const CityCanvasDesktop *desktop, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
// Local content pixels of an additional map viewport, excluding windows above it.
void Sc2kCanvasDesktopVisitViewport(const CityCanvasDesktop *desktop, u32 window_id, CityRect area,
    CityCanvasVisiblePaint paint, void *context);
void DrawCityDesktop(Game *game, CityCanvasDesktop *desktop, Canvas *canvas,
    const u8 *map_indices, i32 map_stride, const u32 *palette);

bool DecodeCityBuildingCursorIcon(Game *game, int kind, const u32 palette[256], u32 icon[40 * 40]);
bool DecodeCityMapIcon(const Drawing *drawing, int sprite_id, const u32 palette[256], u32 *icon, int size);

// Information Text

bool WriteCityInformationText(CityInformationText *text, const char *value, i32 length);
i32 MeasureCityInformationText(CityInformationText *text, const char *value);
void ShiftCityInformationText(CityInformationText *text, i32 dx);
void PositionCityInformationText(CityInformationText *text, i32 x, i32 y, u32 alignment);
u32 ColorCityInformationText(CityInformationText *text, u32 color);

int CityStartupContentTop(int width, int height);

#endif
