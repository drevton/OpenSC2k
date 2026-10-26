#ifndef SC2K_FILE_SERVICES_H
#define SC2K_FILE_SERVICES_H

#include "city/saveload/types.h"
#include "city/types.h"

typedef struct CityFileRuntime CityFileRuntime;
typedef struct CityLoadStream CityLoadStream;
typedef struct CitySaveStream CitySaveStream;

// File Load Services

// The caller owns an open wrapper and keeps its file operation alive. Reads may return a short EOF count;
// host read/seek/close errors throw through that operation's active exception frame. Close consumes the wrapper
// even when it throws. Use CityFileInput's stdio backend for borrowed standard FILE streams.

CityLoadStream *OpenCityLoadStream(CityFileRuntime *file, const char *path);
u32 ReadCityLoadStream(CityLoadStream *stream, void *destination, u32 size);
u32 SeekCityLoadStream(CityLoadStream *stream, i32 offset, i32 origin);
i32 SeekCityPreviewStream(CityLoadStream *stream, i32 offset, i32 origin);
i32 GetCityPreviewStreamPosition(CityLoadStream *stream);
void CloseCityLoadStream(CityLoadStream *stream);
void FillCityLoadOpenError(const CityFileRuntime *file, CityFileExceptionState *exception);

// Include the operation's recorded cause for stream failures, or show only the resource text for format errors.
typedef enum CityFileErrorDetail {
  CITY_FILE_ERROR_WITH_CAUSE = 0,
  CITY_FILE_ERROR_RESOURCE_ONLY = 1,
} CityFileErrorDetail;

// Presentation consumes borrowed paths/exception values synchronously. The UI formats native host error reasons;
// failed decoding keeps its actual partial City writes and does not rebuild successful-load state.
void ShowCityFileException(City *city, u32 resource, const CityFileExceptionState *exception, const char *path);
// Native UI displays message; resource identifies the same error for resource-aware diagnostic presenters.
void ReportCityLoadError(City *city, u32 resource, const char *message);
void ReportCityFileError(CityFileRuntime *file, u32 resource, CityFileErrorDetail detail);
void ReportCityFileCommandFailure(City *city);

// File Save Services

// Streams stay bound to the save attempt that opened them. The selected attempt destination controls host I/O;
// caller_path is terminated observation text borrowed only for this call. Successful open returns an owned wrapper.
// Keep its attempt alive until close. Writes and rewind borrow the wrapper and retain the attempt's first failure.
CitySaveStream *OpenCitySaveStream(CityFileRuntime *file, const char *caller_path);
bool WriteCitySaveStream(CitySaveStream *stream, const void *bytes, u32 size);
bool SeekCitySaveStreamStart(CitySaveStream *stream);
// Observe close before dropping the attempt's open count, then consume the wrapper even when close fails.
void CloseCitySaveStream(CitySaveStream *stream);
// Remove the active attempt's selected destination; caller_path supplies only the observed request text.
void RemoveCitySaveFile(CityFileRuntime *file, const char *caller_path);
void ReportCitySaveError(CityFileRuntime *file, i32 resource, const char *path);

#endif
