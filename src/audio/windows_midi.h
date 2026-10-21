#ifndef SC2K_WINDOWS_MIDI_H
#define SC2K_WINDOWS_MIDI_H

#include "city/types.h"

typedef struct WindowsMidi WindowsMidi;
WindowsMidi *OpenWindowsMidi(char *error, size_t capacity);
void CloseWindowsMidi(WindowsMidi *midi);
bool StopWindowsMidi(WindowsMidi *midi);
// Requires a stopped player; borrows a format 0/1 Standard MIDI File until stopped. Positions are seconds.
bool PlayWindowsMidi(WindowsMidi *midi, ByteSpan song, double seconds, char *error, size_t capacity);
void SetWindowsMidiVolume(WindowsMidi *midi, int volume);
bool ReadWindowsMidiPosition(WindowsMidi *midi, double *seconds);
// Same completion values as the native music controller: pending, finished or failed.
int WindowsMidiCompletion(WindowsMidi *midi);

#endif
