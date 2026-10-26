#ifndef SC2K_NATIVE_SAVE_DIALOG_H
#define SC2K_NATIVE_SAVE_DIALOG_H

#include "city/types.h"

typedef struct Game Game;
typedef struct CityCanvasDesktop CityCanvasDesktop;
typedef struct CityInterfaceState CityInterfaceState;

typedef struct NativeSaveScope NativeSaveScope;
typedef struct NativeFileDialog NativeFileDialog;
typedef struct NativeReplacementPrompt NativeReplacementPrompt;

typedef struct CityFilePresentation {
  // Borrowed picker desktop; NULL means file dialogs are unavailable.
  CityCanvasDesktop *desktop;
  NativeSaveScope *scope;
  NativeFileDialog *dialog;
  NativeReplacementPrompt *prompt;
  int next_token;
  int browser_width, browser_height;
  bool browser_sort_date, browser_descending;
  char *destination;
} CityFilePresentation;

typedef void CityFileChoiceCallback(City *city, const char *path, void *context);
typedef struct CityFilePickerOptions {
  const char *name, *directory, *title;
  bool open;
  CityFileChoiceCallback *callback;
  void *context;
} CityFilePickerOptions;
// The picker owns its state until completion. NULL means cancellation; a path
// is borrowed for the callback. Completion closes the window before calling it.
// Shutdown delivers cancellation so the callback can release its context.
bool OpenCityFileDialog(City *city, const CityFilePickerOptions *options);
void CompleteCityFileDialogs(City *city);
// An unanswered callback picker waits only while its window exists. Retained diagnostic pickers are excluded.
bool CityFileDialogWaiting(const CityInterfaceState *ui);
// Borrow the desktop, copy the initial destination, then initialize Game's host directory. Requires no save scope.
// A directory failure retains the desktop and destination already installed; shutdown releases them.
bool InitializeCityFileDialogPresentation(Game *game, CityCanvasDesktop *desktop, const char *loaded_path);

void ShutdownCityFileDialogPresentation(City *city);
// Validate a live picker/prompt token and copy an accepted path before dismissing its windows.
// Answers affect local chooser/save-attempt state; the retained caller applies resolved City changes.
bool AnswerCityFileDialog(City *city, i32 token, i32 answer, const u8 *path, u32 length);
bool AnswerCityReplacementPrompt(City *city, i32 token, i32 answer);

// Button sets requested by presentation callers.
enum {
  CITY_PROMPT_YES_NO_CANCEL = 3, CITY_PROMPT_YES_NO = 4,
};

// Retained replacement questions borrow text for the call and preserve their original reply values.
i32 PresentCityReplacementQuestion(City *city, const char *text, u32 flags);
// Requires a live file scope. Its selected path is borrowed until that scope changes or ends;
// obtain it again after callbacks that may change the selection.
const char *ReadCitySaveSelectedPath(const City *city);

// File notices copy borrowed text into window-owned storage. Load warnings preserve the imported City state.
void PresentCityFileNotice(City *city, const char *text, bool error);
void ShowCityLoadWarnings(City *city);
void ShowCityLoadCommandResource(City *city, u32 resource);

// Host-only scopes surround Open/Save/replacement and startup New City callers.
NativeSaveScope *NativeSaveDialogBegin(City *city);
void NativeSaveDialogEnd(City *city, NativeSaveScope *scope);
bool NativeSaveDialogOpen(const City *city);
// Retained diagnostic picker. Normal play uses OpenCityFileDialog.
// Replaces the owned selection only after an accepted choice.
i32 ChooseCityCanvasOpenFile(City *city, const char *name, const char *directory, const char *title,
    char **selected);

// Retained menu callers require an active file operation; normal input uses RequestCitySave and its picker callback.
void HandleSaveCityCommand(City *city);
void HandleSaveCityAsCommand(City *city);

void RequestCitySave(Game *game);

#endif
