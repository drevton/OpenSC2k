#ifndef SC2K_UI_ALERTS_H
#define SC2K_UI_ALERTS_H

#include "city/presentation.h"

#include "city/types.h"
#include "ui/dialogs/dialogs.h"

typedef struct City City;

// Canvas presentation receives the City whose services and UI it uses.
// The unlock offer must journal an actual command-16 answer before returning;
// ranked events retain the current-edition newspaper constructor/modal/destructor.

void PresentCityAnnualBudget(City *city);
void PresentCityMissionOutcome(City *city, i32 outcome);
void PresentCityMessage(City *city, u32 resource_id);
// Construct a resource notice without pause reconciliation. Returns a borrowed desktop index or -1.
// Attach completion and initialize caller records before reconciling pauses; the index cannot survive that call.
int CreateCityResourceAlert(City *city, int bitmap_id, int text_id);
// Opens a resource notice without retaining a caller; returns its captured window ID, or zero on failure.
// Pause reconciliation may close/reorder windows before return. Reacquire this ID before accessing the window.
u32 OpenCityResourceAlert(City *city, int bitmap_id, int text_id);
void PresentCityResourceAlert(City *city, i32 bitmap_id, i32 text_id);
i32 ChooseCityStadiumPlacementDialog(City *city, CityCanvasStadiumRequest *request);
void PresentRankedCityEvent(City *city, u32 event_code, u32 event_data);

void ShowCityMissionOutcome(City *city, int outcome);

#endif
