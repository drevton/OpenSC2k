#ifndef SC2K_NATIVE_SAVE_DIALOG_H
#define SC2K_NATIVE_SAVE_DIALOG_H

#include "city/presentation.h"

#include "ui/input.h"
#include "city/engine/commands.h"
#include "drawing/drawing.h"

typedef struct NativeSaveScope NativeSaveScope;
typedef void CityFileChoiceCallback(City *city, const char *path, void *context);
typedef void CityFileQuestionCallback(City *city, int answer, void *context);
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
bool OpenCityFileQuestion(City *city, const char *text, u32 flags,
    CityFileQuestionCallback *callback, void *context);
void CompleteCityFileDialogs(City *city);
bool CityFileDialogWaiting(const City *city);
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

#endif
