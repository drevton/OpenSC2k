#include "game/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/config.h"
#include "base/diagnostics.h"
#include "city/saveload/services.h"
#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "city/engine/commands.h"
#include "audio/backend.h"
#include "ui/text/text.h"

struct GameSettings {
  Config document;
  char *directory, *path;
  double write_at;
  bool writable;
};

char *GameSettingsPath(const Game *game, const char *filename) {
  if (!game->settings || !game->settings->directory) return NULL;
  char *path = NULL;
  FormatText(&path, "%s/%s", game->settings->directory, filename);
  return path;
}

enum { SETTINGS_VALIDATE, SETTINGS_LOAD, SETTINGS_STORE };

static bool TransferInteger(Config *config, const char *key, i64 *value, int minimum, int maximum,
    bool boolean, int mode) {
  if (mode == SETTINGS_STORE) return SetConfigInteger(config, key, *value, boolean);
  if (!HasConfigKey(config, key)) return true;
  if (!ReadConfigInteger(config, key, value) || *value < minimum || *value > maximum) {
    fprintf(stderr, "Invalid configuration value: %s\n", key);
    return false;
  }
  return true;
}

static bool ReadPlaylist(const char *text, u8 tracks[JUKEBOX_CAPACITY], i32 *count) {
  *count = 0;
  while (*text) {
    if (*count == JUKEBOX_CAPACITY || *text < '0' || *text > '9') return false;
    char *end;
    long track = strtol(text, &end, 10);
    if (track < 0 || track >= JUKEBOX_TRACKS) return false;
    tracks[(*count)++] = track;
    text = end;
    if (*text && *text++ != ',') return false;
    if (*end == ',' && !*text) return false;
  }
  return true;
}

static bool ReadBinding(const char *text, CityHotkey *key, CityHotkey *action) {
  char first[48], second[48], extra;
  if (sscanf(text, "%47s %47s %c", first, second, &extra) != 2) return false;
  *key = CityHotkeyFromString(first); *action = CityHotkeyFromString(second);
  return *key && *action && *key != CITY_KEY_F12 && *key != CITY_KEY_ESCAPE && *key != CITY_KEY_CONSOLE;
}

static bool TransferGameSettings(Game *game, int mode, bool scale_override) {
  Config *config = &game->settings->document;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  i64 value;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (mode == SETTINGS_LOAD && scale_override &&
        (setting->action == GAME_OPTIONS_AUTO || !strcmp(setting->key, "display.scale"))) continue;
    value = mode == SETTINGS_STORE || HasConfigKey(config, setting->key) || setting->preserve_initial
        ? ReadCitySettingValue(game, setting, true) : setting->default_value;
    if (mode == SETTINGS_STORE && setting->command && setting->command != CITY_COMMAND_SET_FIX) {
      if (!ReadConfigInteger(config, setting->key, &value)) value = setting->default_value;
    }
    if (!TransferInteger(config, setting->key, &value, setting->minimum, setting->maximum,
        setting->maximum == 1, mode)) return false;
    if (mode == SETTINGS_LOAD) WriteCitySettingValue(game, setting, (int)value, true);
  }
  const char *save_directory = ReadConfigString(config, "files.save_directory");
  if (HasConfigKey(config, "files.save_directory") && !save_directory) return false;
  if (mode == SETTINGS_STORE && !save_directory && !SetConfigString(config, "files.save_directory", "")) return false;
  const char *soundfont = ReadConfigString(config, "audio.soundfont");
  if (HasConfigKey(config, "audio.soundfont") && !soundfont) return false;
  if (mode == SETTINGS_STORE) {
    if (!SetConfigString(config, "audio.soundfont", audio->soundfont_path ? audio->soundfont_path : "")) return false;
  } else if (mode == SETTINGS_LOAD && soundfont && *soundfont) {
    char *copy = strdup(soundfont);
    if (!copy) return false;
    free(audio->soundfont_path); audio->soundfont_path = copy;
  }
  for (int list = 0; list < JUKEBOX_CUSTOM_PROGRAMMES; ++list) {
    char key[32], text[JUKEBOX_CAPACITY * 3 + 1];
    snprintf(key, sizeof(key), "music.playlist%d", list + 1);
    const char *saved = ReadConfigString(config, key);
    if (HasConfigKey(config, key) && !saved) return false;
    if (mode == SETTINGS_STORE) {
      size_t length = 0;
      text[0] = 0;
      for (int i = 0; i < ui->jukebox.custom_count[list]; ++i)
        length += snprintf(text + length, sizeof(text) - length, "%s%d", i ? "," : "", ui->jukebox.custom[list][i]);
      if (!SetConfigString(config, key, text)) return false;
    } else if (saved) {
      u8 tracks[JUKEBOX_CAPACITY];
      i32 count;
      if (!ReadPlaylist(saved, tracks, &count)) return false;
      if (mode == SETTINGS_LOAD) {
        memcpy(ui->jukebox.custom[list], tracks, count);
        ui->jukebox.custom_count[list] = count;
      }
    }
  }
  if (mode == SETTINGS_LOAD) ui->console.binding_count = 0;
  for (int i = 0; i < 64; ++i) {
    char key[32], binding[104];
    snprintf(key, sizeof(key), "bindings.slot%d", i + 1);
    const char *saved = ReadConfigString(config, key);
    if (HasConfigKey(config, key) && !saved) return false;
    if (mode == SETTINGS_STORE) {
      binding[0] = 0;
      if (i < ui->console.binding_count) {
        char first[48], second[48];
        CityHotkeyToString(ui->console.bindings[i].key, first, sizeof(first));
        CityHotkeyToString(ui->console.bindings[i].action, second, sizeof(second));
        snprintf(binding, sizeof(binding), "%s %s", first, second);
      }
      if ((binding[0] || HasConfigKey(config, key)) && !SetConfigString(config, key, binding)) return false;
    } else if (saved && *saved) {
      CityHotkey first, second;
      if (!ReadBinding(saved, &first, &second)) return false;
      if (mode == SETTINGS_LOAD) {
        int index = ui->console.binding_count++;
        ui->console.bindings[index].key = first; ui->console.bindings[index].action = second;
      }
    }
  }
  return true;
}

void LoadGameSettings(Game *game) {
  const char *override = getenv("OPENSC2K_CONFIG_FILE");
  struct GameSettings *settings = calloc(1, sizeof(*settings));
  if (!settings) return;
  bool resolved = ResolveUserPath(override ? override : "~/.opensc2k/config.toml", NULL, &settings->path);
  if (!resolved) {
    fprintf(stderr, "Cannot resolve the configuration filename.\n");
    free(settings);
    return;
  }
  const char *filename = PathFilename(settings->path);
  size_t length = filename - settings->path;
  if (length > 1 && IsPathSeparator(settings->path[length - 1])) --length;
  SetTextN(&settings->directory, settings->path, length);
  game->settings = settings;
  settings->writable = CreateDirectories(settings->directory) && LoadConfig(&settings->document, settings->path) &&
      TransferGameSettings(game, SETTINGS_VALIDATE, false);
  if (!settings->writable) {
    fprintf(stderr, "Cannot read configuration; preserving the file and using defaults.\n");
    FreeConfig(&settings->document);
  }
  // File-loading policy and City-fix defaults are needed before the first city is decoded.
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (setting->owner != SETTING_GAME && setting->command != CITY_COMMAND_SET_FIX) continue;
    i64 value = setting->default_value;
    ReadConfigInteger(&settings->document, setting->key, &value);
    WriteCitySettingValue(game, setting, (int)value, true);
  }
}

void ApplyGameSettings(Game *game, bool scale_override) {
  struct GameSettings *settings = game->settings;
  if (!settings) return;
  if (!TransferGameSettings(game, SETTINGS_LOAD, scale_override)) {
    settings->writable = false;
    fprintf(stderr, "Could not apply configuration.\n");
    return;
  }
  if (game->audio->live_open && game->audio->custom_soundfont && !SetCitySoundFont(game->audio, NULL, true))
    fprintf(stderr, "Could not load the configured MIDI soundfont.\n");
  JukeboxSelect(&game->ui->jukebox, game->ui->jukebox.programme);
  game->audio->music_enabled = game->ui->jukebox.playing;
  NativeAudioVolume(game->audio, &game->ui->jukebox);
  // Saved native cities retain their deterministic settings. Apply configured defaults
  // to stock cities through resolved commands, outside command execution itself.
  if (!game->city->file_extensions) {
    for (int fix = 0; fix < CITY_FIX_COUNT; ++fix)
      if (CITY_FIX_SIMULATION_MASK & (1ull << fix))
        RunCityCommand(game, CITY_COMMAND_SET_FIX, INVALID_TILE, fix,
            (game->default_city_fixes & (1ull << fix)) != 0, 0, NULL, NULL);
  }
  StoreGameSettings(game);
  if (!game->city_active) ApplyGameCityDefaults(game);
}

void ApplyGameCityDefaults(Game *game) {
  if (!game->settings) return;
  Config *config = &game->settings->document;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (!setting->command) continue;
    i64 value = setting->default_value;
    ReadConfigInteger(config, setting->key, &value);
    if (setting->command == CITY_COMMAND_SET_SIMULATION_SPEED) {
      // Pause is temporary; older configurations can contain paused or zero speeds.
      if (value < CITY_SPEED_SLOW) value = CITY_SPEED_NORMAL;
      game->ui->resume_speed = (int)value;
      if (game->ui->dialog_pause_requested) continue;
    }
    RunCitySettingCommand(game, setting, (int)value);
  }
}

void StoreGameSettings(Game *game) {
  if (!game->settings || !game->settings->writable) return;
  if (!TransferGameSettings(game, SETTINGS_STORE, false)) fprintf(stderr, "Could not update configuration.\n");
  if (game->settings->document.changed && !game->settings->write_at)
    game->settings->write_at = -1;
}

void StoreGameCitySetting(Game *game, const char *key, i64 value) {
  if (!game->settings || !game->settings->writable) return;
  const CitySetting *setting = NULL;
  for (int i = 0; i < kCitySettingsCount; ++i)
    if (!strcmp(kCitySettings[i].key, key)) { setting = &kCitySettings[i]; break; }
  if (!setting || value < setting->minimum || value > setting->maximum) return;
  if (!SetConfigInteger(&game->settings->document, key, value, setting->maximum == 1))
    fprintf(stderr, "Could not update configuration: %s\n", key);
  StoreGameSettings(game);
}

void PollGameSettings(Game *game, double now) {
  struct GameSettings *settings = game->settings;
  if (!settings || !settings->writable || !settings->write_at) return;
  if (settings->write_at < 0) settings->write_at = now + 3;
  if (now < settings->write_at) return;
  if (!WriteConfig(&settings->document, settings->path)) {
    fprintf(stderr, "Could not save configuration; will retry.\n");
    settings->write_at = now + 30;
  } else settings->write_at = 0;
}

void CloseGameSettings(Game *game) {
  struct GameSettings *settings = game->settings;
  if (!settings) return;
  if (settings->writable && !WriteConfig(&settings->document, settings->path))
    fprintf(stderr, "Could not save configuration at exit.\n");
  FreeConfig(&settings->document);
  free(settings->path); free(settings->directory); free(settings);
  game->settings = NULL;
}

const char *GameSaveDirectory(const Game *game) {
  return game->settings ? ReadConfigString(&game->settings->document, "files.save_directory") : NULL;
}

bool GetCitySaveDirectory(Game *game, CityPath *output) {
  const char *configured = GameSaveDirectory(game);
  char *path = NULL;
  bool custom = configured && configured[0];
  bool home = custom && configured[0] == '~' && (!configured[1] || IsPathSeparator(configured[1]));
  if (custom && !home && !IsPathAbsolute(configured))
    path = GameSettingsPath(game, configured);
  else if (!ResolveUserPath(custom ? configured : "~/.opensc2k/Cities", "", &path)) return false;
  bool success = path && CreateDirectories(path) && SetCityPathBytes(game, output, (int)strlen(path), path);
  free(path);
  return success;
}


void ConfigureCityDiagnostics(Game *game) {
  char *path = game->diagnostic_log ? GameSettingsPath(game, "sc2k.log") : NULL;
  u32 flags = (game->diagnostic_log ? DIAGNOSTICS_LOG : 0) | (game->crash_diagnostics ? DIAGNOSTICS_CRASH : 0);
  flags = OpenDiagnostics(&game->diagnostics, path, flags);
  free(path);
  game->diagnostic_log = (flags & DIAGNOSTICS_LOG) != 0;
  game->crash_diagnostics = (flags & DIAGNOSTICS_CRASH) != 0;
}

void CloseCityDiagnostics(Game *game) {
  CloseDiagnostics(game->diagnostics);
  game->diagnostics = NULL;
}
