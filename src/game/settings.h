#ifndef SC2K_GAME_SETTINGS_H
#define SC2K_GAME_SETTINGS_H

#include "city/types.h"
#include "city/engine/commands.h"
#include "city/saveload/path.h"

typedef enum CitySettingsGroup {
  CITY_SETTINGS_INTERFACE, CITY_SETTINGS_DISPLAY, CITY_SETTINGS_SOUND, CITY_SETTINGS_ECONOMY,
  CITY_SETTINGS_INFRASTRUCTURE, CITY_SETTINGS_MILITARY, CITY_SETTINGS_LABELS, CITY_SETTINGS_SIMCITY_FIXES,
  CITY_SETTINGS_GROUP_COUNT
} CitySettingsGroup;
typedef struct Game Game;
typedef struct CityDropdownItem CityDropdownItem;

typedef enum {
  SETTING_GAME, SETTING_CITY, SETTING_UI, SETTING_AUDIO, SETTING_DISPLAY, SETTING_JUKEBOX
} CitySettingOwner;
typedef enum { SETTING_BOOL, SETTING_U8, SETTING_I16, SETTING_I32, SETTING_U32, SETTING_MASK,
    SETTING_MASK64 } CitySettingType;
enum {
  SETTING_REDRAW_INTERFACE = 1, SETTING_REDRAW_MAP = 2, SETTING_SYNC_DISPLAY = 4,
  SETTING_UPDATE_VOLUME = 8, SETTING_CLOSE_STATUS = 16, SETTING_CONFIGURE_DIAGNOSTICS = 32,
  SETTING_STOP_EFFECTS = 64,
};
typedef struct {
  const char *label, *tooltip;
  CitySettingsGroup group;
  int action, default_value;
  const char *key;
  CitySettingOwner owner;
  CitySettingType type;
  size_t offset;
  u64 mask;
  int minimum, maximum;
  bool inverted, hidden, preserve_initial, toggle;
  CityCommandId command;
  int parameter;
  u32 effects;
  const CityDropdownItem *choices;
} CitySetting;
extern const int kCitySettingsCount;
extern const CitySetting kCitySettings[];
// defaults selects config defaults for City-owned settings; local presentation values always use their live owners.
int ReadCitySettingValue(const Game *game, const CitySetting *setting, bool defaults);
// Local fields and Game's City-fix defaults only. Shared effects use setting->command.
void WriteCitySettingValue(Game *game, const CitySetting *setting, int value, bool defaults);
// Execute only the City's resolved command. Config defaults remain separate.
bool RunCitySettingCommand(Game *game, const CitySetting *setting, int value);

// Game owns the parsed document; UI actions update it without disk I/O.
void LoadGameSettings(Game *game);
void ApplyGameSettings(Game *game, bool scale_override);
void StoreGameSettings(Game *game);
// Apply gameplay defaults through City commands, stopping at the first failed command.
bool ApplyGameCityDefaults(Game *game);
bool CopyCitySettingsToDefaults(Game *game);
// Read/change defaults for future cities without mutating the current City.
int ReadGameCityDefault(const Game *game, const CitySetting *setting);
bool StoreGameCityDefault(Game *game, const CitySetting *setting, int value);
void PollGameSettings(Game *game, double now);
void CloseGameSettings(Game *game);
// Caller frees the allocated path. NULL if no settings directory is available.
char *GameSettingsPath(const Game *game, const char *filename);
// Borrowed configured text; NULL/empty selects ~/.opensc2k/Cities.
const char *GameSaveDirectory(const Game *game);

bool GetCitySaveDirectory(Game *game, CityPath *output);

void ConfigureCityDiagnostics(Game *game);
void CloseCityDiagnostics(Game *game);

#endif
