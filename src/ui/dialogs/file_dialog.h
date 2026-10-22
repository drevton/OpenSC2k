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
bool OpenCitySaveDialog(City *city, CityFileChoiceCallback *callback, void *context);
void CompleteCityFileDialogs(City *city);
// An unanswered callback picker waits only while its window exists. Retained diagnostic pickers are excluded.
bool CityFileDialogWaiting(const CityInterfaceState *ui);
bool InitializeCityFileDialogPresentation(City *city, CityCanvasDesktop *desktop, const char *loaded_path);

void ShutdownCityFileDialogPresentation(City *city);
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
