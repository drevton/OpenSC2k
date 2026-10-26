#include "game/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/backend.h"
#include "audio/state.h"
#include "base/config.h"
#include "base/diagnostics.h"
#include "base/file.h"
#include "base/text.h"
#include "city/saveload/path.h"
#include "city/state.h"
#include "drawing/state.h"
#include "game/commands.h"
#include "game/state.h"
#include "ui/input.h"
#include "ui/state.h"

struct GameSettings {
  Config document;
  char *directory, *path;
  double write_at;
  bool writable;
};

// Caller frees the allocated path. NULL if no settings directory is available.
static char *GameSettingsPath(const Game *game, const char *filename) {
  if (!game->settings || !game->settings->directory) return NULL;
  char *path = NULL;
  FormatText(&path, "%s/%s", game->settings->directory, filename);
  return path;
}

static const void *CitySettingOwnerAddress(const Game *game, CitySettingOwner owner) {
  switch (owner) {
  case SETTING_GAME: return game;
  case SETTING_CITY: return game->city;
  case SETTING_UI: return game->ui;
  case SETTING_AUDIO: return game->audio;
  case SETTING_DISPLAY: return &game->drawing->options;
  case SETTING_JUKEBOX: return &game->audio->jukebox;
  }
  return NULL;
}

// City defaults live in the config document; the fix mask is cached for City initialization and stock imports.
static int ReadGameCityDefault(const Game *game, const CitySetting *setting) {
  if (setting->command == CITY_COMMAND_SET_FIX) {
    bool enabled = (game->default_city_fixes & setting->mask) != 0;
    return setting->inverted ? !enabled : enabled;
  }
  if (game->settings) {
    ConfigValue value = ReadConfigValue(&game->settings->document, setting->key);
    if (value.type == CONFIG_INTEGER) return (int)value.integer;
  }
  return setting->default_value;
}

int ReadCitySettingValue(const Game *game, const CitySetting *setting, bool defaults) {
  if (defaults && setting->owner == SETTING_CITY) return ReadGameCityDefault(game, setting);
  const u8 *field = (const u8 *)CitySettingOwnerAddress(game, setting->owner) + setting->offset;
  int value;
  switch (setting->type) {
  case SETTING_BOOL: value = *(const bool *)field; break;
  case SETTING_U8: value = *field; break;
  case SETTING_I16: value = *(const i16 *)field; break;
  case SETTING_I32: value = *(const i32 *)field; break;
  case SETTING_U32: value = (int)*(const u32 *)field; break;
  case SETTING_MASK:
    value = (*(const u32 *)field & setting->mask) != 0;
    break;
  case SETTING_MASK64:
    value = (*(const u64 *)field & setting->mask) != 0;
    break;
  }
  return setting->inverted ? !value : value;
}

void WriteLocalSettingValue(Game *game, const CitySetting *setting, int value) {
  if (setting->inverted) value = !value;
  // Authoritative City fields are changed by resolved City commands only.
  if (setting->owner == SETTING_CITY) return;
  u8 *field = (u8 *)CitySettingOwnerAddress(game, setting->owner) + setting->offset;
  switch (setting->type) {
  case SETTING_BOOL: *(bool *)field = value != 0; break;
  case SETTING_U8: *field = value; break;
  case SETTING_I16: *(i16 *)field = value; break;
  case SETTING_I32: *(i32 *)field = value; break;
  case SETTING_U32: *(u32 *)field = value; break;
  case SETTING_MASK: *(u32 *)field = (*(u32 *)field & ~setting->mask) | (value ? setting->mask : 0); break;
  case SETTING_MASK64: *(u64 *)field = (*(u64 *)field & ~setting->mask) | (value ? setting->mask : 0); break;
  }
}

bool RunCitySettingCommand(Game *game, const CitySetting *setting, int value) {
  if (setting->toggle && ReadCitySettingValue(game, setting, false) == value) return true;
  bool fix = setting->command == CITY_COMMAND_SET_FIX;
  return RunCityCommand(game, setting->command, INVALID_TILE,
      fix ? setting->parameter : setting->toggle ? 0 : value, fix ? value : 0, 0, NULL, NULL);
}

// Configured City fixes update their cached default mask; local preferences use their live owner.
static void WriteGameSettingDefault(Game *game, const CitySetting *setting, int value) {
  if (setting->command == CITY_COMMAND_SET_FIX) {
    if (setting->inverted) value = !value;
    game->default_city_fixes = (game->default_city_fixes & ~setting->mask) | (value ? setting->mask : 0);
  } else WriteLocalSettingValue(game, setting, value);
}

// Validate the complete document before using its file policy and City defaults. This reads no live owner fields
// and changes neither the document nor preferences; missing keys retain their descriptor defaults.
static bool ValidateGameSettings(const Config *config) {
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    ConfigValue value = ReadConfigValue(config, setting->key);
    if (value.type != CONFIG_MISSING) {
      if (value.type != CONFIG_INTEGER || value.integer < setting->minimum || value.integer > setting->maximum) {
        fprintf(stderr, "Invalid configuration value: %s\n", setting->key);
        return false;
      }
    }
  }
  ConfigValue save_directory = ReadConfigValue(config, "files.save_directory");
  if (save_directory.type == CONFIG_INTEGER) return false;
  ConfigValue soundfont = ReadConfigValue(config, "audio.soundfont");
  if (soundfont.type == CONFIG_INTEGER) return false;
  for (int list = 0; list < JUKEBOX_CUSTOM_PROGRAMMES; ++list) {
    char key[32];
    snprintf(key, sizeof(key), "music.playlist%d", list + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    JukeboxPlaylist parsed;
    if (saved.type == CONFIG_STRING && !ParseJukeboxPlaylist(saved.string, &parsed)) return false;
  }
  for (int i = 0; i < CITY_KEY_BINDING_CAPACITY; ++i) {
    char key[32];
    snprintf(key, sizeof(key), "bindings.slot%d", i + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    CityKeyBinding parsed;
    if (saved.type == CONFIG_STRING && *saved.string && !ParseCityKeyBinding(saved.string, &parsed)) return false;
  }
  return true;
}

// Apply in descriptor order, checking each value before its write. A later failure retains earlier owner updates;
// scale overrides skip only the two display values, and missing preserved values come from their live owners.
static bool ApplyConfiguredGameSettings(Game *game, bool scale_override) {
  const Config *config = &game->settings->document;
  CityInterfaceState *ui = game->ui;
  CityAudioState *audio = game->audio;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (scale_override && setting->owner == SETTING_DISPLAY &&
        (setting->offset == offsetof(DrawingOptions, automatic) || setting->offset == offsetof(DrawingOptions, scale)))
      continue;
    i64 value = setting->default_value;
    ConfigValue configured = ReadConfigValue(config, setting->key);
    if (configured.type != CONFIG_MISSING) {
      if (configured.type != CONFIG_INTEGER ||
          configured.integer < setting->minimum || configured.integer > setting->maximum) {
        fprintf(stderr, "Invalid configuration value: %s\n", setting->key);
        return false;
      }
      value = configured.integer;
    } else if (setting->preserve_initial) {
      value = ReadCitySettingValue(game, setting, true);
    }
    WriteGameSettingDefault(game, setting, (int)value);
  }
  ConfigValue save_directory = ReadConfigValue(config, "files.save_directory");
  if (save_directory.type == CONFIG_INTEGER) return false;
  ConfigValue soundfont = ReadConfigValue(config, "audio.soundfont");
  if (soundfont.type == CONFIG_INTEGER) return false;
  if (soundfont.type == CONFIG_STRING && *soundfont.string) {
    if (!SetCitySoundFontPath(audio, soundfont.string)) return false;
  }
  for (int list = 0; list < JUKEBOX_CUSTOM_PROGRAMMES; ++list) {
    JukeboxPlaylist *playlist = &audio->jukebox.custom[list];
    char key[32];
    snprintf(key, sizeof(key), "music.playlist%d", list + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    if (saved.type == CONFIG_STRING) {
      JukeboxPlaylist parsed;
      if (!ParseJukeboxPlaylist(saved.string, &parsed)) return false;
      memcpy(playlist->tracks, parsed.tracks, parsed.count);
      playlist->count = parsed.count;
    }
  }
  ui->console.binding_count = 0;
  for (int i = 0; i < CITY_KEY_BINDING_CAPACITY; ++i) {
    char key[32];
    snprintf(key, sizeof(key), "bindings.slot%d", i + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    if (saved.type == CONFIG_STRING && *saved.string) {
      CityKeyBinding parsed;
      if (!ParseCityKeyBinding(saved.string, &parsed)) return false;
      int index = ui->console.binding_count++;
      ui->console.bindings[index] = parsed;
    }
  }
  return true;
}

// Capture live presentation preferences and future-City defaults into the document. Keep existing wrong-type
// string rejection and partial updates; queuing and writing the file remain the caller's responsibility.
static bool CaptureGameSettings(Game *game) {
  Config *config = &game->settings->document;
  const CityInterfaceState *ui = game->ui;
  const CityAudioState *audio = game->audio;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    i64 value = ReadCitySettingValue(game, setting, true);
    if (!SetConfigInteger(config, setting->key, value, setting->maximum == 1)) return false;
  }
  ConfigValue save_directory = ReadConfigValue(config, "files.save_directory");
  if (save_directory.type == CONFIG_INTEGER) return false;
  if (save_directory.type == CONFIG_MISSING && !SetConfigString(config, "files.save_directory", "")) return false;
  ConfigValue soundfont = ReadConfigValue(config, "audio.soundfont");
  if (soundfont.type == CONFIG_INTEGER) return false;
  if (!SetConfigString(config, "audio.soundfont", audio->soundfont_path ? audio->soundfont_path : "")) return false;
  for (int list = 0; list < JUKEBOX_CUSTOM_PROGRAMMES; ++list) {
    const JukeboxPlaylist *playlist = &audio->jukebox.custom[list];
    char key[32], text[JUKEBOX_PLAYLIST_TEXT_CAPACITY];
    snprintf(key, sizeof(key), "music.playlist%d", list + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    if (!FormatJukeboxPlaylist(playlist, text, sizeof(text))) return false;
    if (!SetConfigString(config, key, text)) return false;
  }
  for (int i = 0; i < CITY_KEY_BINDING_CAPACITY; ++i) {
    char key[32], binding[104];
    snprintf(key, sizeof(key), "bindings.slot%d", i + 1);
    ConfigValue saved = ReadConfigValue(config, key);
    if (saved.type == CONFIG_INTEGER) return false;
    binding[0] = 0;
    if (i < ui->console.binding_count) {
      char first[48], second[48];
      CityHotkeyToString(ui->console.bindings[i].key, first, sizeof(first));
      CityHotkeyToString(ui->console.bindings[i].action, second, sizeof(second));
      snprintf(binding, sizeof(binding), "%s %s", first, second);
    }
    if ((binding[0] || saved.type != CONFIG_MISSING) && !SetConfigString(config, key, binding)) return false;
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
      ValidateGameSettings(&settings->document);
  if (!settings->writable) {
    fprintf(stderr, "Cannot read configuration; preserving the file and using defaults.\n");
    FreeConfig(&settings->document);
  }
  // File-loading policy and City-fix defaults are needed before the first city is decoded.
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (setting->owner != SETTING_GAME && setting->command != CITY_COMMAND_SET_FIX) continue;
    ConfigValue value = ReadConfigValue(&settings->document, setting->key);
    WriteGameSettingDefault(game, setting, value.type == CONFIG_INTEGER ? (int)value.integer : setting->default_value);
  }
}

void ApplyGameSettings(Game *game, bool scale_override) {
  struct GameSettings *settings = game->settings;
  if (!settings) return;
  if (!ApplyConfiguredGameSettings(game, scale_override)) {
    settings->writable = false;
    fprintf(stderr, "Could not apply configuration.\n");
    return;
  }
  if (game->audio->live_open && game->audio->custom_soundfont && !SetCitySoundFont(game->audio, NULL, true))
    fprintf(stderr, "Could not load the configured MIDI soundfont.\n");
  JukeboxSelect(&game->audio->jukebox, game->audio->jukebox.programme);
  game->audio->music_enabled = game->audio->jukebox.playing;
  NativeAudioVolume(game->audio);
  // Saved native cities retain their deterministic settings. Apply configured defaults
  // to stock cities through resolved commands, outside command execution itself.
  // Keep their import rule for flat-land flooding; the user can change it per city.
  if (!game->city->file_extensions) {
    for (int fix = 0; fix < CITY_FIX_COUNT; ++fix)
      if (fix != CITY_FIX_WATER_SPREADING && (CITY_FIX_SIMULATION_MASK & (1ull << fix)))
        RunCityCommand(game, CITY_COMMAND_SET_FIX, INVALID_TILE, fix,
            (game->default_city_fixes & (1ull << fix)) != 0, 0, NULL, NULL);
  }
  StoreGameSettings(game);
  if (!game->city_active) ApplyGameCityDefaults(game);
}

bool ApplyGameCityDefaults(Game *game) {
  if (!game->settings) return false;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (!setting->command) continue;
    int value = ReadGameCityDefault(game, setting);
    if (setting->command == CITY_COMMAND_SET_SIMULATION_SPEED) {
      // Pause is temporary; older configurations can contain paused or zero speeds.
      if (value < CITY_SPEED_SLOW) value = CITY_SPEED_NORMAL;
      game->ui->resume_speed = value;
      if (game->ui->dialog_pause_requested) continue;
    }
    if (!RunCitySettingCommand(game, setting, value)) return false;
  }
  return true;
}

void StoreGameSettings(Game *game) {
  if (!game->settings || !game->settings->writable) return;
  if (!CaptureGameSettings(game)) fprintf(stderr, "Could not update configuration.\n");
  if (game->settings->document.changed && !game->settings->write_at)
    game->settings->write_at = -1;
}

// Intro edits change defaults without running a City command. An invalid on-disk config remains untouched;
// its replacement document may still hold this run's defaults in memory.
bool StoreGameCityDefault(Game *game, const CitySetting *setting, int value) {
  if (!game->settings || setting->owner != SETTING_CITY || !setting->command ||
      value < setting->minimum || value > setting->maximum) return false;
  if (!SetConfigInteger(&game->settings->document, setting->key, value, setting->maximum == 1)) return false;
  WriteGameSettingDefault(game, setting, value);
  StoreGameSettings(game);
  return true;
}

// Copy only gameplay values. Presentation preferences keep their existing global owners.
bool CopyCitySettingsToDefaults(Game *game) {
  if (!game->settings) return false;
  for (int i = 0; i < kCitySettingsCount; ++i) {
    const CitySetting *setting = &kCitySettings[i];
    if (setting->owner != SETTING_CITY) continue;
    int value = ReadCitySettingValue(game, setting, false);
    if (!SetConfigInteger(&game->settings->document, setting->key, value, setting->maximum == 1)) return false;
    WriteGameSettingDefault(game, setting, value);
  }
  StoreGameSettings(game);
  return true;
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
  free(settings->path);
  free(settings->directory);
  free(settings);
  game->settings = NULL;
}

// Borrowed configured text; NULL/empty selects ~/.opensc2k/Cities.
static const char *GameSaveDirectory(const Game *game) {
  if (!game->settings) return NULL;
  ConfigValue value = ReadConfigValue(&game->settings->document, "files.save_directory");
  return value.type == CONFIG_STRING ? value.string : NULL;
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
