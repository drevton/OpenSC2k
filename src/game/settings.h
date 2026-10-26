#ifndef SC2K_GAME_SETTINGS_H
#define SC2K_GAME_SETTINGS_H

#include "city/types.h"
#include "city/engine/command_types.h"

typedef enum CitySettingsGroup {
  CITY_SETTINGS_INTERFACE, CITY_SETTINGS_DISPLAY, CITY_SETTINGS_SOUND, CITY_SETTINGS_ECONOMY,
  CITY_SETTINGS_INFRASTRUCTURE, CITY_SETTINGS_MILITARY, CITY_SETTINGS_LABELS, CITY_SETTINGS_SIMCITY_FIXES,
  CITY_SETTINGS_GROUP_COUNT
} CitySettingsGroup;
typedef struct Game Game;
typedef struct CityDropdownItem CityDropdownItem;
typedef struct CityPath CityPath;

typedef enum CitySettingOwner {
  SETTING_GAME, SETTING_CITY, SETTING_UI, SETTING_AUDIO, SETTING_DISPLAY, SETTING_JUKEBOX
} CitySettingOwner;
typedef enum CitySettingType {
  SETTING_BOOL, SETTING_U8, SETTING_I16, SETTING_I32, SETTING_U32, SETTING_MASK, SETTING_MASK64
} CitySettingType;
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
// Live-field reads require an initialized owner and a field matching the descriptor's stored type.
// defaults selects config defaults for City-owned settings; local presentation values always use their live owners.
int ReadCitySettingValue(const Game *game, const CitySetting *setting, bool defaults);
// Write local preference fields only; City-owned descriptors are ignored. The caller applies presentation effects.
// Shared changes use RunCitySettingCommand; future-City defaults use StoreGameCityDefault.
void WriteLocalSettingValue(Game *game, const CitySetting *setting, int value);
// Execute only the City's resolved command. Config defaults remain separate.
bool RunCitySettingCommand(Game *game, const CitySetting *setting, int value);

// Load once after owner initialization. Game owns the parsed document; invalid files remain unwritable.
// Validate configured values before applying the file policy and cached City-fix defaults needed for city loading.
void LoadGameSettings(Game *game);
// Apply local preferences and gameplay defaults under the existing stock/native-city rules, then queue persistence.
// A command-line scale override preserves the current automatic/scale values.
// A later invalid value retains earlier preference updates and makes the configuration unwritable.
void ApplyGameSettings(Game *game, bool scale_override);
// Update the in-memory document from current preferences and City defaults, then queue a deferred write.
// UI changes use this without disk I/O; gameplay settings come from defaults, not the current City.
// Capture failure retains preceding document edits; changed contents still queue the existing deferred write.
void StoreGameSettings(Game *game);
// Apply gameplay defaults through City commands, stopping at the first failed command.
bool ApplyGameCityDefaults(Game *game);
bool CopyCitySettingsToDefaults(Game *game);
// Change defaults for future cities without mutating the current City.
// Read defaults through ReadCitySettingValue with defaults set to true.
bool StoreGameCityDefault(Game *game, const CitySetting *setting, int value);
// Host seconds: the first poll starts the queued write's delay. Failed writes retain changes for a later retry.
void PollGameSettings(Game *game, double now);
// Attempt any remaining write, then release the document and paths even on failure. Safe to repeat.
void CloseGameSettings(Game *game);

// Resolve the configured/default save directory into caller-owned path storage, creating the directory if needed.
bool GetCitySaveDirectory(Game *game, CityPath *output);

void ConfigureCityDiagnostics(Game *game);
void CloseCityDiagnostics(Game *game);

#endif
