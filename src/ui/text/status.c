#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include <stdio.h>
#include <string.h>

#include "city/state.h"
#include "city/saveload/file.h"
#include "game/errors.h"
#include "game/state.h"
#include "audio/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "assets/catalog.h"
#include "ui/text/text.h"

#include "drawing/drawing.h"

const char *const kCityWeatherText[] = {
  "Cold",
  "Clear",
  "Hot",
  "Foggy",
  "Chilly",
  "Cloudy",
  "Snow",
  "Rain",
  "Windy",
  "Blizzard",
  "Hurricane",
  "Tornado",
};
const char *const kCityDisasterText[] = {
  "None",
  "Fire",
  "Flood",
  "Riots",
  "Toxic pollution",
  "Aircraft crash",
  "Earthquake",
  "Tornado",
  "Monster",
  "Nuclear meltdown",
  "Microwave incident",
  "Volcano",
  "Firestorm",
  "Widespread riots",
  "Severe flooding",
  "Chemical Spill",
  "Hurricane",
};


// Status

void SetCityStatusPresentation(CityStatusState *status, CityCanvasDesktop *desktop, int width) {
  status->desktop = desktop;
  status->width = desktop != NULL ? width : 0;
  if (desktop == NULL) status->minimized = false;
}

void SetCityStatusMinimized(CityStatusState *status, bool minimized) {
  status->minimized = minimized;
}

bool IsCityStatusAvailable(const CityStatusState *status) {
  return status->desktop != NULL && status->width > 0;
}

bool InvalidateCityStatus(CityStatusState *status) {
  if (!IsCityStatusAvailable(status)) return false;
  CityCanvasDesktop *desktop = status->desktop;
  Sc2kDirtyMarkRect(&desktop->dirty, CityMenuBounds(desktop, 2));
  return true;
}

// Each of the simulation's 25 monthly phases is one calendar day.
int CityDayOfMonth(const City *city) {
  return 1 + city->clock.tick % CITY_PHASE_COUNT;
}

// City Status
// Transient status strings. The native owned-list dispatcher
// reaches CityMapViewOnUpdate, which can copy them into the document-owned title.

// Format ordinary text; SDK string descriptors are presentation details.
void UpdateCityStatus(City *city) {
  char title[CITY_STATUS_TEXT_CAPACITY] = "";
  const char *status_text = NULL;
  if (city->game->exit_requested)
    return;
  if (city->settings.timer_mode == CITY_TIMER_STOPPED) {
    status_text = "Editing terrain";
  } else {
    if (city->name.length == 0)
      return;
    int name_length = Utf8PrefixBytes(CityNameText(&city->name), CITY_NAME_LIMIT);
    const char *name = CityNameText(&city->name);
    int tick = (int)city->clock.tick;
    int month = (tick / 25) % 12;
    int year = city->geography.start_year + tick / 300;
    if (IsCityTitleMinimized(city)) {
      if (city->disasters.map_update_pending == 0) {
        snprintf(title, sizeof(title), "%.*s - %d", name_length, name, year);
      } else {
        int disaster = city->disasters.last_type;
        if (disaster <= CITY_DISASTER_HURRICANE) {
          if (disaster < 0) {
            SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
            return;
          }
          status_text = kCityDisasterText[disaster];
        }
      }
    } else {
      char number[CITY_CURRENCY_TEXT_CAPACITY];
      FormatCityCurrency(city->game, number, city->indicators.cash);
      if (number[0] != 0) {
        if (month >= 0 && month < 12) {
          const char *month_text = city->game->resources->calendar_names.abbreviated[month];
          int length = snprintf(title, sizeof(title), "%s %4d <%.*s> %s", month_text,
              year, name_length, name, number);
          // Native titles also accommodate full-width treasury values.
          if (length < 0 || length >= (int)sizeof(title)
              ) {
            SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
            title[sizeof(title) - 1] = 0;
          }
        } else {
          SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
        }
      }
    }
  }
  if (status_text) CopyText(title, sizeof(title), status_text);
  PublishCityTitle(city, title);
}

// Status State

// Original resource loading uses a 256-byte buffer, including the terminator.
void SetCityProblemDescription(CityStatusState *status, const char *text) {
  size_t length = text != NULL ? strlen(text) : 0;
  if (length >= CITY_STATUS_TEXT_CAPACITY) length = CITY_STATUS_TEXT_CAPACITY - 1;
  if (length != 0) memmove(status->problem_description, text, length);
  status->problem_description[length] = 0;
}

void ClearCityProblemDescription(CityStatusState *status) {
  status->problem_description[0] = 0;
}

bool IsCityTitleMinimized(const City *city) {
  return city->game->ui->main_window_iconic != 0;
}

void RefreshCitySpeedStatus(City *city, i32 mode) {
  CityInterfaceState *ui = city->game->ui;
  if (city->settings.speed > CITY_SPEED_PAUSED) ui->resume_speed = city->settings.speed;
  RefreshCityStatusPanel(city, &ui->status, city->settings.speed, mode);
}
