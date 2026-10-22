#ifndef SC2K_UI_FILE_DIALOG_INTERNAL_H
#define SC2K_UI_FILE_DIALOG_INTERNAL_H

#include "ui/dialogs/file_browser.h"
#include "ui/dialogs/file_dialog.h"

// Window state and lifecycle shared with the optional retained-caller adapter.
typedef struct NativeFileDialog {
  struct NativeFileDialog *parent;
  NativeSaveScope *scope;
  CityFileChoiceCallback *callback;
  void *context;
  char *selected;
  u32 window_id;
  u32 overwrite_id;
  i32 token;
  i32 answer;
  CityFileBrowser browser;
  bool deleting;
  char *delete_path;
} NativeFileDialog;

// Caller-owned replacement-question scope. A successful Begin owns text/message copies until Finish.
typedef struct NativeReplacementPrompt {
  struct NativeReplacementPrompt *parent;
  u32 window_id;
  i32 token;
  i32 answer;
  u32 flags;
  char *text, *message;
} NativeReplacementPrompt;

void RememberCityFileDialogSize(City *city, NativeFileDialog *dialog);
void SubmitCityFileDialog(City *city, NativeFileDialog *dialog, i32 accept, i32 overwrite);
bool BeginCityFileDialog(City *city, NativeFileDialog *dialog, const CityFilePickerOptions *options);
void FinishCityFileDialog(City *city, NativeFileDialog *dialog);
void SubmitCityFileQuestion(City *city, NativeReplacementPrompt *prompt, i32 answer);
bool BeginCityFileQuestion(City *city, NativeReplacementPrompt *prompt, const char *text, u32 flags);
void FinishCityFileQuestion(City *city, NativeReplacementPrompt *prompt);

#endif
