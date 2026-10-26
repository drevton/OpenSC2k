#include "ui/text/status.h"
#include "base/text.h"
#include "base/utf8.h"

#include <stdio.h>
#include <string.h>

#include "assets/state.h"
#include "base/version.h"
#include "city/disasters/types.h"
#include "city/engine/city_types.h"
#include "city/engine/clock.h"
#include "city/engine/commands.h"
#include "city/events/news.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "city/tools/types.h"
#include "drawing/state.h"
#include "game/errors.h"
#include "game/state.h"
#include "ui/input.h"
#include "ui/sdl.h"
#include "ui/state.h"
#include "ui/text/text.h"
#include "ui/tools/tools.h"
#include "ui/windows/windows.h"

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

// Format the title from live City and UI state without changing the stored calendar.
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
    int month = (tick / CITY_PHASE_COUNT) % CITY_MONTH_COUNT;
    int year = city->geography.start_year + tick / CITY_TICKS_PER_YEAR;
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
        if (month >= 0 && month < CITY_MONTH_COUNT) {
          const char *month_text = city->game->resources->calendar_names.abbreviated[month];
          int length = snprintf(title, sizeof(title), "%s %4d <%.*s> %s", month_text,
              year, name_length, name, number);
          // Native titles also accommodate full-width treasury values.
          if (length < 0 || length >= (int)sizeof(title)) {
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

// Present the simulation's selected problem; wording and retained text belong to UI.
void SetCityProblemNotice(City *city, int problem) {
  CityInterfaceState *ui = city->game->ui;
  if (!ui) return;
  static const char *const kProblemDescriptions[CITY_PROBLEM_COUNT] = {
    [CITY_PROBLEM_POWER] = "More power plants needed",
    [CITY_PROBLEM_TRANSPORT] = "Residents want roads and rail",
    [CITY_PROBLEM_POLICE] = "Residents want more police",
    [CITY_PROBLEM_FIRE_STATION] = "More fire protection needed",
    [CITY_PROBLEM_WATER] = "City water shortage",
    [CITY_PROBLEM_HOSPITAL] = "More hospitals needed",
    [CITY_PROBLEM_SCHOOL] = "Residents want more schools",
    [CITY_PROBLEM_SEAPORT] = "Industry needs a seaport",
    [CITY_PROBLEM_AIRPORT] = "Commerce needs an airport",
    [CITY_PROBLEM_ZOO] = "Residents want a zoo",
    [CITY_PROBLEM_STADIUM] = "Residents want a stadium",
    [CITY_PROBLEM_MARINA] = "Residents want a marina",
    [CITY_PROBLEM_PARK] = "Residents want more parks",
    [CITY_PROBLEM_INDUSTRIAL_CONNECTION] = "Industry needs outside links",
    [CITY_PROBLEM_COMMERCIAL_CONNECTION] = "Commerce needs outside links",
  };
  const char *text = "";
  if (problem == CITY_PROBLEM_WEATHER) {
    switch (city->weather.state) {
    case CITY_WEATHER_BLIZZARD: text = "Heavy snow expected"; break;
    case CITY_WEATHER_HURRICANE: text = "Hurricane approaching"; break;
    case CITY_WEATHER_TORNADO: text = "Tornado risk reported"; break;
    default: break;
    }
  } else if (problem >= 0) {
    text = kProblemDescriptions[problem];
  }
  SetCityProblemDescription(&ui->status, text);
}

void ClearCityProblemDescription(CityStatusState *status) {
  status->problem_description[0] = 0;
}

void PublishCityTitle(City *city, const char *title) {
  snprintf(city->game->ui->status.city_title, CITY_STATUS_TEXT_CAPACITY, "%s", title);
  if (city->game->ui->display != NULL)
    Sc2kSDLTitle(city->game->ui->display, SC2K_APP_NAME);
}

bool IsCityTitleMinimized(const City *city) {
  return city->game->ui->main_window_iconic != 0;
}

void RefreshCitySpeedStatus(City *city, i32 mode) {
  CityInterfaceState *ui = city->game->ui;
  if (city->settings.speed > CITY_SPEED_PAUSED) ui->resume_speed = city->settings.speed;
  RefreshCityStatusPanel(city, &ui->status, city->settings.speed, mode);
}

// Store a legend string/color and invalidate the status area when it changes.
bool SetCityMapLegendColumn(Game *game, CityStatusState *status, int column, const char *text, u32 color) {
  if (column < 0 || column >= CITY_STATUS_COLUMN_COUNT) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  size_t length = strlen(text);
  if (length >= CITY_STATUS_TEXT_CAPACITY) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  CityStatusLegend *legend = &status->legend;
  if (legend->color[column] == color && !strcmp(legend->text[column], text)) return IsCityStatusAvailable(status);
  memmove(legend->text[column], text, length + 1);
  legend->color[column] = color;
  return InvalidateCityStatus(status);
}

// Format tool help or the selected name and construction cost.
void FormatCityToolStatusText(char *text, u32 capacity, int tool) {
  if (tool == CITY_CANVAS_TOOL_RAISE_TERRAIN || tool == CITY_CANVAS_TOOL_LOWER_TERRAIN) {
    CopyText(text, capacity, tool == CITY_CANVAS_TOOL_RAISE_TERRAIN ?
        "Raise terrain: drag to select a range of tiles, then release to raise them" :
        "Lower terrain: drag to select a range of tiles, then release to lower them");
    return;
  }
  if (tool == CITY_CANVAS_TOOL_STREAM) {
    CopyText(text, capacity, "Place stream: click a source to grow a gently winding downhill stream");
    return;
  }
  if (tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN || tool == CITY_CANVAS_TOOL_STRETCH_TERRAIN) {
    CopyText(text, capacity, tool == CITY_CANVAS_TOOL_LEVEL_TERRAIN ?
        "Level terrain: drag an area to flatten it to the starting elevation" :
        "Stretch terrain: grab a tile and drag up or down to change its height");
    return;
  }
  if (tool == CITY_CANVAS_TOOL_ROAD_BRIDGE || tool == CITY_CANVAS_TOOL_RAIL_BRIDGE) {
    CopyText(text, capacity, tool == CITY_CANVAS_TOOL_ROAD_BRIDGE ?
        "Road bridge: drag between two endpoints" : "Rail bridge: drag between two endpoints");
    return;
  }
  i32 packed = Sc2kCanvasMapPointerTools(tool, 0);
  if (packed < 0) {
    snprintf(text, capacity, "%s", "Centering Tool");
    return;
  }
  i32 group = packed & 255;
  i32 variant = (packed >> 8) & 255;
  const char *name = CityToolName(group, variant);
  u32 cost = group < CITY_TOOL_GROUP_QUERY ? kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + variant] : 0;
  if (cost)
    snprintf(text, capacity, "%s $%u", name, cost);
  else
    snprintf(text, capacity, "%s", name);
}

void UpdateCityToolStatus(City *city) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  FormatCityToolStatusText(ui->status.tool_text, sizeof(ui->status.tool_text),
      ui->desktop.selected_tool);
}

// Tool/cost, city problem or disaster, and weather status strings.
void RefreshCityStatusPanel(City *city, CityStatusState *status, int speed, int force) {
  Game *game = city->game;
  CityInterfaceState *ui = game->ui;
  Drawing *drawing = game->drawing;
  if (force == 0 && status->minimized)
    return;
  int group, variant;
  if (IsCtrlKeyPressed(ui)) {
    group = CITY_TOOL_GROUP_CENTER;
    variant = CITY_TOOL_VARIANT_DEFAULT;
  } else if (IsShiftKeyPressed(ui)) {
    group = CITY_TOOL_GROUP_QUERY;
    variant = CITY_TOOL_VARIANT_DEFAULT;
  } else {
    group = ui->tool_group;
    if (group < 0 || group >= CITY_TOOL_GROUP_COUNT) {
      SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
      return;
    }
    variant = (i16)ui->selection.value[group];
  }
  if (variant < 0 || variant >= CITY_TOOL_VARIANT_CAPACITY) {
    SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return;
  }
  const char *name = CityToolName(group, variant);
  int cost = (int)kCityToolCosts[group * CITY_TOOL_VARIANT_CAPACITY + variant];
  char tool_text[CITY_STATUS_TEXT_CAPACITY];
  if (cost != 0) {
    char currency[CITY_CURRENCY_TEXT_CAPACITY];
    FormatCityCurrency(game, currency, cost);
    if (currency[0] == 0)
      return;
    int length = snprintf(tool_text, sizeof(tool_text), "%s  %s", name, currency);
    if (length < 0 || length >= (int)sizeof(tool_text)) {
      SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
      return;
    }
  } else {
    int length = snprintf(tool_text, sizeof(tool_text), "%s", name);
    if (length < 0 || length >= (int)sizeof(tool_text)) {
      SetGameError(city->game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
      return;
    }
  }
  u32 color = cost > 0 && AffordableCityCommandCost(city, cost) < 0 ?
      drawing->map_state.zone_cost_unaffordable_color : drawing->map_state.zone_cost_affordable_color;
  SetCityMapLegendColumn(game, status, CITY_STATUS_COLUMN_TOOL_COST, tool_text, color);

  const char *issue = "";
  if (speed == CITY_SPEED_PAUSED)
    issue = "Paused";
  else if (city->disasters.map_update_pending != 0) {
    int disaster = city->disasters.last_type;
    if (disaster >= CITY_DISASTER_NONE && disaster <= CITY_DISASTER_HURRICANE)
      issue = kCityDisasterText[disaster];
  } else {
    issue = ui->status.problem_description;
  }
  if (issue[0] != 0)
    color = speed == CITY_SPEED_PAUSED ? 25600 : city->disasters.map_update_pending != 0 ?
        drawing->map_state.zone_cost_unaffordable_color : drawing->map_state.zone_cost_affordable_color;
  SetCityMapLegendColumn(game, status, CITY_STATUS_COLUMN_ISSUE, issue, color);

  int weather = city->disasters.map_update_pending != 0 ? CITY_WEATHER_COUNT : city->weather.state;
  const char *weather_text = weather < CITY_WEATHER_COUNT ?
      kCityWeatherText[weather] : "";
  SetCityMapLegendColumn(game, status, CITY_STATUS_COLUMN_WEATHER, weather_text,
      drawing->map_state.zone_cost_affordable_color);
}
