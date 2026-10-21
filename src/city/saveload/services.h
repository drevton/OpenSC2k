#ifndef SC2K_FILE_SERVICES_H
#define SC2K_FILE_SERVICES_H

#include <stddef.h>
#include <stdio.h>

#include "city/saveload/state.h"
#include "base/file.h"
#include "city/types.h"

// File Dialog Answers

// Validate the token against a live picker/prompt; callers own the game writes.
bool AnswerCityFileDialog(City *city, i32 token, i32 answer, const u8 *path, u32 length);
bool AnswerCityReplacementPrompt(City *city, i32 token, i32 answer);

// File Load Services

void *OpenCityLoadStream(CityFileRuntime *file, const char *path);
u32 ReadCityLoadStream(void *stream, void *destination, u32 size);
u32 SeekCityLoadStream(void *stream, i32 offset, i32 origin);
i32 SeekCityPreviewStream(void *stream, i32 offset, i32 origin);
i32 GetCityPreviewStreamPosition(void *stream);
void CloseCityLoadStream(void *stream);
void FillCityLoadOpenError(const CityFileRuntime *file, CityFileExceptionState *exception);
void *AllocateCityFileScratch(CityFileRuntime *file, size_t size);
void FreeCityFileScratch(CityFileRuntime *file, void *pointer);
bool PrepareCityMiscWorkspace(CityFileRuntime *file);
void ReleaseCityMiscWorkspace(CityFileRuntime *file);
void RunCityFileCommand(CityFileRuntime *file, CityFileAction action, void *context);

void ReportCityLoadError(City *city, u32 resource, const char *message);
void ReportCityFileCommandFailure(City *city);
void ShowCityLoadCommandResource(City *city, u32 resource);

// File Save Services

typedef struct CityFileRuntime CityFileRuntime;

// Operation-local streams and ordered serialization writes.
void *OpenCitySaveStream(CityFileRuntime *file, const char *path);
bool WriteCitySaveStream(void *stream, const void *bytes, u32 size);
bool SeekCitySaveStreamStart(void *stream);
void CloseCitySaveStream(void *stream);
void RemoveCitySaveFile(CityFileRuntime *file, const char *path);
void ReportCitySaveError(CityFileRuntime *file, i32 resource, const char *path);

void PublishCityTitle(City *city, const char *title);
bool IsCityTitleMinimized(const City *city);

i32 ChooseCitySaveFile(City *city, const char *name, const char *filter, const char *directory);
const char *ReadCitySaveSelectedPath(City *city);

// Replacement Services

typedef struct City City;

i32 PresentCityReplacementQuestion(City *city, const char *text, u32 flags);

#endif
