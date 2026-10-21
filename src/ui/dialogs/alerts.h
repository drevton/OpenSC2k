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
void PresentCityResourceAlert(City *city, i32 bitmap_id, i32 text_id);
void PresentCityStadiumTeamDialog(City *city, TileIndex tile);
void PresentCityStadiumPlacementDialog(City *city, TileIndex tile);
i32 ChooseCityStadiumPlacementDialog(City *city, CityCanvasStadiumRequest *request);
void PresentRankedCityEvent(City *city, u32 event_code, u32 event_data);

#endif
